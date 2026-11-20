/*
 * @file muse_asr_volc.h
 * @brief Volcano streaming ASR (v3 SAUC "bigmodel") client.
 *
 * One recognition session per voice turn: the caller feeds 16 kHz mono
 * PCM via muse_asr_volc_feed() and muse_asr_volc_finish() ends the stream
 * (negative-seq tail packet). The final transcript arrives exactly once
 * through on_text, or on_error fires exactly once -- never both. All
 * callbacks run on the bk_websocket event task.
 *
 * Auth: X-Api-Key + X-Api-Resource-Id + X-Api-Request-Id + X-Api-Sequence
 * handshake headers (new-console key; the four-header set was verified
 * against the volc edge on 2026-11-10 -- dropping either Request-Id or
 * Sequence gets a 403 before any frame is parsed).
 */
#ifndef MUSE_ASR_VOLC_H
#define MUSE_ASR_VOLC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*muse_asr_volc_text_cb_t)(const char *text);
typedef void (*muse_asr_volc_error_cb_t)(int err, const char *msg);

#define MUSE_ASR_ERR_EMPTY_TEXT  (-10)

#define MUSE_ASR_ERR_TRANSPORT   (-11)

/* Register callbacks (before the first start; may be called again). */
void muse_asr_volc_init(muse_asr_volc_text_cb_t on_text,
                        muse_asr_volc_error_cb_t on_error);

/* Open the session: allocate buffers, connect, send the config frame.
 * Returns 0 on success; <0 when the key is unset, alloc fails, or the
 * connect is refused. On a refused handshake on_error fires with the
 * transport error (e.g. the volc 403). */
int  muse_asr_volc_start(void);

/* Queue one mic chunk (16 kHz s16 mono). Overrun frames are dropped
 * (counted); the caller's VAD keeps utterances well under the ring
 * capacity. Any task may call; internal lock serialises. */
void muse_asr_volc_feed(const int16_t *pcm, size_t samples);

/* Send the tail packet and wait (bounded) for the final transcript,
 * then tear the session down. Without a final text the error callback
 * fires. Blocks up to ~12 s: call from a worker context, never from the
 * audio read callback. */
void muse_asr_volc_finish(void);

/* Abort without waiting for the transcript (disconnect/turn-cancel). */
void muse_asr_volc_abort(void);

bool muse_asr_volc_active(void);

/* Tick (rtos_get_time) of the last frame received from the server, or 0
 * when the current session has received nothing yet. Partials flowing =
 * the user is still talking -- the voice layer's no-speech watchdog must
 * not fire on local VAD silence alone (field 2026-11-13: the watchdog
 * killed a turn 1 s before its final arrived, note never sent). */
uint32_t muse_asr_volc_last_rx_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* MUSE_ASR_VOLC_H */
