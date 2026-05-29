// Copyright 2020-2021 Beken
// beken_robot media device controls -- panel/DPU + MIPI camera + GPU + H.264 encoder.
// See media_devices.h for the public API contract.

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

#include "app_display.h"
#include "app_camera.h"
#include "app_gpu.h"

#include "AvdkVideoReatorOSD.h"
#include "AvdkDetectionModel.h"
#include "PalmDetectionModel.h"
#include "box.h"
#include "bk_aimi_servo.h"

static AvdkVideoReatorOSD *video_reator = NULL;
static PalmDetectionModel *model = NULL;

/* Hardware binding for the palm-tracking servo on this board.
 * The bk_servo component is now HW-agnostic; the PWM channel and the GPIO
 * pin are chosen here at the application layer. */
#define PALM_SERVO_PWM_CHAN_H   PWM_ID_0
#define PALM_SERVO_GPIO_ID_H    GPIO_69

#define PALM_SERVO_PWM_CHAN_V   PWM_ID_1
#define PALM_SERVO_GPIO_ID_V    GPIO_60

/* The whole start-up path (sensor probe, ISP init, C++ object construction,
 * thread creation, ...) is heavy and deep. It must NOT run on a tiny stack
 * such as the FreeRTOS Timer Service Task (the path that delivers ADC-key
 * events), otherwise the timer task stack overflows and corrupts memory.
 *
 * So we offload the start-up to a dedicated one-shot worker thread, and
 * guard against double-start so that repeated key presses cannot create
 * duplicate models / cameras. */
#define PALM_START_TASK_STACK_SIZE   (1024 * 16)
#define PALM_START_TASK_NAME         "palm_start"

static beken_thread_t s_palm_start_thread = NULL;
static volatile bool s_palm_started = false;

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

    /* Drive servo from the chosen box's center: top-left (x,y) + half size. */
    bk_aimi_palm_track_servo(PALM_SERVO_PWM_CHAN_H,
                             boxes[target].x + boxes[target].w * 0.5f,
                             boxes[target].y + boxes[target].h * 0.5f,
                             model->getWidth(), model->getHeight());
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

    plam_detection_config();

    bk_aimi_servo_init(PALM_SERVO_PWM_CHAN_H, PALM_SERVO_GPIO_ID_H);
    bk_aimi_servo_set_angle(PALM_SERVO_PWM_CHAN_H, SERVO_CENTER_ANGLE);

    model = new PalmDetectionModel();
    model->setBoxDetectionCallback(detection_box_cb);
    video_reator = new AvdkVideoReatorOSD(model);
    video_reator->init();
    video_reator->OpenISPCamera();
    video_reator->OpenDisplay();
    video_reator->start();

    bk_printf("palm_detection_start_task: done, exiting worker\n");

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