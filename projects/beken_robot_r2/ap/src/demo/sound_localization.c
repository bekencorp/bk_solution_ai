/**
 * @file sound_localization.c
 * @brief Backend for the page_5 sound source localization demo. Owns
 *        the cached arrow angle (pushed by the audio_engine DOA callback)
 *        and provides the audio_engine ASR start/stop service that feeds
 *        the DOA estimator. LVGL-free: the live angle is pushed into the
 *        UI via a callback registered by page_doa_hooks.c.
 *
 * Backward-compat: keeps `page_5_set_arrow_angle()` /
 * `page_5_get_arrow_angle()` / `page_5_set_arrow_pivot()` /
 * `page_5_cli_init()` exported so audio_engine and legacy
 * call sites continue to compile.
 */
/* ----------------------------------------------------------------------
 * Three-mic geometry
 *
 * Frame: origin O = midpoint of mic0 and mic1
 *        +X = horizontal, pointing right (mic1 -> mic0)
 *        +Y = horizontal, perpendicular to X, pointing to the front of
 *             the device (user side)
 *        +Z = vertical, pointing up (device kept upright)
 *        unit = mm
 *
 * 1. Offset of mic2 relative to O (signed)
 *
 *     dx = mic2.x - O.x =   0 mm   X offset of mic2 from midpoint O
 *                                  + = on the mic0 side; - = on the mic1 side
 *     dy = mic2.y - O.y = -20 mm   Y offset of mic2 from midpoint O
 *                                  + = in front of the mic0-mic1 line
 *                                      (user side)
 *                                  - = behind it (inside the enclosure)
 *     dz = mic2.z - O.z =   0 mm   Z offset of mic2 from midpoint O
 *                                  + = above the mic0-mic1 line
 *                                  - = below it; fill 0 when coplanar
 *
 *     Coordinates (d01 = mic0-mic1 spacing):
 *         mic0 = (-d01/2, 0,     0)
 *         mic1 = (+d01/2, 0,     0)
 *         mic2 = (dx,     dy,    dz)
 *
 * 2. Baseline length
 *
 *     d01 = 56 mm          (spacing between mic0 and mic1)
 *
 * 3. Layout
 *
 *     [ ] Collinear (dy = 0 and dz = 0)
 *     [Y] Triangular (dy or dz is non-zero)
 *     [ ] Other: ____________________
 *
 * 4. Differential wiring
 *
 *     Every mic is wired differentially on its own +/- pair; the three
 *     baselines are mutually independent.
 *
 * 5. Mounting
 *
 *     Mic openings on the device: [Y] front / __ top / __ bottom / __ side
 *     Horizontal offset angle of mic2 from the mic0-mic1 line:
 *         theta = 0 deg
 * -------------------------------------------------------------------- */
#include "demo/sound_localization.h"
#include "page_5_api.h"
#include "page_doa_eyes.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef ROBOT_TEST

#include "audio_engine.h"
#include "audio_engine_hooks.h"
#include "bk_cli.h"
#include "cli.h"
#include "os/os.h"
#include "os/str.h"
#include <components/log.h>

#define TAG "doa_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

#define R2_DOA_SM2_OFFSET       (-12700)
#define R2_DOA_SM2_DEAD_ZONE    (2500)
#define R2_DOA_FB_OFFSET        (13900)
#define R2_DOA_FB_DEAD_ZONE     (5000)
#define R2_DOA_REAR_RIGHT_ANGLE (225)
#define R2_DOA_REAR_CENTER_ANGLE (270)
#define R2_DOA_REAR_LEFT_ANGLE  (315)

#define R2_DOA_DIR_RIGHT    (-1)
#define R2_DOA_DIR_CENTER   (0)
#define R2_DOA_DIR_LEFT     (1)

#define R2_DOA_W_SM2       2
#define R2_DOA_W_DIFF      1

static const audio_engine_doa_arrow_cfg_t s_doa_arrow_cfg = {
    .phs_sm2_min        = -16384,
    .phs_sm2_max        =  20000,
    .phs_sm2_right_max  = R2_DOA_SM2_OFFSET - R2_DOA_SM2_DEAD_ZONE,
    .phs_sm2_left_min   = R2_DOA_SM2_OFFSET + R2_DOA_SM2_DEAD_ZONE,
    .arrow_angle_right  =    135,
    .arrow_angle_center =     90,
    .arrow_angle_left   =     45,
};

static int r2_doa_front_back(int32_t h_sum)
{
    int32_t score = h_sum - R2_DOA_FB_OFFSET;

    if (score >= R2_DOA_FB_DEAD_ZONE) {
        return -1;
    }
    if (score <= -R2_DOA_FB_DEAD_ZONE) {
        return 1;
    }
    return 0;
}

static int r2_doa_h_classify(int32_t h, int32_t right_max, int32_t left_min)
{
    if (h <= right_max) {
        return R2_DOA_DIR_RIGHT;
    }
    if (h >= left_min) {
        return R2_DOA_DIR_LEFT;
    }
    return R2_DOA_DIR_CENTER;
}

static int r2_doa_fusion(const audio_engine_doa_metric_t *metric)
{
    int32_t h_sm2 = metric->phs_sm2;
    int32_t h = h_sm2;
    int32_t right_max = s_doa_arrow_cfg.phs_sm2_right_max;
    int32_t left_min = s_doa_arrow_cfg.phs_sm2_left_min;
    int32_t h_sum = metric->phs02 + metric->phs12;
    int fb = 0;
    int dir;
    int angle;

    if ((metric->mic_num >= 3) && ((metric->phs02 != 0) || (metric->phs12 != 0))) {
        int32_t h_diff = metric->phs02 - metric->phs12;

        h = (h_sm2 * R2_DOA_W_SM2 + h_diff * R2_DOA_W_DIFF) / (R2_DOA_W_SM2 + R2_DOA_W_DIFF);
        fb = r2_doa_front_back(h_sum);
        LOGI("doa fusion: 3mic h_sm2=%d phs02=%d phs12=%d h_diff=%d h_sum=%d fb=%d w=%d/%d h=%d\r\n",
             (int)h_sm2, (int)metric->phs02, (int)metric->phs12, (int)h_diff,
             (int)h_sum, fb, R2_DOA_W_SM2, R2_DOA_W_DIFF, (int)h);
    } else {
        LOGI("doa fusion: skip diff, mic=%u phs02=%d phs12=%d h=%d\r\n",
             (unsigned)metric->mic_num, (int)metric->phs02, (int)metric->phs12, (int)h);
    }

    if (fb > 0) {
        dir = r2_doa_h_classify(h, right_max, left_min);

        switch (dir) {
        case R2_DOA_DIR_RIGHT:
            angle = R2_DOA_REAR_RIGHT_ANGLE;
            break;
        case R2_DOA_DIR_LEFT:
            angle = R2_DOA_REAR_LEFT_ANGLE;
            break;
        default:
            angle = R2_DOA_REAR_CENTER_ANGLE;
            break;
        }

        LOGI("doa decide: rear h_sum=%d h=%d dir=%d angle=%d\r\n",
             (int)h_sum, (int)h, dir, angle);

        return angle;
    }

    dir = r2_doa_h_classify(h, right_max, left_min);

    switch (dir) {
    case R2_DOA_DIR_RIGHT:
        angle = s_doa_arrow_cfg.arrow_angle_right;
        break;
    case R2_DOA_DIR_LEFT:
        angle = s_doa_arrow_cfg.arrow_angle_left;
        break;
    default:
        angle = s_doa_arrow_cfg.arrow_angle_center;
        break;
    }

    LOGI("doa decide: h=%d right_max=%d left_min=%d dir=%d angle=%d\r\n",
         (int)h, (int)right_max, (int)left_min, dir, angle);

    return angle;
}

static int sound_localization_doa_metric(const audio_engine_doa_metric_t *metric)
{
    int angle;

    if (metric->mic_num >= 3) {
        angle = r2_doa_fusion(metric);
    } else {
        angle = audio_engine_doa_to_arrow_default(metric->phs_sm2, &s_doa_arrow_cfg);
        LOGI("doa default: 2mic phs_sm2=%d angle=%d\r\n",
             (int)metric->phs_sm2, angle);
    }

    LOGI("doa metric: mic=%u vad=%u phs_sm2=%d phs02=%d phs12=%d angle=%d\r\n",
         (unsigned)metric->mic_num, (unsigned)metric->vad_state,
         (int)metric->phs_sm2, (int)metric->phs02, (int)metric->phs12, angle);

    return angle;
}

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

    /* Runs at startup via bk_demos_init_all(), before any wakeup DOA report. */
    audio_engine_register_doa_metric_callback(sound_localization_doa_metric);
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
