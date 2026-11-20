/*
 * @file muse_tts_volc.c
 * @brief Volcano TTS (豆包语音合成大模型 2.0) bidirectional-streaming client:
 *        synthesize the assistant reply sentence by sentence into per-
 *        sentence MP3 files on the SD-NAND, handing each one to the caller
 *        (playback) as soon as the server marks the sentence complete.
 *
 * Wire: wss://openspeech.bytedance.com/api/v3/tts/bidirection, event-driven
 * binary frames (same header family as the SAUC ASR client):
 *   header[4]: 0x11 | (type<<4 | flags) | (ser<<4 | comp) | 0
 *   flags: 0x04 event field present (BE32), 0x01/0x02 sequence field (BE32)
 *   then optional session id (BE32 len + bytes, session-scoped events),
 *   then BE32 payload size, then payload.
 * Event flow: 1 StartConnection -> 50 ConnectionStarted,
 *   100 StartSession -> 150 SessionStarted,
 *   200 TaskRequest(text) -> 102 FinishSession,
 *   <- 350 sentence start, <- audio chunks (msg type 11, event 352/0),
 *   <- 351 sentence end, ... <- 152 SessionFinished.
 * Connection-scoped events (1/2/50/51/52) carry NO session id.
 *
 * Auth (field-proven 2026-11-12): new-console API Key (dashed UUID) goes out
 * as X-Api-Key with X-Api-Resource-Id=seed-tts-2.0 -- a 1.0 resource id
 * (volc.service_type.10029) gets 403 on accounts that only activated the
 * 2.0 model. Old-console 32-char tokens fall back to the App-Key pair.
 * Speaker must be a 2.0 voice (*_uranus_bigtts); 1.0 voices are rejected
 * with "resource ID is mismatched with speaker related resource".
 *
 * Threading: the caller's thread (voice_tts_main) runs the session and the
 * playback callback loop; the ws event callback (websocketc task) only
 * streams received audio into the current sentence file and pushes
 * completed files onto a small ring. First audio is typically ~1 s after
 * the request, so sentence 1 is playing while the rest is still streaming.
 */
#include "muse_tts_volc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>  /* ssize_t (as in muse_gadget.c; unistd.h is absent here) */
#include <fcntl.h>

#include <os/os.h>
#include <components/log.h>
#include "bk_websocket_client.h"

#define TAG "muse_tts_volc"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#ifndef CONFIG_MUSE_TTS_VOLC_RESOURCE_ID
#define CONFIG_MUSE_TTS_VOLC_RESOURCE_ID "seed-tts-2.0"
#endif
#ifndef CONFIG_MUSE_TTS_VOLC_VOICE
#define CONFIG_MUSE_TTS_VOLC_VOICE "zh_female_xiaohe_uranus_bigtts"
#endif
#ifndef CONFIG_MUSE_TTS_VOLC_URL
#define CONFIG_MUSE_TTS_VOLC_URL "wss://openspeech.bytedance.com/api/v3/tts/bidirection"
#endif

#define TTS_RX_BYTES        (16U * 1024U)
#define TTS_FRAME_CAP       2048U
#define TTS_WS_BUF          16384
#define TTS_SID_BYTES       40U
#define TTS_SEG_MAX         8U       /* completed-sentence ring depth */
#define TTS_IDLE_TIMEOUT_MS 30000U   /* no rx progress -> fail */
#define TTS_TOTAL_TIMEOUT_MS 120000U /* absolute session cap */

/* Frame header byte 1 nibbles (server side). */
#define T_TYPE_RESP   0x9U
#define T_TYPE_AUDIO  0xBU
#define T_TYPE_ERROR  0xFU

/* Event numbers. */
#define EV_START_CONN      1
#define EV_FINISH_CONN     2
#define EV_CONN_STARTED    50
#define EV_CONN_FAILED     51
#define EV_CONN_FINISHED   52
#define EV_START_SESSION   100
#define EV_FINISH_SESSION  102
#define EV_SESSION_STARTED 150
#define EV_SESSION_FAILED  151
#define EV_SESSION_FINISHED 152
#define EV_SESSION_CANCELED 153
#define EV_TASK_REQUEST    200
#define EV_SENTENCE_START  350
#define EV_SENTENCE_END    351

typedef struct {
    bool        active;
    bool        done;         /* session finished (ok or failed) */
    bool        failed;

    transport   client;

    uint8_t    *rx;           /* psram: one ws message reassembly */
    size_t      rx_len;
    uint8_t    *frame;        /* psram: outbound frame scratch */

    char        session[TTS_SID_BYTES];
    char        text[400];    /* escaped request text snapshot */
    char        api_key[96];
    char        voice[96];

    uint32_t    last_rx;      /* tick of the last ws data (idle watchdog) */
    uint32_t    watchdog_ms;  /* idle watchdog for the current attempt */
    bool        warm_only;    /* prewarm phase: park after ConnectionStarted */

    /* Sentence streaming: current open file + ring of completed files.
     * seg_fd writes happen on the websocketc task; the ring is shared
     * with the caller's thread under seg_lock. */
    int         seg_fd;       /* -1 when no segment file is open */
    unsigned    seg_idx;      /* index of the segment being written */
    unsigned    seg_bytes;    /* bytes written to the current file */
    beken_mutex_t seg_lock;
    char        seg_ring[TTS_SEG_MAX][64];
    unsigned    seg_head;     /* next push slot */
    unsigned    seg_tail;     /* next pop slot */
} tts_ctx_t;

static tts_ctx_t s_tts;
static bool s_busy;

/* 预热（连接驻留）状态机：NONE→CONNECTING→PARKED→ADOPTED/DEAD→NONE。
 * 回复文本到达前的几秒本来就在空等（Muse 思考），先把 TCP+TLS+WS+
 * StartConnection 建好停在那里，synthesize 领养后只需 StartSession，
 * 首音快 ~1.2s（2026-11-13 用户拍板）。 */
static volatile uint8_t s_warm_state;
#define TTS_WARM_NONE          0U
#define TTS_WARM_CONNECTING    1U
#define TTS_WARM_PARKED        2U
#define TTS_WARM_ADOPTED       3U
#define TTS_WARM_DEAD          4U
#define TTS_PREWARM_PARK_MS    20000U  /* 驻留上限：到点没人领养就拆 */
#define TTS_ADOPT_TIMEOUT_MS   8000U   /* 领养连接的首响应看门狗（驻留
                                        * 期间服务器可能已悄悄关闭） */
/* JSON staging for outbound events: the event callback runs on the
 * websocketc task whose stack is not ours to spend (a 1 KB request body
 * on a 4 KB task stack is how the tts_volc overflow happened). */
static char s_json[1536];

bool muse_tts_volc_busy(void)
{
    return s_busy;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Build and send one event frame. sid == NULL for connection-scoped
 * events (they must NOT carry a session id). */
static int tts_send_event(uint32_t event, const char *sid, const char *json)
{
    uint8_t *f = s_tts.frame;
    size_t off = 0;
    size_t plen = json != NULL ? strlen(json) : 0;
    int rc;

    size_t need = 4 + 4 + (sid != NULL ? 4 + strlen(sid) : 0) + 4 + plen;
    if (s_tts.client == NULL || need > TTS_FRAME_CAP) {
        return -1;
    }
    f[0] = 0x11;
    f[1] = (uint8_t)((0x1U << 4) | 0x4U);   /* full client req + event */
    f[2] = (uint8_t)(0x01U << 4);           /* JSON, no compression */
    f[3] = 0;
    off = 4;
    put_be32(f + off, event);
    off += 4;
    if (sid != NULL) {
        size_t sl = strlen(sid);
        put_be32(f + off, (uint32_t)sl);
        off += 4;
        memcpy(f + off, sid, sl);
        off += sl;
    }
    put_be32(f + off, (uint32_t)plen);
    off += 4;
    if (plen > 0) {
        memcpy(f + off, json, plen);
        off += plen;
    }
    rc = websocket_client_send_binary(s_tts.client, (char *)f, (int)off, 1000);
    if (rc != (int)off) {
        LOGE("ws send event %lu failed (%d)\r\n", (unsigned long)event, rc);
        return -2;
    }
    return 0;
}

/* ---- sentence files -----------------------------------------------------*/

extern int bk_vfs_open(const char *path, int oflag);
extern ssize_t bk_vfs_write(int fd, const void *buf, size_t count);
extern int bk_vfs_close(int fd);
extern int bk_vfs_unlink(const char *pathname);

/* "<stem>_s<idx>.mp3" derived from CONFIG_MUSE_TTS_VOLC_OUT_PATH so the
 * configured mount point is honoured. */
static void tts_seg_path(char *out, size_t cap, unsigned idx)
{
    const char *base = CONFIG_MUSE_TTS_VOLC_OUT_PATH;
    const char *dot = strrchr(base, '.');
    size_t stem = (dot != NULL && dot > base) ? (size_t)(dot - base)
                                              : strlen(base);
    if (stem > cap - 16U) {
        stem = cap - 16U;
    }
    memcpy(out, base, stem);
    snprintf(out + stem, cap - stem, "_s%u.mp3", idx);
}

/* Open the file for the current segment if not already open. */
static int tts_seg_ensure_open(void)
{
    char path[64];

    if (s_tts.seg_fd >= 0) {
        return 0;
    }
    tts_seg_path(path, sizeof(path), s_tts.seg_idx);
    s_tts.seg_fd = bk_vfs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (s_tts.seg_fd < 0) {
        LOGE("open %s failed (sd0 mounted?)\r\n", path);
        return -1;
    }
    s_tts.seg_bytes = 0;
    return 0;
}

static bool tts_seg_ring_push(const char *path)
{
    bool ok = false;

    rtos_lock_mutex(&s_tts.seg_lock);
    if (s_tts.seg_head - s_tts.seg_tail < TTS_SEG_MAX) {
        snprintf(s_tts.seg_ring[s_tts.seg_head % TTS_SEG_MAX],
                 sizeof(s_tts.seg_ring[0]), "%s", path);
        s_tts.seg_head++;
        ok = true;
    }
    rtos_unlock_mutex(&s_tts.seg_lock);
    return ok;
}

static bool tts_seg_ring_pop(char *out, size_t cap)
{
    bool ok = false;

    rtos_lock_mutex(&s_tts.seg_lock);
    if (s_tts.seg_tail < s_tts.seg_head) {
        snprintf(out, cap, "%s", s_tts.seg_ring[s_tts.seg_tail % TTS_SEG_MAX]);
        s_tts.seg_tail++;
        ok = true;
    }
    rtos_unlock_mutex(&s_tts.seg_lock);
    return ok;
}

/* Close the current segment file. ready=true publishes it to the playback
 * ring (only when non-empty and the ring has room; otherwise the sentence
 * is folded into the next one by simply not closing early -- handled by
 * the caller keeping the fd). A non-ready or empty file is deleted. */
static void tts_seg_close_current(bool ready)
{
    char path[64];

    if (s_tts.seg_fd < 0) {
        return;
    }
    (void)bk_vfs_close(s_tts.seg_fd);
    s_tts.seg_fd = -1;
    tts_seg_path(path, sizeof(path), s_tts.seg_idx);
    if (ready && s_tts.seg_bytes > 0U && tts_seg_ring_push(path)) {
        LOGI("sentence %u ready: %u B\r\n", s_tts.seg_idx,
             s_tts.seg_bytes);
    } else {
        if (ready && s_tts.seg_bytes > 0U) {
            LOGW("sentence ring full, dropping seg %u\r\n", s_tts.seg_idx);
        }
        (void)bk_vfs_unlink(path);
    }
    s_tts.seg_idx++;
    s_tts.seg_bytes = 0;
}

static void tts_fail(const char *why)
{
    if (s_tts.done) {
        return;   /* disconnect after a timeout etc.: fail once */
    }
    LOGW("tts session failed: %s\r\n", why);
    if (s_tts.seg_fd >= 0) {
        char path[64];
        (void)bk_vfs_close(s_tts.seg_fd);
        s_tts.seg_fd = -1;
        tts_seg_path(path, sizeof(path), s_tts.seg_idx);
        (void)bk_vfs_unlink(path);   /* partial sentence is not playable */
    }
    s_tts.failed = true;
    s_tts.done = true;
}

/* Payload location inside a received frame. Event/seq/session fields vary
 * by message class; validate the payload size against the frame length and
 * retry with a session-id field when the first layout does not check out. */
static bool tts_frame_payload(const uint8_t *p, size_t len, uint8_t type,
                              uint8_t flags, uint32_t *event_out,
                              const uint8_t **payload, size_t *payload_len)
{
    for (int with_sid = 0; with_sid < 2; with_sid++) {
        size_t off = 4;
        uint32_t ev = 0;
        if ((flags & 0x4U) != 0) {
            if (off + 4U > len) {
                return false;
            }
            ev = get_be32(p + off);
            off += 4;
        }
        if ((flags & 0x3U) != 0) {
            if (off + 4U > len) {
                return false;
            }
            off += 4;   /* sequence: informational */
        }
        if (with_sid) {
            if (off + 4U > len) {
                continue;
            }
            uint32_t sl = get_be32(p + off);
            if (sl > 64U || off + 4U + sl > len) {
                continue;
            }
            off += 4U + sl;
        }
        if (off + 4U > len) {
            return false;
        }
        uint32_t plen = get_be32(p + off);
        off += 4;
        if (off + plen != len) {
            continue;   /* layout mismatch: try the other one */
        }
        *event_out = ev;
        *payload = p + off;
        *payload_len = plen;
        (void)type;
        return true;
    }
    return false;
}

/* Length of the first sentence in s (UTF-8 aware): bytes up to and
 * including the first sentence terminator (。！？； or ! ? ; newline).
 * Returns the full string length when there is no terminator. */
static size_t tts_sentence_len(const char *s)
{
    size_t n = 0;
    while (s[n] != '\0') {
        uint8_t b = (uint8_t)s[n];
        if (b == 0xE3U && s[n + 1] == 0x80U && s[n + 2] == 0x82U) {
            return n + 3U;                        /* 。 */
        }
        if (b == 0xEFU && s[n + 1] == 0xBCU &&
            (s[n + 2] == 0x81U || s[n + 2] == 0x9FU || s[n + 2] == 0x9BU)) {
            return n + 3U;                        /* ！？； */
        }
        if (b == '!' || b == '?' || b == ';' || b == '\n') {
            return n + 1U;
        }
        n++;
    }
    return n;
}

static void tts_on_audio(const uint8_t *payload, size_t len)
{
    if (len == 0 || s_tts.failed) {
        return;
    }
    /* Lazily open the current segment so sessions without 350/351
     * boundaries still land in one file (whole reply = sentence 0). */
    if (tts_seg_ensure_open() != 0) {
        tts_fail("segment open failed");
        return;
    }
    ssize_t wr = bk_vfs_write(s_tts.seg_fd, payload, len);
    if (wr != (ssize_t)len) {
        LOGE("sd write %ld != %u\r\n", (long)wr, (unsigned)len);
        tts_fail("sd write failed");
        return;
    }
    s_tts.seg_bytes += (unsigned)len;
}

static void tts_on_message(const uint8_t *p, size_t len)
{
    if (len < 4 || p[0] != 0x11) {
        LOGW("bad frame header\r\n");
        return;
    }
    if ((p[2] & 0x0FU) != 0) {
        LOGW("compressed frame unsupported (comp=%u)\r\n", p[2] & 0x0FU);
        return;
    }
    uint8_t type = (uint8_t)(p[1] >> 4);
    uint8_t flags = (uint8_t)(p[1] & 0x0FU);
    uint32_t event = 0;
    const uint8_t *payload = NULL;
    size_t plen = 0;

    /* Error frames carry BE32 code + BE32 message size + message with NO
     * event field (flags=0, field 2026-11-12) -- the generic event layout
     * misparses the code as the payload size. */
    if (type == T_TYPE_ERROR) {
        size_t off = 4;
        if ((flags & 0x4U) != 0 && off + 4U <= len) {
            off += 4;
        }
        if ((flags & 0x3U) != 0 && off + 4U <= len) {
            off += 4;
        }
        uint32_t code = 0, mlen = 0;
        if (off + 8U <= len) {
            code = get_be32(p + off);
            mlen = get_be32(p + off + 4U);
            off += 8;
        }
        if (mlen > len - off) {
            mlen = (uint32_t)(len - off);
        }
        LOGW("server error frame: code=%lu msg=%.*s\r\n",
             (unsigned long)code, (int)mlen,
             off <= len ? (const char *)p + off : "");
        tts_fail("server error frame");
        return;
    }

    if (!tts_frame_payload(p, len, type, flags, &event, &payload, &plen)) {
        LOGW("unparsable frame (%u B)\r\n", (unsigned)len);
        return;
    }

    switch (type) {
    case T_TYPE_AUDIO:
        tts_on_audio(payload, plen);
        break;
    case T_TYPE_RESP:
        if (event == EV_CONN_STARTED) {
            if (s_tts.warm_only) {
                /* 预热连接：停在这里驻留，StartSession 等领养时带文本发 */
                LOGI("prewarmed connection parked\r\n");
                s_warm_state = TTS_WARM_PARKED;
                break;
            }
            LOGI("connection started\r\n");
            snprintf(s_json, sizeof(s_json),
                     "{\"user\":{\"uid\":\"bk7259\"},"
                     "\"namespace\":\"BidirectionalTTS\","
                     "\"req_params\":{\"speaker\":\"%s\","
                     "\"audio_params\":{\"format\":\"mp3\","
                     "\"sample_rate\":16000,\"speech_rate\":0,"
                     "\"loudness_rate\":0}}}",
                     s_tts.voice);
            if (tts_send_event(EV_START_SESSION, s_tts.session, s_json) != 0) {
                tts_fail("start session send failed");
            }
        } else if (event == EV_SESSION_STARTED) {
            LOGI("session started\r\n");
            /* Feed one TaskRequest per sentence: the server answers each
             * request with its own 350/351 boundary pair + audio run, and
             * it does NOT split a multi-sentence text by itself (field
             * 2026-11-12: whole reply arrived as ONE 85581-byte segment).
             * Billing is per character -- splitting costs nothing. */
            const char *p = s_tts.text;
            unsigned reqs = 0;
            bool send_fail = false;
            while (*p != '\0') {
                size_t sl = tts_sentence_len(p);
                if (sl == 0U) {
                    break;
                }
                snprintf(s_json, sizeof(s_json),
                         "{\"user\":{\"uid\":\"bk7259\"},"
                         "\"namespace\":\"BidirectionalTTS\","
                         "\"req_params\":{\"text\":\"%.*s\","
                         "\"speaker\":\"%s\","
                         "\"audio_params\":{\"format\":\"mp3\","
                         "\"sample_rate\":16000,\"speech_rate\":0,"
                         "\"loudness_rate\":0}}}",
                         (int)sl, p, s_tts.voice);
                if (tts_send_event(EV_TASK_REQUEST, s_tts.session,
                                   s_json) != 0) {
                    send_fail = true;
                    break;
                }
                reqs++;
                p += sl;
            }
            LOGI("task: %u sentence requests\r\n", reqs);
            if (send_fail ||
                tts_send_event(EV_FINISH_SESSION, s_tts.session, "{}") != 0) {
                tts_fail("task request send failed");
            }
        } else if (event == EV_SENTENCE_START) {
            /* New sentence begins: publish the previous one (when the
             * server omits its 351) and open a fresh file. If the ring
             * is full the fd stays open and the new sentence simply
             * appends to it (merged, still playable). */
            if (s_tts.seg_fd >= 0 && s_tts.seg_bytes > 0U) {
                unsigned pending;
                rtos_lock_mutex(&s_tts.seg_lock);
                pending = s_tts.seg_head - s_tts.seg_tail;
                rtos_unlock_mutex(&s_tts.seg_lock);
                if (pending < TTS_SEG_MAX) {
                    tts_seg_close_current(true);
                }
            }
            (void)tts_seg_ensure_open();
        } else if (event == EV_SENTENCE_END) {
            tts_seg_close_current(true);
        } else if (event == EV_SESSION_FINISHED) {
            tts_seg_close_current(true);   /* flush the tail sentence */
            LOGI("session finished: %u sentences\r\n", s_tts.seg_head);
            s_tts.done = true;
        } else if (event == EV_SESSION_FAILED ||
                   event == EV_SESSION_CANCELED ||
                   event == EV_CONN_FAILED) {
            LOGW("event %lu payload: %.*s\r\n", (unsigned long)event,
                 (int)(plen > 200U ? 200U : plen), (const char *)payload);
            tts_fail("server session event");
        } else {
            LOGD("resp event %lu (%u B)\r\n", (unsigned long)event,
                 (unsigned)plen);
        }
        break;
    case T_TYPE_ERROR:
        LOGW("server error frame: %.*s\r\n", (int)(plen > 200U ? 200U : plen),
             (const char *)payload);
        tts_fail("server error frame");
        break;
    default:
        break;
    }
}

static void tts_ws_event(void *, char *, int32_t event_id, void *event_data)
{
    if (!s_tts.active) {
        return;
    }
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        LOGI("ws connected\r\n");
        if (tts_send_event(EV_START_CONN, NULL, "{}") != 0) {
            tts_fail("start connection send failed");
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
        if (s_tts.warm_only) {
            /* 驻留连接夭折：不是会话失败，交给 prewarm 线程拆除 */
            s_warm_state = TTS_WARM_DEAD;
            break;
        }
        if (!s_tts.done) {
            tts_fail("connection dropped");
        }
        break;
    case WEBSOCKET_EVENT_DATA: {
        bk_websocket_event_data_t *d =
            (bk_websocket_event_data_t *)event_data;
        if (d == NULL || d->op_code != WS_TRANSPORT_OPCODES_BINARY ||
            d->data_ptr == NULL || d->data_len <= 0 || d->payload_len <= 0) {
            break;
        }
        if ((size_t)d->payload_len > TTS_RX_BYTES) {
            tts_fail("oversized frame");
            break;
        }
        s_tts.last_rx = rtos_get_time();
        memcpy(s_tts.rx + s_tts.rx_len, d->data_ptr, (size_t)d->data_len);
        s_tts.rx_len += (size_t)d->data_len;
        if (s_tts.rx_len >= (size_t)d->payload_len) {
            tts_on_message(s_tts.rx, s_tts.rx_len);
            s_tts.rx_len = 0;
        }
        break;
    }
    default:
        break;
    }
}

/* 预热线程：建连并驻留（最多 TTS_PREWARM_PARK_MS）。被领养则 ctx 整体
 * 移交 synthesize（线程直接退出，不碰资源）；否则自己拆除干净。 */
static void tts_prewarm_main(void *arg)
{
    (void)arg;
    extern const char *muse_voice_volc_tts_key(void);
    extern const char *muse_voice_volc_appid(void);
    websocket_client_input_t input;

    const char *key = muse_voice_volc_tts_key();
    bool api_key_mode = key != NULL && strchr(key, '-') != NULL;
    const char *appid = muse_voice_volc_appid();
    if (key == NULL || key[0] == '\0' ||
        (!api_key_mode && (appid == NULL || appid[0] == '\0'))) {
        s_warm_state = TTS_WARM_NONE;
        rtos_delete_thread(NULL);
        return;
    }

    memset(&s_tts, 0, sizeof(s_tts));
    s_tts.seg_fd = -1;
    snprintf(s_tts.api_key, sizeof(s_tts.api_key), "%s", key);
    s_tts.warm_only = true;
    s_tts.watchdog_ms = TTS_IDLE_TIMEOUT_MS;
    {
        uint32_t tt = rtos_get_time();
        snprintf(s_tts.session, sizeof(s_tts.session),
                 "%08lx%08lx%08lx%08lx", (unsigned long)tt,
                 (unsigned long)(tt * 2654435761U),
                 (unsigned long)(tt ^ 0x5a5a5a5aU),
                 (unsigned long)(tt * 40503U));
        s_tts.last_rx = tt;
    }
    s_tts.rx = (uint8_t *)psram_malloc(TTS_RX_BYTES);
    s_tts.frame = (uint8_t *)psram_malloc(TTS_FRAME_CAP);
    if (s_tts.rx == NULL || s_tts.frame == NULL ||
        rtos_init_mutex(&s_tts.seg_lock) != BK_OK) {
        LOGE("prewarm alloc failed\r\n");
        goto warm_out;
    }
    {
        char headers[384];
        if (api_key_mode) {
            snprintf(headers, sizeof(headers),
                     "X-Api-Key: %s\r\n"
                     "X-Api-Resource-Id: %s\r\n"
                     "X-Api-Connect-Id: %s\r\n",
                     s_tts.api_key, CONFIG_MUSE_TTS_VOLC_RESOURCE_ID,
                     s_tts.session);
        } else {
            snprintf(headers, sizeof(headers),
                     "X-Api-App-Key: %s\r\n"
                     "X-Api-Access-Key: %s\r\n"
                     "X-Api-Resource-Id: %s\r\n"
                     "X-Api-Connect-Id: %s\r\n",
                     appid, s_tts.api_key, CONFIG_MUSE_TTS_VOLC_RESOURCE_ID,
                     s_tts.session);
        }
        memset(&input, 0, sizeof(input));
        input.uri = CONFIG_MUSE_TTS_VOLC_URL;
        input.enable_auto_reconnect = false;
        input.rx_retry = 10;
        input.user_context = NULL;
        input.buffer_size = TTS_WS_BUF;
        input.headers = headers;
        input.ws_event_handler = tts_ws_event;
        s_tts.client = websocket_client_init(&input);
    }
    if (s_tts.client == NULL) {
        LOGE("prewarm ws init failed\r\n");
        goto warm_out;
    }
    s_tts.active = true;
    if (websocket_client_start(s_tts.client) != BK_OK) {
        LOGE("prewarm ws start failed\r\n");
        s_tts.active = false;
        (void)websocket_client_destroy(s_tts.client);
        s_tts.client = NULL;
        goto warm_out;
    }
    LOGI("tts prewarm: connecting\r\n");

    {
        uint32_t deadline = rtos_get_time() + TTS_PREWARM_PARK_MS;
        while (s_warm_state == TTS_WARM_CONNECTING ||
               s_warm_state == TTS_WARM_PARKED) {
            if ((int32_t)(rtos_get_time() - deadline) >= 0) {
                break;
            }
            rtos_delay_milliseconds(50);
        }
    }
    if (s_warm_state == TTS_WARM_ADOPTED) {
        LOGI("tts prewarm: adopted by synthesize\r\n");
        rtos_delete_thread(NULL);   /* ctx 归 synthesize 所有，不得再碰 */
    }
    if (s_warm_state != TTS_WARM_DEAD) {
        LOGI("tts prewarm: park timeout, closing\r\n");
    }
    s_tts.active = false;
    (void)websocket_client_destroy(s_tts.client);
    s_tts.client = NULL;

warm_out:
    if (s_tts.rx != NULL) {
        psram_free(s_tts.rx);
        s_tts.rx = NULL;
    }
    if (s_tts.frame != NULL) {
        psram_free(s_tts.frame);
        s_tts.frame = NULL;
    }
    rtos_deinit_mutex(&s_tts.seg_lock);
    s_warm_state = TTS_WARM_NONE;
    rtos_delete_thread(NULL);
}

void muse_tts_volc_prewarm(void)
{
    extern const char *muse_voice_volc_tts_key(void);
    beken_thread_t t;
    const char *key = muse_voice_volc_tts_key();

    if (key == NULL || key[0] == '\0' || s_busy ||
        s_warm_state != TTS_WARM_NONE) {
        return;
    }
    s_warm_state = TTS_WARM_CONNECTING;
    /* 8 KB: mbedTLS 握手与 tts_volc 线程同级需求。 */
    if (rtos_create_thread(&t, 4, "tts_warm", tts_prewarm_main,
                           8192, NULL) != BK_OK) {
        s_warm_state = TTS_WARM_NONE;
    }
}

int muse_tts_volc_synthesize_stream(const char *text,
                                    muse_tts_volc_seg_cb cb, void *user)
{
    extern const char *muse_voice_volc_tts_key(void);
    extern const char *muse_voice_volc_appid(void);
    extern const char *muse_voice_volc_tts_voice(void);
    websocket_client_input_t input;
    unsigned delivered = 0;
    int rc = -1;

    if (text == NULL || text[0] == '\0' || cb == NULL) {
        return -1;
    }
    const char *key = muse_voice_volc_tts_key();
    if (key == NULL || key[0] == '\0') {
        LOGE("no volc TTS key -- run 'muse ttskey <key>' first\r\n");
        return -2;
    }
    /* Speaker: CLI (`muse ttsvoice`) overrides the Kconfig default. */
    const char *voice = muse_voice_volc_tts_voice();
    if (voice == NULL || voice[0] == '\0') {
        voice = CONFIG_MUSE_TTS_VOLC_VOICE;
    }
    /* Same dual-credential rule as the ASR client: dashed UUID = X-Api-Key,
     * 32-char token = old-console App-Key pair. */
    bool api_key_mode = strchr(key, '-') != NULL;
    const char *appid = muse_voice_volc_appid();
    if (!api_key_mode && (appid == NULL || appid[0] == '\0')) {
        LOGE("no volc APP ID -- run 'muse volcappid <id>' first\r\n");
        return -2;
    }
    if (s_busy) {
        return -3;
    }
    s_busy = true;

    /* 领养预热连接：PARKED 直接接管；CONNECTING 给 2 秒机会；其他情况
     * （预热夭折/被放弃）让 prewarm 线程自己拆完再全新建连，避免踩它
     * 的 ctx。 */
    bool adopted = false;
    if (s_warm_state == TTS_WARM_CONNECTING) {
        uint32_t dl = rtos_get_time() + 2000U;
        while (s_warm_state == TTS_WARM_CONNECTING &&
               (int32_t)(rtos_get_time() - dl) < 0) {
            rtos_delay_milliseconds(50);
        }
    }
    if (s_warm_state == TTS_WARM_PARKED) {
        s_warm_state = TTS_WARM_ADOPTED;
        s_tts.warm_only = false;
        adopted = true;
        LOGI("tts: adopt prewarmed connection\r\n");
    } else if (s_warm_state != TTS_WARM_NONE) {
        s_warm_state = TTS_WARM_DEAD;   /* 通知 prewarm 线程拆除 */
        uint32_t dl = rtos_get_time() + 1500U;
        while (s_warm_state != TTS_WARM_NONE &&
               (int32_t)(rtos_get_time() - dl) < 0) {
            rtos_delay_milliseconds(20);
        }
        if (s_warm_state != TTS_WARM_NONE) {
            LOGW("prewarm teardown stuck; skip this turn\r\n");
            s_busy = false;
            return -4;
        }
    }

    if (!adopted) {
        memset(&s_tts, 0, sizeof(s_tts));
        s_tts.seg_fd = -1;
        snprintf(s_tts.api_key, sizeof(s_tts.api_key), "%s", key);
        s_tts.last_rx = rtos_get_time();
        s_tts.rx = (uint8_t *)psram_malloc(TTS_RX_BYTES);
        s_tts.frame = (uint8_t *)psram_malloc(TTS_FRAME_CAP);
        if (s_tts.rx == NULL || s_tts.frame == NULL) {
            LOGE("alloc failed\r\n");
            goto out;
        }
        if (rtos_init_mutex(&s_tts.seg_lock) != BK_OK) {
            LOGE("mutex init failed\r\n");
            goto out;
        }
    }
    snprintf(s_tts.voice, sizeof(s_tts.voice), "%s", voice);
    s_tts.watchdog_ms = TTS_IDLE_TIMEOUT_MS;
    /* JSON-escape the reply (quotes/backslashes/control chars). */
    {
        size_t ei = 0;
        for (const char *p = text; *p != '\0' && ei < sizeof(s_tts.text) - 2U;
             p++) {
            if (*p == '"' || *p == '\\') {
                s_tts.text[ei++] = '\\';
            }
            if ((uint8_t)*p >= 0x20U) {
                s_tts.text[ei++] = *p;
            }
        }
        s_tts.text[ei] = '\0';
    }

    /* Whole-session retry: if the attempt dies with NOTHING delivered yet
     * (dropped connection / idle timeout / server error during a WiFi
     * stall -- field 2026-11-12: request sent fine, every response byte
     * lost in a ~10 s RF outage that also killed the idle chat socket), a
     * fresh connection usually succeeds immediately. Once any sentence
     * has been played there is no retry (no double-speaking). */
    for (unsigned attempt = 0U; attempt < 2U && delivered == 0U; attempt++) {
        if (attempt > 0U) {
            LOGW("retrying tts session (attempt %u/2)\r\n", attempt + 1U);
            rtos_delay_milliseconds(500);
        }
        /* Per-attempt state: fresh session id (the server may still hold
         * the corpse of the previous one), fresh segment bookkeeping. */
        {
            uint32_t tt = rtos_get_time();
            snprintf(s_tts.session, sizeof(s_tts.session),
                     "%08lx%08lx%08lx%08lx", (unsigned long)tt,
                     (unsigned long)(tt * 2654435761U),
                     (unsigned long)(tt ^ 0x5a5a5a5aU),
                     (unsigned long)(tt * 40503U));
        }
        s_tts.last_rx = rtos_get_time();
        s_tts.done = false;
        s_tts.failed = false;
        s_tts.seg_fd = -1;
        s_tts.seg_idx = 0;
        s_tts.seg_bytes = 0;
        rtos_lock_mutex(&s_tts.seg_lock);
        s_tts.seg_head = 0;
        s_tts.seg_tail = 0;
        rtos_unlock_mutex(&s_tts.seg_lock);

        bool io = false;
        s_tts.watchdog_ms = (attempt == 0U && adopted) ?
                            TTS_ADOPT_TIMEOUT_MS : TTS_IDLE_TIMEOUT_MS;
        if (attempt == 0U && adopted) {
            /* 预热连接：50 已收过，直接 StartSession（音色参数此刻才确
             * 定）；服务器回 150 后事件机照常发 TaskRequest/Finish。 */
            snprintf(s_json, sizeof(s_json),
                     "{\"user\":{\"uid\":\"bk7259\"},"
                     "\"namespace\":\"BidirectionalTTS\","
                     "\"req_params\":{\"speaker\":\"%s\","
                     "\"audio_params\":{\"format\":\"mp3\","
                     "\"sample_rate\":16000,\"speech_rate\":0,"
                     "\"loudness_rate\":0}}}",
                     s_tts.voice);
            if (tts_send_event(EV_START_SESSION, s_tts.session,
                               s_json) == 0) {
                io = true;
            } else {
                tts_fail("start session send failed");
            }
        } else {
            char headers[384];
            if (api_key_mode) {
                snprintf(headers, sizeof(headers),
                         "X-Api-Key: %s\r\n"
                         "X-Api-Resource-Id: %s\r\n"
                         "X-Api-Connect-Id: %s\r\n",
                         s_tts.api_key, CONFIG_MUSE_TTS_VOLC_RESOURCE_ID,
                         s_tts.session);
            } else {
                snprintf(headers, sizeof(headers),
                         "X-Api-App-Key: %s\r\n"
                         "X-Api-Access-Key: %s\r\n"
                         "X-Api-Resource-Id: %s\r\n"
                         "X-Api-Connect-Id: %s\r\n",
                         appid, s_tts.api_key,
                         CONFIG_MUSE_TTS_VOLC_RESOURCE_ID, s_tts.session);
            }
            memset(&input, 0, sizeof(input));
            input.uri = CONFIG_MUSE_TTS_VOLC_URL;
            input.enable_auto_reconnect = false;
            input.rx_retry = 10;
            input.user_context = NULL;
            input.buffer_size = TTS_WS_BUF;
            input.headers = headers;
            input.ws_event_handler = tts_ws_event;
            s_tts.client = websocket_client_init(&input);
            if (s_tts.client == NULL) {
                LOGE("ws init failed\r\n");
                break;
            }
            if (attempt == 0U) {
                LOGI("tts start: mode=%s voice=%s\r\n",
                     api_key_mode ? "api-key" : "app-token", voice);
            }
            s_tts.active = true;
            if (websocket_client_start(s_tts.client) == BK_OK) {
                io = true;
            } else {
                LOGE("ws start failed\r\n");
            }
        }
        if (io) {
            /* Playback drain loop: hand each completed sentence to the
             * caller as soon as it lands (the websocketc task keeps
             * receiving in parallel), then stop at end-of-stream. The
             * watchdog only applies while the session is still running --
             * draining after done has no time limit. */
            uint32_t start = rtos_get_time();
            char path[64];

            for (;;) {
                if (tts_seg_ring_pop(path, sizeof(path))) {
                    cb(user, delivered, path, false);
                    (void)bk_vfs_unlink(path);
                    delivered++;
                    continue;
                }
                if (s_tts.done) {
                    break;
                }
                uint32_t now = rtos_get_time();
                if ((int32_t)(now - s_tts.last_rx) >=
                        (int32_t)s_tts.watchdog_ms ||
                    (int32_t)(now - start) >= (int32_t)TTS_TOTAL_TIMEOUT_MS) {
                    tts_fail("rx idle/total timeout");
                    break;
                }
                rtos_delay_milliseconds(20);
            }
        }
        s_tts.active = false;
        /* Signal end-of-stream before the slow teardown below (TLS close /
         * select) so the caller can restore KWS standby earlier. The tail
         * call site still fires again; the voice side is idempotent. */
        cb(user, delivered, NULL, true);
        (void)websocket_client_destroy(s_tts.client);
        s_tts.client = NULL;

        /* Reclaim anything left unconsumed (playback cut short, or a
         * failed attempt that still produced files) so the next attempt
         * starts with an empty ring and no orphan files. */
        {
            char path[64];
            while (tts_seg_ring_pop(path, sizeof(path))) {
                (void)bk_vfs_unlink(path);
            }
        }
    }

    cb(user, delivered, NULL, true);   /* end-of-stream marker */
    if (delivered > 0U) {
        LOGI("tts done: %u sentences delivered\r\n", delivered);
        rc = 0;
    }

out:
    rtos_deinit_mutex(&s_tts.seg_lock);
    if (s_tts.rx != NULL) {
        psram_free(s_tts.rx);
    }
    if (s_tts.frame != NULL) {
        psram_free(s_tts.frame);
    }
    s_tts.client = NULL;
    s_warm_state = TTS_WARM_NONE;   /* 本轮（含领养）结束，允许下次预热 */
    s_busy = false;
    return rc;
}