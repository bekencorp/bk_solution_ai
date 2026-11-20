// Copyright 2020-2021 Beken
// Pet (cat/dog) detection overlay demo: MIPI camera + GPU display path
// driven by the PetDetectionModel NN pipeline.

extern "C" {
#include <os/os.h>
#include <driver/gpio.h>
}
#include <stdbool.h>
#include "pet_detection.h"
#include "demo/pet_detection.h"

#include "app_display.h"
#include "app_camera.h"
#include "app_gpu.h"

#include "AvdkVideoReatorOSD.h"
#include "AvdkDetectionModel.h"
#include "PetDetectionModel.h"
#include "box.h"

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
static PetDetectionModel *s_model = NULL;

#ifndef PET_DETECTION_MODEL_SD_PATH
#define PET_DETECTION_MODEL_SD_PATH "1:/tflite/pet_detection_vela.tflite"
#endif

#define PET_DETECTION_DISPLAY_W   400
#define PET_DETECTION_DISPLAY_H   320

#define PET_DETECTION_START_TASK_STACK_SIZE   (1024 * 8)
#define PET_DETECTION_START_TASK_NAME         "pet_detection_start"

static beken_thread_t s_pet_detection_start_thread = NULL;
static volatile bool s_pet_detection_started = false;
static volatile bool s_pet_detection_return_to_edge_ai = false;

/* Defined at the bottom of this file; used by the overlay back gesture. */
extern "C" int pet_detection_exit_to_menu(void);

#if CONFIG_LVGL
static beken_thread_t s_pet_detection_exit_thread = NULL;
#endif

#if CONFIG_LVGL && CONFIG_TP
static void pet_detection_overlay_back(void *arg)
{
    (void)arg;
    (void)pet_detection_exit_to_menu();
}
#endif

static void pet_detection_box_cb(Box *boxes, int count)
{
    if (boxes == NULL || count <= 0) {
        box_detection_path_clear();
        return;
    }

    bk_printf("pet_detection_box_cb: count=%d top score=%.3f xywh=(%.2f,%.2f,%.2fx%.2f)\n",
              count, boxes[0].score,
              boxes[0].x, boxes[0].y, boxes[0].w, boxes[0].h);

    box_detection_path_build(boxes, count, count, 0,
                             s_model->getWidth(), s_model->getHeight(),
                             PET_DETECTION_DISPLAY_W, PET_DETECTION_DISPLAY_H);
}

static void pet_detection_config(void)
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
    camera_board.isp.mp_width = PET_DETECTION_DISPLAY_W;
    camera_board.isp.mp_height = PET_DETECTION_DISPLAY_H;
    camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
    camera_board.isp.sp_enable = false;
    camera_board.isp.sp_flexa = false;

    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = 270;
    gpu_board.flexa.src_width = PET_DETECTION_DISPLAY_W;
    gpu_board.flexa.src_height = PET_DETECTION_DISPLAY_H;
    gpu_board.flexa.dst_width = PET_DETECTION_DISPLAY_W;
    gpu_board.flexa.dst_height = PET_DETECTION_DISPLAY_H;
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    gpu_board.flexa.scale = false;
    gpu_board.flexa.tess_width = PET_DETECTION_DISPLAY_W / 2;
    gpu_board.flexa.tess_height = PET_DETECTION_DISPLAY_H / 2;

    app_camera_board_config_set(&camera_board);
    app_gpu_board_config_set(&gpu_board);

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = true;
        display_config->dpu_video.decompress = true;
        display_config->dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;
    }
}

static void pet_detection_start_task(void *arg)
{
    (void)arg;
    int ret = BK_OK;
    bool camera_opened = false;
    bool display_open_attempted = false;
#if CONFIG_LVGL
    bool model_init_failed = false;
#endif

    bk_printf("pet_detection_start_task: enter\n");

#if CONFIG_LVGL
    lv_vendor_stop();
    bk_printf("pet_detection_start_task: lvgl stopped\n");
#endif

    pet_detection_config();
    bk_printf("pet_detection_start_task: camera/gpu/display config set\n");

    s_model = new PetDetectionModel();
    if (s_model == NULL) {
        bk_printf("pet_detection_start_task: model alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }
    s_model->setBoxDetectionCallback(pet_detection_box_cb);
    s_model->setModelFilePath(PET_DETECTION_MODEL_SD_PATH);
    bk_printf("pet_detection_start_task: model created, path=%s\n",
              PET_DETECTION_MODEL_SD_PATH);

    s_video_reator = new AvdkVideoReatorOSD(s_model);
    if (s_video_reator == NULL) {
        bk_printf("pet_detection_start_task: video reator alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }

    ret = s_video_reator->init();
    if (ret != BK_OK) {
        bk_printf("pet_detection_start_task: init failed (%d)\n", ret);
#if CONFIG_LVGL
        if (ret == -1) {
            model_init_failed = true;
        }
#endif
        goto fail;
    }
    bk_printf("pet_detection_start_task: video reator init ok\n");

    ret = s_video_reator->OpenISPCamera();
    if (ret != BK_OK) {
        bk_printf("pet_detection_start_task: OpenISPCamera failed (%d)\n", ret);
        goto fail;
    }
    camera_opened = true;
    bk_printf("pet_detection_start_task: camera opened\n");

    display_open_attempted = true;
    ret = s_video_reator->OpenDisplay();
    if (ret != BK_OK) {
        bk_printf("pet_detection_start_task: OpenDisplay failed (%d)\n", ret);
        goto fail;
    }
    bk_printf("pet_detection_start_task: lcd/display opened\n");

    ret = s_video_reator->start();
    if (ret != BK_OK) {
        bk_printf("pet_detection_start_task: start failed (%d)\n", ret);
        goto fail;
    }
    bk_printf("pet_detection_start_task: pipeline started, camera preview and model loop running\n");

#if CONFIG_LVGL && CONFIG_TP
    ret = ui_overlay_swipe_back_start(pet_detection_overlay_back, NULL);
    if (ret != BK_OK) {
        bk_printf("pet_detection_start_task: overlay swipe start failed (%d)\n", ret);
    }
#endif

    s_pet_detection_start_thread = NULL;
    rtos_delete_thread(NULL);
    return;

fail:
    bk_printf("pet_detection_start_task: failed (%d), aborting\n", ret);

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
    if (model_init_failed) {
        ui_theme_create_popup("Model file not exist!");
    }
#endif

    s_pet_detection_started = false;
    s_pet_detection_start_thread = NULL;
    rtos_delete_thread(NULL);
}

static int pet_detection_worker_start(void)
{
    if (s_pet_detection_started) {
        bk_printf("pet_detection_start: already started\n");
        return 0;
    }

    s_pet_detection_started = true;
    bk_printf("pet_detection_start: create worker thread\n");

    bk_err_t ret = rtos_create_thread(&s_pet_detection_start_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      PET_DETECTION_START_TASK_NAME,
                                      (beken_thread_function_t)pet_detection_start_task,
                                      PET_DETECTION_START_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_pet_detection_started = false;
        s_pet_detection_start_thread = NULL;
        bk_printf("pet_detection_start: create worker thread failed ret=%d\n", ret);
        return -1;
    }

    bk_printf("pet_detection_start: worker thread created\n");
    return 0;
}

static bool pet_detection_can_start(void)
{
    if (s_pet_detection_started || s_pet_detection_start_thread != NULL) {
        bk_printf("pet_detection_can_start: busy started=%d start_thread=%p\n",
                  (int)s_pet_detection_started, s_pet_detection_start_thread);
        return false;
    }
#if CONFIG_LVGL
    if (s_pet_detection_exit_thread != NULL) {
        bk_printf("pet_detection_can_start: exit thread busy %p\n",
                  s_pet_detection_exit_thread);
        return false;
    }
#endif
    return true;
}

extern "C" bool pet_detection_is_active(void)
{
#if CONFIG_LVGL
    if (s_pet_detection_exit_thread != NULL) {
        return true;
    }
#endif
    return s_pet_detection_started;
}

static int pet_detection_worker_stop(void)
{
    if (!s_pet_detection_started) {
        return 0;
    }

#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif

    for (int i = 0; i < 50 && s_pet_detection_start_thread != NULL; i++) {
        rtos_delay_milliseconds(20);
    }

    if (s_pet_detection_start_thread != NULL) {
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

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = false;
    }

    s_pet_detection_started = false;
    return 0;
}

#if CONFIG_LVGL
#define PET_DETECTION_EXIT_TASK_STACK_SIZE   (1024 * 8)
#define PET_DETECTION_EXIT_TASK_NAME         "pet_detection_exit"

static void pet_detection_exit_task(void *arg)
{
    (void)arg;
    bool return_to_edge_ai = s_pet_detection_return_to_edge_ai;
    s_pet_detection_return_to_edge_ai = false;

    int ret = pet_detection_worker_stop();
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
    s_pet_detection_exit_thread = NULL;
    rtos_delete_thread(NULL);
}
#endif

extern "C" int pet_detection_exit_to_menu(void)
{
#if CONFIG_LVGL
    ui_overlay_swipe_back_stop();

    if (!s_pet_detection_started) {
        return 0;
    }

    if (s_pet_detection_exit_thread != NULL) {
        return 0;
    }

    bk_err_t ret = rtos_create_thread(&s_pet_detection_exit_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      PET_DETECTION_EXIT_TASK_NAME,
                                      (beken_thread_function_t)pet_detection_exit_task,
                                      PET_DETECTION_EXIT_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_pet_detection_exit_thread = NULL;
        return -1;
    }
    return 0;
#else
    return pet_detection_worker_stop();
#endif
}

/* ======================================================================
 * Demo entry points
 * ====================================================================== */
extern "C" int pet_detection_init(void)
{
    return 0;
}

extern "C" void pet_detection_set_return_to_edge_ai(bool enable)
{
    s_pet_detection_return_to_edge_ai = enable;
}

extern "C" int pet_detection_start(void)
{
    bk_printf("pet_detection_start: called\n");

    if (!pet_detection_can_start()) {
        bk_printf("pet_detection_start: cannot start now\n");
        return -1;
    }

    if (pet_detection_worker_start() != 0) {
        bk_printf("pet_detection_start: detection start failed\n");
        return -1;
    }

    bk_printf("pet_detection_start: detection start requested\n");
    return 0;
}

extern "C" int pet_detection_stop(void)
{
    return pet_detection_exit_to_menu();
}

extern "C" const bk_demo_iface_t g_demo_pet_detection = {
    "pet_detection",
    pet_detection_init,
    pet_detection_start,
    pet_detection_stop,
};
