/*
 * @file muse_asr_volc.c
 * @brief Volcano streaming ASR (v3 SAUC bigmodel) client.
 *
 * Wire format (all integers big-endian), verified against the volc v3
 * doc on 2026-11-10:
 *   header[4]: 0x11 | (type<<4 | flags) | (ser<<4 | comp) | 0
 *   types: 1=full client request (config), 2=audio only,
 *          9=full server response, 15=server error
 *   flags: 0x01 seq field present (BE int32 follows the header),
 *          0x02 last packet (audio tail uses negative seq; response
 *          bit1 also marks the final result)
 *   WS frame delimits the payload -- no length field anywhere.
 *
 * Session memory budget (~128 KB total): 96 KB PCM ring (3 s, covers slow
 * handshakes) + 6 KB rx reassembly + 2x6656 B bk_websocket rx/tx +
 * 6.2 KB frame scratch + 512 B text. Allocated on start, freed after a
 * post-destroy grace delay (the ws client finishes self-destruction on
 * its own task after destroy returns). */
#include "muse_asr_volc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <os/os.h>
#include <components/log.h>
#include "bk_websocket_client.h"
#include "cJSON.h"

#define TAG "muse_asr_volc"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define VOLC_URI          "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel"
/* 96 KB = 3 s of PCM: enough to hold speech across a slow handshake
 * (6 s connects happen on a congested link, field 2026-11-12). Hard
 * dropping pre-config audio instead loses the whole utterance when the
 * user finishes before the upgrade completes. */
#define VOLC_RING_BYTES   (96U * 1024U)
#define VOLC_RX_BYTES     (6U * 1024U)
#define VOLC_WS_BUF       6656
#define VOLC_PCM_PER_SEND (16000U * 2U * 192U / 1000U)  /* 6144 B = 192 ms */
#define VOLC_FRAME_CAP    (VOLC_PCM_PER_SEND + 16U)
#define VOLC_TEXT_MAX     512U
#define VOLC_SEND_MS      192U
/* 服务器静默判负时限：ring 里还有没发完的音频，而服务器 >10s 没回任何
 * 包（健康流 partial 每 200-600ms 一到）——说明包根本没到对端：RF 死亡
 * 时 lwip send 成功只是进了 TCP 重传队列，服务器实际一直收不到。等下去
 * 只会撞上服务端 ~15-20s 的"等下一包"超时（45000081）再被拒——本地
 * 先判负，用户早 15 秒得到反馈（2026-11-12 实机：25s 静默尾包被拒）。
 * 必须带 ring 非空条件：用户长停顿期间服务器本就不发 partial。 */
#define VOLC_BACKLOG_TIMEOUT_MS 10000U
#define VOLC_FINAL_WAIT_MS 10000U
#define VOLC_TEARDOWN_GRACE_MS 200U

/* Frame header byte 1: message type (high nibble) + flags (low nibble). */
#define F_TYPE_CONFIG  0x1U
#define F_TYPE_AUDIO   0x2U
#define F_TYPE_RESP    0x9U
#define F_TYPE_ERROR   0xFU
#define F_FLAG_SEQ     0x1U
#define F_FLAG_LAST    0x2U

typedef struct {
    /* Lifecycle */
    bool        active;
    bool        dead;        /* teardown started: events/feed are ignored */
    bool        cb_fired;    /* exactly-one callback guarantee */
    uint32_t    last_rx;     /* 最近一次收到服务器任何帧的时刻 */
    bool        config_sent;
    bool        finishing;
    bool        done;        /* final text received (or session failed) */
    /* CLOSED 已由 ws 任务派发：SDK 随后会 free_client（销毁 mutex、释放
     * client），此时 abort 再 destroy 就是 use-after-free（2026-11-13
     * dump：xQueueSemaphoreTake:1797，abort 拿到 sockfd=0 的野 client）。 */
    bool        ws_closed;
    bool        tail_sent;
    bool        audio_started; /* first audio frame out (liveness marker) */
    uint32_t    final_deadline;

    /* Field instrumentation (2026-11-12: a session streamed fine but the
     * server returned an EMPTY final -- these counters distinguish "we fed
     * silence" from "we fed real speech the server dismissed"). */
    uint32_t    fed_bytes;
    uint32_t    sent_frames;
    int16_t     peak;

    transport   client;
    beken_thread_t sender;
    beken_semaphore_t kick;
    beken_mutex_t ring_lock;
    beken_mutex_t send_lock;

    /* PCM ring (feed -> sender) */
    uint8_t    *ring;
    size_t      ring_r;
    size_t      ring_w;
    uint32_t    dropped;

    /* WS rx reassembly (payload_len/payload_offset chunking) */
    uint8_t    *rx;
    size_t      rx_len;

    /* Scratch */
    uint8_t    *frame;
    /* Sender staging block; a 6 KB stack array here overflowed the 3 KB
     * sender stack (UsageFault STKOF in the field, 2026-11-10). */
    uint8_t    *chunk;
    char        text[VOLC_TEXT_MAX];

    int32_t     seq;
    char        api_key[96];

    muse_asr_volc_text_cb_t  on_text;
    muse_asr_volc_error_cb_t on_error;
} volc_ctx_t;

static volc_ctx_t s_volc;
/* 独立于 s_volc：abort 末尾会 memset(&s_volc)，而 ws 任务可能在之后才
 * 派发 CLOSED，所以这把锁必须活过整个模块生命周期（只 init 不解构）。 */
static beken_mutex_t s_volc_ws_lock;

static void volc_fire_text(const char *text)
{
    muse_asr_volc_text_cb_t cb;
    if (s_volc.cb_fired) {
        return;
    }
    s_volc.cb_fired = true;
    s_volc.done = true;
    cb = s_volc.on_text;
    if (cb != NULL && text != NULL && text[0] != '\0') {
        cb(text);
    } else if (!s_volc.cb_fired) {
        s_volc.cb_fired = true;
    }
}

static void volc_fire_error(int err, const char *msg)
{
    muse_asr_volc_error_cb_t cb;
    if (s_volc.cb_fired) {
        return;
    }
    s_volc.cb_fired = true;
    s_volc.done = true;
    cb = s_volc.on_error;
    if (cb != NULL) {
        cb(err, msg);
    }
}

/* ---- frame build/send -----------------------------------------------------*/

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* One WS binary message = header(+seq) + payload_size(4) + payload, built
 * into frame[]. */
static int volc_send_frame(uint8_t type, uint8_t flags, int32_t seq,
                           const uint8_t *payload, size_t len)
{
    uint8_t *f = s_volc.frame;
    size_t off = 0;
    int rc;

    if (s_volc.client == NULL || s_volc.dead) {
        return -1;
    }
    f[0] = 0x11;
    f[1] = (uint8_t)((type << 4) | (flags & 0x0F));
    /* JSON serialization for the config frame, raw for audio; never
     * compressed. */
    f[2] = (type == F_TYPE_AUDIO) ? 0x00 : (uint8_t)(0x01U << 4);
    f[3] = 0;
    off = 4;
    if (flags & F_FLAG_SEQ) {
        put_be32(f + off, (uint32_t)seq);
        off += 4;
    }
    /* 4-byte BE payload size between header extensions and the body --
     * omitting it made the server parse the JSON head as the size and
     * reject with "declared body size does not match actual body size"
     * (field 2026-11-12). */
    put_be32(f + off, (uint32_t)len);
    off += 4;
    if (len > 0) {
        memcpy(f + off, payload, len);
        off += len;
    }
    rtos_lock_mutex(&s_volc.send_lock);
    rc = websocket_client_send_binary(s_volc.client, (char *)f, (int)off, 2000);
    rtos_unlock_mutex(&s_volc.send_lock);
    if (rc != (int)off) {
        LOGE("ws send failed (%d != %u)\r\n", rc, (unsigned)off);
        return -2;
    }
    return 0;
}

/* 瞬时拥塞（SDK 在 KEEP_CONN_ON_SEND_TIMEOUT 下只报失败不杀连接）不等于
 * 回合死亡：每 200ms 重发同一帧，最多 ~3 秒。ring 里本就有 ~3 秒音频，
 * WiFi 停顿短于此就能原地恢复（2026-11-12：~1.5 秒停顿曾整回合丢失，
 * "asr error -5"）。真断线时 volc_send_frame 内部立即失败，3 秒后放弃，
 * 不会卡死发送线程。 */
static int volc_send_frame_retry(uint8_t type, uint8_t flags, int32_t seq,
                                 const uint8_t *payload, size_t len)
{
    uint32_t start = rtos_get_time();
    int rc;

    for (;;) {
        rc = volc_send_frame(type, flags, seq, payload, len);
        if (rc == 0 || !s_volc.active || s_volc.cb_fired) {
            return rc;
        }
        if ((int32_t)(rtos_get_time() - start) >= 3000) {
            return rc;
        }
        rtos_delay_milliseconds(200);
    }
}

static void volc_send_config(void)
{
    static const char kConfig[] =
        "{\"user\":{\"uid\":\"bk7259-muse\"},"
        "\"audio\":{\"format\":\"pcm\",\"rate\":16000,\"bits\":16,"
        "\"channel\":1,\"codec\":\"raw\"},"
        "\"request\":{\"model_name\":\"bigmodel\",\"enable_itn\":true,"
        "\"enable_punc\":true,\"enable_ddup\":true}}";
    if (volc_send_frame(F_TYPE_CONFIG, F_FLAG_SEQ, 1,
                        (const uint8_t *)kConfig,
                        sizeof(kConfig) - 1U) == 0) {
        s_volc.config_sent = true;
        LOGI("config frame sent (%u B)\r\n", (unsigned)(sizeof(kConfig) - 1U));
    } else {
        volc_fire_error(-2, "config send failed");
    }
}

/* ---- response parsing ------------------------------------------------------*/

/* Accumulate payload_msg.text; finalize on header last-flag or
 * is_last_package. cJSON needs a NUL terminator: rx leaves 1 spare byte. */
static void volc_on_frame(uint8_t type, uint8_t flags,
                          const uint8_t *payload, size_t len)
{
    if (type == F_TYPE_ERROR) {
        /* Error payload is binary: 4-byte BE error code + 4-byte BE message
         * size + message. Printing it as a C string shows garbage (field:
         * 2026-11-12, first 2.0 session died here invisibly). */
        uint32_t code = 0;
        uint32_t msg_len = 0;
        if (len >= 8U) {
            code = ((uint32_t)payload[0] << 24) | ((uint32_t)payload[1] << 16) |
                   ((uint32_t)payload[2] << 8) | (uint32_t)payload[3];
            msg_len = ((uint32_t)payload[4] << 24) |
                      ((uint32_t)payload[5] << 16) |
                      ((uint32_t)payload[6] << 8) | (uint32_t)payload[7];
        }
        if (msg_len > len - 8U) {
            msg_len = (uint32_t)(len - 8U);
        }
        LOGW("server error frame: code=%lu msg=%.*s\r\n",
             (unsigned long)code, (int)msg_len,
             len >= 8U ? (const char *)payload + 8 : "");
        volc_fire_error(-3, "volc error frame");
        return;
    }
    if (type != F_TYPE_RESP || payload == NULL || len == 0) {
        return;
    }
    cJSON *root = cJSON_Parse((const char *)payload);
    if (root == NULL) {
        LOGW("unparseable response frame\r\n");
        return;
    }
    cJSON *msg = cJSON_GetObjectItemCaseSensitive(root, "payload_msg");
    bool last = (flags & F_FLAG_LAST) != 0;
    if (cJSON_IsObject(msg)) {
        /* 1.0 shape: {"payload_msg":{"text": <fragment>, "is_last_package"}} --
         * fragments are appended. */
        cJSON *t = cJSON_GetObjectItemCaseSensitive(msg, "text");
        cJSON *e = cJSON_GetObjectItemCaseSensitive(msg, "is_last_package");
        if (cJSON_IsString(t) && t->valuestring != NULL &&
            t->valuestring[0] != '\0') {
            size_t used = strlen(s_volc.text);
            size_t add = strlen(t->valuestring);
            if (used + add < sizeof(s_volc.text) - 1U) {
                memcpy(s_volc.text + used, t->valuestring, add + 1U);
            }
            LOGI("asr partial: %s\r\n", s_volc.text);
        }
        if (cJSON_IsBool(e)) {
            last = last || cJSON_IsTrue(e);
        }
    } else {
        /* 2.0 shape: {"audio_info":{...},"result":{"text": <cumulative>,
         * "utterances":[...]}} -- text is the full transcript so far, so it
         * REPLACES the accumulator (field 2026-11-12: parsing 2.0 with the
         * 1.0 shape yielded an empty final despite a perfect transcript). */
        cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
        if (cJSON_IsObject(result)) {
            cJSON *t = cJSON_GetObjectItemCaseSensitive(result, "text");
            if (cJSON_IsString(t) && t->valuestring != NULL &&
                t->valuestring[0] != '\0') {
                snprintf(s_volc.text, sizeof(s_volc.text), "%s",
                         t->valuestring);
                LOGI("asr partial: %s\r\n", s_volc.text);
            }
        }
    }
    cJSON_Delete(root);
    if (last) {
        LOGI("asr final: %s\r\n", s_volc.text);
        /* Raw payload when the text is empty: the server's own account of
         * what it heard (utterances/durations) decides whether we fed
         * silence or the model dismissed real speech. */
        if (s_volc.text[0] == '\0') {
            LOGW("asr final raw: %.*s\r\n", (int)(len > 300U ? 300U : len),
                 (const char *)payload);
        }
        volc_fire_text(s_volc.text);
    }
}

/* ---- websocket events ------------------------------------------------------*/

static void volc_ws_event(void *, char *, int32_t event_id, void *event_data)
{
    /* CLOSED 必须在 active/dead 判断之前记录：abort 会把 dead 置 true，
     * 但 SDK 的 ws 任务在这个回调返回后仍然会 free_client。这里持锁置位，
     * 与 abort 里的 destroy 互斥，确保不会与 free_client 并发。 */
    if (event_id == WEBSOCKET_EVENT_CLOSED) {
        rtos_lock_mutex(&s_volc_ws_lock);
        s_volc.ws_closed = true;
        rtos_unlock_mutex(&s_volc_ws_lock);
    }
    if (!s_volc.active || s_volc.dead) {
        return;
    }
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        LOGI("ws connected\r\n");
        volc_send_config();
        rtos_set_semaphore(&s_volc.kick);
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
        /* A refused handshake (the volc 403) lands here before CONNECTED. */
        LOGE("ws event %ld before/without session end\r\n", (long)event_id);
        if (!s_volc.config_sent) {
            volc_fire_error(-4, "handshake refused (403? service not activated)");
        } else if (!s_volc.cb_fired) {
            volc_fire_error(-5, "connection dropped");
        }
        break;
    case WEBSOCKET_EVENT_DATA: {
        bk_websocket_event_data_t *d =
            (bk_websocket_event_data_t *)event_data;
        if (d == NULL || d->op_code != WS_TRANSPORT_OPCODES_BINARY ||
            d->data_ptr == NULL || d->data_len <= 0 || d->payload_len <= 0) {
            break;
        }
        if ((size_t)d->payload_len > VOLC_RX_BYTES) {
            volc_fire_error(-6, "oversized response");
            break;
        }
        s_volc.last_rx = rtos_get_time();   /* 静默判负的活性戳 */
        memcpy(s_volc.rx + s_volc.rx_len, d->data_ptr, (size_t)d->data_len);
        s_volc.rx_len += (size_t)d->data_len;
        if (s_volc.rx_len >= (size_t)d->payload_len) {
            const uint8_t *p = s_volc.rx;
            size_t avail = s_volc.rx_len;
            if (avail < 4 || p[0] != 0x11) {
                volc_fire_error(-6, "bad response header");
            } else {
                uint8_t type = (uint8_t)(p[1] >> 4);
                uint8_t flags = (uint8_t)(p[1] & 0x0F);
                size_t off = 4;
                if ((flags & F_FLAG_SEQ) != 0) {
                    off += 4;   /* seq: informative only */
                }
                if (off <= avail) {
                    /* Response frames carry a 4-byte BE payload size after
                     * the header extensions; skip it (error frames keep
                     * their own code+size layout, parsed in volc_on_frame). */
                    if (type == F_TYPE_RESP) {
                        if (off + 4U > avail) {
                            volc_fire_error(-6, "truncated response header");
                            s_volc.rx_len = 0;
                            break;
                        }
                        off += 4;
                    }
                    volc_on_frame(type, flags, p + off, avail - off);
                }
            }
            s_volc.rx_len = 0;
        }
        break;
    }
    default:
        break;
    }
}

/* ---- sender task -----------------------------------------------------------*/

static void volc_sender_main(void *arg)
{
    (void)arg;
    while (s_volc.active && !s_volc.dead) {
        (void)rtos_get_semaphore(&s_volc.kick, VOLC_SEND_MS);

        if (!s_volc.config_sent || s_volc.cb_fired) {
            continue;
        }
        /* 服务器静默判负：ring 还有音频没发完，而服务器 >10s 没回任何
         * 帧——我们的包根本没到对端（RF 死亡时 lwip send 成功只是进了
         * TCP 重传队列）。继续渗发只会撞上服务端"等下一包"超时再被拒，
         * 本地先判负（2026-11-12：25s 静默后尾包被 45000081 拒收）。 */
        if (s_volc.ring_r != s_volc.ring_w &&
            (int32_t)(rtos_get_time() - s_volc.last_rx) >=
                (int32_t)VOLC_BACKLOG_TIMEOUT_MS) {
            volc_fire_error(-8, "server silent, tx not landing");
            continue;
        }
        for (;;) {
            size_t n = 0;
            rtos_lock_mutex(&s_volc.ring_lock);
            while (n < VOLC_PCM_PER_SEND) {
                size_t avail = (s_volc.ring_w + VOLC_RING_BYTES -
                                s_volc.ring_r) % VOLC_RING_BYTES;
                if (avail == 0) {
                    break;
                }
                s_volc.chunk[n++] = s_volc.ring[s_volc.ring_r];
                s_volc.ring_r = (s_volc.ring_r + 1U) % VOLC_RING_BYTES;
            }
            rtos_unlock_mutex(&s_volc.ring_lock);
            if (n == 0) {
                break;
            }
            if (!s_volc.audio_started) {
                /* Liveness marker: if this never prints, the sender lost the
                 * creation race (fixed) or the ring is never fed. */
                s_volc.audio_started = true;
                LOGI("audio streaming to volc\r\n");
            }
            if (++s_volc.seq <= 0) {
                s_volc.seq = 1;   /* keep audio seq positive */
            }
            if (volc_send_frame_retry(F_TYPE_AUDIO, F_FLAG_SEQ, s_volc.seq,
                                      s_volc.chunk, n) != 0) {
                volc_fire_error(-7, "audio send failed");
                break;
            }
            s_volc.sent_frames++;
            /* Pace by audio duration (n bytes of 16 kHz/16-bit PCM = n/32
             * ms): the average send rate then equals realtime, so even a
             * full-ring backlog from a slow handshake drains without ever
             * over-running the lwip send buffer. A fixed 30 ms delay ran at
             * ~77 KB/s into a ~27 KB/s uplink and killed the session with
             * EINPROGRESS within a second (field 2026-11-12). */
            rtos_delay_milliseconds(n / 32U < 10U ? 10U : n / 32U);
        }
        /* Ring drained and the caller asked to finish: send the tail
         * exactly once. The server validates the tail sequence as the
         * negative of the next sequence number (-(last+1)); a constant -1
         * is rejected with "autoAssignedSequence (-321) mismatch sequence
         * in request (-1)" (field 2026-11-12). */
        if (s_volc.finishing && !s_volc.tail_sent &&
            s_volc.ring_r == s_volc.ring_w) {
            int32_t tail_seq = -(s_volc.seq + 1);
            s_volc.tail_sent = true;
            LOGI("tail packet sent (seq=%ld)\r\n", (long)tail_seq);
            if (volc_send_frame_retry(F_TYPE_AUDIO,
                                      F_FLAG_SEQ | F_FLAG_LAST, tail_seq,
                                      NULL, 0) != 0) {
                volc_fire_error(-7, "tail send failed");
            }
        }
    }
    rtos_delete_thread(NULL);
}

/* ---- public API ------------------------------------------------------------*/

uint32_t muse_asr_volc_last_rx_ms(void)
{
    return s_volc.last_rx;
}

void muse_asr_volc_init(muse_asr_volc_text_cb_t on_text,
                        muse_asr_volc_error_cb_t on_error)
{
    s_volc.on_text = on_text;
    s_volc.on_error = on_error;
    if (rtos_init_mutex(&s_volc_ws_lock) != BK_OK) {
        LOGE("asr ws lock init failed\r\n");
    }
}

int muse_asr_volc_start(void)
{
    extern const char *muse_voice_volc_asr_key(void);
    extern const char *muse_voice_volc_appid(void);
    extern const char *muse_voice_volc_asr_res(void);
    const char *key = muse_voice_volc_asr_key();
    websocket_client_input_t input;

    if (s_volc.active) {
        LOGW("start refused: previous session still active\r\n");
        return -1;
    }
    s_volc.ws_closed = false;
    if (key == NULL || key[0] == '\0') {
        LOGE("no volc ASR key -- run 'muse asrkey <key>' first\r\n");
        return -2;
    }
    /* Credential mode by key shape: a dashed UUID is a new-console API Key
     * (X-Api-Key header, no APP ID needed); a 32-char token is old-console
     * (X-Api-App-Key + X-Api-Access-Key pair). Field-proven 2026-11-12: this
     * account's bigmodel-ASR grant rides on the API Key -- every old-header
     * combination got 403 not-granted / not-allowed, while X-Api-Key +
     * seedasr returned 101. */
    bool api_key_mode = strchr(key, '-') != NULL;
    const char *appid = muse_voice_volc_appid();
    if (!api_key_mode && (appid == NULL || appid[0] == '\0')) {
        LOGE("no volc APP ID -- run 'muse volcappid <id>' first\r\n");
        return -2;
    }
    /* Resource id: CLI (`muse asrres`) overrides the Kconfig default -- the
     * server rejects a mismatched id with "resourceId ... is not allowed"
     * (field 2026-11-12: seedasr default vs a bigasr-activated app), and a
     * CLI swap beats a rebuild when the console binding is unclear. */
    const char *res = muse_voice_volc_asr_res();
    if (res == NULL || res[0] == '\0') {
        res = CONFIG_MUSE_ASR_VOLC_RESOURCE_ID;
    }
    {
        /* Wipe the context but keep the callbacks registered by
         * muse_asr_volc_init() (they are set once at boot). */
        muse_asr_volc_text_cb_t t = s_volc.on_text;
        muse_asr_volc_error_cb_t e = s_volc.on_error;
        memset(&s_volc, 0, sizeof(s_volc));
        s_volc.on_text = t;
        s_volc.on_error = e;
    }
    /* Masked key trace: a 401 against this line's prefix/suffix means the
     * CLI-set key itself is rejected (wrong key/type), nothing else. */
    LOGI("asr key len=%u head=%.4s tail=%.4s mode=%s res=%s\r\n",
         (unsigned)strlen(key),
         strlen(key) >= 8U ? key : "????",
         strlen(key) >= 8U ? key + strlen(key) - 4U : "????",
         api_key_mode ? "api-key" : "app-token", res);

    s_volc.ring = (uint8_t *)psram_malloc(VOLC_RING_BYTES);
    s_volc.rx = (uint8_t *)psram_malloc(VOLC_RX_BYTES);
    s_volc.frame = (uint8_t *)psram_malloc(VOLC_FRAME_CAP);
    s_volc.chunk = (uint8_t *)psram_malloc(VOLC_PCM_PER_SEND);
    if (s_volc.ring == NULL || s_volc.rx == NULL || s_volc.frame == NULL ||
        s_volc.chunk == NULL) {
        goto fail_alloc;
    }
    if (rtos_init_mutex(&s_volc.ring_lock) != BK_OK ||
        rtos_init_mutex(&s_volc.send_lock) != BK_OK ||
        rtos_init_semaphore(&s_volc.kick, 1) != BK_OK) {
        goto fail_sync;
    }

    snprintf(s_volc.api_key, sizeof(s_volc.api_key), "%s", key);

    /* Handshake headers, dual-credential (2026-11-12): new-console API Keys
     * go out as X-Api-Key and skip the APP ID entirely; old-console Access
     * Tokens need the X-Api-App-Key/X-Api-Access-Key pair. Format is raw
     * lines; bk_websocket appends them verbatim to the HTTP upgrade
     * request. */
    {
        uint32_t t = rtos_get_time();
        char headers[384];
        if (api_key_mode) {
            snprintf(headers, sizeof(headers),
                     "X-Api-Key: %s\r\n"
                     "X-Api-Resource-Id: %s\r\n"
                     "X-Api-Request-Id: %08lx-7259-4d75-%04lx-%012lx\r\n"
                     "X-Api-Sequence: -1\r\n"
                     "X-Api-Connect-Id: %08lx-b7b9-49c9-%04lx-%012lx\r\n",
                     s_volc.api_key,
                     res,
                     (unsigned long)(t ^ 0x5a5a0001U),
                     (unsigned long)(t & 0xffffU),
                     (unsigned long)(t * 2654435761U),
                     (unsigned long)(t ^ 0xa5a50002U),
                     (unsigned long)((t >> 8) & 0xffffU),
                     (unsigned long)(t * 40503U));
        } else {
            snprintf(headers, sizeof(headers),
                     "X-Api-App-Key: %s\r\n"
                     "X-Api-Access-Key: %s\r\n"
                     "X-Api-Resource-Id: %s\r\n"
                     "X-Api-Request-Id: %08lx-7259-4d75-%04lx-%012lx\r\n"
                     "X-Api-Sequence: -1\r\n"
                     "X-Api-Connect-Id: %08lx-b7b9-49c9-%04lx-%012lx\r\n",
                     appid,
                     s_volc.api_key,
                     res,
                     (unsigned long)(t ^ 0x5a5a0001U),
                     (unsigned long)(t & 0xffffU),
                     (unsigned long)(t * 2654435761U),
                     (unsigned long)(t ^ 0xa5a50002U),
                     (unsigned long)((t >> 8) & 0xffffU),
                     (unsigned long)(t * 40503U));
        }
        memset(&input, 0, sizeof(input));
        input.uri = VOLC_URI;
        input.enable_auto_reconnect = false;
        input.rx_retry = 10;
        input.user_context = NULL;
        input.buffer_size = VOLC_WS_BUF;
        input.headers = headers;
        input.ws_event_handler = volc_ws_event;
        s_volc.client = websocket_client_init(&input);
    }
    if (s_volc.client == NULL) {
        goto fail_sync;
    }
    /* active/seq BEFORE creating the sender: on SMP the new prio-5 task can
     * start on the other core before create returns, and a sender that sees
     * active==false exits its loop and self-deletes on the spot -- the
     * session then runs silently senderless: no audio frames, no tail, just
     * the 10 s finish timeout (field 2026-11-12, race outcome varied per
     * boot, which is why the same build worked one reset and hung the
     * next). */
    s_volc.active = true;
    s_volc.seq = 1;
    s_volc.last_rx = rtos_get_time();
    /* 4 KB: the send path runs through websocket_client_send_binary into
     * TLS record encryption; the 3 KB stack that overflowed is history. */
    if (rtos_create_thread(&s_volc.sender, 5, "asr_volc",
                           volc_sender_main, 4096, NULL) != BK_OK) {
        s_volc.active = false;
        (void)websocket_client_destroy(s_volc.client);
        goto fail_sync;
    }
    if (websocket_client_start(s_volc.client) != BK_OK) {
        muse_asr_volc_abort();
        return -3;
    }
    return 0;

fail_sync:
    rtos_deinit_mutex(&s_volc.ring_lock);
    rtos_deinit_mutex(&s_volc.send_lock);
fail_alloc:
    if (s_volc.ring != NULL) {
        psram_free(s_volc.ring);
    }
    if (s_volc.rx != NULL) {
        psram_free(s_volc.rx);
    }
    if (s_volc.frame != NULL) {
        psram_free(s_volc.frame);
    }
    if (s_volc.chunk != NULL) {
        psram_free(s_volc.chunk);
    }
    memset(&s_volc, 0, sizeof(s_volc));
    return -3;
}

void muse_asr_volc_feed(const int16_t *pcm, size_t samples)
{
    size_t bytes = samples * 2U;
    const uint8_t *src = (const uint8_t *)pcm;

    if (!s_volc.active || s_volc.dead || pcm == NULL || samples == 0) {
        return;
    }
    /* Cheap content probe: peak |sample| over the frame, so a turn of pure
     * silence is provable from the finish log line. */
    for (size_t i = 0; i < samples; i++) {
        int32_t v = pcm[i];
        if (v < 0) {
            v = -v;
        }
        if (v > s_volc.peak) {
            s_volc.peak = (int16_t)v;
        }
    }
    s_volc.fed_bytes += (uint32_t)(samples * 2U);
    rtos_lock_mutex(&s_volc.ring_lock);
    while (bytes-- > 0) {
        size_t next = (s_volc.ring_w + 1U) % VOLC_RING_BYTES;
        if (next == s_volc.ring_r) {
            s_volc.dropped++;   /* drop newest under pressure */
            break;
        }
        s_volc.ring[s_volc.ring_w] = *src++;
        s_volc.ring_w = next;
    }
    rtos_unlock_mutex(&s_volc.ring_lock);
    rtos_set_semaphore(&s_volc.kick);
}

void muse_asr_volc_finish(void)
{
    if (!s_volc.active) {
        return;
    }
    LOGI("finish: fed=%lu B peak=%d sent=%lu frames dropped=%lu\r\n",
         (unsigned long)s_volc.fed_bytes, (int)s_volc.peak,
         (unsigned long)s_volc.sent_frames, (unsigned long)s_volc.dropped);
    s_volc.finishing = true;
    rtos_set_semaphore(&s_volc.kick);
    s_volc.final_deadline = rtos_get_time() + VOLC_FINAL_WAIT_MS;
    while (!s_volc.done &&
           (int32_t)(rtos_get_time() - s_volc.final_deadline) < 0) {
        rtos_delay_milliseconds(20);
    }
    if (!s_volc.cb_fired) {
        volc_fire_error(-8, "final transcript timeout");
    }
    muse_asr_volc_abort();
}

void muse_asr_volc_abort(void)
{
    transport client;
    beken_thread_t sender;

    if (!s_volc.active) {
        return;
    }
    s_volc.dead = true;          /* late events/sender iterations no-op */
    client = s_volc.client;
    sender = s_volc.sender;
    s_volc.active = false;
    rtos_set_semaphore(&s_volc.kick);   /* wake the sender so it exits */
    if (client != NULL) {
        /* 与 CLOSED 回调互斥：SDK 的 ws 任务派发 CLOSED 后会自己
         * free_client（销毁 mutex、释放 client），那时再 destroy 就是
         * use-after-free（2026-11-13 dump: xQueueSemaphoreTake:1797）。
         * 持锁判断，任务若正在派发 CLOSED 会等我们放锁后再收尸。 */
        rtos_lock_mutex(&s_volc_ws_lock);
        if (!s_volc.ws_closed) {
            /* Serialize destroy against an in-flight volc_send_frame: the
             * sender holds send_lock around send_binary, so taking it here
             * guarantees no memcpy into tx_buffer is running when destroy
             * frees it (MemFault at websocket_client_send_with_opcode,
             * field 2026-11-12). */
            rtos_lock_mutex(&s_volc.send_lock);
            (void)websocket_client_destroy(client);
            rtos_unlock_mutex(&s_volc.send_lock);
        } else {
            LOGW("asr ws already self-freed, skip destroy\r\n");
        }
        s_volc.client = NULL;
        rtos_unlock_mutex(&s_volc_ws_lock);
    }
    /* The sender task and the ws client both finish on their own tasks
     * after the wake/destroy signals; wait past the send cadence and the
     * ws grace before freeing buffers they may still touch. */
    rtos_delay_milliseconds(VOLC_SEND_MS + VOLC_TEARDOWN_GRACE_MS);
    if (s_volc.ring != NULL) {
        psram_free(s_volc.ring);
    }
    if (s_volc.rx != NULL) {
        psram_free(s_volc.rx);
    }
    if (s_volc.frame != NULL) {
        psram_free(s_volc.frame);
    }
    if (s_volc.chunk != NULL) {
        psram_free(s_volc.chunk);
    }
    rtos_deinit_mutex(&s_volc.ring_lock);
    rtos_deinit_mutex(&s_volc.send_lock);
    rtos_deinit_semaphore(&s_volc.kick);
    /* Keep the registered callbacks across sessions. */
    {
        muse_asr_volc_text_cb_t t = s_volc.on_text;
        muse_asr_volc_error_cb_t e = s_volc.on_error;
        memset(&s_volc, 0, sizeof(s_volc));
        s_volc.on_text = t;
        s_volc.on_error = e;
    }
    (void)sender;   /* self-deleted on loop exit */
}

bool muse_asr_volc_active(void)
{
    return s_volc.active && !s_volc.dead;
}
