// Copyright 2020-2021 Beken
// Car-following overlay demo: MIPI camera + GPU display path driven by
// the HandGestureDetectionModel NN pipeline. The largest detected hand-gesture
// box drives the Hiwonder chassis/tilt servo directly.
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
#include <common/bk_err.h>
#include <os/os.h>
#include <driver/gpio.h>
}
#include <stdbool.h>
#include "yoloface_detection.h"
#include "demo/car_tracking.h"

#include "app_display.h"
#include "app_camera.h"
#include "app_gpu.h"

#include "AvdkVideoReatorOSD.h"
#include "AvdkDetectionModel.h"
#include "HandGestureDetectionModel.h"
#include "box.h"
#include "bk_hiwonder_car.h"

#if CONFIG_LVGL
extern "C" {
#include "lvgl.h"
#include "lv_vendor.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "ui_overlay_swipe.h"
#include "ui_theme.h"

bk_err_t bk_robot_lvgl_resume_display(void);
int page_edge_ai_enter(void);
}
#endif

static AvdkVideoReatorOSD *s_video_reator = NULL;
static HandGestureDetectionModel *s_model = NULL;

#ifndef CAR_TRACKING_HAND_GESTURE_MODEL_SD_PATH
#define CAR_TRACKING_HAND_GESTURE_MODEL_SD_PATH "1:/tflite/hand_gesture_detection_vela.tflite"
#endif

/* Display canvas (GPU mp output) the OSD boxes are scaled to. Must match the
 * mp_width/mp_height programmed in car_detection_config() below. The NN
 * input (model->getWidth()/getHeight()) is the *source* coordinate space the
 * boxes are reported in, and the ISP secondary path is auto-sized to it by
 * AvdkVideoReatorOSD::OpenISPCamera(). */
#define CAR_TRACKING_DISPLAY_W   400
#define CAR_TRACKING_DISPLAY_H   320

/* Gesture box closed-loop tracking: turn chassis for X, tilt servo2 for Y,
 * then drive chassis forward/backward for overall gesture box size. */
#define CAR_TRACKING_CONTROL        1
#if CAR_TRACKING_CONTROL
#define CAR_TRACKING_SERVO_2_INIT         1500
#define CAR_TRACKING_SERVO_MIN            500
#define CAR_TRACKING_SERVO_MAX            2500
#define CAR_TRACKING_SERVO_DURATION_MS_DEFAULT    8000.0f
#define CAR_TRACKING_SERVO_STOP_DURATION_MS_DEFAULT 20.0f
#define CAR_TRACKING_SERVO_RESET_DURATION_MS_DEFAULT 500.0f
#define CAR_TRACKING_SERVO_DEADZONE_PX_DEFAULT    25.0f
#define CAR_TRACKING_SERVO_START_DEADZONE_PX_DEFAULT 40.0f
#define CAR_TRACKING_SERVO_STOP_SETTLE_MS_DEFAULT 300U
#define CAR_TRACKING_GESTURE_TARGET_SIZE_PX_DEFAULT     160.0f
#define CAR_TRACKING_GESTURE_SIZE_DEADZONE_PX_DEFAULT   20.0f
#define CAR_TRACKING_CHASSIS_SPEED_DEFAULT              150.0f
#define CAR_TRACKING_CHASSIS_TURN_RATE_DEFAULT          0.6f

static int s_car_tracking_servo2 = CAR_TRACKING_SERVO_2_INIT;
static int s_car_tracking_servo2_start = CAR_TRACKING_SERVO_2_INIT;
static int s_car_tracking_servo2_target = CAR_TRACKING_SERVO_2_INIT;
static uint32_t s_car_tracking_servo2_start_ms = 0;
static uint32_t s_car_tracking_servo2_stop_ms = 0;
static uint16_t s_car_tracking_servo2_duration_ms = 0;
static bool s_car_tracking_servo2_running = false;
static int s_car_tracking_chassis_vx_dir = 0;
static int s_car_tracking_chassis_turn_dir = 0;
#endif

/* Same offload rationale as palm tracking: the start-up path (sensor probe,
 * ISP init, C++ object construction, thread creation, ...) is heavy and deep,
 * so it must NOT run on a tiny stack such as the FreeRTOS Timer Service Task.
 * We offload it to a dedicated one-shot worker thread and guard against
 * double-start so repeated key presses cannot create duplicate pipelines. */
#define CAR_TRACKING_START_TASK_STACK_SIZE   (1024 * 8)
#define CAR_TRACKING_START_TASK_NAME         "car_start"

static beken_thread_t s_car_start_thread = NULL;
static volatile bool s_car_started = false;
static volatile bool s_car_return_to_edge_ai = false;

#if CONFIG_LVGL
static beken_thread_t s_car_exit_thread = NULL;
static volatile bool s_car_lvgl_stopped = false;
#endif

#if CONFIG_LVGL && CONFIG_TP
static void car_overlay_back(void *arg)
{
    (void)arg;
    (void)car_detection_exit_to_menu();
}
#endif

#if CAR_TRACKING_CONTROL
static int car_tracking_servo_clamp(int value, int min_value, int max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static int car_tracking_servo_round(float value)
{
    return (int)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

static uint16_t car_tracking_servo_duration_ms(void)
{
    int duration = car_tracking_servo_round(CAR_TRACKING_SERVO_DURATION_MS_DEFAULT);

    duration = car_tracking_servo_clamp(duration, 0, 65535);
    return (uint16_t)duration;
}

static uint16_t car_tracking_servo_stop_duration_ms(void)
{
    int duration = car_tracking_servo_round(CAR_TRACKING_SERVO_STOP_DURATION_MS_DEFAULT);

    duration = car_tracking_servo_clamp(duration, 0, 65535);
    return (uint16_t)duration;
}

static uint16_t car_tracking_servo_reset_duration_ms(void)
{
    int duration = car_tracking_servo_round(CAR_TRACKING_SERVO_RESET_DURATION_MS_DEFAULT);

    duration = car_tracking_servo_clamp(duration, 0, 65535);
    return (uint16_t)duration;
}

static bool car_tracking_servo_stop_settled(void)
{
    if (s_car_tracking_servo2_stop_ms == 0) {
        return true;
    }

    const uint32_t now = (uint32_t)rtos_get_time();
    return (uint32_t)(now - s_car_tracking_servo2_stop_ms) >=
           CAR_TRACKING_SERVO_STOP_SETTLE_MS_DEFAULT;
}

static int car_tracking_servo2_estimate_current(void)
{
    if (!s_car_tracking_servo2_running ||
        s_car_tracking_servo2_duration_ms == 0 ||
        s_car_tracking_servo2_start == s_car_tracking_servo2_target) {
        return s_car_tracking_servo2;
    }

    const uint32_t now = (uint32_t)rtos_get_time();
    const uint32_t elapsed = now - s_car_tracking_servo2_start_ms;
    if (elapsed >= s_car_tracking_servo2_duration_ms) {
        s_car_tracking_servo2 = s_car_tracking_servo2_target;
        s_car_tracking_servo2_running = false;
        return s_car_tracking_servo2;
    }

    const float progress = (float)elapsed / (float)s_car_tracking_servo2_duration_ms;
    const float pulse = (float)s_car_tracking_servo2_start +
                        ((float)s_car_tracking_servo2_target -
                         (float)s_car_tracking_servo2_start) * progress;
    s_car_tracking_servo2 = car_tracking_servo_clamp(car_tracking_servo_round(pulse),
                                                     CAR_TRACKING_SERVO_MIN,
                                                     CAR_TRACKING_SERVO_MAX);
    return s_car_tracking_servo2;
}

static void car_tracking_servo2_set(int target, uint16_t duration_ms)
{
    const int start = car_tracking_servo2_estimate_current();

    target = car_tracking_servo_clamp(target,
                                      CAR_TRACKING_SERVO_MIN,
                                      CAR_TRACKING_SERVO_MAX);
    if (target == start && !s_car_tracking_servo2_running) {
        return;
    }

    bk_err_t ret = bk_hiwonder_car_set_pwm_servo(2,
                                                 (uint16_t)target,
                                                 duration_ms);
    if (ret == BK_OK) {
        s_car_tracking_servo2 = start;
        s_car_tracking_servo2_start = start;
        s_car_tracking_servo2_target = target;
        s_car_tracking_servo2_start_ms = (uint32_t)rtos_get_time();
        s_car_tracking_servo2_duration_ms = duration_ms;
        s_car_tracking_servo2_running = (target != start) &&
                                        (duration_ms > car_tracking_servo_stop_duration_ms());
        if (s_car_tracking_servo2_running) {
            s_car_tracking_servo2_stop_ms = 0;
        }
    } else {
        bk_printf("car_tracking_servo2_set: target=%d duration=%u failed ret=%d\n",
                  target, duration_ms, ret);
    }
}

static void car_tracking_servo2_stop(void)
{
    if (!s_car_tracking_servo2_running) {
        return;
    }

    const int current = car_tracking_servo2_estimate_current();
    bk_printf("car_tracking: tilt_y stop current=%d target=%d\n",
              current, s_car_tracking_servo2_target);
    car_tracking_servo2_set(current, car_tracking_servo_stop_duration_ms());
    s_car_tracking_servo2_stop_ms = (uint32_t)rtos_get_time();
}

static void car_tracking_servo_reset(void)
{
    s_car_tracking_servo2 = CAR_TRACKING_SERVO_2_INIT;
    s_car_tracking_servo2_start = CAR_TRACKING_SERVO_2_INIT;
    s_car_tracking_servo2_target = CAR_TRACKING_SERVO_2_INIT;
    s_car_tracking_servo2_start_ms = 0;
    s_car_tracking_servo2_stop_ms = 0;
    s_car_tracking_servo2_duration_ms = 0;
    s_car_tracking_servo2_running = false;
    s_car_tracking_chassis_vx_dir = 0;
    s_car_tracking_chassis_turn_dir = 0;
    bk_printf("car_tracking_servo: init record servo2=%d\n",
              s_car_tracking_servo2);
}

static void car_tracking_servo2_reset_position(void)
{
    const uint16_t duration_ms = car_tracking_servo_reset_duration_ms();
    bk_err_t ret = bk_hiwonder_car_set_pwm_servo(2,
                                                 CAR_TRACKING_SERVO_2_INIT,
                                                 duration_ms);
    if (ret == BK_OK) {
        s_car_tracking_servo2 = CAR_TRACKING_SERVO_2_INIT;
        s_car_tracking_servo2_start = CAR_TRACKING_SERVO_2_INIT;
        s_car_tracking_servo2_target = CAR_TRACKING_SERVO_2_INIT;
        s_car_tracking_servo2_start_ms = (uint32_t)rtos_get_time();
        s_car_tracking_servo2_stop_ms = 0;
        s_car_tracking_servo2_duration_ms = duration_ms;
        s_car_tracking_servo2_running = false;
        bk_printf("car_tracking_servo: reset servo2=%d duration=%u\n",
                  CAR_TRACKING_SERVO_2_INIT, duration_ms);
    } else {
        bk_printf("car_tracking_servo: reset servo2 failed ret=%d\n", ret);
    }
}

static void car_tracking_chassis_set(int vx_dir, int turn_dir)
{
    if (vx_dir > 0) {
        vx_dir = 1;
    } else if (vx_dir < 0) {
        vx_dir = -1;
    }
    if (turn_dir > 0) {
        turn_dir = 1;
    } else if (turn_dir < 0) {
        turn_dir = -1;
    }
    if (turn_dir != 0) {
        vx_dir = 0;
    }

    if (vx_dir == s_car_tracking_chassis_vx_dir &&
        turn_dir == s_car_tracking_chassis_turn_dir) {
        return;
    }

    const float vx = (float)vx_dir * CAR_TRACKING_CHASSIS_SPEED_DEFAULT;
    const float rate = (float)turn_dir * CAR_TRACKING_CHASSIS_TURN_RATE_DEFAULT;
    bk_err_t ret = bk_hiwonder_car_run(vx, rate);
    if (ret == BK_OK) {
        s_car_tracking_chassis_vx_dir = vx_dir;
        s_car_tracking_chassis_turn_dir = turn_dir;
        bk_printf("car_tracking_chassis: run %.1f %.1f\n", vx, rate);
    } else {
        bk_printf("car_tracking_chassis: run %.1f %.1f failed ret=%d\n",
                  vx, rate, ret);
    }
}

static void car_tracking_chassis_stop(void)
{
    car_tracking_chassis_set(0, 0);
}

static void car_tracking_chassis_force_stop(void)
{
    bk_err_t ret = bk_hiwonder_car_run(0.0f, 0.0f);
    if (ret == BK_OK) {
        s_car_tracking_chassis_vx_dir = 0;
        s_car_tracking_chassis_turn_dir = 0;
        bk_printf("car_tracking_chassis: force stop\n");
    } else {
        bk_printf("car_tracking_chassis: force stop failed ret=%d\n", ret);
    }
}

static int car_tracking_turn_dir_from_dx(float dx)
{
    if (dx > CAR_TRACKING_SERVO_DEADZONE_PX_DEFAULT) {
        return 1;
    }
    if (dx < -CAR_TRACKING_SERVO_DEADZONE_PX_DEFAULT) {
        return -1;
    }
    return 0;
}

static int car_tracking_vx_dir_from_size(float gesture_size)
{
    const float err_size = CAR_TRACKING_GESTURE_TARGET_SIZE_PX_DEFAULT - gesture_size;

    if (err_size > CAR_TRACKING_GESTURE_SIZE_DEADZONE_PX_DEFAULT) {
        return 1;
    }
    if (err_size < -CAR_TRACKING_GESTURE_SIZE_DEADZONE_PX_DEFAULT) {
        return -1;
    }
    return 0;
}

static bool car_tracking_update_geometry(const Box *box,
                                          float *gesture_center_x,
                                          float *gesture_center_y,
                                          float *dx,
                                          float *dy,
                                          float *gesture_size)
{
    if (box == NULL || s_model == NULL) {
        return false;
    }

    const float src_w = (float)s_model->getWidth();
    const float src_h = (float)s_model->getHeight();
    if (src_w <= 0.0f || src_h <= 0.0f) {
        return false;
    }

    const float screen_w = (float)CAR_TRACKING_DISPLAY_W;
    const float screen_h = (float)CAR_TRACKING_DISPLAY_H;

    const float gesture_w = box->w * screen_w / src_w;
    const float gesture_h = box->h * screen_h / src_h;

    *gesture_center_x = (box->x + box->w * 0.5f) * screen_w / src_w;
    *gesture_center_y = (box->y + box->h * 0.5f) * screen_h / src_h;
    *dx = *gesture_center_x - screen_w * 0.5f;
    *dy = *gesture_center_y - screen_h * 0.5f;
    *gesture_size = (gesture_w + gesture_h) * 0.5f;
    return true;
}

static void car_tracking_update(const Box *box)
{
    float gesture_center_x = 0.0f;
    float gesture_center_y = 0.0f;
    float dx = 0.0f;
    float dy = 0.0f;
    float gesture_size = 0.0f;

    if (!car_tracking_update_geometry(box, &gesture_center_x, &gesture_center_y,
                                       &dx, &dy, &gesture_size)) {
        car_tracking_chassis_stop();
        car_tracking_servo2_stop();
        return;
    }

    car_tracking_servo2_estimate_current();

    int servo2_target = 0;
    if (dy > CAR_TRACKING_SERVO_DEADZONE_PX_DEFAULT) {
        servo2_target = CAR_TRACKING_SERVO_MAX;
    } else if (dy < -CAR_TRACKING_SERVO_DEADZONE_PX_DEFAULT) {
        servo2_target = CAR_TRACKING_SERVO_MIN;
    }

    if (servo2_target == 0) {
        car_tracking_servo2_stop();
    } else if (s_car_tracking_servo2_running &&
               servo2_target != s_car_tracking_servo2_target) {
        /* Crossing the center can make dy flip sign before the stop deadzone
         * catches it. Treat that as "arrived" first instead of immediately
         * commanding the opposite endpoint, which causes head shaking. */
        car_tracking_servo2_stop();
    } else if (!s_car_tracking_servo2_running &&
               (!car_tracking_servo_stop_settled() ||
                (dy < CAR_TRACKING_SERVO_START_DEADZONE_PX_DEFAULT &&
                 dy > -CAR_TRACKING_SERVO_START_DEADZONE_PX_DEFAULT))) {
        /* After a forced stop, require the detection to settle and move well
         * outside the center band before starting another long endpoint move. */
    } else if (!s_car_tracking_servo2_running ||
               servo2_target != s_car_tracking_servo2_target) {
        bk_printf("car_tracking: tilt_y endpoint center=(%.1f,%.1f) err=(%.1f,%.1f) "
                  "current=%d target=%d duration=%u\n",
                  gesture_center_x, gesture_center_y, dx, dy,
                  s_car_tracking_servo2, servo2_target,
                  car_tracking_servo_duration_ms());
        car_tracking_servo2_set(servo2_target, car_tracking_servo_duration_ms());
    }

    const int turn_dir = car_tracking_turn_dir_from_dx(dx);
    const int vx_dir = car_tracking_vx_dir_from_size(gesture_size);
    const float err_size = CAR_TRACKING_GESTURE_TARGET_SIZE_PX_DEFAULT - gesture_size;

    if (s_car_tracking_chassis_turn_dir != 0) {
        if (turn_dir != 0) {
            car_tracking_chassis_set(0, turn_dir);
            bk_printf("car_tracking: turn_x center=(%.1f,%.1f) err=(%.1f,%.1f) dir=%s\n",
                      gesture_center_x, gesture_center_y, dx, dy,
                      (turn_dir > 0) ? "right" : "left");
            return;
        }
        car_tracking_chassis_stop();
        return;
    }

    if (s_car_tracking_chassis_vx_dir != 0) {
        if (vx_dir != 0) {
            car_tracking_chassis_set(vx_dir, 0);
            bk_printf("car_tracking: drive_size center=(%.1f,%.1f) err=(%.1f,%.1f) "
                      "gesture_size=%.1f target=%.1f err_size=%.1f vx_dir=%d\n",
                      gesture_center_x, gesture_center_y, dx, dy, gesture_size,
                      CAR_TRACKING_GESTURE_TARGET_SIZE_PX_DEFAULT, err_size,
                      s_car_tracking_chassis_vx_dir);
            return;
        }
        car_tracking_chassis_stop();
        return;
    }

    if (turn_dir > 0) {
        car_tracking_chassis_set(0, turn_dir);
        bk_printf("car_tracking: turn_x center=(%.1f,%.1f) err=(%.1f,%.1f) dir=right\n",
                  gesture_center_x, gesture_center_y, dx, dy);
        return;
    }
    if (turn_dir < 0) {
        car_tracking_chassis_set(0, turn_dir);
        bk_printf("car_tracking: turn_x center=(%.1f,%.1f) err=(%.1f,%.1f) dir=left\n",
                  gesture_center_x, gesture_center_y, dx, dy);
        return;
    }

    if (vx_dir != 0) {
        car_tracking_chassis_set(vx_dir, 0);
    } else {
        car_tracking_chassis_stop();
    }

    bk_printf("car_tracking: drive_size center=(%.1f,%.1f) err=(%.1f,%.1f) "
              "gesture_size=%.1f target=%.1f err_size=%.1f vx_dir=%d\n",
              gesture_center_x, gesture_center_y, dx, dy, gesture_size,
              CAR_TRACKING_GESTURE_TARGET_SIZE_PX_DEFAULT, err_size,
              s_car_tracking_chassis_vx_dir);
}

#endif

/* Draw detected gesture boxes and let the largest one drive the chassis.
 *
 * src = model input size, dst = display canvas size. The OSD
 * holds the previous boxes until the next successful frame overwrites them, so
 * on an empty frame we explicitly clear the path. */
static void car_detection_box_cb(Box *boxes, int count)
{
    if (boxes == NULL || count <= 0) {
#if CAR_TRACKING_CONTROL
        car_tracking_chassis_stop();
        car_tracking_servo2_stop();
#endif
        box_detection_path_clear();
        return;
    }

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

    bk_printf("car_detection_box_cb: count=%d target=%d score=%.3f xywh=(%.2f,%.2f,%.2fx%.2f)\n",
              count, target, boxes[target].score,
              boxes[target].x, boxes[target].y, boxes[target].w, boxes[target].h);

#if CAR_TRACKING_CONTROL
    car_tracking_update(&boxes[target]);
#endif

    box_detection_path_build(boxes, count, count, 0,
                             s_model->getWidth(), s_model->getHeight(),
                             CAR_TRACKING_DISPLAY_W, CAR_TRACKING_DISPLAY_H);
}

static void car_detection_config(void)
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
    camera_board.mipi.hmirror = 1;
    camera_board.mipi.vflip = 0;
    camera_board.isp.mp_enable = true;
    camera_board.isp.mp_flexa = true;
    camera_board.isp.mp_width = CAR_TRACKING_DISPLAY_W;
    camera_board.isp.mp_height = CAR_TRACKING_DISPLAY_H;
    camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
    camera_board.isp.sp_enable = false;
    camera_board.isp.sp_flexa = false;

    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = 270;
    gpu_board.flexa.tess_width = CAR_TRACKING_DISPLAY_W / 2;
    gpu_board.flexa.tess_height = CAR_TRACKING_DISPLAY_H / 2;
    gpu_board.flexa.src_width = CAR_TRACKING_DISPLAY_W;
    gpu_board.flexa.src_height = CAR_TRACKING_DISPLAY_H;
    gpu_board.flexa.dst_width = CAR_TRACKING_DISPLAY_W;
    gpu_board.flexa.dst_height = CAR_TRACKING_DISPLAY_H;
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    gpu_board.flexa.scale = false;

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

static void car_detection_start_task(void *arg)
{
    (void)arg;
    int ret = BK_OK;
    bool camera_opened = false;
    bool display_open_attempted = false;
#if CONFIG_LVGL
    bool model_init_failed = false;
#endif

#if CONFIG_LVGL
    lv_vendor_stop();
    s_car_lvgl_stopped = true;
#endif

    car_detection_config();

    s_model = new HandGestureDetectionModel();
    if (s_model == NULL) {
        bk_printf("car_detection_start_task: model alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }
    s_model->setBoxDetectionCallback(car_detection_box_cb);
    s_model->setModelFilePath(CAR_TRACKING_HAND_GESTURE_MODEL_SD_PATH);
#if CAR_TRACKING_CONTROL
    car_tracking_servo_reset();
    car_tracking_servo2_reset_position();
#endif

    s_video_reator = new AvdkVideoReatorOSD(s_model);
    if (s_video_reator == NULL) {
        bk_printf("car_detection_start_task: video reator alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }

    ret = s_video_reator->init();
    if (ret != BK_OK) {
        bk_printf("car_detection_start_task: init failed (%d)\n", ret);
#if CONFIG_LVGL
        if (ret == -1) {
            model_init_failed = true;
        }
#endif
        goto fail;
    }

    ret = s_video_reator->OpenISPCamera();
    if (ret != BK_OK) {
        bk_printf("car_detection_start_task: OpenISPCamera failed (%d)\n", ret);
        goto fail;
    }
    camera_opened = true;

    display_open_attempted = true;
    ret = s_video_reator->OpenDisplay();
    if (ret != BK_OK) {
        bk_printf("car_detection_start_task: OpenDisplay failed (%d)\n", ret);
        goto fail;
    }

    ret = s_video_reator->start();
    if (ret != BK_OK) {
        bk_printf("car_detection_start_task: start failed (%d)\n", ret);
        goto fail;
    }

#if CONFIG_LVGL && CONFIG_TP
    ret = ui_overlay_swipe_back_start(car_overlay_back, NULL);
    if (ret != BK_OK) {
        bk_printf("car_detection_start_task: overlay swipe start failed (%d)\n", ret);
    }
#endif

    bk_printf("car_detection_start_task: done, exiting worker\n");

    s_car_start_thread = NULL;
    rtos_delete_thread(NULL);
    return;

fail:
    bk_printf("car_detection_start_task: failed (%d), aborting\n", ret);

#if CAR_TRACKING_CONTROL
    car_tracking_chassis_stop();
#endif

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
    if (s_car_lvgl_stopped && display_open_attempted) {
        if (bk_robot_lvgl_resume_display() != BK_OK) {
            bk_printf("car_detection_start_task: resume display failed\n");
        }
    }
#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif
    if (s_car_lvgl_stopped) {
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
    s_car_lvgl_stopped = false;
    if (model_init_failed) {
        ui_theme_create_popup("Model file not exist!");
    }
#endif

    s_car_started = false;
    s_car_start_thread = NULL;
    rtos_delete_thread(NULL);
}

static int car_detection_start(void)
{
    if (s_car_started) {
        bk_printf("car_detection_start: already started, ignore\n");
        return 0;
    }

    s_car_started = true;

    bk_err_t ret = rtos_create_thread(&s_car_start_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      CAR_TRACKING_START_TASK_NAME,
                                      (beken_thread_function_t)car_detection_start_task,
                                      CAR_TRACKING_START_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_car_started = false;
        s_car_start_thread = NULL;
        bk_printf("car_detection_start: create thread failed, ret=%d\n", ret);
        return -1;
    }

    return 0;
}

static bool car_detection_can_start(void)
{
    /* Pipeline currently active (live or mid-startup). */
    if (s_car_started || s_car_start_thread != NULL) {
        return false;
    }
#if CONFIG_LVGL
    /* Exit task from the previous session is still tearing things down;
     * starting now would race the teardown on shared statics. */
    if (s_car_exit_thread != NULL) {
        return false;
    }
#endif
    return true;
}

extern "C" bool car_detection_is_active(void)
{
#if CONFIG_LVGL
    if (s_car_exit_thread != NULL) {
        return true;
    }
#endif
    return s_car_started;
}

static int car_detection_stop(void)
{
    if (!s_car_started) {
        return 0;
    }

#if CAR_TRACKING_CONTROL
    car_tracking_chassis_stop();
#endif

#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif

    for (int i = 0; i < 50 && s_car_start_thread != NULL; i++) {
        rtos_delay_milliseconds(20);
    }

    if (s_car_start_thread != NULL) {
        bk_printf("car_detection_stop: start task still running, abort stop\n");
        return BK_FAIL;
    }

#if CONFIG_LVGL
    if (!s_car_lvgl_stopped) {
        lv_vendor_stop();
        s_car_lvgl_stopped = true;
    }
#endif

    if (s_video_reator != NULL) {
        int ret = s_video_reator->stop();
        if (ret != BK_OK) {
            bk_printf("car_detection_stop: video stop failed (%d), abort stop\n", ret);
            return ret;
        }
    }

#if CAR_TRACKING_CONTROL
    car_tracking_chassis_force_stop();
    car_tracking_servo2_reset_position();
#endif

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

    s_car_started = false;
    return 0;
}

#if CONFIG_LVGL
#define CAR_TRACKING_EXIT_TASK_STACK_SIZE   (1024 * 8)
#define CAR_TRACKING_EXIT_TASK_NAME         "car_exit"

static void car_detection_exit_task(void *arg)
{
    (void)arg;
    bool return_to_edge_ai = s_car_return_to_edge_ai;
    bool lvgl_stopped = false;
    s_car_return_to_edge_ai = false;

    int ret = car_detection_stop();
    if (ret != 0) {
        bk_printf("car_detection_exit_task: stop failed (%d)\n", ret);
        goto done;
    }

    lvgl_stopped = s_car_lvgl_stopped;
    if (lvgl_stopped && bk_robot_lvgl_resume_display() != BK_OK) {
        bk_printf("car_detection_exit_task: resume display failed\n");
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
    s_car_lvgl_stopped = false;

done:
    s_car_exit_thread = NULL;
    rtos_delete_thread(NULL);
}
#endif /* CONFIG_LVGL */

extern "C" int car_detection_exit_to_menu(void)
{
#if CONFIG_LVGL
    ui_overlay_swipe_back_stop();

    /* Idempotent fast path: nothing to exit. */
    if (!s_car_started) {
        return 0;
    }

    if (s_car_exit_thread != NULL) {
        return 0;
    }

    bk_err_t ret = rtos_create_thread(&s_car_exit_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      CAR_TRACKING_EXIT_TASK_NAME,
                                      (beken_thread_function_t)car_detection_exit_task,
                                      CAR_TRACKING_EXIT_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_car_exit_thread = NULL;
        bk_printf("car_detection_exit_to_menu: create thread failed, ret=%d\n", ret);
        return -1;
    }
    return 0;
#else
    return car_detection_stop();
#endif
}

/* ----------------------------------------------------------------------
 * bk_demo_iface_t wiring.
 *
 * start() pauses LVGL before spawning the NN pipeline worker so the GPU
 * display path can take over the framebuffer; stop() routes through
 * car_detection_exit_to_menu() which tears down and navigates back.
 * -------------------------------------------------------------------- */

extern "C" int car_tracking_init(void)
{
    return 0;
}

extern "C" int car_tracking_start(void)
{
    if (!car_detection_can_start()) {
        bk_printf("car_tracking_start: busy (start/exit in progress), ignore\n");
        return -1;
    }

    if (car_detection_start() != 0) {
        bk_printf("car_detection_start trigger failed\r\n");
        return -1;
    }

    return 0;
}

extern "C" void car_tracking_set_return_to_edge_ai(bool enable)
{
    s_car_return_to_edge_ai = enable;
}

extern "C" int car_tracking_stop(void)
{
    return car_detection_exit_to_menu();
}

extern "C" const bk_demo_iface_t g_demo_car_tracking = {
    "car_tracking",
    car_tracking_init,
    car_tracking_start,
    car_tracking_stop,
};
