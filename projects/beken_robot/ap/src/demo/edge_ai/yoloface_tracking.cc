// Copyright 2020-2021 Beken
// Yoloface-detection overlay demo: MIPI camera + GPU display path driven by
// the YolofaceDetectionModel NN pipeline. This is a DISPLAY-ONLY variant of
// palm_tracking: it draws every detected face box on the OSD but does not
// drive any servo (no pan/tilt tracking).
//
// Note on the "no LVGL in src/demo/" rule: like palm tracking, this is an
// overlay demo that takes the framebuffer away from LVGL, so the lifecycle
// inherently has to coordinate with the LVGL vendor (pause on enter, resume +
// navigate-back on exit). lv_vendor.h + a minimal LVGL include are therefore
// admitted here as a documented exception.

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
#include "yoloface_detection.h"
#include "demo/yoloface_tracking.h"

#include "app_display.h"
#include "app_camera.h"
#include "app_gpu.h"
#include "bk_camera_lvgl_blend.h"

#include "AvdkVideoReatorOSD.h"
#include "AvdkDetectionModel.h"
#include "YolofaceDetectionModel.h"
#include "box.h"

#if CONFIG_LVGL
extern "C" {
#include "lvgl.h"
#include "lv_vendor.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "ui_overlay_swipe.h"

bk_err_t bk_robot_lvgl_resume_display(void);
int page_edge_ai_enter(void);
}
#endif

static AvdkVideoReatorOSD *s_video_reator = NULL;
static YolofaceDetectionModel *s_model = NULL;

#ifndef YOLOFACE_TRACKING_MODEL_SD_PATH
#define YOLOFACE_TRACKING_MODEL_SD_PATH "1:/tflite/yoloface_int8_vela.tflite"
#endif

/* Display canvas (GPU mp output) the OSD boxes are scaled to. Must match the
 * mp_width/mp_height programmed in yoloface_detection_config() below. The NN
 * input (model->getWidth()/getHeight()) is the *source* coordinate space the
 * boxes are reported in, and the ISP secondary path is auto-sized to it by
 * AvdkVideoReatorOSD::OpenISPCamera(). */
#define YOLOFACE_DISPLAY_W   400
#define YOLOFACE_DISPLAY_H   320
#define YOLOFACE_LVGL_CAMERA_W   256
#define YOLOFACE_LVGL_CAMERA_H   256
#define YOLOFACE_BLEND_BG_W       320
#define YOLOFACE_BLEND_BG_H       388
#define YOLOFACE_BLEND_BG_SIZE    (YOLOFACE_BLEND_BG_W * YOLOFACE_BLEND_BG_H)
/* Solution page preview logical top-left is panel(25,20)+border(2,2).
 * LVGL flush is ROTATE_90, so physical_x=logical_y and
 * physical_y=LVGL_DISP_HEIGHT-logical_x-preview_width. */
#define YOLOFACE_BLEND_FG_X       22
#define YOLOFACE_BLEND_FG_Y       102
#define YOLOFACE_BLEND_BOX_MAX    8

/* Same offload rationale as palm tracking: the start-up path (sensor probe,
 * ISP init, C++ object construction, thread creation, ...) is heavy and deep,
 * so it must NOT run on a tiny stack such as the FreeRTOS Timer Service Task.
 * We offload it to a dedicated one-shot worker thread and guard against
 * double-start so repeated key presses cannot create duplicate pipelines. */
#define YOLOFACE_START_TASK_STACK_SIZE   (1024 * 8)
#define YOLOFACE_START_TASK_NAME         "yoloface_start"

static beken_thread_t s_yoloface_start_thread = NULL;
static volatile bool s_yoloface_started = false;
static volatile bool s_yoloface_return_to_edge_ai = false;
static volatile bool s_yoloface_lvgl_camera_blend = false;

#if CONFIG_LVGL
static beken_thread_t s_yoloface_exit_thread = NULL;
static volatile bool s_yoloface_lvgl_stopped = false;
#endif

#if CONFIG_LVGL && CONFIG_TP
static void yoloface_overlay_back(void *arg)
{
    (void)arg;
    (void)yoloface_detection_exit_to_menu();
}
#endif

/* Display-only callback: draw every detected face. No servo is driven.
 *
 * src = model input size (yoloface 56x56), dst = display canvas size. The OSD
 * holds the previous boxes until the next successful frame overwrites them, so
 * on an empty frame we explicitly clear the path. */
static void yoloface_detection_box_cb(Box *boxes, int count)
{
    if (boxes == NULL || count <= 0) {
        if (s_yoloface_lvgl_camera_blend) {
            (void)bk_camera_lvgl_blend_set_boxes(NULL, 0);
        }
        box_detection_path_clear();
        return;
    }

    bk_printf("yoloface_detection_box_cb: count=%d top score=%.3f xywh=(%.2f,%.2f,%.2fx%.2f)\n",
              count, boxes[0].score,
              boxes[0].x, boxes[0].y, boxes[0].w, boxes[0].h);

    if (s_yoloface_lvgl_camera_blend) {
        bk_camera_lvgl_blend_rect_t rects[YOLOFACE_BLEND_BOX_MAX];
        int rect_count = (count > YOLOFACE_BLEND_BOX_MAX) ? YOLOFACE_BLEND_BOX_MAX : count;
        float src_w = (float)s_model->getWidth();
        float src_h = (float)s_model->getHeight();
        float xscale = (float)YOLOFACE_LVGL_CAMERA_W / src_h;
        float yscale = (float)YOLOFACE_LVGL_CAMERA_H / src_w;

        for (int i = 0; i < rect_count; i++) {
            float x0 = boxes[i].x;
            float y0 = boxes[i].y;
            float x1 = boxes[i].x + boxes[i].w;
            float y1 = boxes[i].y + boxes[i].h;

            float rx0 = y0;
            float ry0 = src_w - x0;
            float rx1 = y1;
            float ry1 = src_w - x1;

            float fxmin = rx0 * xscale;
            float fymin = ry0 * yscale;
            float fxmax = rx1 * xscale;
            float fymax = ry1 * yscale;

            if (fxmin > fxmax) { float t = fxmin; fxmin = fxmax; fxmax = t; }
            if (fymin > fymax) { float t = fymin; fymin = fymax; fymax = t; }
            if (fxmin < 0.0f) fxmin = 0.0f;
            if (fymin < 0.0f) fymin = 0.0f;
            if (fxmax > (float)(YOLOFACE_LVGL_CAMERA_W - 1)) fxmax = (float)(YOLOFACE_LVGL_CAMERA_W - 1);
            if (fymax > (float)(YOLOFACE_LVGL_CAMERA_H - 1)) fymax = (float)(YOLOFACE_LVGL_CAMERA_H - 1);

            rects[i].x1 = (int16_t)(fxmin + 0.5f);
            rects[i].y1 = (int16_t)(fymin + 0.5f);
            rects[i].x2 = (int16_t)(fxmax + 0.5f);
            rects[i].y2 = (int16_t)(fymax + 0.5f);
        }

        (void)bk_camera_lvgl_blend_set_boxes(rects, rect_count);
        box_detection_path_clear();
        return;
    }

    box_detection_path_build(boxes, count, count, 0,
                             s_model->getWidth(), s_model->getHeight(),
                             YOLOFACE_DISPLAY_W, YOLOFACE_DISPLAY_H);
}

static void yoloface_detection_frame_complete(void *frame, uint32_t frame_size, void *args)
{
    (void)args;

    if (s_yoloface_lvgl_camera_blend) {
        (void)bk_camera_lvgl_blend_push_camera_frame(frame, frame_size);
    } else {
        app_gpu_frame_free(frame);
    }
}

static void yoloface_detection_config(void)
{
    camera_board_config_t camera_board = {0};
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

    if (s_yoloface_lvgl_camera_blend) {
        camera_board.isp.mp_width = YOLOFACE_LVGL_CAMERA_W;
        camera_board.isp.mp_height = YOLOFACE_LVGL_CAMERA_H;
    } else {
        camera_board.isp.mp_width = YOLOFACE_DISPLAY_W;
        camera_board.isp.mp_height = YOLOFACE_DISPLAY_H;
    }
    camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
    camera_board.isp.sp_enable = false;
    camera_board.isp.sp_flexa = false;

    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = 270;
    if (s_yoloface_lvgl_camera_blend) {
        gpu_board.flexa.tess_width = YOLOFACE_LVGL_CAMERA_W / 2;
        gpu_board.flexa.tess_height = YOLOFACE_LVGL_CAMERA_H / 2;
        gpu_board.flexa.src_width = YOLOFACE_LVGL_CAMERA_W;
        gpu_board.flexa.src_height = YOLOFACE_LVGL_CAMERA_H;
        gpu_board.flexa.dst_width = YOLOFACE_LVGL_CAMERA_W;
        gpu_board.flexa.dst_height = YOLOFACE_LVGL_CAMERA_H;
    } else {
        gpu_board.flexa.tess_width = YOLOFACE_DISPLAY_W / 2;
        gpu_board.flexa.tess_height = YOLOFACE_DISPLAY_H / 2;
        gpu_board.flexa.src_width = YOLOFACE_DISPLAY_W;
        gpu_board.flexa.src_height = YOLOFACE_DISPLAY_H;
        gpu_board.flexa.dst_width = YOLOFACE_DISPLAY_W;
        gpu_board.flexa.dst_height = YOLOFACE_DISPLAY_H;
    }
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    gpu_board.flexa.scale = false;

    if (s_yoloface_lvgl_camera_blend) {
        gpu_board.flexa.frame_done = yoloface_detection_frame_complete;
        gpu_board.flexa.frame_done_args = NULL;
    } else {
        gpu_board.flexa.frame_done = NULL;
        gpu_board.flexa.frame_done_args = NULL;
    }

    /* Board config for Multimedia config */
    app_camera_board_config_set(&camera_board);
    app_gpu_board_config_set(&gpu_board);

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = true;
        display_config->dpu_video.decompress = true;
        display_config->dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;
    }
}

static void yoloface_detection_start_task(void *arg)
{
    (void)arg;
    int ret = BK_OK;
    bool camera_opened = false;
    bool display_open_attempted = false;

#if CONFIG_LVGL
    if (!s_yoloface_lvgl_camera_blend) {
        lv_vendor_stop();
        s_yoloface_lvgl_stopped = true;
    } else {
        s_yoloface_lvgl_stopped = false;
    }
#endif

    if (s_yoloface_lvgl_camera_blend) {
        bk_camera_lvgl_blend_config_t blend_cfg = {
            .bg_width = YOLOFACE_BLEND_BG_W,
            .bg_height = YOLOFACE_BLEND_BG_H,
            .bg_frame_size = YOLOFACE_BLEND_BG_SIZE,
            .fg_width = YOLOFACE_LVGL_CAMERA_W,
            .fg_height = YOLOFACE_LVGL_CAMERA_H,
            .fg_x = YOLOFACE_BLEND_FG_X,
            .fg_y = YOLOFACE_BLEND_FG_Y,
        };
        ret = bk_camera_lvgl_blend_start(&blend_cfg);
        if (ret != BK_OK) {
            bk_printf("yoloface_detection_start_task: blend start failed (%d)\n", ret);
            goto fail;
        }
#if CONFIG_LVGL
        lv_vendor_disp_lock();
        {
            lv_obj_t *active = lv_screen_active();
            if (active != NULL) {
                lv_obj_invalidate(active);
            }
        }
        lv_vendor_disp_unlock();
#endif
    }

    yoloface_detection_config();

    s_model = new YolofaceDetectionModel();
    if (s_model == NULL) {
        bk_printf("yoloface_detection_start_task: model alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }
    s_model->setBoxDetectionCallback(yoloface_detection_box_cb);
    s_model->setModelFilePath(YOLOFACE_TRACKING_MODEL_SD_PATH);

    s_video_reator = new AvdkVideoReatorOSD(s_model);
    if (s_video_reator == NULL) {
        bk_printf("yoloface_detection_start_task: video reator alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }

    ret = s_video_reator->init();
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: init failed (%d)\n", ret);
        goto fail;
    }

    ret = s_video_reator->OpenISPCamera();
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: OpenISPCamera failed (%d)\n", ret);
        goto fail;
    }
    camera_opened = true;

    display_open_attempted = true;
    ret = s_video_reator->OpenDisplay();
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: OpenDisplay failed (%d)\n", ret);
        goto fail;
    }

#if CONFIG_LVGL
    if (s_yoloface_lvgl_camera_blend) {
        lv_vendor_disp_lock();
        {
            lv_obj_t *active = lv_screen_active();
            if (active != NULL) {
                lv_obj_invalidate(active);
            }
        }
        lv_vendor_disp_unlock();
    }
#endif

    ret = s_video_reator->start();
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: start failed (%d)\n", ret);
        goto fail;
    }

#if CONFIG_LVGL && CONFIG_TP
    ret = ui_overlay_swipe_back_start(yoloface_overlay_back, NULL);
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: overlay swipe start failed (%d)\n", ret);
    }
#endif

    bk_printf("yoloface_detection_start_task: done, exiting worker\n");

    s_yoloface_start_thread = NULL;
    rtos_delete_thread(NULL);
    return;

fail:
    bk_printf("yoloface_detection_start_task: failed (%d), aborting\n", ret);

    if (s_video_reator != NULL) {
        (void)s_video_reator->stop();
        if (display_open_attempted) {
            (void)s_video_reator->CloseDisplay();
        }
        if (camera_opened) {
            (void)s_video_reator->CloseCamera();
        }
        delete s_video_reator;
        s_video_reator = NULL;
    }

    if (s_model != NULL) {
        (void)s_model->deinit();
        delete s_model;
        s_model = NULL;
    }

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = false;
    }

#if CONFIG_LVGL
    if (s_yoloface_lvgl_camera_blend) {
        (void)bk_camera_lvgl_blend_stop();
    }
    if (s_yoloface_lvgl_stopped && display_open_attempted) {
        if (bk_robot_lvgl_resume_display() != BK_OK) {
            bk_printf("yoloface_detection_start_task: resume display failed\n");
        }
    }
#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif
    if (s_yoloface_lvgl_stopped) {
        lv_vendor_start();
    }
    lv_vendor_disp_lock();
    {
        lv_obj_t *active = lv_screen_active();
        if (active != NULL) {
            lv_obj_invalidate(active);
        }
    }
    lv_vendor_disp_unlock();
    s_yoloface_lvgl_stopped = false;
#endif

    s_yoloface_started = false;
    s_yoloface_lvgl_camera_blend = false;
    s_yoloface_start_thread = NULL;
    rtos_delete_thread(NULL);
}

static int yoloface_detection_start(void)
{
    if (s_yoloface_started) {
        bk_printf("yoloface_detection_start: already started, ignore\n");
        return 0;
    }

    s_yoloface_started = true;

    bk_err_t ret = rtos_create_thread(&s_yoloface_start_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      YOLOFACE_START_TASK_NAME,
                                      (beken_thread_function_t)yoloface_detection_start_task,
                                      YOLOFACE_START_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_yoloface_started = false;
        s_yoloface_start_thread = NULL;
        bk_printf("yoloface_detection_start: create thread failed, ret=%d\n", ret);
        return -1;
    }

    return 0;
}

static bool yoloface_detection_can_start(void)
{
    /* Pipeline currently active (live or mid-startup). */
    if (s_yoloface_started || s_yoloface_start_thread != NULL) {
        return false;
    }
#if CONFIG_LVGL
    /* Exit task from the previous session is still tearing things down;
     * starting now would race the teardown on shared statics. */
    if (s_yoloface_exit_thread != NULL) {
        return false;
    }
#endif
    return true;
}

extern "C" bool yoloface_detection_is_active(void)
{
#if CONFIG_LVGL
    if (s_yoloface_exit_thread != NULL) {
        return true;
    }
#endif
    return s_yoloface_started;
}

static int yoloface_detection_stop(void)
{
    if (!s_yoloface_started) {
        return 0;
    }

#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif

    for (int i = 0; i < 50 && s_yoloface_start_thread != NULL; i++) {
        rtos_delay_milliseconds(20);
    }

    if (s_yoloface_start_thread != NULL) {
        bk_printf("yoloface_detection_stop: start task still running, abort stop\n");
        return BK_FAIL;
    }

#if CONFIG_LVGL
    if (!s_yoloface_lvgl_stopped) {
        lv_vendor_stop();
        s_yoloface_lvgl_stopped = true;
    }
#endif

    if (s_yoloface_lvgl_camera_blend) {
        (void)bk_camera_lvgl_blend_stop();
    }

    if (s_video_reator != NULL) {
        int ret = s_video_reator->stop();
        if (ret != BK_OK) {
            bk_printf("yoloface_detection_stop: video stop failed (%d), abort stop\n", ret);
            return ret;
        }
    }

    box_detection_path_clear();

    if (s_video_reator != NULL) {
        (void)s_video_reator->CloseDisplay();
        (void)s_video_reator->CloseCamera();
        delete s_video_reator;
        s_video_reator = NULL;
    }

    if (s_model != NULL) {
        (void)s_model->deinit();
        delete s_model;
        s_model = NULL;
    }

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = false;
    }

    s_yoloface_started = false;
    s_yoloface_lvgl_camera_blend = false;
    return 0;
}

#if CONFIG_LVGL
#define YOLOFACE_EXIT_TASK_STACK_SIZE   (1024 * 8)
#define YOLOFACE_EXIT_TASK_NAME         "yoloface_exit"

static void yoloface_detection_exit_task(void *arg)
{
    (void)arg;
    bool return_to_edge_ai = s_yoloface_return_to_edge_ai;
    bool lvgl_stopped = false;
    s_yoloface_return_to_edge_ai = false;

    int ret = yoloface_detection_stop();
    if (ret != 0) {
        bk_printf("yoloface_detection_exit_task: stop failed (%d)\n", ret);
        goto done;
    }

    lvgl_stopped = s_yoloface_lvgl_stopped;
    if (lvgl_stopped && bk_robot_lvgl_resume_display() != BK_OK) {
        bk_printf("yoloface_detection_exit_task: resume display failed\n");
        goto done;
    }

#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif
    if (lvgl_stopped) {
        lv_vendor_start();
    }

    lv_vendor_disp_lock();
    if (return_to_edge_ai) {
        (void)page_edge_ai_enter();
    } else {
        navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_3,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_3);
    }

    {
        lv_obj_t *active = lv_screen_active();
        if (active != NULL) {
            lv_obj_invalidate(active);
        }
    }
    lv_vendor_disp_unlock();
    s_yoloface_lvgl_stopped = false;

done:
    s_yoloface_exit_thread = NULL;
    rtos_delete_thread(NULL);
}
#endif /* CONFIG_LVGL */

extern "C" int yoloface_detection_exit_to_menu(void)
{
#if CONFIG_LVGL
    ui_overlay_swipe_back_stop();

    /* Idempotent fast path: nothing to exit. */
    if (!s_yoloface_started) {
        return 0;
    }

    if (s_yoloface_exit_thread != NULL) {
        return 0;
    }

    bk_err_t ret = rtos_create_thread(&s_yoloface_exit_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      YOLOFACE_EXIT_TASK_NAME,
                                      (beken_thread_function_t)yoloface_detection_exit_task,
                                      YOLOFACE_EXIT_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_yoloface_exit_thread = NULL;
        bk_printf("yoloface_detection_exit_to_menu: create thread failed, ret=%d\n", ret);
        return -1;
    }
    return 0;
#else
    return yoloface_detection_stop();
#endif
}

/* ----------------------------------------------------------------------
 * bk_demo_iface_t wiring.
 *
 * start() pauses LVGL before spawning the NN pipeline worker so the GPU
 * display path can take over the framebuffer; stop() routes through
 * yoloface_detection_exit_to_menu() which tears down and navigates back.
 * -------------------------------------------------------------------- */

extern "C" int yoloface_tracking_init(void)
{
    return 0;
}

extern "C" int yoloface_tracking_start(void)
{
    if (!yoloface_detection_can_start()) {
        bk_printf("yoloface_tracking_start: busy (start/exit in progress), ignore\n");
        return -1;
    }

    if (yoloface_detection_start() != 0) {
        bk_printf("yoloface_detection_start trigger failed\r\n");
        return -1;
    }

    return 0;
}

extern "C" void yoloface_tracking_set_return_to_edge_ai(bool enable)
{
    s_yoloface_return_to_edge_ai = enable;
}

extern "C" void yoloface_tracking_set_lvgl_camera_blend(bool enable)
{
    s_yoloface_lvgl_camera_blend = enable;
}

extern "C" int yoloface_tracking_stop(void)
{
    return yoloface_detection_exit_to_menu();
}

extern "C" const bk_demo_iface_t g_demo_yoloface_tracking = {
    "yoloface_tracking",
    yoloface_tracking_init,
    yoloface_tracking_start,
    yoloface_tracking_stop,
};
