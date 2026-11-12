/**
 * @file muse_voice.c
 * @brief Push-to-talk voice: record mic -> PCM stage -> base64 chunks ->
 *        POST /chat/stream (voice note), then read the assistant reply from
 *        the POST /chat/subscribe NDJSON stream and show it on the panel.
 *
 * Threading: see muse_voice.h. One mutex guards state/flags; the chunk pool
 * uses two queues (free_q/data_q) so the audio callback never allocates and
 * the session worker never frees. The mic callback appends raw PCM to a
 * stage buffer and emits one base64 chunk per MUSE_VOICE_STAGE_BYTES PCM
 * (2048 chars), mirroring the pinned upstream muse_chat_link.c turn flow.
 */
#include "muse_voice.h"

#include <stdio.h>
#include <string.h>

#include "voice_history.h"

#include <components/log.h>
#include <os/os.h>
#include <os/mem.h>
#include "bk_posix.h"        /* stat(): wakeup ack-tone file presence check */

#include "audio_engine.h"
#include "audio_engine_prompt_tone.h" /* wakeup ack tone (voice-mix port) */
#include "app_event.h"      /* APP_EVT_ASR_WAKEUP: KWS voice wakeup */
#include "camera_preview.h" /* camera_preview_is_frozen/_stop */
#include "muse_display.h"
#include "muse_service.h"   /* muse_service_node_id() */
#include "muse_voice_proto.h"
#include "ui/voice_chat_page.h" /* voice_chat_page_is_active() */

#define TAG "muse_voice"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#ifndef CONFIG_MUSE_VOICE_MAX_RECORD_MS
#define CONFIG_MUSE_VOICE_MAX_RECORD_MS    30000
#endif
#ifndef CONFIG_MUSE_VOICE_VAD_END_MS
/* Auto-end a voice note once VAD reported speech and then this much
 * trailing silence: keyword-wakeup turns must not need a second button
 * press (the same rule also ends a button PTT take early). */
#define CONFIG_MUSE_VOICE_VAD_END_MS       1200
#endif
#ifndef CONFIG_MUSE_VOICE_VAD_MIN_SPEECH_MS
/* Ignore VAD "speech" shorter than this before allowing the auto-end
 * above: the wakeup-word residue can bleed into the first samples of a
 * keyword-triggered take and must not close it with a near-empty note. */
#define CONFIG_MUSE_VOICE_VAD_MIN_SPEECH_MS 500
#endif
#ifndef CONFIG_MUSE_VOICE_WAKEUP_RESIDUE_MS
/* The wakeup word's own VAD tail fires one more AI_THINKING right after the
 * take starts ("你好博通" alone passes the engine's 1 s MIN_LISTENING gate);
 * without this guard the event stops the just-started recording and an
 * almost-empty note goes out (field log 2026-11-05: server answered "这条语
 * 音只有5毫秒，里面没有声音"). The same window also absorbs the ack tone's
 * own VAD edges (the tone plays over the voice engine right at take start).
 * Real speech-end edges arrive later and are unaffected. */
#define CONFIG_MUSE_VOICE_WAKEUP_RESIDUE_MS 1200
#endif
#ifndef CONFIG_MUSE_VOICE_ACK_TONE_MAX_MS
/* 应答音 VAD 抑制窗：应答音经 mic 漏回被 VAD 当成"用户在说话"，其尾音
 * 边沿会把真正的录音提前停掉（2026-11-06 实机：用户答完"在的"稍一停顿，
 * 发出去的是 1.5s 应答音+静音，服务器转写为空）。播放开始起 MAX_MS 内
 * （FINISH 未到的兜底）及播完后 TAIL_MS 内，两条自动停止路径都屏蔽。 */
#define CONFIG_MUSE_VOICE_ACK_TONE_MAX_MS 3000
#endif
#ifndef CONFIG_MUSE_VOICE_ACK_TONE_TAIL_MS
#define CONFIG_MUSE_VOICE_ACK_TONE_TAIL_MS 800
#endif
#ifndef CONFIG_MUSE_VOICE_ACK_TONE_PLAY_TAIL_MS
/* 可听窗口的余量（比 VAD 抑制窗的 800ms 短）：FINISH 后 PCM 丢弃只延续
 * 这么长。窗内用户若开口会丢开头几个字——等价于"听到应答音后再说话"
 * 的常规交互。 */
#define CONFIG_MUSE_VOICE_ACK_TONE_PLAY_TAIL_MS 200
#endif
#ifndef CONFIG_MUSE_VOICE_REPLY_TIMEOUT_MS
/* Upstream waits 60 s (REPLY_TIMEOUT_US) for the first assistant event.
 * Measured on this link: evening run reply landed at end+35 s, next
 * morning at end+92 s — a 90 s deadline again cut the stream 3 s before
 * the first chunk. Their generation latency clearly swings by 3x, so use
 * 180 s. */
#define CONFIG_MUSE_VOICE_REPLY_TIMEOUT_MS 180000
#endif
#ifndef CONFIG_MUSE_VOICE_REPLY_SHOW_MS
/* How long the reply page stays up before the normal UI returns. 10 s
 * felt instant to users watching a reply land; 30 s gives reading room. */
#define CONFIG_MUSE_VOICE_REPLY_SHOW_MS 30000
#endif
#ifndef CONFIG_MUSE_PHOTO_MAX_JPEG_BYTES
/* Hard cap on one photo note: the chunk pool is 256 x 1536 raw bytes
 * (~384 KB of base64), and a photo turn must never exhaust the pool a
 * voice turn needs. A 640x480 quality-5 JPEG lands at ~20-80 KB, far
 * below this. */
#define CONFIG_MUSE_PHOTO_MAX_JPEG_BYTES (256U * MUSE_VOICE_STAGE_BYTES)
#endif
#ifndef CONFIG_MUSE_PHOTO_REPLY_TIMEOUT_MS
/* Image recognition is slower than voice in practice: measured 2026-10-29,
 * the draw_text reply for a photo note landed at +63 s and +109 s (the VM
 * also talks to the app user before pushing). 60 s timed out just before
 * the answer and the reply missed its turn; 120 s covers the observed
 * range. A follow-up shot no longer needs the timeout to expire first —
 * SHOWING turns accept a new photo/voice turn immediately. */
#define CONFIG_MUSE_PHOTO_REPLY_TIMEOUT_MS 120000
#endif
#ifndef CONFIG_MUSE_NOTE_REPLY_HINT
/* In-band instruction baked into every note's message field: Muse pushes
 * a reply to the board via device.invoke only when the VM was told to
 * (conversational behaviour, not a protocol flag — Muse support 2026-10),
 * so the note carries the instruction itself instead of relying on chat
 * history. Empty string disables. */
#define CONFIG_MUSE_NOTE_REPLY_HINT "请通过 display.draw_text 把回复发到设备屏幕上"
#endif
/* bool Kconfigs are undefined when off, so the choice must be made by the
 * preprocessor (a C expression would be an undeclared identifier). */
#ifdef CONFIG_MUSE_NOTE_SEND_SESSION_ID
#define VOICE_NOTE_SESSION_ID s_session_id
#else
#define VOICE_NOTE_SESSION_ID ""
#endif
#define VOICE_TX_WAIT_MS          40U     /* pump latency budget per loop */
/* Buffering depth: 128 chunks x 48 ms ~= 6 s of 16 kHz audio (~264 KB of
 * the shared PSRAM heap, which also feeds the session's 32 KB scratch).
 * A full pool no longer drops audio — the turn ends instead (see the
 * pool_full flag), so the note is always intact; its maximum length is
 * the pool depth. 128 (was 256) keeps spoken questions well under the
 * limit while freeing 264 KB: the heap ran dry in the field (PTT with
 * 48 KB left -> audio_engine_start wedged, chat tx_buffer alloc failed
 * at 4 KB) and 6 s of headroom is plenty for a button-push question. */
#define VOICE_CHUNK_POOL          128U

#define VOICE_HEARD_MAX           192U
#define VOICE_REPLY_MAX           768U
/* Queued overlay display text (see voice_show_locked / voice_display_flush):
 * title is a short static label, body mirrors the voice_show_turn layout. */
#define VOICE_SHOW_TITLE_MAX      64U
#define VOICE_SHOW_BODY_MAX       (VOICE_HEARD_MAX + VOICE_REPLY_MAX + 24U)
/* Upstream NDJSON_LINE_MAX is 16 KB; keep 8 KB so a long assistant row
 * still parses (fields are capped at parse time anyway). */
#define VOICE_LINE_CAP            8192U

typedef enum {
    VOICE_STATE_IDLE = 0,
    VOICE_STATE_RECORDING,
    VOICE_STATE_CLOSING,   /* tail chunk queued; sub open pending */
    VOICE_STATE_WAIT_REPLY,
    VOICE_STATE_SHOWING,   /* reply on screen until show deadline */
} voice_state_t;

typedef struct {
    size_t len;
    uint8_t end;           /* last chunk of the note body (end_body) */
    uint8_t data[MUSE_VOICE_CHUNK_CAP];
} voice_chunk_t;

typedef struct {
    beken_mutex_t lock;
    voice_state_t state;
    bool connected;
    bool engine_inited;     /* audio_engine_init() ran at least once */
    bool engine_ours;       /* engine currently started with the voice cfg */
    bool had_kws;           /* KWS standby owned the engine before PTT */

    bool need_open_note;
    bool need_open_sub;
    bool need_reset;

    /* Daemon stream ids of the current turn, freshly allocated per turn
     * (upstream never reuses a stream id). 0 while idle. */
    int64_t note_stream;
    int64_t sub_stream;

    uint32_t record_deadline;
    uint32_t reply_deadline;
    uint32_t show_deadline;

    /* VAD auto-end bookkeeping for the current take (tick-driven):
     * speech accumulates while audio_engine_get_mic_active() is 1,
     * silence_since marks the start of the trailing silent run (0 =
     * currently hearing speech or no speech seen yet), last_ms is the
     * previous tick timestamp used to integrate speech duration. */
    uint32_t vad_speech_ms;
    uint32_t vad_silence_since;
    uint32_t vad_last_ms;

    beken_queue_t free_q;
    beken_queue_t data_q;
    uint32_t dropped_frames;

    bool replied;
    bool pool_full;         /* chunk pool exhausted: end the turn intact */
    bool photo;             /* current turn uploads a JPEG, not audio */
    bool history_pushed;    /* this turn's reply already in voice_history */

    /* Overlay display actions queued under the state lock, executed by
     * voice_display_flush() AFTER the lock is released. Never call the
     * display (LVGL / lv_vendor_disp_lock) APIs under s_voice.lock: the
     * voice-chat page timer refreshes under disp_lock and reads voice
     * state back through muse_voice_recording(), so inverting the lock
     * order deadlocks both tasks (field log: tp queue full, keys dead).
     *
     * engine_busy/stop_while_busy serve the same rule for the audio
     * engine: voice_engine_acquire()/release() run audio_engine_start/
     * stop + KWS park/restore (hundreds of ms, and they take audio-side
     * locks whose owners also touch disp_lock). ptt_start sets engine_busy
     * under the lock, does the acquire lock-free, then commits the new
     * state; ptt_stop arriving in between is deferred via
     * stop_while_busy. */
    bool pending_show;
    bool pending_hide;
    bool engine_busy;
    bool stop_while_busy;
    bool engine_had_ours;   /* stale engine to stop lock-free before re-acquire */
    bool kws_restore_pending; /* 回合进入 SHOWING：锁外恢复 KWS 待机 */
} voice_ctx_t;

static voice_ctx_t s_voice;
/* Daemon stream ids: note streams are per-turn, the subscribe stream is
 * per-connection (both allocated from here; upstream s_conn.next_id++).
 * Streams 1-3 are the control channel and the first turn's legacy ids. */
static int64_t s_next_stream = 4;
/* Stream id a pending MUSE_VOICE_TX_RESET must target. Voice_abort()
 * stashes the dying note stream here because a barge-in allocates the new
 * turn's note stream BEFORE the worker has pumped the reset: read at send
 * time from the current note stream, the reset would hit the NEW stream.
 * 0 = none pending (reset falls back to the current note stream). */
static int64_t s_reset_stream_id;
/* Set only by muse_voice_init(); the session calls that late (when it
 * starts), so CLI/key entry points must refuse to run before it, or
 * rtos_lock_mutex() on a NULL handle asserts (xQueueSemaphoreTake). */
static bool s_voice_inited;
/* Turn correlation (ACK) + reply assembly: ~1.1 KB lives in PSRAM, not in
 * the scarce SRAM .bss (the link-time heap floor trips otherwise). */
static char s_note_id[80] __attribute__((section(".psram.bss")));
static char s_parent_id[80] __attribute__((section(".psram.bss")));
static char s_heard[VOICE_HEARD_MAX] __attribute__((section(".psram.bss")));
static char s_reply[VOICE_REPLY_MAX] __attribute__((section(".psram.bss")));
/* The chunk pool is ~1.3 MB — far beyond the small .psram.bss linker
 * section, so it is psram_zalloc'd at init instead (PSRAM total is 16 MB). */
static voice_chunk_t *s_chunk_pool;
/* Assistant text arriving as delta.text_append before the final
 * message.assistant row (upstream subscription_event accumulates per
 * message id). */
static char s_delta[VOICE_REPLY_MAX] __attribute__((section(".psram.bss")));
static char s_delta_msg[80] __attribute__((section(".psram.bss")));
/* POST /chat/stream ACK body, accumulated until end_body. 8 KB: the ACK is
 * normally ~200 B, but a server-side shape change once grew it past the old
 * 2 KB buffer and the turn lost its message ids (field log: "ACK without a
 * message id" right after end_body). */
static char s_ack[8192] __attribute__((section(".psram.bss")));
static size_t s_ack_len;
static int s_ack_status;
static bool s_ack_ovf;
/* PCM staging (WAV header + mic frames), filled on the audio read task. */
static uint8_t s_stage[MUSE_VOICE_STAGE_BYTES]
    __attribute__((section(".psram.bss")));
static size_t s_stage_len;

/* NDJSON line assembly, fed on the session worker only. */
static char s_sub_line[VOICE_LINE_CAP] __attribute__((section(".psram.bss")));
static size_t s_sub_len;
static bool s_sub_ovf;
static muse_voice_row_t s_row __attribute__((section(".psram.bss")));

/* Queued overlay display text: written under s_voice.lock by
 * voice_show_locked(), snapshotted into s_flush_* by voice_display_flush()
 * under the same lock, then shown after unlock. PSRAM: ~1 KB of text is
 * too much for the scarce SRAM .bss (see the link-time heap floor note
 * on the correlation buffers above). */
static char s_pending_title[VOICE_SHOW_TITLE_MAX] __attribute__((section(".psram.bss")));
static char s_pending_body[VOICE_SHOW_BODY_MAX] __attribute__((section(".psram.bss")));
static char s_flush_title[VOICE_SHOW_TITLE_MAX] __attribute__((section(".psram.bss")));
static char s_flush_body[VOICE_SHOW_BODY_MAX] __attribute__((section(".psram.bss")));
/* tx_probe: first-frames-of-a-note dump window (see muse_voice_tx_next). */
static unsigned s_tx_probe_count;
/* 应答音 VAD 抑制窗截止时间（0 = 无窗口）：voice_wakeup_tone_play 起播
 * 成功时设为 now+MAX（FINISH 不到的兜底）；PROMPT_TONE_FINISH 到来时缩
 * 短为 now+TAIL（尾音 hangover）。窗口内事件/tick 两条自动停止路径都
 * 忽略 VAD。 */
static uint32_t s_ack_tone_suppress_until;
/* 应答音可听窗口（0 = 无窗口）：期内 voice_mic_read_cb 丢弃 mic PCM，
 * 防止应答音混进语音条被服务器误转写（2026-11-06 实机："今天周几"被
 * 听成"打开钉钉"）。起播设 now+MAX 兜底，FINISH 收紧为 now+200ms。 */
static uint32_t s_ack_tone_playing_until;

/* Side-chat identity for this boot: every note (voice and photo) names
 * it as session_id so the server keeps the board's turns out of the
 * account main chat (Muse support, 2026-10). UUID-shaped to match the
 * server-generated ids; regenerated per boot (fresh side chat). */
static char s_session_id[40] __attribute__((section(".psram.bss")));
static uint32_t s_rng_state;

static uint32_t voice_rng_next(void)
{
    s_rng_state ^= s_rng_state << 13;
    s_rng_state ^= s_rng_state >> 17;
    s_rng_state ^= s_rng_state << 5;
    return s_rng_state;
}

static int voice_mic_read_cb(unsigned char *data, unsigned int len, void *args);
static void voice_engine_release(void);
static void voice_show_locked(const char *title, const char *body);
static void voice_hide_locked(void);
static void voice_display_flush(void);

static bool deadline_passed(uint32_t deadline)
{
    return (int32_t)(rtos_get_time() - deadline) >= 0;
}

/* Clear the per-turn correlation/reply buffers (lock held). Shared by
 * ptt_start and muse_photo_send. */
static void voice_turn_reset_locked(void)
{
    s_note_id[0] = '\0';
    s_parent_id[0] = '\0';
    s_heard[0] = '\0';
    s_reply[0] = '\0';
    s_voice.replied = false;
    s_sub_len = 0;
    s_sub_ovf = false;
    s_delta[0] = '\0';
    s_delta_msg[0] = '\0';
    s_ack_len = 0;
    s_ack_status = 0;
    s_ack_ovf = false;
    s_voice.history_pushed = false;
}

/* Overlay title by turn kind. */
static const char *voice_turn_title(void)
{
    return s_voice.photo ? "Muse photo" : "Muse voice";
}

/* ---- chunk pool -----------------------------------------------------------*/

static voice_chunk_t *chunk_alloc(void)
{
    voice_chunk_t *c = NULL;
    if (rtos_pop_from_queue(&s_voice.free_q, &c, 0) == BK_OK && c != NULL) {
        return c;
    }
    return NULL;   /* caller flags pool_full; audio is never dropped */
}

static void chunk_free(voice_chunk_t *c)
{
    if (c != NULL) {
        (void)rtos_push_to_queue(&s_voice.free_q, &c, 0);
    }
}

static bool chunk_push(const void *data, size_t len, bool end)
{
    voice_chunk_t *c = chunk_alloc();
    if (c == NULL) {
        /* Pool exhausted: do NOT steal an older chunk (that would punch a
         * hole into the note). Flag it; on_tick ends the turn so the
         * recorded part stays intact. */
        s_voice.pool_full = true;
        return false;
    }
    if (len > sizeof(c->data)) {
        len = sizeof(c->data);
    }
    memcpy(c->data, data, len);
    c->len = len;
    c->end = end ? 1U : 0U;
    if (rtos_push_to_queue(&s_voice.data_q, &c, 0) != BK_OK) {
        chunk_free(c);
        s_voice.pool_full = true;
        return false;
    }
    return true;
}

/* ---- PCM stage ------------------------------------------------------------ */

/* base64-encode the staged PCM as one body chunk. The final chunk also
 * carries NOTE_TAIL (stage is always a whole number of 3-byte groups when
 * full, so only the final chunk may need '=' padding). */
static void stage_emit(bool final)
{
    /* Static: 2 KB on the audio read task's stack is not safe. Re-entry
     * is excluded by s_voice.lock: the only callers are voice_mic_read_cb
     * and voice_stop_recording_tail, both holding it. */
    static char b64[MUSE_VOICE_CHUNK_CAP] __attribute__((section(".psram.bss")));
    size_t n = muse_voice_proto_base64(s_stage, s_stage_len, b64);
    s_stage_len = 0;
    if (final) {
        size_t tail_len = 0;
        const char *tail = muse_voice_proto_note_tail(&tail_len);
        memcpy(b64 + n, tail, tail_len);
        n += tail_len;
    }
    (void)chunk_push(b64, n, final);
}

/* ---- audio engine helpers -------------------------------------------------*/

#if CONFIG_BEKEN_KWS
/* Engine cfg the voice pipeline runs with; shared by voice_engine_acquire()
 * and the KWS standby start below. standby=true keeps the mic/AEC/KWS path
 * but leaves the speaker amp off: the standby engine is resident from boot,
 * and with PA on its silence-fill pipeline is audible as a constant hiss
 * (field report 2026-11-04). */
static void voice_engine_cfg_init(audio_engine_cfg_t *cfg, bool standby)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->mic_sample_rate = MUSE_VOICE_SAMPLE_RATE_HZ;
    /* The play pipeline (A2DP speaker) rejects 8 kHz (see
     * onboard_speaker_stream_init: "A2DP 8k is not supported") and fails
     * the whole engine init; voice never plays audio, so keep spk at 16k. */
    cfg->spk_sample_rate = 16000;
    cfg->aec_enable = 1;
    cfg->eq_enable = 1;
    /* Raw PCM16: the chat/stream body is a base64 WAV note, so PCM is the
     * wire format (upstream sends no compressed audio here). */
    cfg->enc_type = AUDIO_ENC_TYPE_PCM;
    cfg->dec_type = AUDIO_DEC_TYPE_PCM;
    cfg->read_cb = voice_mic_read_cb;
#if CONFIG_AE_ENABLE_PA_CNTRL
    cfg->pa_enable = !standby;
    cfg->pa_gpio = CONFIG_AE_PA_CNTRL_GPIO;
    cfg->pa_on_level = CONFIG_AE_PA_ON_LEVEL;
    cfg->pa_on_delay = CONFIG_AE_PA_ON_DELAY;
    cfg->pa_off_delay = CONFIG_AE_PA_OFF_DELAY;
#endif
}

/* Keyword voice-wakeup standby, mirroring beken_robot's
 * sound_localization_start_service(): keep the engine resident with the
 * WAKEUP model so the KWS library fires APP_EVT_ASR_WAKEUP; the handler
 * below starts a PTT turn, and the park/restore in
 * voice_engine_acquire()/release() keeps the two users apart.
 * Fails soft (button PTT unaffected) when the model file is not deployed
 * on the SD-NAND/U-disk (CONFIG_BEKEN_KWS_MODEL_FROM_UDISK). */
static int voice_kws_standby_start(void)
{
    if (audio_engine_is_running()) {
        if (audio_engine_asr_is_started()) {
            return 0;   /* already standing by */
        }
    } else {
        if (!s_voice.engine_inited) {
            if (audio_engine_init() != AUDIO_ENGINE_SUCCESS) {
                LOGW("KWS standby: audio_engine_init failed\r\n");
                return -1;
            }
            s_voice.engine_inited = true;
        }
        audio_engine_cfg_t cfg;
        voice_engine_cfg_init(&cfg, true);
        if (audio_engine_start(&cfg) != AUDIO_ENGINE_SUCCESS) {
            LOGW("KWS standby: engine start failed\r\n");
            return -1;
        }
        s_voice.engine_ours = true;
    }
    if (audio_engine_asr_switch_model(AUDIO_ENGINE_KWS_MODEL_WAKEUP) !=
        AUDIO_ENGINE_SUCCESS) {
        LOGW("KWS standby: switch WAKEUP model failed "
             "(model file missing?)\r\n");
        return -1;
    }
    if (audio_engine_asr_start() != AUDIO_ENGINE_SUCCESS) {
        LOGW("KWS standby: asr start failed (model file missing?)\r\n");
        return -1;
    }
    LOGI("KWS voice-wakeup standby on\r\n");
    return 0;
}

/* 唤醒应答音（用户自制中文"在的"）：app_event 的自动播放已对本项目关闭
 * （触发时发声通路还挂在待机引擎上，pa_ctrl_en=false 播了也无声，且会被
 * PTT 引擎切换掐断），muse_voice 在 voice 引擎就绪后自播，见
 * voice_kws_wakeup_handler()。文件须为 MP3/16kHz/单声道，放在 SD 根目录。 */
#define VOICE_WAKEUP_TONE_PATH  "/sd0/reply.mp3"

#if CONFIG_AE_SUPPORT_PROMPT_TONE
extern audio_engine_prompt_tone_handle_t g_audio_engine_prompt_tone;
#endif

/* 唤醒应答音（"在的"）：在 voice 引擎起来之后播，混进通话链路的
 * voice-mix 口。voice 引擎 pa_enable=true，PA 真正打开才出声。AEC 对它
 * 的消除并不完全（实机：泄漏触发 mic VAD），所以起播同时打开 VAD 抑制
 * 窗（见 s_ack_tone_suppress_until），播完由 FINISH 收紧。 */
static void voice_wakeup_tone_play(void)
{
#if CONFIG_AE_SUPPORT_PROMPT_TONE
    struct stat st;

    if (g_audio_engine_prompt_tone == NULL ||
        stat(VOICE_WAKEUP_TONE_PATH, &st) != 0) {
        return;
    }
    prompt_tone_uri_info_t info;
    memset(&info, 0, sizeof(info));
    info.uri = (char *)VOICE_WAKEUP_TONE_PATH;
    if (audio_engine_prompt_tone_start(g_audio_engine_prompt_tone, &info) !=
        BK_OK) {
        LOGW("wakeup ack tone start failed\r\n");
        return;
    }
    s_ack_tone_suppress_until = rtos_get_time() +
                                CONFIG_MUSE_VOICE_ACK_TONE_MAX_MS;
    s_ack_tone_playing_until = s_ack_tone_suppress_until;
#endif
}

/* 应答音播完：抑制窗从 MAX 兜底收紧为"播完 + TAIL 尾音"，让用户尽早可
 * 以正常说话收尾。FINISH 不到时 MAX 兜底自然到期，无需额外处理。 */
static void voice_prompt_tone_finish_handler(app_evt_msg_t *msg,
                                             void *user_data)
{
    (void)msg;
    (void)user_data;
    if (s_ack_tone_suppress_until != 0U) {
        s_ack_tone_suppress_until = rtos_get_time() +
                                    CONFIG_MUSE_VOICE_ACK_TONE_TAIL_MS;
        s_ack_tone_playing_until = rtos_get_time() +
                                   CONFIG_MUSE_VOICE_ACK_TONE_PLAY_TAIL_MS;
    }
}

/* KWS hit -> start a voice turn, exactly as if the mic button were tapped.
 * Runs on the app-event task; ptt_start takes its own locks and its engine
 * acquire completes before it returns. If a turn is already in flight
 * (RECORDING/CLOSING) ptt_start refuses it, which is the right behaviour
 * for a double-hit. The ack tone plays only on a successful start. */
static void voice_kws_wakeup_handler(app_evt_msg_t *msg, void *user_data)
{
    (void)msg;
    (void)user_data;
    LOGI("KWS wakeup word hit -> PTT\r\n");
    if (muse_voice_ptt_start() == 0) {
        voice_wakeup_tone_play();
    }
}
#endif /* CONFIG_BEKEN_KWS */

/* 引擎判定"用户说完了一段有效的话"（VAD STOP 边沿 + 时长达标）-> 结束当前
 * 录音并发送。on_tick 里采样式 VAD 跟踪在发送高峰期会被饿死（泵一组 32 块
 * 约 2.5s 不跑 tick，整段语音落在两个采样点之间，speech_ms 恒为 0），改为
 * 事件驱动。空闲时的环境音 THINKING 不会误触发：不在录音就直接忽略。 */
static void voice_ai_thinking_handler(app_evt_msg_t *msg, void *user_data)
{
    uint32_t age = 0;
    bool recording;

    (void)msg;
    (void)user_data;
    rtos_lock_mutex(&s_voice.lock);
    recording = (s_voice.state == VOICE_STATE_RECORDING);
    if (recording) {
        age = rtos_get_time() -
              (s_voice.record_deadline - CONFIG_MUSE_VOICE_MAX_RECORD_MS);
    }
    rtos_unlock_mutex(&s_voice.lock);
    if (!recording) {
        return;
    }
    /* 唤醒词自身的尾音边沿（见 CONFIG_MUSE_VOICE_WAKEUP_RESIDUE_MS）：刚
     * 开始录音的短窗口内收到的 THINKING 是"你好博通"的残尾，不是用户
     * 说完了，直接忽略；真正的语音端点会在之后产生新事件。 */
    if (age < CONFIG_MUSE_VOICE_WAKEUP_RESIDUE_MS) {
        LOGI("AI thinking %lu ms into take (wakeup residue) -> ignored\r\n",
             (unsigned long)age);
        return;
    }
    /* 应答音抑制窗：窗内的 THINKING 是应答音泄漏进 mic 的 VAD 触发的，
     * 不是用户说完了（2026-11-06 实机：用户一停顿就发空语音）。 */
    if (!deadline_passed(s_ack_tone_suppress_until)) {
        LOGI("AI thinking in ack-tone window -> ignored\r\n");
        return;
    }
    LOGI("AI thinking (speech end) -> auto stop\r\n");
    (void)muse_voice_ptt_stop();
}

/* Park a running KWS/ASR standby so the mic is free for the voice turn.
 * beken_muse has no ASR demo; this is the former asr_stop_service() inlined
 * (stop the engine, then put the KWS model back to WAKEUP). */
static int voice_park_kws_standby(void)
{
#if (CONFIG_ASR_SERVICE)
    if (!audio_engine_is_running()) {
        return 0;
    }
    int ret = (AUDIO_ENGINE_SUCCESS == audio_engine_stop()) ? 0 : -1;
#if CONFIG_BEKEN_KWS
    (void)audio_engine_asr_switch_model(AUDIO_ENGINE_KWS_MODEL_WAKEUP);
#endif
    return ret;
#else
    return 0;
#endif
}

static int voice_engine_acquire(void)
{
    /* Resident-engine fast path: the voice-config engine is still up
     * (kept across turns by voice_engine_release) — nothing to do. The
     * is_running/asr checks still guard against another module having
     * taken the engine over in between. */
    if (s_voice.engine_ours && audio_engine_is_running() &&
        !audio_engine_asr_is_started()) {
        return 0;
    }
    s_voice.engine_ours = false;

    if (audio_engine_is_running()) {
        if (audio_engine_asr_is_started()) {
            /* KWS standby: park it, restore on release. */
            if (voice_park_kws_standby() != 0) {
                LOGE("failed to park KWS audio engine\r\n");
                return -1;
            }
            s_voice.had_kws = true;
        } else {
            /* RTC/A2DP/player owns the engine; do not steal it. */
            LOGW("audio engine busy (RTC/player)\r\n");
            return -1;
        }
    } else if (!s_voice.engine_inited) {
        /* audio_engine_start() expects the one-time init (volume/PA state);
         * the default cfg engine is stopped again right away. */
        if (audio_engine_init() != AUDIO_ENGINE_SUCCESS) {
            LOGE("audio_engine_init failed\r\n");
            return -1;
        }
        s_voice.engine_inited = true;
        (void)audio_engine_stop();
    }

    audio_engine_cfg_t cfg;
    voice_engine_cfg_init(&cfg, false);
    if (audio_engine_start(&cfg) != AUDIO_ENGINE_SUCCESS) {
        LOGE("audio_engine_start(voice) failed\r\n");
        /* had_kws (if we parked the standby) is restored here. */
        voice_engine_release();
        return -1;
    }
    s_voice.engine_ours = true;
    return 0;
}

static void voice_engine_release(void)
{
    /* The engine stays resident across turns: audio_engine_stop/start
     * racing wedged the third consecutive PTT in the field (voice note
     * head logged, "PTT start" never followed, UI dead). The mic keeps
     * running; voice_mic_read_cb consumes and drops frames while the
     * state is not RECORDING. Only the KWS standby restore remains
     * meaningful here. */
    if (s_voice.had_kws) {
#if CONFIG_BEKEN_KWS
        /* Restore the KWS standby exactly as the demo layer builds it:
         * engine running with the WAKEUP model (sound_localization path). */
        if (!audio_engine_is_running() &&
            audio_engine_init() != AUDIO_ENGINE_SUCCESS) {
            LOGW("KWS restore failed; standby is off\r\n");
            s_voice.had_kws = false;
            return;
        }
        (void)audio_engine_asr_switch_model(AUDIO_ENGINE_KWS_MODEL_WAKEUP);
        if (audio_engine_asr_start() != AUDIO_ENGINE_SUCCESS) {
            LOGW("KWS ASR restart failed; standby is off\r\n");
        }
#else
        /* KWS not built (beken_muse trims TFLITE_MICRO); nothing to restore. */
        LOGW("had_kws set but KWS not built; standby stays off\r\n");
#endif
        s_voice.had_kws = false;
    }
}

/* ---- mic read callback (audio-engine read task) ----------------------------*/

static int voice_mic_read_cb(unsigned char *data, unsigned int len, void *args)
{
    const uint8_t *p = data;
    size_t n = len;
    (void)args;

    if (data == NULL || len == 0U) {
        return 0;
    }

    /* The engine is resident across turns (see voice_engine_release):
     * while not recording, consume and drop the mic frames. The lock
     * also serialises this callback against stage_emit — stopping the
     * engine used to provide that implicitly, it no longer does. */
    rtos_lock_mutex(&s_voice.lock);
    if (s_voice.state != VOICE_STATE_RECORDING) {
        rtos_unlock_mutex(&s_voice.lock);
        return (int)len;
    }
    /* 应答音可听窗口内丢弃 mic PCM：播"在的"期间采到的基本是应答音自身
     * 的泄漏，混进语音条会污染服务器 ASR（实机转写出错）。用户被应答音
     * 自然引导为"音落再开口"。 */
    if (!deadline_passed(s_ack_tone_playing_until)) {
        rtos_unlock_mutex(&s_voice.lock);
        return (int)len;
    }
#if CONFIG_MUSE_VOICE_PCM_8BIT
    /* 16-bit -> 8-bit 无符号 PCM 在入队前完成（128=静音）：stage 里直接
     * 存 8bit，emit 的每块仍是 1536B（3 的倍数），base64 流结构不变；
     * 上传码流因此减半（42.7 -> 21.3 KB/s），低于实测 ~25KB/s 的上行管
     * 道，说完≈发完。 */
    while (n >= 2U) {
        size_t room = MUSE_VOICE_STAGE_BYTES - s_stage_len;
        size_t samples = n / 2U;
        if (samples > room) {
            samples = room;
        }
        for (size_t i = 0; i < samples; i++) {
            int16_t v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            s_stage[s_stage_len++] = (uint8_t)((v >> 8) + 128);
            p += 2;
        }
        n -= samples * 2U;
        if (s_stage_len == MUSE_VOICE_STAGE_BYTES) {
            stage_emit(false);
        }
    }
#else
    while (n) {
        size_t take = MUSE_VOICE_STAGE_BYTES - s_stage_len;
        if (take > n) {
            take = n;
        }
        memcpy(s_stage + s_stage_len, p, take);
        s_stage_len += take;
        p += take;
        n -= take;
        if (s_stage_len == MUSE_VOICE_STAGE_BYTES) {
            stage_emit(false);
        }
    }
#endif
    rtos_unlock_mutex(&s_voice.lock);
    return (int)len;
}

/* ---- state helpers (state lock held unless noted) --------------------------*/

static void voice_queue_drain(void)
{
    voice_chunk_t *c = NULL;
    while (rtos_pop_from_queue(&s_voice.data_q, &c, 0) == BK_OK && c != NULL) {
        chunk_free(c);
    }
}

/* Tear down the TURN locally; send_reset asks the session worker to emit
 * Reset() on the note stream so the server stops expecting body bytes
 * (ignored when there is no session). The subscribe stream is
 * per-connection and survives: aborting a turn must not kill the only
 * channel late assistant replies arrive on.
 *
 * State cleanup only — NEVER touches the audio engine: engine stop is a
 * blocking, lock-taking operation and must run lock-free (see the
 * engine_busy note in voice_ctx_t). Callers run voice_engine_release()
 * after the state lock is released. */
static void voice_abort_locked(bool send_reset)
{
    bool note_touched = s_voice.need_open_note ||
                        s_voice.state == VOICE_STATE_RECORDING ||
                        s_voice.state == VOICE_STATE_CLOSING ||
                        s_voice.state == VOICE_STATE_WAIT_REPLY;
    if (s_voice.dropped_frames > 0U) {
        LOGW("voice: %lu chunks dropped (pump slower than capture)\r\n",
             (unsigned long)s_voice.dropped_frames);
        s_voice.dropped_frames = 0;
    }
    s_voice.need_open_note = false;
    voice_queue_drain();
    s_stage_len = 0;
    s_delta[0] = '\0';
    s_delta_msg[0] = '\0';
    s_ack_len = 0;
    s_ack_status = 0;
    s_ack_ovf = false;
    if (send_reset && note_touched) {
        s_reset_stream_id = s_voice.note_stream;
        s_voice.need_reset = true;
    }
    s_voice.note_stream = 0;
    s_voice.photo = false;
    s_voice.state = VOICE_STATE_IDLE;
}

/* Queue a "show reply overlay" for after the state lock is released.
 * Lock held. Never touches LVGL here: the LVGL-side refresh holds
 * lv_vendor_disp_lock and reads voice state back, so calling display
 * APIs under s_voice.lock inverts the lock order and deadlocks. */
static void voice_show_locked(const char *title, const char *body)
{
    snprintf(s_pending_title, sizeof(s_pending_title), "%s", title);
    snprintf(s_pending_body, sizeof(s_pending_body), "%s", body);
    s_voice.pending_hide = false;
    s_voice.pending_show = true;
    /* 首次进入 SHOWING = 回合正常完成（回复已收到）。此时录音/发送都已
     * 结束，引擎空转，立刻恢复 KWS 待机，用户不用等 30s 展示窗口就能
     * 说唤醒词开下一轮。恢复动作必须在锁外做（voice_display_flush）。 */
    if (s_voice.state != VOICE_STATE_SHOWING) {
        s_voice.kws_restore_pending = true;
    }
    s_voice.show_deadline =
        rtos_get_time() + CONFIG_MUSE_VOICE_REPLY_SHOW_MS;
    s_voice.state = VOICE_STATE_SHOWING;
}

static void voice_hide_locked(void)
{
    s_voice.pending_show = false;
    s_voice.pending_hide = true;
}

/* Execute queued display actions. Call WITHOUT s_voice.lock held, from
 * any task: the snapshot is taken under a short critical section and the
 * display calls run after it. Concurrent flushers may overwrite
 * s_flush_* between the snapshot and the display call; the visible
 * glitch self-corrects on the next streaming update and is bounded to
 * identical overlay content, never to mixed locking. */
static void voice_display_flush(void)
{
    bool show, hide, restore_kws;

    if (!s_voice_inited) {
        return;
    }
    rtos_lock_mutex(&s_voice.lock);
    show = s_voice.pending_show;
    hide = s_voice.pending_hide;
    restore_kws = s_voice.kws_restore_pending;
    s_voice.pending_show = false;
    s_voice.pending_hide = false;
    s_voice.kws_restore_pending = false;
    if (show) {
        memcpy(s_flush_title, s_pending_title, sizeof(s_flush_title));
        memcpy(s_flush_body, s_pending_body, sizeof(s_flush_body));
    }
    rtos_unlock_mutex(&s_voice.lock);

    if (restore_kws) {
        /* 回合完成，引擎空转中：恢复 KWS 待机（voice_engine_release 内部
         * 只在 had_kws 时动作，幂等）。 */
        voice_engine_release();
    }

    if (show) {
        if (voice_chat_page_is_active()) {
            /* The voice-chat page shows the latest reply in its own
             * preview label (refreshed from voice_history by the page
             * timer), so skip the fullscreen overlay there — the user
             * stays on the page and can fire the next question directly.
             * Photo turns and other pages keep the overlay. */
        } else {
            (void)muse_display_show_voice_text(s_flush_title, s_flush_body);
            if (camera_preview_is_frozen()) {
                /* The reply answers a photo: the preview still owns the
                 * screen (LVGL stopped), so the overlay above was stashed
                 * by muse_display. Leave the preview — its stop flow resumes
                 * LVGL and the stash flushes — instead of leaving the user
                 * stranded on the frozen shot until they press back. */
                (void)camera_preview_stop();
            }
        }
    } else if (hide) {
        (void)muse_display_hide_voice_text();
    }
}

/* Compose and show "You said: ..." + the assistant text so far. */
static void voice_show_turn(void)
{
    char body[VOICE_HEARD_MAX + VOICE_REPLY_MAX + 24];
    if (s_heard[0] != '\0') {
        snprintf(body, sizeof(body), "You said:\n%s\n\n%s", s_heard,
                 s_reply[0] ? s_reply : "...");
    } else {
        snprintf(body, sizeof(body), "%s",
                 s_reply[0] ? s_reply : "...");
    }
    /* Archive the turn once (first time the reply text exists); heard text
     * may arrive later and only affects the live overlay, not history. */
    if (!s_voice.history_pushed && s_reply[0] != '\0') {
        voice_history_push(voice_turn_title(), s_reply);
        s_voice.history_pushed = true;
    }
    voice_show_locked(voice_turn_title(), body);
}

/* ---- user entry points (any task) ------------------------------------------*/

int muse_voice_ptt_start(void)
{
    int rc = 0;
    /* HEAD is generated in the first locked section but queued only at
     * commit time (after the engine bring-up below), so that the pump
     * can never pop it before need_open_note is set. */
    char head_buf[400];
    size_t head_len = 0;

    if (!s_voice_inited) {
        LOGW("PTT refused: voice not initialized (session not started)\r\n");
        return -2;
    }
    rtos_lock_mutex(&s_voice.lock);
    do {
        if (s_voice.engine_busy) {
            /* A previous bring-up is still running lock-free below. */
            LOGW("PTT refused: engine bring-up in progress\r\n");
            rc = -5;
            break;
        }
        if (s_voice.state == VOICE_STATE_RECORDING) {
            /* Finish the current take first (stop / the S4 toggle);
             * aborting mid-record would send a truncated note. */
            rc = -4;
            break;
        }
        if (s_voice.state == VOICE_STATE_CLOSING ||
            s_voice.state == VOICE_STATE_WAIT_REPLY) {
            /* Barge-in: the previous turn is still closing or its reply
             * is taking too long. Abort it (Reset on the old note stream;
             * voice_abort_locked stashes the id so the fresh stream
             * allocated below is not the reset target) and start fresh. */
            LOGW("PTT barge-in: aborting the previous turn\r\n");
            voice_abort_locked(true);
            s_voice.engine_had_ours = s_voice.engine_ours;
        }
        if (s_voice.state == VOICE_STATE_SHOWING) {
            /* A new question replaces the shown reply: drop the overlay
             * now; this turn's reply will redraw it. Without this the
             * 30 s display window blocked every follow-up question.
             * Queued here, executed by voice_display_flush() after the
             * lock is released. */
            voice_hide_locked();
        }
        if (!s_voice.connected) {
            LOGW("PTT refused: muse session not connected\r\n");
            rc = -2;
            break;
        }
        s_voice.dropped_frames = 0;
        s_voice.pool_full = false;
        s_voice.photo = false;
        voice_turn_reset_locked();
        /* The note body starts with the JSON head (its own first chunk),
         * then base64 of the WAV header + PCM. The head names this board
         * (device_id) so the server attributes the turn to the device
         * instead of the account main chat, and its message field asks
         * the VM to push the reply via display.draw_text (both
         * Muse-documented requirements, 2026-10). session_id is only
         * sent when MUSE_NOTE_SEND_SESSION_ID is enabled: a fresh id
         * black-holed the whole turn in the field (side-chat events do
         * not reach our unfiltered subscription). */
        head_len =
            muse_voice_proto_note_head(head_buf, sizeof(head_buf),
                                       CONFIG_MUSE_NOTE_REPLY_HINT,
                                       muse_service_node_id(),
                                       VOICE_NOTE_SESSION_ID);
         if (!head_len) {
             rc = -3;
             break;
         }
         LOGI("voice note head: %s\r\n", head_buf);
        /* Mark the bring-up; it runs AFTER the lock is released:
         * audio_engine_start / KWS park block on audio-side locks and
         * must never run under s_voice.lock (LVGL reads the state under
         * disp_lock; field log: second PTT deadlocked the whole UI). */
        s_voice.engine_busy = true;
        s_voice.engine_had_ours = s_voice.engine_had_ours ||
                                  s_voice.engine_ours;
    } while (0);
    rtos_unlock_mutex(&s_voice.lock);
    voice_display_flush();

    if (rc != 0) {
        return rc;
    }

    if (s_voice.engine_had_ours) {
        /* Barge-in over a live recording: stop the old engine before
         * acquiring it again (acquire refuses a busy engine). Lock-free,
         * idempotent when the engine was not ours. */
        voice_engine_release();
        s_voice.engine_had_ours = false;
    }
    if (voice_engine_acquire() != 0) {
        rtos_lock_mutex(&s_voice.lock);
        voice_queue_drain();
        s_voice.engine_busy = false;
        s_voice.engine_had_ours = false;
        rtos_unlock_mutex(&s_voice.lock);
        voice_display_flush();
        return -3;
    }

    bool immediate_stop;
    rtos_lock_mutex(&s_voice.lock);
    s_voice.engine_busy = false;
    if (!s_voice.connected) {
        /* The chat link dropped while the engine was coming up. The HEAD
         * chunk is already queued (and may even have been consumed by a
         * dying send): committing now would record a note whose body
         * resumes on a fresh OPEN_NOTE with no JSON head — the server
         * parses base64 as JSON and rejects the stream with
         * "400 invalid JSON at line 1 column 1" (field-verified). Drop
         * the take; the user re-speaks once the link is back. */
        voice_queue_drain();
        s_voice.engine_had_ours = false;
        rtos_unlock_mutex(&s_voice.lock);
        voice_display_flush();
        return -6;
    }
    /* Fresh note stream id per turn (upstream s_conn.next_id++); the
     * subscribe stream is opened once per connection. */
    s_voice.note_stream = s_next_stream++;
    muse_voice_proto_wav_header(s_stage, MUSE_VOICE_SAMPLE_RATE_HZ);
    s_stage_len = MUSE_VOICE_WAV_HEADER_BYTES;
    s_voice.need_open_note = true;
    /* Queue the HEAD only now, atomically with the OPEN_NOTE flag. The
     * pump pops FIFO and, with need_open_note already true, emits
     * OPEN_NOTE before any body byte. Queueing it in the first locked
     * section let the pump race ahead of the lock-free engine bring-up
     * and send the HEAD as a bare DATA on a stream the server never
     * opened — the server rejected it and the take died (field: the
     * first PTT after boot always failed this way, when the engine
     * bring-up is slowest; later takes reused the hot engine and
     * committed before the pump woke up). */
    if (!chunk_push(head_buf, head_len, false)) {
        s_voice.need_open_note = false;
        s_voice.note_stream = 0;
        voice_queue_drain();
        s_voice.engine_had_ours = false;
        s_voice.state = VOICE_STATE_IDLE;
        rtos_unlock_mutex(&s_voice.lock);
        voice_display_flush();
        return -3;
    }
    s_voice.record_deadline = rtos_get_time() +
                              CONFIG_MUSE_VOICE_MAX_RECORD_MS;
    s_voice.vad_speech_ms = 0;
    s_voice.vad_silence_since = 0;
    s_voice.vad_last_ms = 0;
    s_voice.state = VOICE_STATE_RECORDING;
    immediate_stop = s_voice.stop_while_busy;
    s_voice.stop_while_busy = false;
    rtos_unlock_mutex(&s_voice.lock);
    voice_display_flush();

    LOGI("PTT start\r\n");
    LOGI("PTT psram: free=%lu min=%lu\r\n",
         (unsigned long)rtos_get_psram_free_heap_size(),
         (unsigned long)rtos_get_psram_minimum_free_heap_size());
    if (immediate_stop) {
        (void)muse_voice_ptt_stop();
    }
    return 0;
}

/* Flush the staged PCM tail as the final body chunk. The caller has
 * already moved the state out of RECORDING, so the resident mic callback
 * (voice_mic_read_cb) drops frames from here on; the short lock around
 * stage_emit serialises against a callback already in flight (the engine
 * no longer stops to provide that implicit sync — see
 * voice_engine_release). */
static void voice_stop_recording_tail(void)
{
    rtos_lock_mutex(&s_voice.lock);
    stage_emit(true);
    rtos_unlock_mutex(&s_voice.lock);
}

int muse_voice_ptt_stop(void)
{
    bool do_release = false;

    if (!s_voice_inited) {
        return 0;
    }
    rtos_lock_mutex(&s_voice.lock);
    if (s_voice.state == VOICE_STATE_RECORDING) {
        if (s_voice.engine_busy) {
            /* ptt_start is still bringing the engine up lock-free; it
             * re-checks this flag right after committing and stops then. */
            s_voice.stop_while_busy = true;
            rtos_unlock_mutex(&s_voice.lock);
            return 0;
        }
        /* Queued chunks keep streaming to the server. The mic stop and
         * the staged-PCM tail flush happen after the lock is released,
         * in the original order (mic off first). */
        s_voice.state = VOICE_STATE_CLOSING;
        do_release = true;
    }
    rtos_unlock_mutex(&s_voice.lock);

    if (do_release) {
        voice_stop_recording_tail();
        voice_display_flush();
        LOGI("PTT stop\r\n");
        LOGI("PTT psram: free=%lu min=%lu\r\n",
             (unsigned long)rtos_get_psram_free_heap_size(),
             (unsigned long)rtos_get_psram_minimum_free_heap_size());
    }
    return 0;
}

int muse_voice_ptt_toggle(void)
{
    return muse_voice_recording() ? muse_voice_ptt_stop()
                                  : muse_voice_ptt_start();
}

bool muse_voice_recording(void)
{
    bool recording;
    if (!s_voice_inited) {
        return false;
    }
    rtos_lock_mutex(&s_voice.lock);
    recording = s_voice.state == VOICE_STATE_RECORDING;
    rtos_unlock_mutex(&s_voice.lock);
    return recording;
}

muse_voice_ui_phase_t muse_voice_ui_phase(void)
{
    muse_voice_ui_phase_t phase = MUSE_VOICE_UI_IDLE;
    if (!s_voice_inited) {
        return phase;
    }
    rtos_lock_mutex(&s_voice.lock);
    switch (s_voice.state) {
    case VOICE_STATE_RECORDING:
        phase = MUSE_VOICE_UI_RECORDING;
        break;
    case VOICE_STATE_CLOSING:
    case VOICE_STATE_WAIT_REPLY:
        phase = MUSE_VOICE_UI_WAITING;
        break;
    case VOICE_STATE_SHOWING:
        phase = MUSE_VOICE_UI_SHOWING;
        break;
    default:
        break;
    }
    rtos_unlock_mutex(&s_voice.lock);
    return phase;
}

bool muse_voice_heard(char *out, size_t cap)
{
    bool ok = false;
    if (out == NULL || cap == 0U) {
        return false;
    }
    out[0] = '\0';
    if (!s_voice_inited) {
        return false;
    }
    rtos_lock_mutex(&s_voice.lock);
    /* 服务器转写失败时回声是 "[Voice note]" 占位符（实机日志），不是有用
     * 的反馈，按无回声处理。 */
    if ((s_voice.state == VOICE_STATE_CLOSING ||
         s_voice.state == VOICE_STATE_WAIT_REPLY) &&
        s_heard[0] != '\0' && s_heard[0] != '[') {
        snprintf(out, cap, "%s", s_heard);
        ok = true;
    }
    rtos_unlock_mutex(&s_voice.lock);
    return ok;
}

int muse_photo_send(const uint8_t *jpeg, size_t jpeg_len, const char *query)
{
    int rc = 0;
    char *b64 = NULL;
    size_t b64_cap, b64_len;

    if (!s_voice_inited || jpeg == NULL || jpeg_len == 0U) {
        return -4;
    }
    if (jpeg_len > CONFIG_MUSE_PHOTO_MAX_JPEG_BYTES) {
        LOGW("photo refused: %u bytes > cap %u\r\n",
             (unsigned)jpeg_len,
             (unsigned)CONFIG_MUSE_PHOTO_MAX_JPEG_BYTES);
        return -5;
    }

    /* Encode up-front, outside the lock: a 640x480 q5 JPEG is tens of KB,
     * its base64 ~4/3 of that; the staging buffer comes from the shared
     * PSRAM heap. +8: the final chunk appends NOTE_TAIL after the base64
     * end (same slack as the voice stage buffer). */
    b64_cap = ((jpeg_len + 2U) / 3U) * 4U + 8U;
    b64 = psram_zalloc(b64_cap);
    if (b64 == NULL) {
        LOGE("photo: base64 staging alloc failed (%u bytes)\r\n",
             (unsigned)b64_cap);
        return -3;
    }
    b64_len = muse_voice_proto_base64(jpeg, jpeg_len, b64);

    rtos_lock_mutex(&s_voice.lock);
    do {
        if (s_voice.state == VOICE_STATE_RECORDING) {
            rc = -1;   /* finish the current take first */
            break;
        }
        if (s_voice.state == VOICE_STATE_CLOSING ||
            s_voice.state == VOICE_STATE_WAIT_REPLY) {
            /* Same barge-in rule as ptt_start. */
            LOGW("photo barge-in: aborting the previous turn\r\n");
            voice_abort_locked(true);
            s_voice.engine_had_ours = s_voice.engine_ours;
        }
        if (s_voice.state == VOICE_STATE_SHOWING) {
            /* Same replace-the-shown-reply rule as ptt_start. */
            voice_hide_locked();
        }
        if (!s_voice.connected) {
            LOGW("photo refused: muse session not connected\r\n");
            rc = -2;
            break;
        }
        voice_turn_reset_locked();
        /* Commit the stream BEFORE queueing any chunk: the pump checks
         * need_open_note under this same lock, so a reader can never
         * pop body bytes ahead of OPEN_NOTE (same race as ptt_start).
         * On any queueing failure voice_abort_locked rolls the commit
         * back (need_open_note, note_stream, photo, drain). */
        s_voice.note_stream = s_next_stream++;
        s_voice.photo = true;
        s_voice.need_open_note = true;
        /* HEAD chunk: JSON head naming this board (same identity story
         * as the voice note; session_id gated by the Kconfig) and the
         * image item. */
        char head_buf[400];
        size_t head_len =
            muse_voice_proto_image_note_head(head_buf, sizeof(head_buf),
                                             query != NULL ? query
                                                           : CONFIG_MUSE_NOTE_REPLY_HINT,
                                             muse_service_node_id(),
                                             VOICE_NOTE_SESSION_ID);
         if (head_len == 0U || !chunk_push(head_buf, head_len, false)) {
             voice_abort_locked(false);
             rc = -3;
             break;
         }
         LOGI("photo note head: %s\r\n", head_buf);
        /* base64 body, MUSE_VOICE_STAGE_BYTES raw per chunk so only the
         * final chunk carries '=' padding; it also appends NOTE_TAIL
         * (identical to the voice tail) and is flagged end_body. */
        size_t tail_len = 0;
        const char *tail = muse_voice_proto_note_tail(&tail_len);
        bool queued = true;
        size_t off = 0;
        while (off < b64_len) {
            size_t n = b64_len - off;
            bool last = false;
            if (n > MUSE_VOICE_STAGE_BYTES) {
                n = MUSE_VOICE_STAGE_BYTES;
            } else {
                last = true;
                memcpy(b64 + off + n, tail, tail_len);
                n += tail_len;
            }
            if (!chunk_push(b64 + off, n, last)) {
                queued = false;
                break;
            }
            off += last ? (b64_len - off) : n;
        }
        if (!queued) {
            /* Pool exhausted mid-photo: pull the partial note back out;
             * voice_abort_locked also rolls back the commit above. */
            voice_abort_locked(false);
            rc = -3;
            break;
        }
        /* The whole note is queued and the commit already done, so the
         * turn goes straight to CLOSING; the pump drains it (OPEN_NOTE,
         * DATA..., END) and the END ack moves it to WAIT_REPLY exactly
         * like a voice note. */
        s_voice.state = VOICE_STATE_CLOSING;
        LOGI("photo note queued: jpeg=%u b64=%u chunks\r\n",
             (unsigned)jpeg_len, (unsigned)b64_len);
    } while (0);
    rtos_unlock_mutex(&s_voice.lock);
    if (s_voice.engine_had_ours) {
        /* Photo barge-in over a live recording: stop the old engine
         * lock-free (photo never re-acquires it). */
        voice_engine_release();
        s_voice.engine_had_ours = false;
    }
    voice_display_flush();
    free(b64);
    return rc;
}

bool muse_voice_tx_active(void)
{
    bool active;
    rtos_lock_mutex(&s_voice.lock);
    active = s_voice.state == VOICE_STATE_RECORDING ||
             s_voice.state == VOICE_STATE_CLOSING ||
             s_voice.state == VOICE_STATE_WAIT_REPLY ||
             s_voice.need_open_note || s_voice.need_open_sub ||
             s_voice.need_reset;
    rtos_unlock_mutex(&s_voice.lock);
    return active;
}

/* ---- session worker glue ----------------------------------------------------*/

muse_voice_tx_kind_t muse_voice_tx_next(uint8_t *frame, size_t cap,
                                        size_t *frame_len,
                                        uint32_t frame_wait_ms)
{
    muse_voice_tx_kind_t kind = MUSE_VOICE_TX_NONE;
    voice_chunk_t *c = NULL;

    rtos_lock_mutex(&s_voice.lock);
    /* Open the subscription first: upstream subscribes right after
     * connect, before any turn's note, and the reply for a note only
     * arrives on the long-lived subscription. */
    if (s_voice.need_reset) {
        kind = MUSE_VOICE_TX_RESET;
    } else if (s_voice.need_open_sub) {
        kind = MUSE_VOICE_TX_OPEN_SUB;
    } else if (s_voice.need_open_note) {
        kind = MUSE_VOICE_TX_OPEN_NOTE;
    }
    rtos_unlock_mutex(&s_voice.lock);
    if (kind != MUSE_VOICE_TX_NONE) {
        LOGI("tx ctrl: %s\r\n",
             kind == MUSE_VOICE_TX_RESET ? "RESET" :
             kind == MUSE_VOICE_TX_OPEN_SUB ? "OPEN_SUB" : "OPEN_NOTE");
        return kind;
    }

    /* Chunks pop in FIFO order, so HEAD -> PCM chunks -> final TAIL chunk.
     * Block only while recording (the mic may still fill the stage); when
     * closing the queue can only drain. */
    if (frame != NULL && cap > 0U &&
        rtos_pop_from_queue(&s_voice.data_q, &c,
                            muse_voice_recording() ? frame_wait_ms : 0U) == BK_OK &&
        c != NULL) {
        size_t n = c->len < cap ? c->len : cap;
        memcpy(frame, c->data, n);
        *frame_len = n;
        kind = c->end ? MUSE_VOICE_TX_END : MUSE_VOICE_TX_DATA;
        /* Field diagnosis for the recurring chat/stream 400 "invalid JSON
         * at line 1 column 1": dump the first bytes of the first frames so
         * a captured log shows whether HEAD ("{\"messag") really goes out
         * first. Reset the window on each new END. */
        if (kind == MUSE_VOICE_TX_DATA) {
            if (s_tx_probe_count < 4U) {
                LOGI("tx DATA #%u len=%u: %.10s\r\n",
                     (unsigned)s_tx_probe_count, (unsigned)n, (char *)c->data);
                s_tx_probe_count++;
            }
        } else {
            LOGI("tx END len=%u tail=%.10s\r\n", (unsigned)n,
                 n >= 10U ? (char *)frame + n - 10U : (char *)frame);
            s_tx_probe_count = 0U;
        }
        chunk_free(c);
    }
    return kind;
}

void muse_voice_tx_ack(muse_voice_tx_kind_t kind, bool ok)
{
    LOGI("tx ack: %s ok=%d\r\n",
         kind == MUSE_VOICE_TX_RESET ? "RESET" :
         kind == MUSE_VOICE_TX_OPEN_SUB ? "OPEN_SUB" :
         kind == MUSE_VOICE_TX_OPEN_NOTE ? "OPEN_NOTE" :
         kind == MUSE_VOICE_TX_END ? "END" : "DATA", (int)ok);
    if (!ok) {
        bool had_ours;
        rtos_lock_mutex(&s_voice.lock);
        voice_abort_locked(false);
        had_ours = s_voice.engine_ours;
        rtos_unlock_mutex(&s_voice.lock);
        if (had_ours) {
            voice_engine_release();   /* lock-free, see voice_abort_locked */
        }
        return;
    }

    rtos_lock_mutex(&s_voice.lock);
    switch (kind) {
    case MUSE_VOICE_TX_OPEN_NOTE:
        s_voice.need_open_note = false;
        break;
    case MUSE_VOICE_TX_OPEN_SUB:
        s_voice.need_open_sub = false;
        break;
    case MUSE_VOICE_TX_END:
        if (s_voice.state == VOICE_STATE_CLOSING) {
            s_voice.state = VOICE_STATE_WAIT_REPLY;
            s_voice.reply_deadline =
                rtos_get_time() +
                (s_voice.photo ? CONFIG_MUSE_PHOTO_REPLY_TIMEOUT_MS
                               : CONFIG_MUSE_VOICE_REPLY_TIMEOUT_MS);
        }
        break;
    case MUSE_VOICE_TX_RESET:
        s_voice.need_reset = false;
        s_reset_stream_id = 0;
        break;
    default:
        break;
    }
    rtos_unlock_mutex(&s_voice.lock);
}

int64_t muse_voice_note_stream(void)
{
    int64_t id;
    rtos_lock_mutex(&s_voice.lock);
    id = s_voice.note_stream;
    rtos_unlock_mutex(&s_voice.lock);
    return id;
}

int64_t muse_voice_sub_stream(void)
{
    int64_t id;
    rtos_lock_mutex(&s_voice.lock);
    id = s_voice.sub_stream;
    rtos_unlock_mutex(&s_voice.lock);
    return id;
}

/* Stream id the pending RESET must target (set by voice_abort on a
 * barge-in so a just-allocated fresh note stream is not reset by mistake).
 * 0 falls back to the current note stream. */
int64_t muse_voice_reset_stream(void)
{
    int64_t id;
    rtos_lock_mutex(&s_voice.lock);
    id = s_reset_stream_id != 0 ? s_reset_stream_id : s_voice.note_stream;
    rtos_unlock_mutex(&s_voice.lock);
    return id;
}

void muse_voice_on_session(bool connected)
{
    rtos_lock_mutex(&s_voice.lock);
    s_voice.connected = connected;
    LOGI("voice session: %s (state=%d)\r\n", connected ? "up" : "down",
         (int)s_voice.state);
    if (connected) {
        /* Per-connection long-lived subscription (upstream opens it right
         * after the handshake and keeps it for the connection's life).
         * Per-turn subscribes had the server fanning assistant events out
         * to zombie subscriptions from dead links while the live one got
         * user events only. */
        if (s_voice.sub_stream == 0) {
            s_voice.sub_stream = s_next_stream++;
            s_voice.need_open_sub = true;
        }
    } else {
        bool was_active = s_voice.state != VOICE_STATE_IDLE &&
                          s_voice.state != VOICE_STATE_SHOWING;
        if (was_active) {
            LOGW("session lost; aborting voice\r\n");
            voice_abort_locked(false);
            s_voice.engine_had_ours = s_voice.engine_ours;
        } else {
            /* State looks idle, but a turn can sit between "HEAD queued"
             * and "state committed" (ptt_start in progress) — drain it so
             * a note reopened on the next connection never resumes
             * body-first (server-side 400 invalid JSON). */
            voice_queue_drain();
        }
        s_voice.need_open_note = false;
        s_voice.note_stream = 0;
        s_voice.sub_stream = 0;
        s_voice.need_open_sub = false;
        s_sub_len = 0;
        s_sub_ovf = false;
    }
    rtos_unlock_mutex(&s_voice.lock);
    if (s_voice.engine_had_ours) {
        voice_engine_release();   /* lock-free, see voice_abort_locked */
        s_voice.engine_had_ours = false;
    }
}

/* ---- inbound: note ACK -------------------------------------------------------*/

void muse_voice_on_rx_ack(int status, const uint8_t *data, size_t len,
                          bool end)
{
    if (status != 0) {
        s_ack_status = status;
    }
    if (data != NULL && len > 0U) {
        if (len >= sizeof(s_ack) - s_ack_len) {
            s_ack_ovf = true;
        } else {
            memcpy(s_ack + s_ack_len, data, len);
            s_ack_len += len;
        }
    }
    if (!end) {
        return;
    }

    rtos_lock_mutex(&s_voice.lock);
    const bool active = s_voice.state != VOICE_STATE_IDLE &&
                        s_voice.state != VOICE_STATE_SHOWING;
    if (!active) {
        s_ack_len = 0;
        s_ack_status = 0;
        s_ack_ovf = false;
        rtos_unlock_mutex(&s_voice.lock);
        return;
    }
    if (s_ack_len < sizeof(s_ack)) {
        s_ack[s_ack_len] = '\0';
    }
    LOGI("chat/stream ACK status=%d body=%.480s\r\n", s_ack_status,
         s_ack_ovf ? "(overflow)" : s_ack);
    if (s_ack_status != 200) {
        voice_abort_locked(false);
        s_voice.engine_had_ours = s_voice.engine_ours;
        voice_show_locked(voice_turn_title(), "MUSE DIDN'T TAKE IT");
    } else if (!s_ack_ovf &&
               muse_voice_proto_parse_ack(s_ack, s_note_id,
                                          sizeof(s_note_id),
                                          s_parent_id,
                                          sizeof(s_parent_id)) == 0) {
        LOGI("note acked: id=%.40s parent=%.40s\r\n",
             s_note_id, s_parent_id);
    } else {
        /* WARN carries the body head so a dropped INFO line (serial loss
         * under debug spam) doesn't take the evidence with it. Turn
         * correlation degrades but replies still bind (empty note id =>
         * row_related accepts, draw_text binding is id-less). */
        LOGW("chat/stream ACK %s: %.200s\r\n",
             s_ack_ovf ? "overflow" : "without a message id", s_ack);
    }
    s_ack_len = 0;
    s_ack_status = 0;
    s_ack_ovf = false;
    rtos_unlock_mutex(&s_voice.lock);
    if (s_voice.engine_had_ours) {
        voice_engine_release();   /* lock-free, see voice_abort_locked */
        s_voice.engine_had_ours = false;
    }
    voice_display_flush();
}

/* ---- inbound: subscription NDJSON --------------------------------------------*/

/* Row relatedness: once the ACK supplied the note ids, a row carrying an
 * explicit parent must match one of them (the subscription is shared);
 * before the ACK we accept everything (single-turn device). Lock held. */
static bool row_related(const muse_voice_row_t *row)
{
    return !(row->reply_to[0] != '\0' && s_note_id[0] != '\0' &&
             strcmp(row->reply_to, s_note_id) != 0 &&
             strcmp(row->reply_to, s_parent_id) != 0);
}

/* Append assistant text to the shown reply; skips exact duplicates (a
 * message may arrive both as flushed deltas and as the full row). */
static void reply_append(const char *text)
{
    if (text[0] == '\0' || (s_reply[0] != '\0' && strstr(s_reply, text))) {
        return;
    }
    size_t used = strlen(s_reply);
    if (used) {
        snprintf(s_reply + used, sizeof(s_reply) - used, " ");
        used = strlen(s_reply);
    }
    snprintf(s_reply + used, sizeof(s_reply) - used, "%s", text);
}

/* Row handling, state lock held. */
static void voice_on_row(muse_voice_row_t *row)
{
    /* Between turns (IDLE) only assistant output matters — with the
     * persistent subscription a reply can land after the turn timed out,
     * and it should still be shown. User messages from other clients of
     * the shared chat session (the phone app) are not for the panel. */
    if (s_voice.state == VOICE_STATE_IDLE &&
        strcmp(row->event, "message.user") == 0) {
        return;
    }
    /* Streaming assistant text: accumulate deltas, flush at message_done
     * (upstream subscription_event merges s_delta into the done row). */
    if (strcmp(row->event, "delta.message_start") == 0 ||
        strcmp(row->event, "delta.text_append") == 0) {
        if (!row_related(row)) {
            return;
        }
        if (strcmp(s_delta_msg, row->msg) != 0) {
            snprintf(s_delta_msg, sizeof(s_delta_msg), "%s", row->msg);
            s_delta[0] = '\0';
        }
        if (strcmp(row->event, "delta.text_append") == 0 &&
            row->text[0] != '\0') {
            size_t used = strlen(s_delta);
            snprintf(s_delta + used, sizeof(s_delta) - used, "%s", row->text);
        }
        return;
    }
    if (strcmp(row->event, "delta.message_done") == 0) {
        if (s_delta[0] != '\0' && row_related(row)) {
            reply_append(s_delta);
            if (!s_voice.replied) {
                LOGI("voice reply: %.80s\r\n", s_delta);
            }
            s_voice.replied = true;
            voice_show_turn();
        }
        s_delta[0] = '\0';
        s_delta_msg[0] = '\0';
        return;
    }
    if (strcmp(row->event, "message.user") == 0) {
        /* Our own note, transcribed: "<text>\n[file:audio/wav ...]", or
         * "[Voice note]" before transcription. */
        if (s_note_id[0] != '\0' &&
            strcmp(row->msg, s_note_id) != 0 &&
            strcmp(row->msg, s_parent_id) != 0) {
            return;
        }
        char *att = strstr(row->text, "\n[file:");
        if (att != NULL) {
            *att = '\0';
        }
        /* A photo note transcribes to just the file item ("[image:...]"
         * in the 2026-10 server shape, "[file:...]" earlier); neither is a
         * "you said". Server-generated transcripts are bracketed. */
        if (row->text[0] && row->text[0] != '[' &&
            s_heard[0] == '\0') {
            snprintf(s_heard, sizeof(s_heard), "%s", row->text);
            LOGI("heard: %.60s\r\n", s_heard);
            if (s_voice.state == VOICE_STATE_SHOWING) {
                voice_show_turn();
            }
        }
        return;
    }
    if (strcmp(row->event, "message.assistant") != 0) {
        return;   /* other traffic */
    }
    if (!row->ready || row->text[0] == '\0' || !row_related(row)) {
        return;
    }
    reply_append(row->text);
    if (!s_voice.replied) {
        LOGI("voice reply: %.80s\r\n", row->text);
    }
    s_voice.replied = true;
    voice_show_turn();
}

bool muse_voice_on_reply_text(const char *text)
{
    bool claimed = false;

    if (!s_voice_inited || text == NULL || text[0] == '\0') {
        return false;
    }
    rtos_lock_mutex(&s_voice.lock);
    if (s_voice.state == VOICE_STATE_CLOSING ||
        s_voice.state == VOICE_STATE_WAIT_REPLY) {
        /* The note is fully queued/sent and this invoke IS the reply, so
         * the subscription wait is over: show the text like a subscription
         * reply (state -> SHOWING drops the reply deadline) and never Reset
         * the note stream — that would cancel the server-side turn that
         * just answered. The note stream id is kept (no reset queued); an
         * ACK arriving after the claim is ignored like any late ACK, and a
         * duplicate subscription message.assistant is deduped by
         * reply_append(). */
        reply_append(text);
        if (!s_voice.replied) {
            LOGI("voice reply (draw_text): %.80s\r\n", text);
        }
        s_voice.replied = true;
        voice_show_turn();
        claimed = true;
    }
    rtos_unlock_mutex(&s_voice.lock);
    voice_display_flush();
    return claimed;
}

static void voice_sub_feed(const uint8_t *data, size_t len)
{
    while (len) {
        const uint8_t *newline = memchr(data, '\n', len);
        size_t n = newline ? (size_t)(newline - (const uint8_t *)data) : len;
        if (!s_sub_ovf && n) {
            if (n >= sizeof(s_sub_line) - s_sub_len) {
                s_sub_ovf = true;   /* row too long: drop it, keep parsing */
            } else {
                memcpy(s_sub_line + s_sub_len, data, n);
                s_sub_len += n;
            }
        }
        if (newline) {
            if (!s_sub_ovf && s_sub_len &&
                muse_voice_proto_parse_row(s_sub_line, &s_row)) {
                LOGI("sub row: ev=%s ready=%d msg=%.24s reply_to=%.24s text=%.40s\r\n",
                     s_row.event, s_row.ready ? 1 : 0, s_row.msg,
                     s_row.reply_to, s_row.text);
                rtos_lock_mutex(&s_voice.lock);
                voice_on_row(&s_row);
                rtos_unlock_mutex(&s_voice.lock);
                voice_display_flush();
            }
            s_sub_len = 0;
            s_sub_ovf = false;
            n++;
        }
        data += n;
        len -= n;
    }
}

/* The subscription is long-lived; the peer ending or resetting it is a
 * transport event, not a reply verdict — reopen a fresh one under the
 * state lock and keep any in-flight turn waiting. */
static void sub_reopen_locked(void)
{
    s_voice.sub_stream = 0;
    s_sub_len = 0;
    s_sub_ovf = false;
    if (s_voice.connected) {
        s_voice.sub_stream = s_next_stream++;
        s_voice.need_open_sub = true;
    }
}

void muse_voice_on_rx_sub_data(int status, const uint8_t *data, size_t len,
                               bool end)
{
    if (status >= 400) {
        LOGW("subscribe rejected (HTTP %d); reopening\r\n", status);
        rtos_lock_mutex(&s_voice.lock);
        sub_reopen_locked();
        rtos_unlock_mutex(&s_voice.lock);
        return;
    }
    if (data != NULL && len > 0U) {
        voice_sub_feed(data, len);
    }
    if (end) {
        rtos_lock_mutex(&s_voice.lock);
        LOGW("subscribe stream closed by peer; reopening\r\n");
        sub_reopen_locked();
        rtos_unlock_mutex(&s_voice.lock);
    }
}

void muse_voice_on_rx_note_reset(void)
{
    rtos_lock_mutex(&s_voice.lock);
    if (s_voice.state != VOICE_STATE_IDLE &&
        s_voice.state != VOICE_STATE_SHOWING) {
        LOGW("voice note stream reset by peer\r\n");
        voice_abort_locked(false);
        s_voice.engine_had_ours = s_voice.engine_ours;
        voice_show_locked(voice_turn_title(), "request was reset");
    }
    rtos_unlock_mutex(&s_voice.lock);
    if (s_voice.engine_had_ours) {
        voice_engine_release();   /* lock-free, see voice_abort_locked */
        s_voice.engine_had_ours = false;
    }
    voice_display_flush();
}

void muse_voice_on_rx_sub_reset(void)
{
    rtos_lock_mutex(&s_voice.lock);
    LOGW("subscribe stream reset by peer; reopening\r\n");
    sub_reopen_locked();
    rtos_unlock_mutex(&s_voice.lock);
}

void muse_voice_on_tick(void)
{
    bool do_release = false;

    rtos_lock_mutex(&s_voice.lock);
    switch (s_voice.state) {
    case VOICE_STATE_RECORDING: {
        /* 诊断：确认 tick 在跑 + VAD 累计值（排查唤醒后不能自动停止） */
        static uint32_t s_vad_dbg_ms;
        uint32_t dbg_now = rtos_get_time();
        if ((uint32_t)(dbg_now - s_vad_dbg_ms) >= 500U) {
            s_vad_dbg_ms = dbg_now;
            LOGI("vad tick: mic=%u speech=%lu silence_since=%lu busy=%d\r\n",
                 (unsigned)audio_engine_get_mic_active(),
                 (unsigned long)s_voice.vad_speech_ms,
                 (unsigned long)s_voice.vad_silence_since,
                 (int)s_voice.engine_busy);
        }
        if (s_voice.pool_full) {
            /* The buffer is the real record limit: end the turn now so
             * what was captured stays intact. */
            LOGW("chunk pool full; ending the note\r\n");
            s_voice.pool_full = false;
            s_voice.state = VOICE_STATE_CLOSING;
            do_release = true;
        } else if (deadline_passed(s_voice.record_deadline)) {
            LOGW("max record duration reached\r\n");
            s_voice.state = VOICE_STATE_CLOSING;
            do_release = true;
        } else if (!s_voice.engine_busy) {
            /* VAD end-point (audio_engine AEC V3 vad_state_cb edge, the
             * same hysteresis-filtered signal that drives the UI meter):
             * after >= MIN_SPEECH of speech, END_MS of trailing silence
             * closes the note automatically — a keyword-wakeup turn would
             * otherwise wait for a button press that never comes. */
            uint32_t now = rtos_get_time();
            if (!deadline_passed(s_ack_tone_suppress_until)) {
                /* 应答音抑制窗：泄漏进 mic 的 VAD 不计入说话/静音累计，
                 * 防止 tick 路径把录音提前停掉（事件路径同样被挡）。 */
                s_voice.vad_speech_ms = 0;
                s_voice.vad_silence_since = 0;
            } else if (audio_engine_get_mic_active()) {
                if (s_voice.vad_last_ms != 0U) {
                    s_voice.vad_speech_ms += now - s_voice.vad_last_ms;
                }
                s_voice.vad_silence_since = 0;
            } else if (s_voice.vad_speech_ms != 0U) {
                if (s_voice.vad_silence_since == 0U) {
                    s_voice.vad_silence_since = (now == 0U) ? 1U : now;
                } else if (s_voice.vad_speech_ms >=
                               CONFIG_MUSE_VOICE_VAD_MIN_SPEECH_MS &&
                           now - s_voice.vad_silence_since >=
                               CONFIG_MUSE_VOICE_VAD_END_MS) {
                    LOGI("VAD end: speech=%lu ms, silence=%lu ms -> auto stop\r\n",
                         (unsigned long)s_voice.vad_speech_ms,
                         (unsigned long)(now - s_voice.vad_silence_since));
                    s_voice.state = VOICE_STATE_CLOSING;
                    do_release = true;
                }
            }
            s_voice.vad_last_ms = now;
        }
        break;
    }
    case VOICE_STATE_WAIT_REPLY:
        if (deadline_passed(s_voice.reply_deadline)) {
            LOGW("voice reply timeout\r\n");
            voice_abort_locked(true);
            s_voice.engine_had_ours = s_voice.engine_ours;
            voice_show_locked(voice_turn_title(), "no reply (timeout)");
        }
        break;
    case VOICE_STATE_SHOWING:
        if (deadline_passed(s_voice.show_deadline)) {
            voice_hide_locked();
            s_voice.state = VOICE_STATE_IDLE;
        }
        break;
    default:
        break;
    }
    rtos_unlock_mutex(&s_voice.lock);

    if (s_voice.engine_had_ours) {
        voice_engine_release();   /* lock-free, see voice_abort_locked */
        s_voice.engine_had_ours = false;
    }
    if (do_release) {
        /* Recording ended by limit/timeout: mic off first, then the
         * staged tail, engine work lock-free. */
        voice_stop_recording_tail();
    }
    voice_display_flush();
}

/* ---- init --------------------------------------------------------------------*/

int muse_voice_init(void)
{
    uint32_t i;

    if (rtos_init_mutex(&s_voice.lock) != BK_OK) {
        return -1;
    }
    if (rtos_init_queue(&s_voice.free_q, "muse_voice_free",
                        sizeof(voice_chunk_t *), VOICE_CHUNK_POOL) != BK_OK) {
        rtos_deinit_mutex(&s_voice.lock);
        return -1;
    }
    if (rtos_init_queue(&s_voice.data_q, "muse_voice_data",
                        sizeof(voice_chunk_t *), VOICE_CHUNK_POOL) != BK_OK) {
        rtos_deinit_queue(&s_voice.free_q);
        rtos_deinit_mutex(&s_voice.lock);
        return -1;
    }
    /* ~1.3 MB pool from the PSRAM heap (psram_zalloc'd blocks are released
     * with plain free(); this pool lives forever). */
    s_chunk_pool = psram_zalloc(VOICE_CHUNK_POOL * sizeof(voice_chunk_t));
    if (s_chunk_pool == NULL) {
        LOGE("chunk pool alloc failed (%u bytes)\r\n",
             (unsigned)(VOICE_CHUNK_POOL * sizeof(voice_chunk_t)));
        rtos_deinit_queue(&s_voice.data_q);
        rtos_deinit_queue(&s_voice.free_q);
        rtos_deinit_mutex(&s_voice.lock);
        return -1;
    }
    LOGI("voice init ok: pool=%u chunks (%u bytes)\r\n",
         (unsigned)VOICE_CHUNK_POOL,
         (unsigned)(VOICE_CHUNK_POOL * sizeof(voice_chunk_t)));
    for (i = 0U; i < VOICE_CHUNK_POOL; i++) {
        voice_chunk_t *c = &s_chunk_pool[i];
        if (rtos_push_to_queue(&s_voice.free_q, &c, 0) != BK_OK) {
            break;
        }
    }
    /* Fresh side-chat id for this boot (see the declaration above). */
    s_rng_state = rtos_get_time() ^ 0xA5A5A5A5U;
    if (s_rng_state == 0U) {
        s_rng_state = 0x12345678U;
    }
    (void)snprintf(s_session_id, sizeof(s_session_id),
                   "%08lx-%04lx-4%03lx-9%03lx-%04lx%08lx",
                   (unsigned long)voice_rng_next(),
                   (unsigned long)(voice_rng_next() & 0xffffU),
                   (unsigned long)(voice_rng_next() & 0xfffU),
                   (unsigned long)(voice_rng_next() & 0xfffU),
                   (unsigned long)(voice_rng_next() & 0xffffU),
                   (unsigned long)voice_rng_next());
    LOGI("voice init ok: pool=%u chunks (%u bytes), chat session %s "
         "(node %s)\r\n",
         (unsigned)VOICE_CHUNK_POOL,
         (unsigned)(VOICE_CHUNK_POOL * sizeof(voice_chunk_t)),
         s_session_id, muse_service_node_id());
    s_voice.state = VOICE_STATE_IDLE;
    s_voice_inited = true;
#if CONFIG_BEKEN_KWS
    /* Voice-wakeup standby: keyword hit -> APP_EVT_ASR_WAKEUP ->
     * voice_kws_wakeup_handler() -> muse_voice_ptt_start(). Registered
     * before standby start so no hit is lost between the two calls. */
    app_event_register_handler(APP_EVT_ASR_WAKEUP,
                               voice_kws_wakeup_handler, NULL);
    /* 应答音播完收紧 VAD 抑制窗（见 voice_prompt_tone_finish_handler）。 */
    app_event_register_handler(APP_EVT_PROMPT_TONE_FINISH,
                               voice_prompt_tone_finish_handler, NULL);
    (void)voice_kws_standby_start();
#endif
    /* 语音端点（说完自动停止）走引擎的 AI_THINKING 边沿事件，见
     * voice_ai_thinking_handler 的注释。 */
    app_event_register_handler(APP_EVT_AI_THINKING,
                               voice_ai_thinking_handler, NULL);
    return 0;
}
