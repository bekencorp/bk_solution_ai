/**
 * @file sound_localization.c
 * @brief Backend for the page_5 sound source localization demo. Owns
 *        the cached arrow angle (consumed by audio_engine DOA weak link)
 *        and provides the audio_engine ASR start/stop service that feeds
 *        the DOA estimator. LVGL-free: the live angle is pushed into the
 *        UI via a callback registered by page_doa_hooks.c.
 *
 * Backward-compat: keeps `page_5_set_arrow_angle()` /
 * `page_5_get_arrow_angle()` / `page_5_set_arrow_pivot()` /
 * `page_5_cli_init()` exported so audio_engine (weak link) and legacy
 * call sites continue to compile.
 */
#include "demo/sound_localization.h"
#include "page_5_api.h"
#include "page_doa_eyes.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef ROBOT_TEST

#include "audio_engine.h"
#include "bk_cli.h"
#include "cli.h"
#include "os/os.h"
#include "os/str.h"
#include <components/log.h>

#define TAG "doa_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

/* Defaults to 90 deg: blue KNOB under the mouth, pupils default to the
 * top/bottom region with the brown tint. */
static volatile int s_arrow_deg = 90;

/* UI bridge: page_doa_hooks.c calls sound_localization_register_ui_sink()
 * on page init and unregisters on destroy. */
static sound_loc_ui_sink_t s_ui_sink;

void sound_localization_register_ui_sink(sound_loc_ui_sink_t sink)
{
    s_ui_sink = sink;
}

void sound_localization_set_angle(int degrees)
{
    int d = degrees % 360;
    if (d < 0) {
        d += 360;
    }
    s_arrow_deg = d;
    sound_loc_ui_sink_t sink = s_ui_sink;
    if (sink != NULL) {
        sink(d);
    }
}

int sound_localization_get_angle(void)
{
    return s_arrow_deg;
}

/* Legacy API compat (audio_engine weak ref / existing business code). */
void page_5_set_arrow_angle(int degrees)
{
    sound_localization_set_angle(degrees);
}

int page_5_get_arrow_angle(void)
{
    return sound_localization_get_angle();
}

void page_5_set_arrow_pivot(int x, int y)
{
    (void)x;
    (void)y;
    LOGW("page_5_set_arrow_pivot ignored: page_5 now uses lv_arc, no pivot\r\n");
}

/* ----------------------------------------------------------------------
 * CLI: arrow ...
 * -------------------------------------------------------------------- */
static beken_timer_t s_sweep_timer;
static bool          s_sweep_timer_inited;
static int           s_sweep_step;

static void sweep_tick(void *arg)
{
    (void)arg;
    sound_localization_set_angle(s_arrow_deg + s_sweep_step);
}

static void sweep_stop(void)
{
    if (!s_sweep_timer_inited) {
        return;
    }
    if (rtos_is_timer_running(&s_sweep_timer)) {
        rtos_stop_timer(&s_sweep_timer);
    }
    rtos_deinit_timer(&s_sweep_timer);
    s_sweep_timer_inited = false;
}

static int sweep_start(int step_deg, int interval_ms)
{
    if (interval_ms < 10) {
        return -1;
    }
    sweep_stop();
    s_sweep_step = step_deg;
    if (rtos_init_timer(&s_sweep_timer, (uint32_t)interval_ms,
                        sweep_tick, NULL) != BK_OK) {
        return -2;
    }
    s_sweep_timer_inited = true;
    if (rtos_start_timer(&s_sweep_timer) != BK_OK) {
        sweep_stop();
        return -3;
    }
    return 0;
}

static void arrow_cli_usage(void)
{
    LOGI("Usage (page_5 uses lv_arc; angle = KNOB position on the ring):\r\n"
         "  arrow                       show this help\r\n"
         "  arrow get                   print current angle\r\n"
         "  arrow set <deg>             set angle 0~360 (or: arrow <deg>)\r\n"
         "  arrow sweep <step> <ms>     auto-rotate every <ms> by <step> deg\r\n"
         "  arrow stop                  stop sweep\r\n");
}

static void arrow_cli_handler(char *out, int out_len, int argc, char **argv)
{
    (void)out;
    (void)out_len;

    if (argc < 2) {
        arrow_cli_usage();
        return;
    }
    if (argc == 2 && argv[1][0] != '\0' &&
        (argv[1][0] == '-' || (argv[1][0] >= '0' && argv[1][0] <= '9'))) {
        int deg = atoi(argv[1]);
        sound_localization_set_angle(deg);
        LOGI("arrow set deg=%d (now=%d)\r\n", deg, sound_localization_get_angle());
        return;
    }
    if (os_strcmp(argv[1], "get") == 0) {
        LOGI("arrow angle=%d\r\n", sound_localization_get_angle());
        return;
    }
    if (os_strcmp(argv[1], "set") == 0 && argc >= 3) {
        int deg = atoi(argv[2]);
        sound_localization_set_angle(deg);
        LOGI("arrow set deg=%d (now=%d)\r\n", deg, sound_localization_get_angle());
        return;
    }
    if (os_strcmp(argv[1], "sweep") == 0 && argc >= 4) {
        int step = atoi(argv[2]);
        int ms = atoi(argv[3]);
        int ret = sweep_start(step, ms);
        if (ret == 0) {
            LOGI("arrow sweep started: step=%d deg, interval=%d ms\r\n", step, ms);
        } else {
            LOGW("arrow sweep start failed: ret=%d (interval must be >= 10)\r\n", ret);
        }
        return;
    }
    if (os_strcmp(argv[1], "stop") == 0) {
        sweep_stop();
        LOGI("arrow sweep stopped\r\n");
        return;
    }
    arrow_cli_usage();
}

static const struct cli_command s_arrow_cli_cmd[] = {
    {"arrow", "arrow [get|set <deg>|sweep <step> <ms>|stop]", arrow_cli_handler},
};

int page_5_cli_init(void)
{
    static bool s_inited;
    if (s_inited) {
        return 0;
    }
    int ret = cli_register_commands(s_arrow_cli_cmd,
                                    sizeof(s_arrow_cli_cmd) / sizeof(s_arrow_cli_cmd[0]));
    if (ret == 0) {
        s_inited = true;
        LOGI("arrow CLI registered\r\n");
    } else {
        LOGW("arrow CLI register failed: ret=%d\r\n", ret);
    }
    return ret;
}

/* ----------------------------------------------------------------------
 * ASR service wrapper (feeds the DOA estimator)
 * -------------------------------------------------------------------- */
int sound_localization_start_service(void)
{
#if (CONFIG_ASR_SERVICE)
    if (!audio_engine_is_running()) {
        LOGI("audio engine stopped, restart for page5 doa\r\n");
        if (AUDIO_ENGINE_SUCCESS != audio_engine_init()) {
            LOGI("page5 restart audio engine failed\r\n");
            return -1;
        }
    }
#if CONFIG_BEKEN_KWS
    if (AUDIO_ENGINE_SUCCESS != audio_engine_asr_switch_model(AUDIO_ENGINE_KWS_MODEL_WAKEUP)) {
        LOGI("page5 switch kws wakeup model failed\r\n");
        return -1;
    }
#endif
    return (AUDIO_ENGINE_SUCCESS == audio_engine_asr_start()) ? 0 : -1;
#else
    return 0;
#endif
}

int sound_localization_stop_service(void)
{
#if (CONFIG_ASR_SERVICE)
    if (!audio_engine_is_running()) {
        return 0;
    }
    return (AUDIO_ENGINE_SUCCESS == audio_engine_stop()) ? 0 : -1;
#else
    return 0;
#endif
}

int sound_localization_init(void)
{
    (void)page_5_cli_init();
    return 0;
}

extern int page_doa_enter(void);

int sound_localization_start(void)
{
    LOGI("Sound source localization -> page_5\r\n");
    if (page_doa_enter() != 0) {
        LOGI("page5 init failed\r\n");
        return -1;
    }

    if (sound_localization_start_service() != 0) {
        LOGI("page5 start asr failed\r\n");
        return 0;
    }
    LOGI("page5 start asr\r\n");
    return 0;
}

int sound_localization_stop(void)
{
    (void)sound_localization_stop_service();
    return 0;
}

#else  /* !ROBOT_TEST */

void sound_localization_register_ui_sink(sound_loc_ui_sink_t sink) { (void)sink; }
void sound_localization_set_angle(int degrees) { (void)degrees; }
int  sound_localization_get_angle(void)        { return 0; }
int  sound_localization_start_service(void)    { return 0; }
int  sound_localization_stop_service(void)     { return 0; }
void page_5_set_arrow_angle(int degrees)       { (void)degrees; }
int  page_5_get_arrow_angle(void)              { return 0; }
void page_5_set_arrow_pivot(int x, int y)      { (void)x; (void)y; }
int  page_5_cli_init(void)                     { return 0; }

int sound_localization_init(void)  { return 0; }
int sound_localization_start(void) { return 0; }
int sound_localization_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_sound_localization = {
    .name  = "sound_localization",
    .init  = sound_localization_init,
    .start = sound_localization_start,
    .stop  = sound_localization_stop,
};
