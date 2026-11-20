/**
 * @file muse_voice.h
 * @brief Push-to-talk voice: user entry points + session-worker glue.
 *
 * Threading model (see docs/muse_voice_design.md):
 * - muse_voice_ptt_*() may be called from any task (CLI, key bridge).
 * - The audio read callback runs on the audio-engine read task.
 * - tx_next()/tx_ack()/on_rx_*()/on_tick()/on_session() run ONLY on the
 *   muse chat worker (muse_chat.cpp). The module keeps its state under one
 *   mutex; the worker never blocks on it for longer than one queue pop.
 */
#ifndef __BK_MUSE_VOICE_H__
#define __BK_MUSE_VOICE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- user entry points (any task) -------------------------------------- */

/* 0 ok, <0 error: -1 busy (RTC/A2DP owns audio), -2 not connected,
 * -3 engine start failed, -4 already recording. */
int  muse_voice_ptt_start(void);
int  muse_voice_ptt_stop(void);
int  muse_voice_ptt_toggle(void);
bool muse_voice_recording(void);

/* Photo recognition turn: base64-encode a freshly captured JPEG and send
 * it as a chat note (image/jpeg file item) on the dedicated chat
 * connection, then wait for the recognition reply exactly like a voice
 * turn (reply binds via the same subscription/draw_text paths, shows on
 * the same overlay). Any task may call.
 * Returns 0 when the note is queued; <0: -1 a turn is already in flight,
 * -2 muse not connected, -3 alloc/queue failure, -4 bad args,
 * -5 jpeg larger than CONFIG_MUSE_PHOTO_MAX_JPEG_BYTES. */
int  muse_photo_send(const uint8_t *jpeg, size_t jpeg_len, const char *query);

/* Assistant reply delivered by a display.draw_text invoke (Muse's current
 * reply channel): when a voice turn is waiting (note sent, reply pending),
 * bind the text as the turn's reply — shown like a subscription reply, the
 * reply deadline no longer applies and the note stream is retired WITHOUT
 * a reset. Returns true when a turn claimed the text; false when no turn
 * is waiting (caller shows the text standalone). Any task may call. */
bool muse_voice_on_reply_text(const char *text);

/* An image reply (display.draw_url invoke) closes a waiting voice turn the
 * same way a text reply does: restores KWS immediately and shortens the
 * reply wait to a short caption grace. No-op when no turn is waiting. Any
 * task may call. */
void muse_voice_on_reply_image(void);

/* Muse-initiated push (display.draw_text that no voice turn claimed): show
 * it and speak it, but NEVER interrupt a conversation. While a voice turn
 * is recording/closing/awaiting a reply, or a previous TTS is still
 * playing, the text is parked (latest wins) and flushed by the voice tick
 * once the device is idle again. Any task may call. */
void muse_voice_push_text(const char *text);

/* Volcano speech credentials (old console: APP ID + per-service Access
 * Token), runtime-only: `muse volcappid <id>` / `muse asrkey <key>` /
 * `muse ttskey <key>` on the CLI persist them to easyflash. ASR/TTS each
 * stay disabled until their own credential is set. Setters return 0 on
 * success (<0: bad args / storage fail). */
int  muse_voice_set_volc_asr_key(const char *key);
int  muse_voice_set_volc_tts_key(const char *key);
int  muse_voice_set_volc_appid(const char *appid);
int  muse_voice_set_volc_asr_res(const char *res);
int  muse_voice_set_volc_tts_voice(const char *voice);
const char *muse_voice_volc_asr_key(void);
const char *muse_voice_volc_tts_key(void);
const char *muse_voice_volc_appid(void);
const char *muse_voice_volc_asr_res(void);
const char *muse_voice_volc_tts_voice(void);

/* True while outbound voice work is in flight (recording, closing, or a
 * control transition pending); the session shortens its inbound wait then. */
bool muse_voice_tx_active(void);

/* High-level UI phase for the voice-chat page animation (any task may
 * call; reads the state under the voice mutex). */
typedef enum {
    MUSE_VOICE_UI_IDLE = 0,
    MUSE_VOICE_UI_RECORDING,
    MUSE_VOICE_UI_WAITING,   /* closing the note or waiting for the reply */
    MUSE_VOICE_UI_SHOWING,   /* assistant reply on screen */
} muse_voice_ui_phase_t;
muse_voice_ui_phase_t muse_voice_ui_phase(void);

/* Copy the current turn's ASR echo (what the server heard, arrives ~1 s
 * after the note closes — long before the reply). Fills out[] and returns
 * true only while a turn is closing/waiting AND the echo has arrived;
 * false otherwise (out[] = ""). For the UI's "heard it" interim display. */
bool muse_voice_heard(char *out, size_t cap);

/* Copy the current turn's status/error notice (e.g. "请求被中断，请再说一次")
 * and return true while the turn is in SHOWING because of such a notice
 * instead of an assistant reply. The voice-chat page uses this to show the
 * notice in its bubble — its bubble otherwise mirrors voice_history and
 * would replay the previous turn's reply. false when a real reply (or
 * nothing) is on screen. */
bool muse_voice_ui_notice(char *out, size_t cap);

/* True while the turn's reply text is held back waiting for the first TTS
 * sentence to actually start (see VOICE_REPLY_SHOW_DELAY_MS). The page
 * must not fall back to the previous turn's history bubble in this window;
 * it shows a "speaking soon" hint instead. Any task may call. */
bool muse_voice_ui_holding(void);

/* ---- chat worker glue (muse_chat.cpp only) ----------------------------- */

typedef enum {
    MUSE_VOICE_TX_NONE = 0,  /* nothing pending (timeout) */
    MUSE_VOICE_TX_OPEN_NOTE, /* POST /chat/stream on the note stream */
    MUSE_VOICE_TX_DATA,      /* one base64 body chunk copied into caller buf */
    MUSE_VOICE_TX_END,       /* final base64 chunk + NOTE_TAIL (end_body) */
    MUSE_VOICE_TX_OPEN_SUB,  /* POST /chat/subscribe, body "{}", end_body */
    MUSE_VOICE_TX_RESET,     /* Reset(Cancelled) the note stream */
} muse_voice_tx_kind_t;

/*
 * Decide the next outbound unit. The OPEN kinds and RESET are control
 * transitions and are returned immediately; DATA/END pop one encoded
 * chunk and block up to frame_wait_ms while recording. For DATA/END the
 * chunk bytes are copied into frame/cap (max MUSE_VOICE_CHUNK_CAP) and
 * *frame_len set.
 */
muse_voice_tx_kind_t muse_voice_tx_next(uint8_t *frame, size_t cap,
                                        size_t *frame_len,
                                        uint32_t frame_wait_ms);

/* Acknowledge that the unit returned by tx_next() was encoded and flushed.
 * ok=false means the session could not send it (connection fault): the
 * voice state is torn down locally without emitting further frames. */
void muse_voice_tx_ack(muse_voice_tx_kind_t kind, bool ok);

/* Stream ids (0 = none). The note stream is per-turn with a fresh id
 * (upstream s_conn.next_id++). The subscribe stream is per-CONNECTION,
 * opened once after link-up and kept open for its lifetime (upstream
 * connect_once() -> open_subscription() pattern): per-turn subscribes made
 * the server fan assistant events out to zombie subscriptions from dead
 * connections while the live one got nothing. Read them when dispatching
 * or sending. */
int64_t muse_voice_note_stream(void);
int64_t muse_voice_sub_stream(void);
/* Stream id a pending MUSE_VOICE_TX_RESET must hit: voice_abort() stashes
 * the dying note stream here so a barge-in's fresh note stream (allocated
 * before the worker pumped the reset) is not the reset target. 0 = use
 * the current note stream. Read it when building the Reset frame. */
int64_t muse_voice_reset_stream(void);

/* Session connectivity/state hook: false aborts any active voice work. */
void muse_voice_on_session(bool connected);

/* Note-stream (POST /chat/stream) inbound data, session worker context:
 * the ACK arrives as the Response (status + first body bytes) possibly
 * followed by BodyChunks; pieces are accumulated until end_body, then
 * parsed. status is the HTTP status of the Response, 0 for bare
 * BodyChunks. */
void muse_voice_on_rx_ack(int status, const uint8_t *data, size_t len,
                          bool end);
/* Subscription-stream (POST /chat/subscribe) inbound data: a piece of the
 * NDJSON reply (Response body or BodyChunk; caller-owned bytes) and the
 * frame's end_body flag. status is the HTTP status of the Response, 0 for
 * bare BodyChunks. */
void muse_voice_on_rx_sub_data(int status, const uint8_t *data, size_t len,
                               bool end);
/* The note stream was reset by the peer: abort the turn. */
void muse_voice_on_rx_note_reset(void);
/* The subscribe stream was reset by the peer: reopen it (it is the only
 * channel assistant replies arrive on). */
void muse_voice_on_rx_sub_reset(void);

/* 1 Hz-ish progress (WAIT_REPLY deadline, overlay auto-dismiss). Called
 * from the session worker loop; harmless when voice is idle. */
void muse_voice_on_tick(void);

/* One-time init (queues + mutex); called by the session init. */
int muse_voice_init(void);

#ifdef __cplusplus
}
#endif

#endif
