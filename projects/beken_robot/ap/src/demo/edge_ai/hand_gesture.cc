// Copyright 2020-2021 Beken
// Hand-gesture overlay demo: MIPI camera + GPU display path driven by the
// HandGestureDetectionModel NN pipeline, with Hiwonder 6-DOF hand servos
// slaved to the top detected gesture class.

extern "C" {
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <driver/gpio.h>
}
#include <stdbool.h>
#include "hand_gesture_detection.h"
#include "demo/hand_gesture.h"

#include "app_display.h"
#include "app_camera.h"
#include "app_gpu.h"

#include "AvdkVideoReatorOSD.h"
#include "AvdkDetectionModel.h"
#include "HandGestureDetectionModel.h"
#include "box.h"
#include "bk_hiwonder_hand_servo.h"

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
static HandGestureDetectionModel *s_model = NULL;

/* Hiwonder hand servo hardware map for the beken_robot board.
 * Entry i maps to servo id (i + 1): {PWM channel, GPIO pad}. */
static const bk_hiwonder_hand_servo_hw_map_t s_hiwonder_servo_hw_map[] = {
    {8, GPIO_22},  /* servo 1 */
    {6, GPIO_23},  /* servo 2 */
    {7, GPIO_24},  /* servo 3 */
    {5, GPIO_25},  /* servo 4 */
    {4, GPIO_26},  /* servo 5 */
    {3, GPIO_27},  /* servo 6 */
};

#ifndef HAND_GESTURE_MODEL_SD_PATH
#define HAND_GESTURE_MODEL_SD_PATH "1:/tflite/hand_gesture_detection_vela.tflite"
#endif

#define HAND_GESTURE_DISPLAY_W   400
#define HAND_GESTURE_DISPLAY_H   320

#define HAND_GESTURE_START_TASK_STACK_SIZE   (1024 * 8)
#define HAND_GESTURE_START_TASK_NAME         "hand_gesture_start"

static beken_thread_t s_hand_gesture_start_thread = NULL;
static volatile bool s_hand_gesture_started = false;
static volatile bool s_hand_gesture_return_to_edge_ai = false;
static bk_hiwonder_hand_servo_handle_t s_hiwonder_servo = NULL;

#if CONFIG_LVGL
static beken_thread_t s_hand_gesture_exit_thread = NULL;
#endif

#if CONFIG_LVGL && CONFIG_TP
static void hand_gesture_overlay_back(void *arg)
{
    (void)arg;
    (void)hand_gesture_detection_exit_to_menu();
}
#endif

static void hand_gesture_action_cb(uint8_t preset_id)
{
    bk_printf("hand_gesture_action_cb: preset=%u\n", (unsigned)preset_id);
    if (s_hiwonder_servo != NULL) {
        bk_hiwonder_hand_servo_apply_preset(s_hiwonder_servo, preset_id);
    }
}

static void hand_gesture_result_cb(int class_id, const char *class_name, float score, int count)
{
    bk_printf("hand_gesture_result_cb: count=%d class_id=%d (%s) score=%.3f\n",
              count,
              class_id,
              (class_name != NULL) ? class_name : "?",
              score);
}

static void hand_gesture_detection_box_cb(Box *boxes, int count)
{
    if (boxes == NULL || count <= 0) {
        box_detection_path_clear();
        return;
    }

    bk_printf("hand_gesture_detection_box_cb: count=%d top score=%.3f xywh=(%.2f,%.2f,%.2fx%.2f)\n",
              count, boxes[0].score,
              boxes[0].x, boxes[0].y, boxes[0].w, boxes[0].h);

    box_detection_path_build(boxes, count, count, 0,
                             s_model->getWidth(), s_model->getHeight(),
                             HAND_GESTURE_DISPLAY_W, HAND_GESTURE_DISPLAY_H);
}

static void hand_gesture_detection_config(void)
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
    camera_board.isp.mp_width = HAND_GESTURE_DISPLAY_W;
    camera_board.isp.mp_height = HAND_GESTURE_DISPLAY_H;
    camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
    camera_board.isp.sp_enable = false;
    camera_board.isp.sp_flexa = false;

    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = 270;
    gpu_board.flexa.src_width = HAND_GESTURE_DISPLAY_W;
    gpu_board.flexa.src_height = HAND_GESTURE_DISPLAY_H;
    gpu_board.flexa.dst_width = HAND_GESTURE_DISPLAY_W;
    gpu_board.flexa.dst_height = HAND_GESTURE_DISPLAY_H;
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    gpu_board.flexa.scale = false;
    gpu_board.flexa.tess_width = HAND_GESTURE_DISPLAY_W / 2;
    gpu_board.flexa.tess_height = HAND_GESTURE_DISPLAY_H / 2;

    app_camera_board_config_set(&camera_board);
    app_gpu_board_config_set(&gpu_board);

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = true;
        display_config->dpu_video.decompress = true;
        display_config->dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;
    }
}

static void hand_gesture_detection_start_task(void *arg)
{
    (void)arg;
    int ret = BK_OK;
    bool camera_opened = false;
    bool display_open_attempted = false;

    bk_printf("hand_gesture_detection_start_task: enter\n");

#if CONFIG_LVGL
    lv_vendor_stop();
    bk_printf("hand_gesture_detection_start_task: lvgl stopped\n");
#endif

    hand_gesture_detection_config();
    bk_printf("hand_gesture_detection_start_task: camera/gpu/display config set\n");

    s_hiwonder_servo = bk_hiwonder_hand_servo_init(s_hiwonder_servo_hw_map,
                                                   sizeof(s_hiwonder_servo_hw_map) /
                                                       sizeof(s_hiwonder_servo_hw_map[0]));
    if (s_hiwonder_servo == NULL) {
        bk_printf("hand_gesture_detection_start_task: hiwonder servo init failed\n");
        ret = BK_FAIL;
        goto fail;
    }

    bk_printf("hand_gesture_detection_start_task: hiwonder servo init ok\n");

    s_model = new HandGestureDetectionModel();
    if (s_model == NULL) {
        bk_printf("hand_gesture_detection_start_task: model alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }
    s_model->setBoxDetectionCallback(hand_gesture_detection_box_cb);
    s_model->setGestureActionCallback(hand_gesture_action_cb);
    s_model->setGestureResultCallback(hand_gesture_result_cb);
    s_model->setModelFilePath(HAND_GESTURE_MODEL_SD_PATH);
    bk_printf("hand_gesture_detection_start_task: model created, path=%s\n",
              HAND_GESTURE_MODEL_SD_PATH);

    s_video_reator = new AvdkVideoReatorOSD(s_model);
    if (s_video_reator == NULL) {
        bk_printf("hand_gesture_detection_start_task: video reator alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }

    ret = s_video_reator->init();
    if (ret != BK_OK) {
        bk_printf("hand_gesture_detection_start_task: init failed (%d)\n", ret);
        goto fail;
    }
    bk_printf("hand_gesture_detection_start_task: video reator init ok\n");

    ret = s_video_reator->OpenISPCamera();
    if (ret != BK_OK) {
        bk_printf("hand_gesture_detection_start_task: OpenISPCamera failed (%d)\n", ret);
        goto fail;
    }
    camera_opened = true;
    bk_printf("hand_gesture_detection_start_task: camera opened\n");

    display_open_attempted = true;
    ret = s_video_reator->OpenDisplay();
    if (ret != BK_OK) {
        bk_printf("hand_gesture_detection_start_task: OpenDisplay failed (%d)\n", ret);
        goto fail;
    }
    bk_printf("hand_gesture_detection_start_task: lcd/display opened\n");

    ret = s_video_reator->start();
    if (ret != BK_OK) {
        bk_printf("hand_gesture_detection_start_task: start failed (%d)\n", ret);
        goto fail;
    }
    bk_printf("hand_gesture_detection_start_task: pipeline started, camera preview and model loop running\n");

#if CONFIG_LVGL && CONFIG_TP
    ret = ui_overlay_swipe_back_start(hand_gesture_overlay_back, NULL);
    if (ret != BK_OK) {
        bk_printf("hand_gesture_detection_start_task: overlay swipe start failed (%d)\n", ret);
    }
#endif

    s_hand_gesture_start_thread = NULL;
    rtos_delete_thread(NULL);
    return;

fail:
    bk_printf("hand_gesture_detection_start_task: failed (%d), aborting\n", ret);

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

    if (s_hiwonder_servo != NULL) {
        bk_hiwonder_hand_servo_deinit(s_hiwonder_servo);
        s_hiwonder_servo = NULL;
    }

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = false;
    }

#if CONFIG_LVGL
  if (display_open_attempted) {
    (void)bk_robot_lvgl_resume_display();
  }
#if CONFIG_TP
    ui_overlay_swipe_back_stop();
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

    s_hand_gesture_started = false;
    s_hand_gesture_start_thread = NULL;
    rtos_delete_thread(NULL);
}

static int hand_gesture_detection_start(void)
{
    if (s_hand_gesture_started) {
        bk_printf("hand_gesture_detection_start: already started\n");
        return 0;
    }

    s_hand_gesture_started = true;
    bk_printf("hand_gesture_detection_start: create worker thread\n");

    bk_err_t ret = rtos_create_thread(&s_hand_gesture_start_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      HAND_GESTURE_START_TASK_NAME,
                                      (beken_thread_function_t)hand_gesture_detection_start_task,
                                      HAND_GESTURE_START_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_hand_gesture_started = false;
        s_hand_gesture_start_thread = NULL;
        bk_printf("hand_gesture_detection_start: create worker thread failed ret=%d\n", ret);
        return -1;
    }

    bk_printf("hand_gesture_detection_start: worker thread created\n");
    return 0;
}

static bool hand_gesture_detection_can_start(void)
{
    if (s_hand_gesture_started || s_hand_gesture_start_thread != NULL) {
        bk_printf("hand_gesture_detection_can_start: busy started=%d start_thread=%p\n",
                  (int)s_hand_gesture_started, s_hand_gesture_start_thread);
        return false;
    }
#if CONFIG_LVGL
    if (s_hand_gesture_exit_thread != NULL) {
        bk_printf("hand_gesture_detection_can_start: exit thread busy %p\n",
                  s_hand_gesture_exit_thread);
        return false;
    }
#endif
    return true;
}

extern "C" bool hand_gesture_detection_is_active(void)
{
#if CONFIG_LVGL
    if (s_hand_gesture_exit_thread != NULL) {
        return true;
    }
#endif
    return s_hand_gesture_started;
}

static int hand_gesture_detection_stop(void)
{
    if (!s_hand_gesture_started) {
        return 0;
    }

#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif

    for (int i = 0; i < 50 && s_hand_gesture_start_thread != NULL; i++) {
        rtos_delay_milliseconds(20);
    }

    if (s_hand_gesture_start_thread != NULL) {
        return BK_FAIL;
    }

    if (s_video_reator != NULL) {
        int ret = s_video_reator->stop();
        if (ret != BK_OK) {
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

    if (s_hiwonder_servo != NULL) {
        bk_hiwonder_hand_servo_deinit(s_hiwonder_servo);
        s_hiwonder_servo = NULL;
    }

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = false;
    }

    s_hand_gesture_started = false;
    return 0;
}

#if CONFIG_LVGL
#define HAND_GESTURE_EXIT_TASK_STACK_SIZE   (1024 * 8)
#define HAND_GESTURE_EXIT_TASK_NAME         "hand_gesture_exit"

static void hand_gesture_detection_exit_task(void *arg)
{
    (void)arg;
    bool return_to_edge_ai = s_hand_gesture_return_to_edge_ai;
    s_hand_gesture_return_to_edge_ai = false;

    int ret = hand_gesture_detection_stop();
    if (ret != 0) {
        goto done;
    }

    if (bk_robot_lvgl_resume_display() != BK_OK) {
        goto done;
    }

#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif
    lv_vendor_start();

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

done:
    s_hand_gesture_exit_thread = NULL;
    rtos_delete_thread(NULL);
}
#endif

extern "C" int hand_gesture_detection_exit_to_menu(void)
{
#if CONFIG_LVGL
    ui_overlay_swipe_back_stop();

    if (!s_hand_gesture_started) {
        return 0;
    }

    if (s_hand_gesture_exit_thread != NULL) {
        return 0;
    }

    bk_err_t ret = rtos_create_thread(&s_hand_gesture_exit_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      HAND_GESTURE_EXIT_TASK_NAME,
                                      (beken_thread_function_t)hand_gesture_detection_exit_task,
                                      HAND_GESTURE_EXIT_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_hand_gesture_exit_thread = NULL;
        return -1;
    }
    return 0;
#else
    return hand_gesture_detection_stop();
#endif
}

extern "C" int hand_gesture_init(void)
{
    return 0;
}

extern "C" int hand_gesture_start(void)
{
    bk_printf("hand_gesture_start: called\n");

    if (!hand_gesture_detection_can_start()) {
        bk_printf("hand_gesture_start: cannot start now\n");
        return -1;
    }

    if (hand_gesture_detection_start() != 0) {
        bk_printf("hand_gesture_start: detection start failed\n");
        return -1;
    }

    bk_printf("hand_gesture_start: detection start requested\n");
    return 0;
}

extern "C" void hand_gesture_set_return_to_edge_ai(bool enable)
{
    s_hand_gesture_return_to_edge_ai = enable;
}

extern "C" int hand_gesture_stop(void)
{
    return hand_gesture_detection_exit_to_menu();
}

extern "C" const bk_demo_iface_t g_demo_hand_gesture = {
    "hand_gesture",
    hand_gesture_init,
    hand_gesture_start,
    hand_gesture_stop,
};
