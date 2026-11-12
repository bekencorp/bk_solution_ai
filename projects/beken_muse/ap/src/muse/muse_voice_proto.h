/**
 * @file muse_voice_proto.h
 * @brief Push-to-talk voice protocol policy: Muse Hatch chat wire format.
 *
 * This is the ONLY file that knows the upstream voice wire format. Values
 * are transcribed from the pinned upstream sources
 * (muse-gadget-sdk-main/esp32/components/muse/muse_chat_link.c,
 *  muse_chat_text.c, muse_chat_priv.h):
 *
 * Upload:  POST /chat/stream, JSON body
 *          NOTE_HEAD + base64( WAV(44B, 16 kHz mono PCM16) + mic PCM ) + NOTE_TAIL
 *          split into body chunks of STAGE_BYTES PCM (2048 base64 chars).
 *          The 200 ACK carries result.message_id / reply_to_message_id.
 * Reply:   POST /chat/subscribe with body "{}", opened right before the
 *          note's final chunk; the response is an NDJSON stream of
 *          {"type":"event","payload":{...}} rows. We consume
 *          message.assistant (reply text) and message.user (transcript).
 */
#ifndef __BK_MUSE_VOICE_PROTO_H__
#define __BK_MUSE_VOICE_PROTO_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- request line / headers --------------------------------------------- */
const char *muse_voice_proto_note_path(void);    /* "/chat/stream" */
const char *muse_voice_proto_sub_path(void);     /* "/chat/subscribe" */
const char *muse_voice_proto_content_type(void); /* "application/json" */
const char *muse_voice_proto_app_id(void);       /* "hatch-web" */
const char *muse_voice_proto_note_accept(void);  /* "application/json" */
const char *muse_voice_proto_sub_accept(void);   /* "application/x-ndjson" */

/* ---- note body encoding --------------------------------------------------- */

/* Audio: 16 kHz mono PCM16 (upstream's rate; 8 kHz ASR garbles the high
 * consonants of Chinese). The uplink (~26 KB/s) is slower than 16 kHz
 * base64 production (42.7 KB/s), so instead of dropping audio the voice
 * pool is deep enough to buffer a full 30 s note and drain it after the
 * release — intact audio, at the cost of a longer wait for the ACK. */
#define MUSE_VOICE_SAMPLE_RATE_HZ   16000U

/* 上传波形位深：0 = 16-bit（默认，勿改）。8-bit 量化实验已否决（2026-11-06
 * 实机）：量化噪声使云端 ASR 几乎句句识别错（中文被转写成荷兰语），且
 * TCP 窗口调到 32850 后上行实测 ~80KB/s，16bit 的 42.7KB/s 本就不再积压，
 * 8bit 失去存在意义。保留代码路径仅作记录。 */
#ifndef CONFIG_MUSE_VOICE_PCM_8BIT
#define CONFIG_MUSE_VOICE_PCM_8BIT  0
#endif
#define MUSE_VOICE_FRAME_MS         20U
#define MUSE_VOICE_FRAME_BYTES      ((MUSE_VOICE_SAMPLE_RATE_HZ * 2U * MUSE_VOICE_FRAME_MS) / 1000U)  /* 640 @16k */
#define MUSE_VOICE_WAV_HEADER_BYTES 44U
#define MUSE_VOICE_STAGE_BYTES      1536U
#define MUSE_VOICE_CHUNK_CAP        (2048U + 8U)

/* JSON body wrapper (upstream MUSE_HATCH_NOTE_HEAD/TAIL + the Linux
 * SDK's identity fields). The head ends with `data_base64":"`; body
 * chunks are pure base64; the last chunk appends the tail. The tail
 * pointer is static, *len excludes the NUL.
 *
 * message: text for the note's "message" field (upstream send_voice:
 * instructions accompanying the file); NULL/"" for none.
 * device_id/session_id: when both are non-empty they are sent in the
 * Linux-SDK order so the server attributes the turn to this board's
 * conversation; with both empty the legacy upstream esp32 head shape is
 * emitted. All strings are our own fixed values, emitted without JSON
 * escaping. Returns the head length, 0 on overflow. */
size_t muse_voice_proto_note_head(char *buf, size_t cap,
                                  const char *message,
                                  const char *device_id,
                                  const char *session_id);
const char *muse_voice_proto_note_tail(size_t *len);

/* Photo note: same envelope as the voice note (and the same tail), but
 * the single file item is a JPEG. `query` lands in "message"; pass
 * NULL/"" to let the VM describe the image freely. Same identity-field
 * semantics as muse_voice_proto_note_head(). */
size_t muse_voice_proto_image_note_head(char *buf, size_t cap,
                                        const char *query,
                                        const char *device_id,
                                        const char *session_id);

/* Streaming WAV: RIFF/data sizes are 0xFFFFFFFF (unknown while streaming). */
void   muse_voice_proto_wav_header(uint8_t h[MUSE_VOICE_WAV_HEADER_BYTES], uint32_t rate);
/* RFC 4648 with '=' padding; returns chars written (no NUL appended). */
size_t muse_voice_proto_base64(const uint8_t *in, size_t n, char *out);

/* ---- response parsing ----------------------------------------------------  */

/* Parse the /chat/stream 200 ACK. Fills note_id/parent_id (NUL-terminated,
 * empty when absent). Returns 0 when a message_id was found, else -1. */
int muse_voice_proto_parse_ack(const char *json,
                               char *note_id, size_t note_cap,
                               char *parent_id, size_t parent_cap);

/* One decoded NDJSON subscription row. */
typedef struct {
    char event[24];    /* e.g. "message.assistant", "message.user", "delta.text_append" */
    char msg[80];      /* message_id */
    char reply_to[80]; /* reply_to_message_id / parent_message_id */
    char text[1024];   /* display_text > content > text */
    bool ready;        /* display_text_ready (default true) */
} muse_voice_row_t;

/* Parse one NUL-terminated NDJSON line. Returns true when it carried an
 * event payload (row filled); false for heartbeats/malformed lines. */
bool muse_voice_proto_parse_row(const char *line, muse_voice_row_t *row);

#ifdef __cplusplus
}
#endif

#endif
