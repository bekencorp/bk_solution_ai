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
#include "muse_chat.h"

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
#include "muse_storage.h"   /* muse_storage_save/load_volc_asr/tts/appid() */
#include "muse_tts_volc.h"  /* muse_tts_volc_prewarm() */
#include "muse_voice_proto.h"
#include "ui/voice_chat_page.h" /* voice_chat_page_is_active() */

#define TAG "muse_voice"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
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
#ifndef CONFIG_MUSE_VOICE_VAD_GRACE_MS
/* Wake-up grace on the tick path: until this much time has passed since
 * PTT start, neither speech nor silence is accumulated. The wake word's
 * tail audio otherwise counts as speech and a short thinking pause then
 * trips the 1.2 s auto-end before the user has said anything (field
 * 2026-11-11). */
#define CONFIG_MUSE_VOICE_VAD_GRACE_MS    2500
#endif
#ifndef CONFIG_MUSE_VOICE_NO_SPEECH_MS
/* After the grace window, this much continued silence (no speech ever
 * heard) ends the take WITHOUT sending a note -- an empty WAV only earns
 * the server's "no transcript" reply and wastes a turn. The user gets a
 * "didn't hear anything" nudge on the panel instead (field 2026-11-11:
 * user often pauses after the wake word and the note ships silence). */
#define CONFIG_MUSE_VOICE_NO_SPEECH_MS    5000
#endif
#ifndef CONFIG_MUSE_VOICE_WAKEUP_RESIDUE_MS
/* The wakeup word's own VAD tail fires one more AI_THINKING right after the
 * take starts ("你好博通" alone passes the engine's 1 s MIN_LISTENING gate);
 * without this guard the event stops the just-started recording and an
 * almost-empty note goes out (field log 2026-11-05: server answered  The same window also absorbs the ack tone's
 * own VAD edges (the tone plays over the voice engine right at take start).
 * Real speech-end edges arrive later and are unaffected. */
#define CONFIG_MUSE_VOICE_WAKEUP_RESIDUE_MS 1200
#endif
#ifndef CONFIG_MUSE_VOICE_ACK_TONE_MAX_MS

#define CONFIG_MUSE_VOICE_ACK_TONE_MAX_MS 3000
#endif
#ifndef CONFIG_MUSE_VOICE_ACK_TONE_TAIL_MS
#define CONFIG_MUSE_VOICE_ACK_TONE_TAIL_MS 800
#endif
#ifndef CONFIG_MUSE_VOICE_ACK_TONE_PLAY_TAIL_MS

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
#ifndef CONFIG_MUSE_VOICE_CLOSING_TIMEOUT_MS

#define CONFIG_MUSE_VOICE_CLOSING_TIMEOUT_MS 15000
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
/* Grace window for a caption text after an image reply: the VM may send
 * draw_url alone or draw_url + draw_text; the text claim path needs the
 * turn still in CLOSING/WAIT_REPLY. */
#define VOICE_IMAGE_REPLY_GRACE_MS 5000U
#ifndef CONFIG_MUSE_NOTE_REPLY_HINT
/* In-band instruction baked into every note's message field: Muse pushes
 * a reply to the board via device.invoke only when the VM was told to
 * (conversational behaviour, not a protocol flag — Muse support 2026-10),
 * so the note carries the instruction itself instead of relying on chat
 * history. Empty string disables. Effective value comes from Kconfig
 * (Kconfig.projbuild MUSE_NOTE_REPLY_HINT; project defconfig overrides). */
#define CONFIG_MUSE_NOTE_REPLY_HINT "请通过 display.draw_text 把文字回复发到设备屏幕上；用户要图片时用 display.draw_url 把图片发到设备屏幕上，图片按 385x320 横屏尺寸生成，直接生成发送无需向我确认"
#endif
/* bool Kconfigs are undefined when off, so the choice must be made by the
 * preprocessor (a C expression would be an undeclared identifier). */
#ifdef CONFIG_MUSE_NOTE_SEND_SESSION_ID
#define VOICE_NOTE_SESSION_ID s_session_id
#else
#define VOICE_NOTE_SESSION_ID ""
#endif
/* 2026-11-09 experiment CONCLUDED (reverted same day): dropping device_id
 * did NOT switch the VM to delta streaming — with the draw_text hint
 * present it still answered via one-shot link.invoke (field log: zero
 * delta.* rows), and the turns landed in the account main chat where the
 * round visibly degraded (ASR echo 14.9 s, reply still missing 22 s
 * after END vs ~10 s total with board identity). Board attribution is
 * back on; keep "" here only if re-testing with an empty hint too. */
#define VOICE_NOTE_DEVICE_ID      muse_service_node_id()
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
    /* CLOSING 进入时刻（tick 懒置位，离开 CLOSING 即清零）：该状态只在
     * 等外部回调（END ack / 火山 final），没有任何自带超时，丢回调就会
     * 永久停住。见 CONFIG_MUSE_VOICE_CLOSING_TIMEOUT_MS。 */
    uint32_t closing_since;

    /* VAD auto-end bookkeeping for the current take (tick-driven):
     * speech accumulates while audio_engine_get_mic_active() is 1,
     * silence_since marks the start of the trailing silent run (0 =
     * currently hearing speech or no speech seen yet), last_ms is the
     * previous tick timestamp used to integrate speech duration. */
    uint32_t vad_speech_ms;
    uint32_t vad_silence_since;
    uint32_t vad_last_ms;
    /* Latched once any speech edge was seen this take: distinguishes
     * "never heard anything" (no-speech nudge, no note) from "spoke then
     * fell silent" (normal trailing-silence auto-end). */
    bool     vad_seen_speech;
    /* Wake-up grace: the wake word's own tail audio trips mic_active and
     * users often pause to think before speaking; until this deadline the
     * tick neither accumulates speech nor arms the silence timer, so the
     * trailing-silence auto-end can't fire on wake-word residue alone. */
    uint32_t vad_grace_until;
    /* 事件门闩：本回合出现过"起点落在宽限窗/应答音窗之外"的 VAD 语音段
     *（AI_LISTENING 时置位）。THINKING 自动结束只认这种段——窗内起头的段
     *（唤醒残尾、应答音泄漏、用户还没开口就在响的环境音）不配关掉 note
     *（2026-11-11 实机：2.6s 环境音起头于宽限窗内，其 THINKING 在用户
     * 开口前就把空 note 发出去了）。 */
    bool     vad_evt_speech;

    beken_queue_t free_q;
    beken_queue_t data_q;
    uint32_t dropped_frames;
    /* 本回合 read_cb 递进来的帧数。0 = 引擎采集链路是死的（不是用户没
     * 说话：静音也会出帧），收尾时用它决定要不要整机重启待机引擎。 */
    uint32_t mic_rx_frames;

    bool replied;
    bool image_replied;     /* draw_url answered this turn (short reply grace) */
    bool asr_turn;          /* CONFIG_MUSE_ASR_VOLC: PCM goes to volc, note is text */
    bool tts_done;          /* CONFIG_MUSE_TTS_VOLC: playback already kicked this turn */
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
     * engine: voice_engine_acquire()/release() touch the audio engine
     * (KWS park/restore, PA on/off, and a full start only when the
     * engine is not running; they take audio-side locks whose owners
     * also touch disp_lock). ptt_start sets engine_busy under the lock,
     * does the acquire lock-free, then commits the new state; ptt_stop
     * arriving in between is deferred via stop_while_busy. */
    bool pending_show;
    bool pending_hide;
    bool pending_dismiss_image; /* 新回合退役 draw_url 图片罩（锁外执行） */
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

/* 回复正文延迟上屏：回复到达即 kick TTS，正文先显示 "..."，等"第一句
 * 真正出声"再一起上屏，消除"文字先到、声音后到"的观感错位。放行条件：
 * 首句出声 / TTS 结束且一句都没播 / 下面的兜底超时。0 = 未挂起。
 *
 * 兜底只防 TTS 线程卡死：以前是 3s，会在首句音频稍晚（3~5s）时抢先
 * 放行，正是错位的来源；改成 15s 后正常路径不再被它干扰。 */
#define VOICE_REPLY_SHOW_DELAY_MS 15000U
static uint32_t s_reply_hold_until;
/* 本轮 TTS 是否真的播出过声音：用于"合成结束但一句都没播"时立即放行
 * 正文，而不是等 15s 兜底。 */
static bool s_tts_ever_played;
/* 本回合的状态/错误提示（不是助手回复）。语音页的气泡只认 voice_history，
 * 出错时不记提示就会一直显示上一条回复；muse_voice_ui_notice() 把它交给
 * 页面按当前回合展示。新回合或真实回复到达时清空。 */
static char s_turn_notice[96];
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

/* 兜底语音活动信号（0 = 无活动）：AEC VAD 会漏掉紧跟应答音开口的提问
 *（2026-11-09 实机： vad 全程未进 speech_start，说完不收尾，note 空挂
 * ~16s）。voice_mic_read_cb 对未被丢弃的帧算 mean|s16| 能量，超阈记为
 * now+HANGOVER；tick 路径与 audio_engine_get_mic_active() 取或。应答音
 * 本身在上方丢弃点被拦、到不了这里；窗后尾音由 suppress 窗遮罩。 */
static uint32_t s_energy_active_until;
#define VOICE_ENERGY_FLOOR        200U   /* mean|s16|：远高于环境噪底、低于正常说话 */
#define VOICE_ENERGY_HANGOVER_MS  300U

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
static int voice_engine_acquire(void);
static void voice_show_locked(const char *title, const char *body);
static void voice_notice_locked(const char *body);
static void voice_display_flush(void);
static void voice_reply_hold_release(void);
static void voice_push_hold_release(void);
static void voice_abort_locked(bool send_reset);
static bool chunk_push(const void *data, size_t len, bool end);
static void voice_show_turn(void);
static const char *voice_turn_title(void);

/* ---- Volcano speech credentials (runtime-only, never in source/defconfig)
 * Old console: one APP ID + one Access Token per service. `muse volcappid`,
 * `muse asrkey`, `muse ttskey` persist to easyflash and load here at voice
 * init; each module stays disabled until its own credential is set. */
static char s_volc_asr_key[128];
static char s_volc_tts_key[128];
static char s_volc_appid[128];
static char s_volc_asr_res[128];
static char s_volc_tts_voice[96];

const char *muse_voice_volc_asr_key(void)
{
    return s_volc_asr_key;
}

const char *muse_voice_volc_tts_key(void)
{
    return s_volc_tts_key;
}

const char *muse_voice_volc_appid(void)
{
    return s_volc_appid;
}

const char *muse_voice_volc_asr_res(void)
{
    return s_volc_asr_res;
}

const char *muse_voice_volc_tts_voice(void)
{
    return s_volc_tts_voice;
}

static int voice_set_volc_str(char *slot, size_t slot_cap, const char *value,
                              bool (*save)(const char *))
{
    if (value == NULL || value[0] == '\0' || strlen(value) >= slot_cap) {
        return -1;
    }
    if (!save(value)) {
        return -2;
    }
    snprintf(slot, slot_cap, "%s", value);
    return 0;
}

int muse_voice_set_volc_asr_key(const char *key)
{
    return voice_set_volc_str(s_volc_asr_key, sizeof(s_volc_asr_key), key,
                              muse_storage_save_volc_asr_key);
}

int muse_voice_set_volc_tts_key(const char *key)
{
    return voice_set_volc_str(s_volc_tts_key, sizeof(s_volc_tts_key), key,
                              muse_storage_save_volc_tts_key);
}

int muse_voice_set_volc_appid(const char *appid)
{
    return voice_set_volc_str(s_volc_appid, sizeof(s_volc_appid), appid,
                              muse_storage_save_volc_appid);
}

int muse_voice_set_volc_asr_res(const char *res)
{
    return voice_set_volc_str(s_volc_asr_res, sizeof(s_volc_asr_res), res,
                              muse_storage_save_volc_asr_res);
}

int muse_voice_set_volc_tts_voice(const char *voice)
{
    return voice_set_volc_str(s_volc_tts_voice, sizeof(s_volc_tts_voice),
                              voice, muse_storage_save_volc_tts_voice);
}

static void voice_volc_key_load(void)
{
    (void)muse_storage_load_volc_asr_key(s_volc_asr_key,
                                         sizeof(s_volc_asr_key));
    (void)muse_storage_load_volc_tts_key(s_volc_tts_key,
                                         sizeof(s_volc_tts_key));
    (void)muse_storage_load_volc_appid(s_volc_appid, sizeof(s_volc_appid));
    (void)muse_storage_load_volc_asr_res(s_volc_asr_res,
                                         sizeof(s_volc_asr_res));
    (void)muse_storage_load_volc_tts_voice(s_volc_tts_voice,
                                           sizeof(s_volc_tts_voice));
}

#if CONFIG_MUSE_ASR_VOLC
#include "muse_asr_volc.h"

/* Runs on the volc ws event task. */
static void voice_asr_on_text(const char *text);
static void voice_asr_on_error(int err, const char *msg);

/* One-shot finisher: volc_finish() blocks (<=12 s) waiting for the final
 * transcript, so it needs its own task -- never the audio read callback
 * or the chat worker. */
static void voice_asr_finish_main(void *arg)
{
    (void)arg;
    muse_asr_volc_finish();   /* fires exactly one callback */
    rtos_delete_thread(NULL);
}

static void voice_asr_finish_kick(void)
{
    beken_thread_t t;
    if (rtos_create_thread(&t, 6, "asr_fin", voice_asr_finish_main,
                           3072, NULL) != BK_OK) {
        muse_asr_volc_abort();
        voice_asr_on_error(-9, "finish task spawn failed");
    }
}

/* JSON-escape the ASR text into msg (cap given) and build a TEXT note
 * (no file items -- the transcript IS the message). */
static size_t voice_text_note_json(char *buf, size_t cap, const char *msg,
                                   const char *device_id,
                                   const char *session_id)
{
    char esc[320];
    size_t ei = 0;
    for (const char *p = msg; *p != '\0' && ei < sizeof(esc) - 2U; p++) {
        if (*p == '"' || *p == '\\') {
            esc[ei++] = '\\';
        }
        if ((uint8_t)*p >= 0x20U) {
            esc[ei++] = *p;   /* drop control chars */
        }
    }
    esc[ei] = '\0';
    const char *d = device_id ? device_id : "";
    const char *s = session_id ? session_id : "";
    int n;
    if (d[0] != '\0' && s[0] != '\0') {
        n = snprintf(buf, cap,
                     "{\"message\":\"%s\",\"device_id\":\"%s\","
                     "\"session_id\":\"%s\",\"output_modality\":\"text\"}",
                     esc, d, s);
    } else if (d[0] != '\0') {
        n = snprintf(buf, cap,
                     "{\"message\":\"%s\",\"device_id\":\"%s\","
                     "\"output_modality\":\"text\"}", esc, d);
    } else if (s[0] != '\0') {
        n = snprintf(buf, cap,
                     "{\"message\":\"%s\",\"session_id\":\"%s\","
                     "\"output_modality\":\"text\"}", esc, s);
    } else {
        n = snprintf(buf, cap,
                     "{\"message\":\"%s\",\"output_modality\":\"text\"}", esc);
    }
    return n > 0 && (size_t)n < cap ? (size_t)n : 0;
}

static void voice_asr_on_text(const char *text)
{
    char json[640];
    size_t len;
    bool kick_finish = false;

    rtos_lock_mutex(&s_voice.lock);
    if (!s_voice.asr_turn ||
        (s_voice.state != VOICE_STATE_CLOSING &&
         s_voice.state != VOICE_STATE_RECORDING)) {
        rtos_unlock_mutex(&s_voice.lock);
        return;
    }
    if (s_voice.state == VOICE_STATE_RECORDING) {
        /* 服务端先判尾（2.0 服务端端点检测）：本地 VAD 没来得及收尾，
         * final 在 RECORDING 中先到——按 CLOSING 同路径入队文本，同时把
         * 回合推进 CLOSING；收尾 kick 锁外做（finish 幂等：final 已在
         * 手，只负责拆除会话）。2026-11-13 实机：本地 VAD 对"呃"后停顿
         * 卡死，服务端 final 在 RECORDING 到达被丢弃，note 永远没发，
         * 屏幕只剩上一回合的回复。 */
        s_voice.state = VOICE_STATE_CLOSING;
        kick_finish = true;
    }
    len = voice_text_note_json(json, sizeof(json), text,
                               VOICE_NOTE_DEVICE_ID, VOICE_NOTE_SESSION_ID);
    snprintf(s_heard, sizeof(s_heard), "%s", text);
    /* OPEN_NOTE goes out only now, atomically with real content behind it:
     * ptt_start leaves need_open_note clear on ASR turns, so a failed ASR
     * session never leaves a bare OPEN_NOTE dangling on the server. */
    s_voice.need_open_note = true;
    if (len == 0U || !chunk_push(json, len, true)) {
        s_voice.need_open_note = false;
        voice_abort_locked(false);
        s_voice.engine_had_ours = s_voice.engine_ours;
        voice_notice_locked("语音发送失败，请重试");
        rtos_unlock_mutex(&s_voice.lock);
        voice_display_flush();
        return;
    }
    /* end_body on the only chunk makes the pump emit
     * OPEN_NOTE -> DATA(json) -> END on the ptt_start stream id.
     *
     * Do NOT voice_show_turn() here: voice_show_locked() unconditionally
     * moves the state to SHOWING, which used to (a) defeat the END-ack
     * transition CLOSING->WAIT_REPLY, so the draw_text reply was never
     * claimed into s_reply, and (b) run the TTS-kick check while s_reply
     * was still empty -- together they made TTS silent on every ASR turn
     * (field 2026-11-12). Let the END ack drive the turn into WAIT_REPLY;
     * the reply claim then shows the turn and kicks TTS, exactly like a
     * WAV note turn. */
    LOGI("asr text note: %s\r\n", text);
    rtos_unlock_mutex(&s_voice.lock);
    if (kick_finish) {
        /* 服务端已判尾：驱动会话拆除（muse_asr_volc_finish 对已完成的
         * 会话幂等），与 tick 的 VAD-end 收尾同路。 */
        voice_asr_finish_kick();
    }
#if CONFIG_MUSE_TTS_VOLC
    /* TTS prewarm now that the ASR uplink is finished (final transcript in
     * hand and the note queued). Starting it earlier - right after the ack
     * tone - put the prewarm TLS handshake on top of the ASR audio upload;
     * the uplink then hit ws_tcp_poll_write timeouts (errno=115) and the
     * turn died with "server silent" although speech had been recognized
     * (field 2026-11-19). The reply's own thinking pause is still long
     * enough for the handshake to park. */
    muse_tts_volc_prewarm();
#endif
    voice_display_flush();
}

static void voice_asr_on_error(int err, const char *msg)
{
    LOGW("asr error %d: %s\r\n", err, msg != NULL ? msg : "?");
    /* Release the volc session as well: without this, a refused handshake
     * left s_volc.active stuck true and every later start failed silently
     * with -1 until reboot (field 2026-11-12). Idempotent (the finish path
     * aborts first) and blocks ~400 ms, so keep it out of s_voice.lock. */
    muse_asr_volc_abort();
    rtos_lock_mutex(&s_voice.lock);
    if (s_voice.asr_turn &&
        (s_voice.state == VOICE_STATE_RECORDING ||
         s_voice.state == VOICE_STATE_CLOSING)) {
        voice_abort_locked(false);
        s_voice.engine_had_ours = s_voice.engine_ours;
        /* 空转写不是识别故障：宽限窗后只有噪声时 VAD 仍可能判出一段
         * "语音"，火山回空 final。按"没听到声音"提示，与本地静音收尾
         * 一致（2026-11-17 实机：唤醒后没说话，用户只看到唤醒无响应）。
         * 传输层被 reset 也与凭据无关（2026-11-17：openspeech 域名整条
         * 链路 RST），提示用户查网络而不是以为识别坏了。 */
        if (err == MUSE_ASR_ERR_EMPTY_TEXT) {
            voice_notice_locked("没听到声音，请再试一次");
        } else if (err == MUSE_ASR_ERR_TRANSPORT) {
            voice_notice_locked("网络连接失败，请检查网络");
        } else if (err == -7) {
            /* 上行音频发不出去（现场日志里都是 WiFi 拥塞导致 ws poll
             * 超时），不是识别模型听错，提示用户重说而不是以为设备坏了。 */
            voice_notice_locked("网络卡顿，请再说一次");
        } else if (err == -8) {
            /* 音频没有及时送到，服务端出不了 final。 */
            voice_notice_locked("没听清，请再说一次");
        } else {
            voice_notice_locked("语音识别失败，请重试");
        }
    }
    s_voice.asr_turn = false;
    rtos_unlock_mutex(&s_voice.lock);
    voice_display_flush();
}
#endif /* CONFIG_MUSE_ASR_VOLC */

#if CONFIG_MUSE_TTS_VOLC
#define VOICE_TTS_TEXT_MAX 400
static char s_tts_text[VOICE_TTS_TEXT_MAX];
static bool s_tts_busy;
static volatile bool s_tts_playing;
static bool s_tts_engine_held;
static bool s_tts_chain_stop;

/* 每句合成完成即回调（运行在 tts_volc 线程）：立即起播该句；回调返回后
 * tts 模块删除该文件。效果：第 1 句在发声时，后面的句子还在合成/下载，
 * 首音延迟只取决于第一句长度。path==NULL 是流结束标记。用户起新录音
 * （打断）时丢弃后续句子。 */
static void voice_tts_seg_cb(void *user, unsigned seg, const char *path,
                             bool last)
{
    (void)user; (void)seg; (void)last;
    extern audio_engine_prompt_tone_handle_t g_audio_engine_prompt_tone;
    prompt_tone_uri_info_t info;
    uint32_t deadline;

    if (path == NULL) {
        /* End of stream: restore KWS standby now instead of waiting for the
         * TTS websocket teardown. The tail call site fires again, but
         * s_tts_engine_held is already cleared so the release runs once. */
        if (s_tts_engine_held) {
            voice_engine_release();
            s_tts_engine_held = false;
        }
        return;   /* end-of-stream marker */
    }
    if (s_tts_chain_stop || muse_voice_recording()) {
        s_tts_chain_stop = true;
        return;
    }
    /* voice 引擎占用 PA；prompt-tone 混音口按引擎 16 kHz 速率消费
     * 16 kHz MP3。引擎只取一次，整条句子链播完再放。 */
    if (!s_tts_engine_held) {
        if (voice_engine_acquire() != 0) {
            s_tts_chain_stop = true;
            return;
        }
        s_tts_engine_held = true;
    }
    if (g_audio_engine_prompt_tone == NULL) {
        s_tts_chain_stop = true;
        return;
    }
    memset(&info, 0, sizeof(info));
    info.uri = (char *)path;
    s_tts_playing = true;
    if (audio_engine_prompt_tone_start(g_audio_engine_prompt_tone,
                                       &info) == BK_OK) {
        s_tts_ever_played = true;
        voice_reply_hold_release();   /* 已出声：正文同步上屏 */
        voice_push_hold_release();    /* same rule for a Muse-initiated push */
        deadline = rtos_get_time() + 60000U;
        while (s_tts_playing &&
               (int32_t)(rtos_get_time() - deadline) < 0) {
            rtos_delay_milliseconds(100);
        }
    }
    s_tts_playing = false;
}

static void voice_tts_main(void *arg)
{
    (void)arg;
    s_tts_chain_stop = false;
    s_tts_engine_held = false;
    s_tts_ever_played = false;
    if (muse_tts_volc_synthesize_stream(s_tts_text, voice_tts_seg_cb,
                                        NULL) != 0) {
        voice_reply_hold_release();   /* TTS 失败：正文立即放行，不跟着等 */
        voice_push_hold_release();    /* same for pushes: never stay held */
    } else if (!s_tts_ever_played) {
        /* 合成结束却一句都没播（录音抢占引擎、链被 stop、或全部句子
         * 合成失败）：正文不能一直停在 "..." 等 15s 兜底。 */
        voice_reply_hold_release();
        voice_push_hold_release();
    }
    if (s_tts_engine_held) {
        voice_engine_release();   /* 恢复 KWS 待机 */
        s_tts_engine_held = false;
    }
    s_tts_busy = false;
    rtos_delete_thread(NULL);
}

/* Called from voice_show_locked (s_voice.lock held) on the first show of
 * a reply; spawns the synth+playback task exactly once per turn. */
static void voice_tts_kick(const char *reply)
{
    beken_thread_t t;
    if (s_tts_busy) {
        return;
    }
    snprintf(s_tts_text, sizeof(s_tts_text), "%s", reply);
    s_tts_busy = true;
    /* 8 KB: the task runs an HTTPS POST (mbedTLS handshake alone needs
     * 4-6 KB) plus ~1.9 KB of local buffers and cJSON -- 4096 overflowed
     * inside bk_printf mid-synthesis (UsageFault STKOF, field 2026-11-12). */
    if (rtos_create_thread(&t, 4, "tts_volc", voice_tts_main,
                           8192, NULL) != BK_OK) {
        s_tts_busy = false;
    }
}
#endif /* CONFIG_MUSE_TTS_VOLC */

/* ---- Muse-initiated push (display.draw_text outside a voice turn) -------- */
/* 主动推送绝不打断对话：语音回合进行中（录音/收尾/等回复）或上一段 TTS 还在
 * 播时先挂起，等回到空闲再由 tick 上屏+播报。只保留最新一条——推送表达的是
 * "当前该提醒的事"，积压多条只会连续叨叨。 */
static char s_push_text[VOICE_REPLY_MAX];
static bool s_push_pending;
/* Pushed text is held until the first TTS sentence actually starts playing,
 * exactly like a reply: showing it at kick time puts the bubble on screen
 * seconds before the voice (synthesis needs a network round trip). */
static bool s_push_hold;
static uint32_t s_push_hold_until;
static char s_push_hold_text[VOICE_REPLY_MAX];

/* Lock held. */
static bool voice_push_busy_locked(void)
{
    if (s_voice.state == VOICE_STATE_RECORDING ||
        s_voice.state == VOICE_STATE_CLOSING ||
        s_voice.state == VOICE_STATE_WAIT_REPLY) {
        return true;
    }
#if CONFIG_MUSE_TTS_VOLC
    if (s_tts_busy) {
        return true;
    }
#endif
    return false;
}

/* 上屏：语音页在前台用页面气泡（field 2026-11-11，避免黑底覆盖视频），
 * 否则用常规文字浮层。Never called under s_voice.lock (LVGL lock order). */
static void voice_push_show(const char *text)
{
    if (voice_chat_page_is_active()) {
        voice_chat_page_show_push(text);
    } else {
        (void)muse_display_show_voice_text("Muse", text);
    }
}

/* Lock held. Start the push TTS and hold the text until the first sentence is
 * actually on the speaker; returns true when the caller must NOT show the text
 * yet (voice_push_hold_release() will, once the voice starts). */
static bool voice_push_begin_locked(const char *text)
{
#if CONFIG_MUSE_TTS_VOLC
    if (s_tts_busy) {
        return false;   /* another TTS run owns the speaker */
    }
    if (s_volc_tts_key[0] != '\0') {
        /* Arm the hold before the task starts: the first sentence can be on
         * the speaker before voice_tts_kick() returns on a fast network. */
        snprintf(s_push_hold_text, sizeof(s_push_hold_text), "%s", text);
        s_push_hold = true;
        s_push_hold_until = rtos_get_time() + VOICE_REPLY_SHOW_DELAY_MS;
        voice_tts_kick(text);
        if (s_tts_busy) {
            return true;
        }
        s_push_hold = false;
        s_push_hold_until = 0U;
        return false;
    }
    voice_tts_kick(text);   /* no TTS key: kick fails fast, show right away */
#else
    (void)text;
#endif
    return false;
}

/* Show a held push as soon as its first sentence starts, or when TTS failed
 * without playing anything. No-op when nothing is held. */
static void voice_push_hold_release(void)
{
    static char show[VOICE_REPLY_MAX];
    bool do_show = false;

    rtos_lock_mutex(&s_voice.lock);
    if (s_push_hold) {
        s_push_hold = false;
        s_push_hold_until = 0U;
        snprintf(show, sizeof(show), "%s", s_push_hold_text);
        do_show = true;
    }
    rtos_unlock_mutex(&s_voice.lock);
    if (do_show) {
        LOGI("push shown with voice: %.40s\r\n", show);
        voice_push_show(show);
    }
}

void muse_voice_push_text(const char *text)
{
    bool held = false;
    if (!s_voice_inited || text == NULL || text[0] == '\0') {
        return;
    }
    rtos_lock_mutex(&s_voice.lock);
    if (voice_push_busy_locked()) {
        if (s_push_pending) {
            LOGI("push replaced, still busy: %.40s\r\n", s_push_text);
        }
        snprintf(s_push_text, sizeof(s_push_text), "%s", text);
        s_push_pending = true;
        rtos_unlock_mutex(&s_voice.lock);
        LOGI("push deferred until idle: %.40s\r\n", text);
        return;
    }
    held = voice_push_begin_locked(text);
    rtos_unlock_mutex(&s_voice.lock);
    if (!held) {
        voice_push_show(text);
    }
}

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
    s_turn_notice[0] = '\0';   /* 新回合不再显示上一回合的提示 */
    s_reply_hold_until = 0U;   /* 新回合清掉上一回合的延迟窗 */
    s_voice.replied = false;
    s_voice.image_replied = false;
    s_voice.asr_turn = false;
    s_voice.tts_done = false;
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

/* Rebuild the standby engine after a turn: fresh mic/AEC/speaker elements,
 * KWS model and ASR. */
static void voice_rebuild_standby_engine(void)
{
    if (!s_voice.engine_inited) {
        if (audio_engine_init() != AUDIO_ENGINE_SUCCESS) {
            LOGW("standby rebuild: audio_engine_init failed\r\n");
            return;
        }
        s_voice.engine_inited = true;
    }
    s_voice.engine_ours = false;
    (void)audio_engine_stop();
    audio_engine_cfg_t sb_cfg;
    voice_engine_cfg_init(&sb_cfg, true);
    if (audio_engine_start(&sb_cfg) != AUDIO_ENGINE_SUCCESS) {
        LOGW("standby rebuild: engine start failed\r\n");
        return;
    }
    s_voice.engine_ours = true;
    (void)audio_engine_asr_switch_model(AUDIO_ENGINE_KWS_MODEL_WAKEUP);
    if (audio_engine_asr_start() != AUDIO_ENGINE_SUCCESS) {
        LOGW("KWS ASR restart failed; standby is off\r\n");
    } else {
        LOGI("KWS voice-wakeup standby restored\r\n");
    }
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
        /* audio_engine_init() 内部会用演示环回 cfg 直接把引擎拉起来
         * (read_cb=voice_read_callback: mic 编码帧写回喇叭解码，G722+PA开)，
         * 随后的 audio_engine_start() 因 is_started 变 no-op，待机引擎就
         * 一直是环回 demo（2026-11-10 实机：mic 声音 ~850ms 后从喇叭回放，
         * WIFI_TX/WIFI_RX 计数恒等）。必须 stop 掉再以待机 cfg 重启，
         * 与 voice_engine_acquire() 的 init->stop->start 顺序一致。 */
        (void)audio_engine_stop();
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

/* 唤醒应答音：与 beken_robot 一致（Beken 资源包的英文唤醒音，16kHz
 * 单声道 MP3）：app_event 的自动播放已对本项目关闭（触发时发声通路还挂
 * 在待机引擎上，pa_ctrl_en=false 播了也无声，且会被 PTT 引擎切换掐断），
 * muse_voice 在 voice 引擎就绪后自播，见 voice_kws_wakeup_handler()。 */
#define VOICE_WAKEUP_TONE_PATH  "/sd0/asr_wakeup_16k_mono_16bit_en.mp3"

#if CONFIG_AE_SUPPORT_PROMPT_TONE
extern audio_engine_prompt_tone_handle_t g_audio_engine_prompt_tone;
#endif

/* 唤醒应答音（"在的"）：在 voice 引擎起来之后播，混进通话链路的
 * voice-mix 口。voice 引擎 pa_enable=true，PA 真正打开才出声。AEC 对它
 * 的消除并不完全（实机：泄漏触发 mic VAD），所以起播同时打开 VAD 抑制
 * 窗（见 s_ack_tone_suppress_until），播完由 FINISH 收紧。 */
/* Returns 0: playback started; 1: tone disabled or file missing (skip it,
 * do not block the wakeup); -1: playback failed to start. */
static int voice_wakeup_tone_play(void)
{
#if CONFIG_AE_SUPPORT_PROMPT_TONE
    struct stat st;

    if (g_audio_engine_prompt_tone == NULL ||
        stat(VOICE_WAKEUP_TONE_PATH, &st) != 0) {
        return 1;
    }
    prompt_tone_uri_info_t info;
    memset(&info, 0, sizeof(info));
    info.uri = (char *)VOICE_WAKEUP_TONE_PATH;
    if (audio_engine_prompt_tone_start(g_audio_engine_prompt_tone, &info) !=
        BK_OK) {
        LOGW("wakeup ack tone start failed\r\n");
        return -1;
    }
    s_ack_tone_suppress_until = rtos_get_time() +
                                CONFIG_MUSE_VOICE_ACK_TONE_MAX_MS;
    s_ack_tone_playing_until = s_ack_tone_suppress_until;
    return 0;
#else
    return 1;
#endif
}

/* 应答音播完：抑制窗从 MAX 兜底收紧为"播完 + TAIL 尾音"，让用户尽早可
 * 以正常说话收尾。FINISH 不到时 MAX 兜底自然到期，无需额外处理。 */
static void voice_prompt_tone_finish_handler(app_evt_msg_t *msg,
                                             void *user_data)
{
    (void)msg;
    (void)user_data;
#if CONFIG_MUSE_TTS_VOLC
    s_tts_playing = false;   /* any prompt-tone FINISH ends TTS playback */
#endif
    if (s_ack_tone_suppress_until != 0U) {
        s_ack_tone_suppress_until = rtos_get_time() +
                                    CONFIG_MUSE_VOICE_ACK_TONE_TAIL_MS;
        s_ack_tone_playing_until = rtos_get_time() +
                                   CONFIG_MUSE_VOICE_ACK_TONE_PLAY_TAIL_MS;
    }
}

static int voice_ptt_start_common(bool ack_tone);

/* KWS hit -> start a voice turn, exactly as if the mic button were tapped.
 * Runs on the app-event task; ptt_start takes its own locks and its engine
 * acquire completes before it returns. If a turn is already in flight
 * (RECORDING/CLOSING) ptt_start refuses it, which is the right behaviour
 * for a double-hit. On the wake path the ack tone plays before the ASR
 * connection is created: if it fails to start, the wake is cancelled (a
 * user who hears no prompt must not start talking). */
static void voice_kws_wakeup_handler(app_evt_msg_t *msg, void *user_data)
{
    (void)msg;
    (void)user_data;
    LOGI("KWS wakeup word hit -> PTT\r\n");
    (void)voice_ptt_start_common(true);
}
#endif /* CONFIG_BEKEN_KWS */

/* VAD 语音段起点（SPEECH_START 边沿）-> AI_LISTENING：只为"起点落在宽限窗
 * 和应答音窗之外"的段置 vad_evt_speech 门闩，voice_ai_thinking_handler 靠它
 * 区分"用户说完"和"窗内起头的环境音"。 */
static void voice_ai_listening_handler(app_evt_msg_t *msg, void *user_data)
{
    (void)msg;
    (void)user_data;
    rtos_lock_mutex(&s_voice.lock);
    if (s_voice.state == VOICE_STATE_RECORDING &&
        deadline_passed(s_voice.vad_grace_until) &&
        deadline_passed(s_ack_tone_suppress_until)) {
        s_voice.vad_evt_speech = true;
    }
    rtos_unlock_mutex(&s_voice.lock);
}

/* 引擎判定"用户说完了一段有效的话"（VAD STOP 边沿 + 时长达标）-> 结束当前
 * 录音并发送。on_tick 里采样式 VAD 跟踪在发送高峰期会被饿死（泵一组 32 块
 * 约 2.5s 不跑 tick，整段语音落在两个采样点之间，speech_ms 恒为 0），改为
 * 事件驱动。空闲时的环境音 THINKING 不会误触发：不在录音就直接忽略。 */
static void voice_ai_thinking_handler(app_evt_msg_t *msg, void *user_data)
{
    uint32_t age = 0;
    bool recording;
    bool evt_speech;

    (void)msg;
    (void)user_data;
    rtos_lock_mutex(&s_voice.lock);
    recording = (s_voice.state == VOICE_STATE_RECORDING);
    evt_speech = s_voice.vad_evt_speech;
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
    /* 窗内起头的段（环境音在宽限/应答音窗内就在响）产生的 THINKING 不配
     * 结束 note：note 保持打开，等用户真正开口（2026-11-11 实机：2.6s
     * 环境音起头于宽限窗内，THINKING 在用户开口前发出空 note）。门闩由
     * voice_ai_listening_handler 置位；段起点在窗内但一直延续到窗外的，
     * 由 tick 路径的 speech>=MIN_SPEECH + 静默 1.2s 兜底收尾。 */
    if (!evt_speech) {
        LOGI("AI thinking without post-window speech (env noise) -> "
             "ignored\r\n");
        return;
    }
    LOGI("AI thinking (speech end) -> auto stop\r\n");
    (void)muse_voice_ptt_stop();
}

/* Park a running KWS/ASR standby so the mic is free for the voice turn.
 * This stops the whole engine on purpose: stopping only the ASR element and
 * reusing the engine does not work (measured 2026-11-19). ASR is the
 * downstream consumer of the mic element; once stopped, nothing reads the
 * mic port and PCM stalls within 1-2 s. The turn then receives only a few
 * hundred ms of silence, and asr_start cannot revive it afterwards: the
 * symptom is "no audio after wakeup, never wakes up again". */
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
    /* End of turn: rebuild the standby engine from scratch (stop + start +
     * asr_start). Switching model and calling asr_start on the resident
     * engine is not enough: after ASR is stopped the mic element may stay in
     * a "no downstream" state with PCM stalled, and restarting ASR alone
     * cannot recover it. Rebuilding guarantees fresh mic/AEC elements.
     * Runs on ptt_stop / TTS end, where this cost is not user-visible. */
    if (s_voice.had_kws) {
#if CONFIG_BEKEN_KWS
        voice_rebuild_standby_engine();
#else
        /* KWS not built (beken_muse trims TFLITE_MICRO); nothing to restore. */
        LOGW("had_kws set but KWS not built; standby stays off\r\n");
#endif
        s_voice.had_kws = false;
    }
}

/* ---- mic read callback (audio-engine read task) ----------------------------*/

/* Lock held. Cheap mean-|s16| of one frame; only feeds the fallback
 * activity signal, never the audio path itself. */
static void voice_note_energy(const unsigned char *data, unsigned int len)
{
    const uint8_t *p = data;
    size_t cnt = len / 2U;
    uint32_t sum = 0;

    if (cnt == 0U) {
        return;
    }
    for (size_t i = 0; i < cnt; i++) {
        int16_t v = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        sum += (uint32_t)((v < 0) ? -(int32_t)v : (int32_t)v);
        p += 2;
    }
    if (sum / cnt >= VOICE_ENERGY_FLOOR) {
        s_energy_active_until = rtos_get_time() + VOICE_ENERGY_HANGOVER_MS;
    }
}

static int voice_mic_read_cb(unsigned char *data, unsigned int len, void *args)
{
    const uint8_t *p = data;
    size_t n = len;
    (void)args;

    if (data == NULL || len == 0U) {
        return 0;
    }

    /* The engine is rebuilt per turn (see voice_engine_release): while not
     * recording, consume and drop the mic frames. The lock also serialises
     * this callback against stage_emit (engine stop/start can overlap the
     * callback). mic_rx_frames counts every delivered frame (silence
     * included). */
    rtos_lock_mutex(&s_voice.lock);
    s_voice.mic_rx_frames++;
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
#if CONFIG_MUSE_ASR_VOLC
    if (s_voice.asr_turn) {
        /* The volc SAUC session consumes the PCM; nothing is staged or
         * base64'd locally. Feed a copy BEFORE unlocking: the ring has
         * its own lock, but the turn flag is voice-state. */
        rtos_unlock_mutex(&s_voice.lock);
        muse_asr_volc_feed((const int16_t *)data, len / 2U);
        return (int)len;
    }
#endif
    voice_note_energy(data, len);
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
#if CONFIG_MUSE_TTS_VOLC
    /* Speak the reply once per turn. The synth+playback task re-acquires
     * the voice engine (PA on) and releases it afterwards; skipped while
     * a previous playback is still running. s_reply empty (error/nudge
     * shows) never triggers synthesis. */
    if (!s_voice.tts_done && s_reply[0] != '\0' &&
        s_volc_tts_key[0] != '\0') {
        s_voice.tts_done = true;
        /* 正文挂起在 voice_show_turn() 组装 body 之前就置位了（否则首次
         * 渲染会整段先出）；这里只负责合成。 */
        voice_tts_kick(s_reply);
    }
#endif
}

static void voice_hide_locked(void)
{
    s_voice.pending_show = false;
    s_voice.pending_hide = true;
}

/* 展示本回合的状态/错误提示（非助手回复）。与 voice_show_locked 同语义，
 * 额外记下提示文本，供语音页在气泡里显示（否则页面只会回放上一条回复）。
 * Lock held. */
static void voice_notice_locked(const char *body)
{
    snprintf(s_turn_notice, sizeof(s_turn_notice), "%s", body);
    voice_show_locked(voice_turn_title(), body);
}

/* Execute queued display actions. Call WITHOUT s_voice.lock held, from
 * any task: the snapshot is taken under a short critical section and the
 * display calls run after it. Concurrent flushers may overwrite
 * s_flush_* between the snapshot and the display call; the visible
 * glitch self-corrects on the next streaming update and is bounded to
 * identical overlay content, never to mixed locking. */
static void voice_display_flush(void)
{
    bool show, hide, restore_kws, dismiss_image;

    if (!s_voice_inited) {
        return;
    }
    rtos_lock_mutex(&s_voice.lock);
    show = s_voice.pending_show;
    hide = s_voice.pending_hide;
    restore_kws = s_voice.kws_restore_pending;
    dismiss_image = s_voice.pending_dismiss_image;
    s_voice.pending_show = false;
    s_voice.pending_hide = false;
    s_voice.kws_restore_pending = false;
    s_voice.pending_dismiss_image = false;
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
    if (dismiss_image) {
        /* 新回合开始：揭掉 draw_url 图片罩（show_animation 内部已带
         * LVGL 线程判定，show/hide 路径同一把锁）。 */
        (void)muse_display_show_animation();
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
    s_turn_notice[0] = '\0';   /* 真实回合内容上屏，撤掉错误提示 */
#if CONFIG_MUSE_TTS_VOLC
    /* 本回合第一次拿到回复：先挂起正文，等第一句真正出声再一起上屏。
     * 必须在下面算 hold_reply 之前置位——之前这句放在 voice_show_locked()
     * 里，而 body 是按"无挂起"先组好的，首次渲染照样整段文字先出，延迟窗
     * 形同虚设（正是"文字先到、声音后到"的来源）。 */
    if (!s_voice.tts_done && s_reply[0] != '\0' &&
        s_volc_tts_key[0] != '\0') {
        s_reply_hold_until = rtos_get_time() + VOICE_REPLY_SHOW_DELAY_MS;
    }
#endif
    /* 延迟窗内正文先显示 "..."（问题和状态照常，不装死）。 */
    bool hold_reply = s_reply[0] != '\0' && s_reply_hold_until != 0U &&
                      !deadline_passed(s_reply_hold_until);
    const char *reply_text = hold_reply ? "..." :
                             (s_reply[0] != '\0' ? s_reply : "...");
    if (s_heard[0] != '\0') {
        snprintf(body, sizeof(body), "You said:\n%s\n\n%s", s_heard,
                 reply_text);
    } else {
        snprintf(body, sizeof(body), "%s", reply_text);
    }
    /* Archive the turn once (first time the reply text exists); heard text
     * may arrive later and only affects the live overlay, not history.
     * 延迟窗内先不写：语音页的气泡是定时从 history 刷新的，提前写进去会
     * 绕过上面的 "..." 挂起，造成页面文字先于声音。放行时再写。 */
    if (!hold_reply && !s_voice.history_pushed && s_reply[0] != '\0') {
        voice_history_push(voice_turn_title(), s_reply);
        s_voice.history_pushed = true;
    }
    voice_show_locked(voice_turn_title(), body);
}

/* 正文延迟窗提前放行（任意线程）：第一句已出声（文字同步上屏）或 TTS
 * 失败（文字不能跟着等死）。无挂起/已过期时无副作用。 */
static void voice_reply_hold_release(void)
{
    bool reshow = false;
    rtos_lock_mutex(&s_voice.lock);
    if (s_reply_hold_until != 0U) {
        s_reply_hold_until = 0U;
        if (s_voice.state == VOICE_STATE_SHOWING) {
            voice_show_turn();
            reshow = true;
        }
    }
    rtos_unlock_mutex(&s_voice.lock);
    if (reshow) {
        voice_display_flush();
    }
}

/* ---- user entry points (any task) ------------------------------------------*/

static int voice_ptt_start_common(bool ack_tone)
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
        if (s_voice.state == VOICE_STATE_CLOSING) {
            /* Barge-in while the tail is still uploading: the old note
             * stream is half-open, abort it (Reset on the old note stream;
             * voice_abort_locked stashes the id so the fresh stream
             * allocated below is not the reset target) and start fresh. */
            LOGW("PTT barge-in: aborting the previous turn\r\n");
            voice_abort_locked(true);
            s_voice.engine_had_ours = s_voice.engine_ours;
        } else if (s_voice.state == VOICE_STATE_WAIT_REPLY) {
            /* 补充说明而不是打断：旧 note 已完整发出，Muse 还没回。不发
             * Reset（留着旧 note，Muse 没思考完会把两条合并只回一个，与
             * App 内连续追问的行为一致），直接开新回合（2026-11-11 用户
             * 拍板的形式 2：END 之后、回复之前允许补充）。 */
            LOGI("PTT supplement: previous note stays, starting a new one\r\n");
            voice_abort_locked(false);
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
            /* 唤醒盲区反装死：开机/重连窗口里命中却无声无息，用户只能
             * 反复喊（field 2026-11-12：一次开机连拒 3 次）。上屏短提示，
             * 3s 自动消（覆盖 voice_show_locked 默认的 30s 展示窗）。 */
            voice_notice_locked("网络连接中，请稍后再试");
            s_voice.show_deadline = rtos_get_time() + 3000U;
            rc = -2;
            break;
        }
        s_voice.dropped_frames = 0;
        s_voice.pool_full = false;
        s_voice.photo = false;
        voice_turn_reset_locked();
        /* A new turn also retires any draw_url image on screen: the VM
         * never sends show_animation on its own, and turn replies render
         * underneath the image overlay (field 2026-11-10: screen stuck on
         * the image while voice turns kept going). Queued here, executed
         * lock-free by voice_display_flush(). */
        s_voice.pending_dismiss_image = true;
        /* The note body starts with the JSON head (its own first chunk),
         * then base64 of the WAV header + PCM. The head names this board
         * (device_id) so the server attributes the turn to the device
         * instead of the account main chat, and its message field asks
         * the VM to push the reply via display.draw_text (both
         * Muse-documented requirements, 2026-10). session_id is only
         * sent when MUSE_NOTE_SEND_SESSION_ID is enabled: a fresh id
         * black-holed the whole turn in the field (side-chat events do
         * not reach our unfiltered subscription). device_id 由
         * VOICE_NOTE_DEVICE_ID 宏控制（见宏旁注释：2026-11-09 空值实验
         * 已证伪并回退为板身份）。 */
        head_len =
            muse_voice_proto_note_head(head_buf, sizeof(head_buf),
                                       CONFIG_MUSE_NOTE_REPLY_HINT,
                                       VOICE_NOTE_DEVICE_ID,
                                       VOICE_NOTE_SESSION_ID);
         if (!head_len) {
             rc = -3;
             break;
         }
         LOGD("voice note head: %s\r\n", head_buf);
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

    if (ack_tone) {
        /* The tone must be audible before the ASR link is built: first for
         * product semantics (no "I'm here" means the user should not be
         * listened to yet), and second because the ASR ws thread/TLS is not
         * created yet, freeing 4 KB of internal SRAM for the MP3 decoder
         * thread (field: with two TLS handshakes in flight
         * audio_thread_create failed, the tone was silent while ASR still
         * came up normally). */
        int tone = voice_wakeup_tone_play();
        if (tone < 0) {
            /* The first failure is usually transient resource pressure; yield
             * 200 ms and retry once. The ASR link is still not built, so the
             * memory has usually been released by then. */
            rtos_delay_milliseconds(200);
            tone = voice_wakeup_tone_play();
        }
        if (tone < 0) {
            /* Still failing: cancel this wakeup so the user does not end up
             * talking to the device without hearing any acknowledgement. */
            LOGE("wakeup ack tone unavailable; cancel this wakeup\r\n");
            voice_engine_release();
            rtos_lock_mutex(&s_voice.lock);
            voice_queue_drain();
            s_voice.engine_busy = false;
            s_voice.engine_had_ours = false;
            voice_notice_locked("请再说一次");
            s_voice.show_deadline = rtos_get_time() + 3000U;
            rtos_unlock_mutex(&s_voice.lock);
            voice_display_flush();
            return -8;
        }
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
#if CONFIG_MUSE_ASR_VOLC
    /* ASR turn: PCM streams to volc and the note carries the final
     * transcript instead of WAV base64. No head/WAV staging now -- the
     * text note JSON is queued when the transcript arrives. */
    s_voice.asr_turn = s_volc_asr_key[0] != '\0';
#endif
    if (!s_voice.asr_turn) {
        muse_voice_proto_wav_header(s_stage, MUSE_VOICE_SAMPLE_RATE_HZ);
        s_stage_len = MUSE_VOICE_WAV_HEADER_BYTES;
    }
    /* ASR turn: nothing is queued until the transcript arrives, so the
     * OPEN_NOTE flag stays clear here -- voice_asr_on_text sets it together
     * with the text chunk. Setting it now would emit a bare OPEN_NOTE with
     * no body behind it (field 2026-11-12: OPEN_NOTE 44 ms after PTT start,
     * then silence when the ASR handshake was rejected). */
    s_voice.need_open_note = !s_voice.asr_turn;
    /* Queue the HEAD only now, atomically with the OPEN_NOTE flag. The
     * pump pops FIFO and, with need_open_note already true, emits
     * OPEN_NOTE before any body byte. Queueing it in the first locked
     * section let the pump race ahead of the lock-free engine bring-up
     * and send the HEAD as a bare DATA on a stream the server never
     * opened — the server rejected it and the take died (field: the
     * first PTT after boot always failed this way, when the engine
     * bring-up is slowest; later takes reused the hot engine and
     * committed before the pump woke up). */
    if (!s_voice.asr_turn && !chunk_push(head_buf, head_len, false)) {
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
    s_voice.vad_seen_speech = false;
    s_voice.vad_evt_speech = false;
    s_voice.vad_grace_until = rtos_get_time() +
                              CONFIG_MUSE_VOICE_VAD_GRACE_MS;
    s_energy_active_until = 0;
    s_voice.mic_rx_frames = 0;
    s_voice.state = VOICE_STATE_RECORDING;
    immediate_stop = s_voice.stop_while_busy;
    s_voice.stop_while_busy = false;
    rtos_unlock_mutex(&s_voice.lock);
    voice_display_flush();

    LOGI("PTT start\r\n");
    LOGI("PTT psram: free=%lu min=%lu\r\n",
         (unsigned long)rtos_get_psram_free_heap_size(),
         (unsigned long)rtos_get_psram_minimum_free_heap_size());
#if CONFIG_MUSE_ASR_VOLC
    if (s_voice.asr_turn && !immediate_stop) {
        /* Open the volc session after the commit: the mic callback feeds
         * it from the very next frame. On failure roll the turn back --
         * no note bytes were queued yet. */
        if (muse_asr_volc_start() != 0) {
            LOGE("ASR session start failed\r\n");
            rtos_lock_mutex(&s_voice.lock);
            s_voice.need_open_note = false;
            s_voice.note_stream = 0;
            s_voice.asr_turn = false;
            s_voice.state = VOICE_STATE_IDLE;
            rtos_unlock_mutex(&s_voice.lock);
            voice_engine_release();
            voice_display_flush();
            return -7;
        }
    }
#endif
    if (immediate_stop) {
        (void)muse_voice_ptt_stop();
    }
    return 0;
}

int muse_voice_ptt_start(void)
{
    /* 按钮/其它入口：不播应答音，预热时机保持原样。 */
    return voice_ptt_start_common(false);
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
#if CONFIG_MUSE_ASR_VOLC
        bool asr_turn;
        rtos_lock_mutex(&s_voice.lock);
        asr_turn = s_voice.asr_turn;
        rtos_unlock_mutex(&s_voice.lock);
        if (asr_turn) {
            /* Tail packet + final transcript wait runs on its own task;
             * voice_asr_on_text queues the text note when it arrives. */
            voice_asr_finish_kick();
        } else
#endif
        {
            voice_stop_recording_tail();
#if CONFIG_MUSE_TTS_VOLC
            /* Non-ASR (WAV) turn: the mic uplink stops here, so prewarm the
             * TTS connection now instead of at wakeup (see
             * voice_asr_on_text for the ASR turn). */
            muse_tts_volc_prewarm();
#endif
        }
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

bool muse_voice_ui_notice(char *out, size_t cap)
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
    if (s_voice.state == VOICE_STATE_SHOWING && s_turn_notice[0] != '\0') {
        snprintf(out, cap, "%s", s_turn_notice);
        ok = true;
    }
    rtos_unlock_mutex(&s_voice.lock);
    return ok;
}

bool muse_voice_ui_holding(void)
{
    bool holding = false;
    if (!s_voice_inited) {
        return false;
    }
    rtos_lock_mutex(&s_voice.lock);
    holding = (s_voice.state == VOICE_STATE_SHOWING) &&
              (s_reply_hold_until != 0U);
    rtos_unlock_mutex(&s_voice.lock);
    return holding;
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
        /* HEAD chunk: JSON head and the image item. device_id follows
         * VOICE_NOTE_DEVICE_ID like the voice note (currently empty:
         * account-conversation shape, same experiment); session_id is
         * gated by the Kconfig. */
        char head_buf[400];
        size_t head_len =
            muse_voice_proto_image_note_head(head_buf, sizeof(head_buf),
                                             query != NULL ? query
                                                           : CONFIG_MUSE_NOTE_REPLY_HINT,
                                             VOICE_NOTE_DEVICE_ID,
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

/* A chunk popped while a control transition (OPEN_NOTE/OPEN_SUB/RESET) rose
 * during the blocking pop: it must go out AFTER that control unit. Held here
 * and replayed on the next tx_next() call (single session-worker thread). */
static voice_chunk_t *s_hold_chunk;

static const char *voice_tx_kind_name(muse_voice_tx_kind_t kind)
{
    switch (kind) {
    case MUSE_VOICE_TX_RESET:     return "RESET";
    case MUSE_VOICE_TX_OPEN_SUB:  return "OPEN_SUB";
    case MUSE_VOICE_TX_OPEN_NOTE: return "OPEN_NOTE";
    case MUSE_VOICE_TX_END:       return "END";
    case MUSE_VOICE_TX_DATA:      return "DATA";
    default:                      return "NONE";
    }
}

/* Control units pending right now (state lock held). */
static muse_voice_tx_kind_t voice_tx_ctrl_locked(void)
{
    /* Open the subscription first: upstream subscribes right after
     * connect, before any turn's note, and the reply for a note only
     * arrives on the long-lived subscription. */
    if (s_voice.need_reset) {
        return MUSE_VOICE_TX_RESET;
    }
    if (s_voice.need_open_sub) {
        return MUSE_VOICE_TX_OPEN_SUB;
    }
    if (s_voice.need_open_note) {
        return MUSE_VOICE_TX_OPEN_NOTE;
    }
    return MUSE_VOICE_TX_NONE;
}

muse_voice_tx_kind_t muse_voice_tx_next(uint8_t *frame, size_t cap,
                                        size_t *frame_len,
                                        uint32_t frame_wait_ms)
{
    muse_voice_tx_kind_t kind = MUSE_VOICE_TX_NONE;
    voice_chunk_t *c = NULL;

    rtos_lock_mutex(&s_voice.lock);
    kind = voice_tx_ctrl_locked();
    rtos_unlock_mutex(&s_voice.lock);
    if (kind != MUSE_VOICE_TX_NONE) {
        LOGI("tx ctrl: %s\r\n", voice_tx_kind_name(kind));
        return kind;
    }

    if (s_hold_chunk != NULL) {
        if (frame == NULL || cap == 0U) {
            return MUSE_VOICE_TX_NONE;   /* 只查控制类调用：held 留着下次 */
        }
        /* Drop the held chunk if the turn died while a control was in
         * flight (abort drained the queue but cannot see this slot). */
        rtos_lock_mutex(&s_voice.lock);
        bool active = s_voice.state == VOICE_STATE_RECORDING ||
                      s_voice.state == VOICE_STATE_CLOSING ||
                      s_voice.state == VOICE_STATE_WAIT_REPLY;
        rtos_unlock_mutex(&s_voice.lock);
        if (!active) {
            chunk_free(s_hold_chunk);
            s_hold_chunk = NULL;
            return MUSE_VOICE_TX_NONE;
        }
        c = s_hold_chunk;
        s_hold_chunk = NULL;
    } else {
        /* Chunks pop in FIFO order, so HEAD -> PCM chunks -> final TAIL
         * chunk. Block only while recording (the mic may still fill the
         * stage); when closing the queue can only drain. */
        if (frame == NULL || cap == 0U ||
            rtos_pop_from_queue(&s_voice.data_q, &c,
                                muse_voice_recording() ? frame_wait_ms : 0U) != BK_OK ||
            c == NULL) {
            return MUSE_VOICE_TX_NONE;
        }
        /* The pop can block for a whole frame window and the ASR final lands
         * in exactly that window: it sets need_open_note and queues the END
         * text. Returning END here sent the body onto a stream the server
         * never opened — it reset the note stream and killed the turn
         * (field 2026-11-13). Emit the control unit first, replay after. */
        rtos_lock_mutex(&s_voice.lock);
        kind = voice_tx_ctrl_locked();
        rtos_unlock_mutex(&s_voice.lock);
        if (kind != MUSE_VOICE_TX_NONE) {
            s_hold_chunk = c;
            LOGI("tx ctrl: %s (chunk held)\r\n", voice_tx_kind_name(kind));
            return kind;
        }
    }

    {
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
                LOGD("tx DATA #%u len=%u: %.10s\r\n",
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
            /* 正文已经整包送达（END 已 ack），引擎空转中，麦克风只剩
             * 采集。这里立刻把 KWS 待机恢复起来：服务端不回包时（实机
             * 2026-11-13：ACK 200 但 agent 回复从不下发）用户要干等满
             * 3 分钟才恢复，期间完全唤不醒。恢复后引擎状态由
             * audio_engine_asr_is_started() 判定，下一回合照样能 park。 */
            s_voice.kws_restore_pending = true;
            /* An image reply that beat the END ack (draw_url landed while
             * the note was still draining) keeps only a caption grace
             * instead of the full 3-minute wait. */
            s_voice.reply_deadline =
                rtos_get_time() +
                (s_voice.image_replied
                     ? VOICE_IMAGE_REPLY_GRACE_MS
                     : (s_voice.photo ? CONFIG_MUSE_PHOTO_REPLY_TIMEOUT_MS
                                      : CONFIG_MUSE_VOICE_REPLY_TIMEOUT_MS));
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
    /* 锁外执行：上面置了 kws_restore_pending，这里立即恢复唤醒词待机，
     * 不必等服务端回复（回包丢失时也能马上再唤醒）。 */
    voice_display_flush();
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
            /* 被踢那一刻的回合快速失败并明确告知：否则气泡会回放上一条
             * 回复，看着像"答错了"（2026-11-13 field）。 */
            voice_notice_locked("请求被中断，请再说一次");
            s_voice.show_deadline = rtos_get_time() + 5000U;
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
    voice_display_flush();        /* 上屏"请求被中断"的提示 */
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
    if (s_ack_status != 200) {
        LOGW("chat/stream ACK status=%d body=%.480s\r\n", s_ack_status,
             s_ack_ovf ? "(overflow)" : s_ack);
        voice_abort_locked(false);
        s_voice.engine_had_ours = s_voice.engine_ours;
        voice_notice_locked("消息未送达，请重试");
    } else if (!s_ack_ovf &&
               muse_voice_proto_parse_ack(s_ack, s_note_id,
                                          sizeof(s_note_id),
                                          s_parent_id,
                                          sizeof(s_parent_id)) == 0) {
        LOGD("chat/stream ACK ok: %.480s\r\n", s_ack);
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

/* The VM can also answer a voice turn with an IMAGE (draw_url) instead of
 * text, and the reply wait only accepts text: an image-only reply held
 * the engine for the full CONFIG_MUSE_VOICE_REPLY_TIMEOUT_MS (3 min) with
 * KWS dead and the screen stuck on the image (field 2026-11-10). Close
 * the wait promptly: restore KWS now (the engine is idle once the note is
 * out; the pump drains from the chunk pool, not the engine) and shorten
 * the reply wait to a caption grace. */
void muse_voice_on_reply_image(void)
{
    if (!s_voice_inited) {
        return;
    }
    rtos_lock_mutex(&s_voice.lock);
    if (s_voice.state == VOICE_STATE_CLOSING ||
        s_voice.state == VOICE_STATE_WAIT_REPLY) {
        s_voice.image_replied = true;
        s_voice.kws_restore_pending = true;
        if (s_voice.state == VOICE_STATE_WAIT_REPLY) {
            s_voice.reply_deadline = rtos_get_time() +
                                     VOICE_IMAGE_REPLY_GRACE_MS;
        }
    }
    rtos_unlock_mutex(&s_voice.lock);
    voice_display_flush();
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
                LOGD("sub row: ev=%s ready=%d msg=%.24s reply_to=%.24s text=%.40s\r\n",
                     s_row.event, s_row.ready ? 1 : 0, s_row.msg,
                     s_row.reply_to, s_row.text);
                rtos_lock_mutex(&s_voice.lock);
                voice_on_row(&s_row);
                rtos_unlock_mutex(&s_voice.lock);
                voice_display_flush();
            } else if (s_sub_len) {
                /* 诊断：行收到了但解析不出事件（或超过 VOICE_LINE_CAP 被
                 * 丢弃）。以前这里静默丢弃，现场就无法区分"平台没回"和
                 * "回了但格式不认识/太长"（2026-11-17 排查"卡在思考"）。 */
                char head[81];
                size_t m = s_sub_len < sizeof(head) - 1U
                               ? s_sub_len : sizeof(head) - 1U;
                memcpy(head, s_sub_line, m);
                head[m] = '\0';
                LOGD("sub row dropped (ovf=%d len=%u): %s\r\n",
                     (int)s_sub_ovf, (unsigned)s_sub_len, head);
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
        voice_notice_locked("请求被中断，请再说一次");
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
    bool push_flush = false;
    static char s_push_flush[VOICE_REPLY_MAX];

    rtos_lock_mutex(&s_voice.lock);
    if (s_voice.state != VOICE_STATE_CLOSING) {
        /* 离开 CLOSING 就清时刻，避免下一回合继承旧值被立刻判超时。 */
        s_voice.closing_since = 0U;
    }
    switch (s_voice.state) {
    case VOICE_STATE_RECORDING: {
        /* 诊断：确认 tick 在跑 + VAD 累计值（排查唤醒后不能自动停止） */
        static uint32_t s_vad_dbg_ms;
        uint32_t dbg_now = rtos_get_time();
        if ((uint32_t)(dbg_now - s_vad_dbg_ms) >= 500U) {
            s_vad_dbg_ms = dbg_now;
            LOGD("vad tick: mic=%u speech=%lu silence_since=%lu busy=%d\r\n",
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
            if (!deadline_passed(s_voice.vad_grace_until)) {
                /* 唤醒宽限窗：唤醒词尾音与用户停顿都不计入 VAD（否则
                 * 停顿 1.2 s 就自动停，用户真正的话还没说）。 */
                s_voice.vad_speech_ms = 0;
                s_voice.vad_silence_since = 0;
            } else if (s_voice.vad_speech_ms == 0U &&
                       !s_voice.vad_seen_speech &&
                       (int32_t)(now - s_voice.vad_grace_until) >=
                           (int32_t)CONFIG_MUSE_VOICE_NO_SPEECH_MS
#if CONFIG_MUSE_ASR_VOLC
                       /* 云端豁免：ASR 回合里火山最近 5s 还在回包
                        *（partial 流 = 用户在说，只是本地 VAD 瞎了——
                        * 2026-11-13 实机："呃"后停顿 VAD 提前判死，看门
                        * 狗在 final 到达前 1s 误杀整回合，note 没发出）。 */
                       && (!s_voice.asr_turn ||
                           (int32_t)(now - muse_asr_volc_last_rx_ms()) >=
                               (int32_t)CONFIG_MUSE_VOICE_NO_SPEECH_MS)
#endif
                       ) {
                /* 宽限窗后 5 s 从未听到语音：不发空 note，只提醒。引擎释放
                 * 走锁外的 do_release（与正常收尾同路，自动恢复 KWS）。 */
                LOGW("no speech after wakeup; ending without note\r\n");
                voice_abort_locked(false);
                s_voice.engine_had_ours = s_voice.engine_ours;
                voice_notice_locked("没听到声音，请再试一次");
                do_release = true;
            } else if (!deadline_passed(s_ack_tone_suppress_until)) {
                /* 应答音抑制窗：泄漏进 mic 的 VAD 不计入说话/静音累计，
                 * 防止 tick 路径把录音提前停掉（事件路径同样被挡）。 */
                s_voice.vad_speech_ms = 0;
                s_voice.vad_silence_since = 0;
            } else if (audio_engine_get_mic_active() ||
                       !deadline_passed(s_energy_active_until)) {
                s_voice.vad_seen_speech = true;
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
    case VOICE_STATE_CLOSING: {
        /* 兜底：CLOSING 等的是外部回调（WAV 的 END ack / ASR 的火山
         * final）。回调丢失时这里没有任何出口，回合永久停住，引擎不
         * 释放、KWS 待机不恢复（2026-11-17 实机：唤醒后没说话，空 final
         * 没有任何回调，之后所有唤醒失效，只能重启）。超时按失败收尾，
         * 由锁外的 engine_had_ours 分支释放引擎、恢复唤醒待机。
         * 火山会话无需在此 abort：进入 CLOSING 时都 kick 过 finish 任务，
         * 它自己会在 ~12 s 内收尾（幂等）。 */
        uint32_t now = rtos_get_time();
        if (s_voice.closing_since == 0U) {
            s_voice.closing_since = (now == 0U) ? 1U : now;
        } else if ((uint32_t)(now - s_voice.closing_since) >=
                   CONFIG_MUSE_VOICE_CLOSING_TIMEOUT_MS) {
            LOGW("closing timeout (%lu ms); aborting the turn\r\n",
                 (unsigned long)(now - s_voice.closing_since));
            s_voice.closing_since = 0U;
            voice_abort_locked(false);
            s_voice.asr_turn = false;
            s_voice.engine_had_ours = s_voice.engine_ours;
            voice_notice_locked("请求超时，请再试一次");
        }
        break;
    }
    case VOICE_STATE_WAIT_REPLY:
        if (deadline_passed(s_voice.reply_deadline)) {
            LOGW("voice reply timeout\r\n");
            voice_abort_locked(true);
            s_voice.engine_had_ours = s_voice.engine_ours;
            voice_notice_locked("没有收到回复，请重试");
            /* 兜底：连接层正常但服务端把回复投给了旧会话/旧 stream 时，
             * 设备侧收不到任何关闭信号。note 已发出却没有回应，只能
             * 主动重建 chat 会话（代替用户重启设备）。 */
            muse_chat_request_reconnect("voice reply timeout");
        }
        break;
    case VOICE_STATE_SHOWING:
        /* 正文延迟窗到期：补显示回复（voice_show_turn 内 hold 已清）。 */
        if (s_reply_hold_until != 0U && deadline_passed(s_reply_hold_until)) {
            s_reply_hold_until = 0U;
            voice_show_turn();
        }
        if (deadline_passed(s_voice.show_deadline)) {
            voice_hide_locked();
            s_voice.state = VOICE_STATE_IDLE;
        }
        break;
    default:
        break;
    }
    /* 挂起的主动推送：对话结束、上一段 TTS 也播完了才上屏+播报。 */
    if (s_push_pending && !voice_push_busy_locked()) {
        snprintf(s_push_flush, sizeof(s_push_flush), "%s", s_push_text);
        s_push_pending = false;
        push_flush = !voice_push_begin_locked(s_push_flush);
    }
    /* Fallback for a held push whose TTS never starts (stuck task, synth
     * error swallowed): show it rather than leave it invisible. */
    if (s_push_hold && deadline_passed(s_push_hold_until)) {
        s_push_hold = false;
        s_push_hold_until = 0U;
        snprintf(s_push_flush, sizeof(s_push_flush), "%s", s_push_hold_text);
        push_flush = true;
    }
    rtos_unlock_mutex(&s_voice.lock);

    if (push_flush) {
        LOGI("push flushed: %.40s\r\n", s_push_flush);
        voice_push_show(s_push_flush);
    }

    if (s_voice.engine_had_ours) {
        voice_engine_release();   /* lock-free, see voice_abort_locked */
        s_voice.engine_had_ours = false;
    }
    if (do_release) {
        /* Recording ended by limit/timeout: mic off first, then the
         * staged tail, engine work lock-free. */
#if CONFIG_MUSE_ASR_VOLC
        bool asr_turn;
        rtos_lock_mutex(&s_voice.lock);
        asr_turn = s_voice.asr_turn;
        rtos_unlock_mutex(&s_voice.lock);
        /* ASR turn: never flush the (empty) stage as END -- the transcript
         * note is queued when volc returns it (same as ptt_stop). */
        if (asr_turn) {
            voice_asr_finish_kick();
        } else
#endif
        {
            voice_stop_recording_tail();
        }
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
    voice_volc_key_load();
#if CONFIG_MUSE_ASR_VOLC
    muse_asr_volc_init(voice_asr_on_text, voice_asr_on_error);
#endif
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
     * voice_ai_thinking_handler 的注释；AI_LISTENING 提供"段起点是否在
     * 窗外"的门闩（voice_ai_listening_handler）。 */
    app_event_register_handler(APP_EVT_AI_THINKING,
                               voice_ai_thinking_handler, NULL);
    app_event_register_handler(APP_EVT_AI_LISTENING,
                               voice_ai_listening_handler, NULL);
    return 0;
}
