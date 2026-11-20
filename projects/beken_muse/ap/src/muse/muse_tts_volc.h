/*
 * @file muse_tts_volc.h
 * @brief Volcano TTS client: sentence-streaming synthesis of the assistant
 *        reply into per-sentence MP3 files on SD-NAND for playback through
 *        the prompt-tone path.
 *
 * The server already synthesizes sentence by sentence and marks the
 * boundaries (event 350 = sentence start, 351 = sentence end), so the
 * client hands each completed sentence file to the caller as it streams
 * in -- the first sentence can be playing while the rest is still being
 * synthesized. Billing is per character either way; the split costs
 * nothing (2026-11-12).
 *
 * Key comes from muse_voice (runtime `muse ttskey <key>`), never from
 * build config.
 */
#ifndef MUSE_TTS_VOLC_H
#define MUSE_TTS_VOLC_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Invoked (from the caller's thread) once per completed sentence MP3.
 * `path` is a ready-to-play file; it stays valid until the callback
 * returns (the module deletes it afterwards), so play it synchronously.
 * The final invocation has path == NULL and last == true, marking the
 * end of the stream (with or without a preceding server error). */
typedef void (*muse_tts_volc_seg_cb)(void *user, unsigned seg,
                                     const char *path, bool last);

/* Synthesize text, invoking cb per sentence as audio streams in.
 * Blocks for the whole session (bounded); call from a worker context.
 * Returns 0 when at least one sentence was delivered; <0 otherwise. */
int muse_tts_volc_synthesize_stream(const char *text,
                                    muse_tts_volc_seg_cb cb, void *user);

/* Pre-establish the TTS websocket (TCP+TLS+upgrade+StartConnection) and
 * park it (<=20 s) so a later synthesize_stream only needs StartSession.
 * Call once the turn's audio uplink is finished (ASR final / PTT stop);
 * no-op without a configured key or while a warm connection exists. */
void muse_tts_volc_prewarm(void);

/* True while a synthesis is in flight. */
bool muse_tts_volc_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* MUSE_TTS_VOLC_H */
