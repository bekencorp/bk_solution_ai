// Copyright 2020-2021 Beken
// Palm-tracking overlay demo: MIPI camera + GPU display path driven by the
// PalmDetectionModel NN pipeline, with the on-board pan/tilt servos slaved
// to the largest detected palm.
//
// Note on the "no LVGL in src/demo/" rule: palm tracking is an overlay
// demo that takes the framebuffer away from LVGL, so the lifecycle
// inherently has to coordinate with the LVGL vendor (pause on enter,
// resume + navigate-back on exit). lv_vendor.h + a minimal LVGL include
// are therefore admitted here as a documented exception. Page-bound demos
// (asr, music, volume, ...) stay strictly LVGL-free.

/* os/os.h does NOT wrap its declarations in `extern "C"`, so including it
 * directly from a .cc file causes C++ name mangling on RTOS APIs such as
 * rtos_create_thread / rtos_delete_thread, and the linker then cannot find
 * the un-mangled C symbols compiled from the SDK. Wrap them ourselves. */
extern "C" {
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <driver/gpio.h>
}
#include <stdbool.h>
#include "palm_detection.h"
#include "demo/palm_tracking.h"

#include "app_display.h"
#include "app_camera.h"
#include "app_gpu.h"

#include "AvdkVideoReatorOSD.h"
#include "AvdkDetectionModel.h"
#include "PalmDetectionModel.h"
#include "box.h"
#include "bk_aimi_servo.h"
#include "bk_aimi_palm_tracker.h"

#if CONFIG_LVGL
extern "C" {
#include "lvgl.h"
#include "lv_vendor.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "driver/drv_tp.h"

bk_err_t bk_robot_lvgl_resume_display(void);
}
#endif

static AvdkVideoReatorOSD *video_reator = NULL;
static PalmDetectionModel *model = NULL;

#ifndef PALM_TRACKING_MODEL_SD_PATH
#define PALM_TRACKING_MODEL_SD_PATH "1:/tflite/palm_detection_builtin_256_integer_quant_vela.tflite"
#endif

/* Hardware binding for the palm-tracking servos on this board.
 * The bk_servo component is HW-agnostic: PWM channel and GPIO pin are
 * picked here at the application layer and passed in via config structs. */
#define PALM_SERVO_PWM_CHAN_H   PWM_ID_0
#define PALM_SERVO_GPIO_ID_H    GPIO_69

#define PALM_SERVO_PWM_CHAN_V   PWM_ID_1
#define PALM_SERVO_GPIO_ID_V    GPIO_60

/* Per-axis mechanical safe range on this rig:
 *   - H (pan)  can sweep the full 0..180 (no obstruction).
 *   - V (tilt) is limited to 45..135 (i.e. neutral 90 ±45) so the
 *     camera can't crash into the chassis at either extreme. */
#define PALM_SERVO_MIN_ANGLE_H  0
#define PALM_SERVO_MAX_ANGLE_H  180
#define PALM_SERVO_MIN_ANGLE_V  45
#define PALM_SERVO_MAX_ANGLE_V  (90 + 45)

/* One handle per physical motor; tracking now drives both axes. */
static bk_aimi_servo_handle_t s_servo_h = NULL;
static bk_aimi_servo_handle_t s_servo_v = NULL;

/* Per-axis tracker tuning. Two structs (not one with a "use_y" toggle) so
 * H and V can be tuned independently -- in practice the vertical motor
 * often needs a different `dir` (mounting orientation) and a smaller
 * gain because vertical motion is less natural to the viewer. */
static bk_aimi_palm_tracker_axis_cfg_t s_tracker_cfg_h;
static bk_aimi_palm_tracker_axis_cfg_t s_tracker_cfg_v;

/* Build a tracker cfg whose mechanical limits track those of the given
 * servo handle, so the tracker math and the servo set_angle() clamp can
 * never disagree (otherwise the tracker thinks the angle is X while the
 * servo silently clamps to Y, and the next-frame delta is computed from
 * the wrong baseline). */
static void palm_tracker_axis_cfg_init(bk_aimi_palm_tracker_axis_cfg_t *cfg,
                                       bk_aimi_servo_handle_t servo,
                                       int dir)
{
    cfg->gain      = BK_AIMI_PALM_TRACKER_DEFAULT_GAIN;
    cfg->max_step  = BK_AIMI_PALM_TRACKER_DEFAULT_MAX_STEP;
    cfg->deadband  = BK_AIMI_PALM_TRACKER_DEFAULT_DEADBAND;
    cfg->dir       = dir;
    cfg->min_angle = bk_aimi_servo_get_min_angle(servo);
    cfg->max_angle = bk_aimi_servo_get_max_angle(servo);
}

/* The whole start-up path (sensor probe, ISP init, C++ object construction,
 * thread creation, ...) is heavy and deep. It must NOT run on a tiny stack
 * such as the FreeRTOS Timer Service Task (the path that delivers ADC-key
 * events), otherwise the timer task stack overflows and corrupts memory.
 *
 * So we offload the start-up to a dedicated one-shot worker thread, and
 * guard against double-start so that repeated key presses cannot create
 * duplicate models / cameras. */
#define PALM_START_TASK_STACK_SIZE   (1024 * 8)
#define PALM_START_TASK_NAME         "palm_start"

static beken_thread_t s_palm_start_thread = NULL;
static volatile bool s_palm_started = false;

#if CONFIG_LVGL
static beken_thread_t s_palm_exit_thread = NULL;
#if CONFIG_TP
#define PALM_TP_HOR_SIZE  360
#define PALM_TP_VER_SIZE  390
static bool s_tp_closed_for_palm = false;
#endif
#endif

#if CONFIG_LVGL && CONFIG_TP
static void palm_tp_close(void)
{
    int tp_ret = drv_tp_close();
    if (tp_ret == BK_OK) {
        s_tp_closed_for_palm = true;
    } else {
        bk_printf("palm_tp_close: drv_tp_close failed (%d)\n", tp_ret);
    }
}

static void palm_tp_open(void)
{
    if (!s_tp_closed_for_palm) {
        return;
    }

    int tp_ret = drv_tp_open(PALM_TP_HOR_SIZE, PALM_TP_VER_SIZE, TP_MIRROR_NONE);
    if (tp_ret == BK_OK) {
        s_tp_closed_for_palm = false;
    } else {
        bk_printf("palm_tp_open: drv_tp_open failed (%d)\n", tp_ret);
    }
}
#endif

static void detection_box_cb(Box *boxes, int count)
{
    if (boxes == NULL || count <= 0) {
        box_detection_path_clear();
        return;
    }

    /* Pick the box with the largest area (w*h) to drive the servo.
     *
     * boxes[] arrives sorted by score (NMS output), but the highest-score box
     * is not always the largest one -- a small but very distinct palm in a
     * corner can outscore a partially-clipped larger palm in the center. For
     * servo tracking we want "the palm closest to the camera", and box area
     * is a robust proxy for that regardless of pose. When count==1 we skip
     * the scan entirely. */
    int target = 0;
    if (count > 1) {
        float best_area = boxes[0].w * boxes[0].h;
        for (int i = 1; i < count; i++) {
            float area = boxes[i].w * boxes[i].h;
            if (area > best_area) {
                best_area = area;
                target = i;
            }
        }
    }

    bk_printf("detection_box_cb: count=%d target=%d score=%.3f xywh=(%.2f,%.2f,%.2fx%.2f)\n",
              count, target, boxes[target].score,
              boxes[target].x, boxes[target].y, boxes[target].w, boxes[target].h);

    /* Draw ALL detected palms. The user can still see secondary palms on the
     * OSD even though the servo only follows the largest one.
     * src = model input size (256x256), dst = display canvas size (1088x1088). */
    box_detection_path_build(boxes, count, count, 0, model->getWidth(), model->getHeight(), 400, 368);

    /* Drive both servos from the chosen box's center. Each axis runs the
     * same stateless tracker step with its own cfg, then commits the new
     * angle (if any) via the servo PWM API. Keeping the math out of the
     * servo driver lets us reuse the driver for non-tracking motion and
     * test the tracker logic standalone. */
    float palm_cx = boxes[target].x + boxes[target].w * 0.5f;
    float palm_cy = boxes[target].y + boxes[target].h * 0.5f;
    float img_w   = (float)model->getWidth();
    float img_h   = (float)model->getHeight();

    uint32_t next_h;
    if (bk_aimi_palm_tracker_step(&s_tracker_cfg_h, palm_cx, img_w,
                                  bk_aimi_servo_get_angle(s_servo_h),
                                  &next_h, "H")) {
        bk_aimi_servo_set_angle(s_servo_h, next_h);
    }

    uint32_t next_v;
    if (bk_aimi_palm_tracker_step(&s_tracker_cfg_v, palm_cy, img_h,
                                  bk_aimi_servo_get_angle(s_servo_v),
                                  &next_v, "V")) {
        bk_aimi_servo_set_angle(s_servo_v, next_v);
    }
}

void plam_detection_config(void)
{
    camera_board_config_t camera_board = {0};
    //display_board_config_t display_board = {0};
    gpu_board_config_t gpu_board = {0};

    camera_board.mipi.enable = true;
    camera_board.mipi.pin_scl = GPIO_70;
    camera_board.mipi.pin_sda = GPIO_71;
    camera_board.mipi.i2c_id = 1;
    camera_board.mipi.pin_reset = GPIO_31;
    camera_board.mipi.pin_pwdn = -1;
    camera_board.mipi.pin_xclk = GPIO_59;
    camera_board.mipi.sensor_max_width = 1088;
    camera_board.mipi.sensor_max_height = 1088;
    camera_board.mipi.sensor_fps = 15;
    camera_board.isp.mp_enable = true;
    camera_board.isp.mp_flexa = true;
    camera_board.isp.mp_width = 400;
    camera_board.isp.mp_height = 368;
    camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
    camera_board.isp.sp_enable = false;
    camera_board.isp.sp_flexa = false;

    // display_board.mipi.enable = true;
    // display_board.mipi.pin_reset = GPIO_60;
    // display_board.mipi.pin_backlight = GPIO_7;
    // display_board.mipi.panel = &lcd_device_hx8399c_mipi_1080x1920;
    // display_board.dpu_video.enable = true;
    // display_board.dpu_video.decompress = true;
    // display_board.dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;


    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = 270;
    gpu_board.flexa.src_width = 400;
    gpu_board.flexa.src_height = 368;
    gpu_board.flexa.dst_width = 400;
    gpu_board.flexa.dst_height = 368;
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    gpu_board.flexa.scale = false;
    gpu_board.flexa.tess_width = 400 / 4;
    gpu_board.flexa.tess_height = 368 / 4;

    /* Board config for Multimedia config */
    app_camera_board_config_set(&camera_board);
    //app_display_board_config_set(&display_board);
    app_gpu_board_config_set(&gpu_board);

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = true;
        display_config->dpu_video.decompress = true;
        display_config->dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;
    }
}

static void palm_detection_start_task(void *arg)
{
    (void)arg;
    int ret = BK_OK;
    bool camera_opened = false;
    bool display_open_attempted = false;
    bk_aimi_servo_config_t servo_cfg_h;
    bk_aimi_servo_config_t servo_cfg_v;

#if CONFIG_LVGL
    lv_vendor_stop();
#if CONFIG_TP
    palm_tp_close();
#endif
#endif

    plam_detection_config();

    /* Bring up both servos and park them at the neutral 90 degrees.
     * `initial_angle` does two jobs in one call: programs the PWM duty
     * AND primes handle->angle, so the first track frame computes its
     * delta against the real physical position (no startup jump). */
    servo_cfg_h.chan          = PALM_SERVO_PWM_CHAN_H;
    servo_cfg_h.gpio          = PALM_SERVO_GPIO_ID_H;
    servo_cfg_h.initial_angle = SERVO_CENTER_ANGLE;
    servo_cfg_h.min_angle     = PALM_SERVO_MIN_ANGLE_H;
    servo_cfg_h.max_angle     = PALM_SERVO_MAX_ANGLE_H;
    s_servo_h = bk_aimi_servo_init(&servo_cfg_h);
    if (s_servo_h == NULL) {
        bk_printf("palm_detection_start_task: H servo init failed\n");
        ret = BK_FAIL;
        goto fail;
    }

    servo_cfg_v.chan          = PALM_SERVO_PWM_CHAN_V;
    servo_cfg_v.gpio          = PALM_SERVO_GPIO_ID_V;
    servo_cfg_v.initial_angle = SERVO_CENTER_ANGLE;
    servo_cfg_v.min_angle     = PALM_SERVO_MIN_ANGLE_V;
    servo_cfg_v.max_angle     = PALM_SERVO_MAX_ANGLE_V;
    s_servo_v = bk_aimi_servo_init(&servo_cfg_v);
    if (s_servo_v == NULL) {
        bk_printf("palm_detection_start_task: V servo init failed\n");
        ret = BK_FAIL;
        goto fail;
    }

    /* Per-axis tracker cfg. `dir` is the sign that maps "palm offset on
     * this axis" to "angle delta on this motor":
     *   - H: palm-on-right (norm > 0) -> camera swings right ->
     *        H angle *increases* -> dir_h = +1.
     *   - V: palm-down     (norm > 0) -> camera tilts down ->
     *        V angle *increases* on this rig too -> dir_v = +1.
     * Flip the sign if you ever change the mechanical mounting; the
     * tracker math is otherwise the same for both axes. The tracker's
     * min/max are read back from the servo handle so the two clamps
     * always agree (tracker state == servo state). */
    palm_tracker_axis_cfg_init(&s_tracker_cfg_h, s_servo_h, +1);
    palm_tracker_axis_cfg_init(&s_tracker_cfg_v, s_servo_v, +1);

    model = new PalmDetectionModel();
    if (model == NULL) {
        bk_printf("palm_detection_start_task: model alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }
    model->setBoxDetectionCallback(detection_box_cb);
    model->setModelFilePath(PALM_TRACKING_MODEL_SD_PATH);

    video_reator = new AvdkVideoReatorOSD(model);
    if (video_reator == NULL) {
        bk_printf("palm_detection_start_task: video reator alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }

    ret = video_reator->init();
    if (ret != BK_OK) {
        bk_printf("palm_detection_start_task: init failed (%d)\n", ret);
        goto fail;
    }

    ret = video_reator->OpenISPCamera();
    if (ret != BK_OK) {
        bk_printf("palm_detection_start_task: OpenISPCamera failed (%d)\n", ret);
        goto fail;
    }
    camera_opened = true;

    display_open_attempted = true;
    ret = video_reator->OpenDisplay();
    if (ret != BK_OK) {
        bk_printf("palm_detection_start_task: OpenDisplay failed (%d)\n", ret);
        goto fail;
    }

    ret = video_reator->start();
    if (ret != BK_OK) {
        bk_printf("palm_detection_start_task: start failed (%d)\n", ret);
        goto fail;
    }

    bk_printf("palm_detection_start_task: done, exiting worker\n");

    s_palm_start_thread = NULL;
    rtos_delete_thread(NULL);
    return;

fail:
    bk_printf("palm_detection_start_task: failed (%d), aborting\n", ret);

    if (video_reator != NULL) {
        (void)video_reator->stop();
        if (display_open_attempted) {
            (void)video_reator->CloseDisplay();
        }
        if (camera_opened) {
            (void)video_reator->CloseCamera();
        }
        delete video_reator;
        video_reator = NULL;
    }

    if (model != NULL) {
        (void)model->deinit();
        delete model;
        model = NULL;
    }

    if (s_servo_h != NULL) {
        bk_aimi_servo_deinit(s_servo_h);
        s_servo_h = NULL;
    }
    if (s_servo_v != NULL) {
        bk_aimi_servo_deinit(s_servo_v);
        s_servo_v = NULL;
    }

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = false;
    }

#if CONFIG_LVGL
    if (display_open_attempted) {
        if (bk_robot_lvgl_resume_display() != BK_OK) {
            bk_printf("palm_detection_start_task: resume display failed\n");
        }
    }
#if CONFIG_TP
    palm_tp_open();
#endif
    lv_vendor_start();
    lv_vendor_disp_lock();
    {
        lv_obj_t *active = lv_screen_active();
        if (active != NULL) {
            lv_obj_invalidate(active);
        }
    }
    lv_vendor_disp_unlock();
#endif

    s_palm_started = false;
    s_palm_start_thread = NULL;
    rtos_delete_thread(NULL);
}

int palm_detection_start()
{
    if (s_palm_started) {
        bk_printf("palm_detection_start: already started, ignore\n");
        return 0;
    }

    s_palm_started = true;

    bk_err_t ret = rtos_create_thread(&s_palm_start_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      PALM_START_TASK_NAME,
                                      (beken_thread_function_t)palm_detection_start_task,
                                      PALM_START_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_palm_started = false;
        s_palm_start_thread = NULL;
        bk_printf("palm_detection_start: create thread failed, ret=%d\n", ret);
        return -1;
    }

    return 0;
}

bool palm_detection_can_start(void)
{
    /* Pipeline currently active (live or mid-startup). */
    if (s_palm_started || s_palm_start_thread != NULL) {
        return false;
    }
#if CONFIG_LVGL
    /* Exit task from the previous session is still tearing things down;
     * starting now would race the teardown on shared statics
     * (video_reator, model, s_servo_*) and on the LVGL display handle. */
    if (s_palm_exit_thread != NULL) {
        return false;
    }
#endif
    return true;
}

bool palm_detection_is_active(void)
{
#if CONFIG_LVGL
    if (s_palm_exit_thread != NULL) {
        return true;
    }
#endif
    return s_palm_started;
}

int palm_detection_stop(void)
{
    if (!s_palm_started) {
        return 0;
    }

    for (int i = 0; i < 50 && s_palm_start_thread != NULL; i++) {
        rtos_delay_milliseconds(20);
    }

    if (s_palm_start_thread != NULL) {
        bk_printf("palm_detection_stop: start task still running, abort stop\n");
        return BK_FAIL;
    }

    if (video_reator != NULL) {
        int ret = video_reator->stop();
        if (ret != BK_OK) {
            bk_printf("palm_detection_stop: video stop failed (%d), abort stop\n", ret);
            return ret;
        }
    }

    box_detection_path_clear();

    if (video_reator != NULL) {
        (void)video_reator->CloseDisplay();
        (void)video_reator->CloseCamera();
        delete video_reator;
        video_reator = NULL;
    }

    if (model != NULL) {
        (void)model->deinit();
        delete model;
        model = NULL;
    }

    if (s_servo_h != NULL) {
        bk_aimi_servo_set_angle(s_servo_h, SERVO_CENTER_ANGLE);
    }

    if (s_servo_v != NULL) {
        bk_aimi_servo_set_angle(s_servo_v, SERVO_CENTER_ANGLE);
    }

    if (s_servo_h != NULL || s_servo_v != NULL) {
        rtos_delay_milliseconds(300);
    }

    bk_aimi_servo_deinit(s_servo_h);
    s_servo_h = NULL;
    bk_aimi_servo_deinit(s_servo_v);
    s_servo_v = NULL;

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = false;
    }

    s_palm_started = false;
    return 0;
}

#if CONFIG_LVGL
#define PALM_EXIT_TASK_STACK_SIZE   (1024 * 8)
#define PALM_EXIT_TASK_NAME         "palm_exit"

static void palm_detection_exit_task(void *arg)
{
    (void)arg;

    int ret = palm_detection_stop();
    if (ret != 0) {
        bk_printf("palm_detection_exit_task: stop failed (%d)\n", ret);
        goto done;
    }

    if (bk_robot_lvgl_resume_display() != BK_OK) {
        bk_printf("palm_detection_exit_task: resume display failed\n");
        goto done;
    }

#if CONFIG_TP
    palm_tp_open();
#endif
    lv_vendor_start();

    lv_vendor_disp_lock();
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);

    {
        lv_obj_t *active = lv_screen_active();
        if (active != NULL) {
            lv_obj_invalidate(active);
        }
    }
    lv_vendor_disp_unlock();

done:
    s_palm_exit_thread = NULL;
    rtos_delete_thread(NULL);
}
#endif /* CONFIG_LVGL */

extern "C" int palm_detection_exit_to_menu(void)
{
#if CONFIG_LVGL
    /* Idempotent fast path: nothing to exit. */
    if (!s_palm_started) {
        return 0;
    }

    if (s_palm_exit_thread != NULL) {
        return 0;
    }

    bk_err_t ret = rtos_create_thread(&s_palm_exit_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      PALM_EXIT_TASK_NAME,
                                      (beken_thread_function_t)palm_detection_exit_task,
                                      PALM_EXIT_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_palm_exit_thread = NULL;
        bk_printf("palm_detection_exit_to_menu: create thread failed, ret=%d\n", ret);
        return -1;
    }
    return 0;
#else
    return palm_detection_stop();
#endif
}

/* ----------------------------------------------------------------------
 * bk_demo_iface_t wiring (merged in from the old src/demo/palm_tracking.c
 * thin wrapper).
 *
 * start() pauses LVGL before spawning the NN pipeline worker so the GPU
 * display path can take over the framebuffer; stop() is the synchronous
 * teardown path used by callers that already drove the UI exit themselves
 * (e.g. ui_key_bridge "S4 double" routes through palm_detection_exit_to_menu).
 * -------------------------------------------------------------------- */

extern "C" int palm_tracking_init(void)
{
    return 0;
}

extern "C" int palm_tracking_start(void)
{
    if (!palm_detection_can_start()) {
        bk_printf("palm_tracking_start: busy (start/exit in progress), ignore\n");
        return -1;
    }

    if (palm_detection_start() != 0) {
        bk_printf("palm_detection_start trigger failed\r\n");
        return -1;
    }

    return 0;
}

extern "C" int palm_tracking_stop(void)
{
    return palm_detection_exit_to_menu();
}

extern "C" const bk_demo_iface_t g_demo_palm_tracking = {
    "palm_tracking",
    palm_tracking_init,
    palm_tracking_start,
    palm_tracking_stop,
};