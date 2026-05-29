// Copyright 2025 Beken
// camera_preview.c -- implementation of camera_preview.h.
//
// Design:
//   * Callers (page_3 nav) run on Tmr Svc (~2 KB stack). DSI/DPU bring-up on
//     that task causes UsageFault stack overflow.
//   * Follow palm_detection: callers only trigger; heavy work runs on 16 KB
//     one-shot workers (lv_vendor, panel, camera, GPU).
//   * Simple state machine prevents re-entry on rapid key presses.

#include "camera_preview.h"

#include <components/log.h>
#include <common/avdk_pixel_types.h>
#include <components/bk_display.h>
#include <components/bk_frame_buffer.h>
#include <os/os.h>
#include <os/mem.h>

#include "lvgl.h"
#include "media_devices.h"
#include "lv_vendor.h"

#define TAG "cam_preview"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Panel object from bk_peripheral; extern here to avoid coupling ap_main macros. */
extern const bk_display_dsi_panel_t lcd_device_jd9855_mipi_360x390;
#define PREVIEW_MIPI_PANEL (&lcd_device_jd9855_mipi_360x390)

/* Sensor mode must match gc2053_format_array (csi_gc2053.c). SP 1280x720 needs
 * sensor 1280x720@20fps (720p supports 30/25/20, not 15). MP still 400x368. */
#define PREVIEW_SENSOR_W    1280
#define PREVIEW_SENSOR_H    720
#define PREVIEW_SENSOR_FPS  20
#define PREVIEW_ISP_W       400
#define PREVIEW_ISP_H       368
/* 270 deg rotate: GC2053 mount vs jd9855 scan (same as palm_detection). */
#define PREVIEW_GPU_ROTATE  270

/* SP 1280x720 NV12 (1:1 with sensor). NV12 from UNCODED slab (~4 MB total
 * with SP rings + photo copy); PSRAM_HEAP (920 KB) cannot hold 720p NV12. */
#define PREVIEW_SP_W           1280
#define PREVIEW_SP_H           720
#define PREVIEW_SP_NV12_BYTES  ((uint32_t)PREVIEW_SP_W * PREVIEW_SP_H * 3 / 2)

/* SP read timeout: cam_thread may sit in 1s delay on MP flexa chnl before
 * serving SP; first read can take ~1s. Display freeze is still fast via GPU. */
#define PREVIEW_SP_READ_TIMEOUT_MS  1500

/* JPEG: quality 0..10 (5 balanced). Output in PSRAM_HEAP, cap 512 KB. */
#define PREVIEW_JPEG_QUALITY    5
#define PREVIEW_JPEG_CAP_BYTES  (512 * 1024)

/* Max wait for photo worker on stop/resume (sp_read + JPEG + margin). */
#define PREVIEW_PHOTO_WAIT_MS       2000
#define PREVIEW_PHOTO_WAIT_STEP_MS  20

/* Start/stop workers: 16 KB stack (DSI/DPU/ISP/GPU bring-up). */
#define PREVIEW_TASK_STACK_SIZE   (1024 * 16)
#define PREVIEW_START_TASK_NAME   "cam_prv_strt"
#define PREVIEW_STOP_TASK_NAME    "cam_prv_stop"
/* Photo worker: SP read + sync JPEG encode; 8 KB stack. */
#define PREVIEW_PHOTO_TASK_STACK_SIZE (1024 * 8)
#define PREVIEW_PHOTO_TASK_NAME       "cam_prv_pho"

typedef enum {
    PREVIEW_STATE_IDLE = 0,
    PREVIEW_STATE_STARTING,
    PREVIEW_STATE_RUNNING,
    PREVIEW_STATE_FROZEN,
    PREVIEW_STATE_STOPPING,
} preview_state_t;

/* volatile: state read/written from Tmr Svc and workers. */
static volatile preview_state_t s_preview_state = PREVIEW_STATE_IDLE;
static beken_thread_t s_preview_thread = NULL;

/* Photo globals: single-writer (photo worker), readers wait in_progress==0. */
static beken_thread_t   s_photo_thread = NULL;
static volatile uint8_t s_photo_in_progress = 0;
static void            *s_photo_buf  = NULL;
static uint32_t         s_photo_size = 0;
static uint16_t         s_photo_w    = 0;
static uint16_t         s_photo_h    = 0;
/* JPEG buffer (PSRAM_HEAP); released with NV12 on resume/stop. */
static void            *s_photo_jpeg      = NULL;
static uint32_t         s_photo_jpeg_size = 0;

bool camera_preview_is_running(void)
{
    return s_preview_state == PREVIEW_STATE_RUNNING;
}

bool camera_preview_is_frozen(void)
{
    return s_preview_state == PREVIEW_STATE_FROZEN;
}

bool camera_preview_is_active(void)
{
    return s_preview_state != PREVIEW_STATE_IDLE;
}

/* Reopen panel as RGB565 for LVGL (rollback on start/stop failure). */
static int camera_preview_reopen_lvgl_panel(void)
{
    /* close is idempotent when panel already off */
    (void)media_lcd_panel_close();

    if (media_lcd_panel_open(PREVIEW_MIPI_PANEL,
                             BK_PIXEL_FORMAT_RGB565,
                             false) != AVDK_ERR_OK) {
        LOGE("media_lcd_panel_open(RGB565) failed; LVGL won't have a panel\n");
        return -1;
    }
    return 0;
}

/* Restart LVGL; invalidate full screen after panel swap (partial-render). */
static void camera_preview_resume_lvgl(void)
{
    lv_vendor_start();
    lv_vendor_disp_lock();
    lv_obj_invalidate(lv_scr_act());
    lv_vendor_disp_unlock();
}

static void camera_preview_start_task(void *arg)
{
    (void)arg;
    LOGI("camera_preview_start_task: begin\n");

    lv_vendor_stop();

    if (media_lcd_panel_close() != AVDK_ERR_OK) {
        LOGE("media_lcd_panel_close failed before preview (continue)\n");
    }

    if (media_lcd_panel_open(PREVIEW_MIPI_PANEL,
                             BK_PIXEL_FORMAT_ARGB8888,
                             true) != AVDK_ERR_OK) {
        LOGE("media_lcd_panel_open(ARGB8888) failed\n");
        goto err_rollback;
    }

    if (media_camera_open(PREVIEW_SENSOR_W, PREVIEW_SENSOR_H,
                          PREVIEW_SENSOR_FPS,
                          PREVIEW_ISP_W, PREVIEW_ISP_H) != AVDK_ERR_OK) {
        LOGE("media_camera_open failed\n");
        goto err_rollback;
    }

    if (media_gpu_open(PREVIEW_ISP_W, PREVIEW_ISP_H,
                       PREVIEW_GPU_ROTATE) != AVDK_ERR_OK) {
        LOGE("media_gpu_open failed\n");
        (void)media_camera_close();
        goto err_rollback;
    }

    if (media_camera_sp_open(PREVIEW_SP_W, PREVIEW_SP_H) != AVDK_ERR_OK) {
        LOGW("media_camera_sp_open(%dx%d) failed; photo capture disabled\n",
             PREVIEW_SP_W, PREVIEW_SP_H);
    }

    s_preview_state = PREVIEW_STATE_RUNNING;
    s_preview_thread = NULL;
    LOGI("camera_preview_start_task: running\n");
    rtos_delete_thread(NULL);
    return;

err_rollback:
    (void)camera_preview_reopen_lvgl_panel();
    camera_preview_resume_lvgl();
    s_preview_state = PREVIEW_STATE_IDLE;
    s_preview_thread = NULL;
    LOGI("camera_preview_start_task: rollback done\n");
    rtos_delete_thread(NULL);
}

static void camera_preview_wait_photo_worker(uint32_t timeout_ms)
{
    while (s_photo_in_progress && timeout_ms > 0) {
        rtos_delay_milliseconds(PREVIEW_PHOTO_WAIT_STEP_MS);
        timeout_ms = (timeout_ms > PREVIEW_PHOTO_WAIT_STEP_MS)
                     ? timeout_ms - PREVIEW_PHOTO_WAIT_STEP_MS : 0;
    }
    if (s_photo_in_progress) {
        LOGW("photo worker still in_progress after %ums, force-continue\n",
             (unsigned)PREVIEW_PHOTO_WAIT_MS);
    }
}

/* NV12: bk_frame_buffer_free; JPEG: os_free */
static void camera_preview_release_photo(void)
{
    void *nv12 = s_photo_buf;
    void *jpeg = s_photo_jpeg;
    s_photo_buf       = NULL;
    s_photo_size      = 0;
    s_photo_w         = 0;
    s_photo_h         = 0;
    s_photo_jpeg      = NULL;
    s_photo_jpeg_size = 0;
    if (nv12 != NULL) {
        bk_frame_buffer_free(nv12);
    }
    if (jpeg != NULL) {
        os_free(jpeg);
    }
}

static void camera_preview_photo_task(void *arg)
{
    (void)arg;
    LOGI("photo_task: begin\n");

    void *buf = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                       PREVIEW_SP_NV12_BYTES + 32U);
    if (buf == NULL) {
        LOGE("photo_task: bk_frame_buffer_malloc(UNCODED, %u) failed\n",
             (unsigned)(PREVIEW_SP_NV12_BYTES + 32U));
        goto out;
    }

    avdk_err_t ret = media_camera_sp_read((uint8_t *)buf,
                                          PREVIEW_SP_NV12_BYTES,
                                          PREVIEW_SP_READ_TIMEOUT_MS);
    if (ret != AVDK_ERR_OK) {
        LOGE("photo_task: media_camera_sp_read failed: %d\n", (int)ret);
        bk_frame_buffer_free(buf);
        buf = NULL;
        goto out;
    }

    if (s_preview_state != PREVIEW_STATE_FROZEN) {
        LOGI("photo_task: state changed to %d during capture, drop photo\n",
             (int)s_preview_state);
        bk_frame_buffer_free(buf);
        buf = NULL;
        goto out;
    }

    void *jpeg_buf = psram_malloc(PREVIEW_JPEG_CAP_BYTES);
    if (jpeg_buf == NULL) {
        LOGE("photo_task: psram_malloc(%u) for jpeg failed; keep NV12 only\n",
             (unsigned)PREVIEW_JPEG_CAP_BYTES);
    } else {
        uint32_t jpeg_len = 0;
        avdk_err_t jret = media_jpeg_encode_nv12_oneshot(buf,
                                                         PREVIEW_SP_W,
                                                         PREVIEW_SP_H,
                                                         PREVIEW_JPEG_QUALITY,
                                                         jpeg_buf,
                                                         PREVIEW_JPEG_CAP_BYTES,
                                                         &jpeg_len);
        if (jret != AVDK_ERR_OK || jpeg_len == 0) {
            LOGE("photo_task: jpeg encode failed: %d len=%u\n",
                 (int)jret, (unsigned)jpeg_len);
            os_free(jpeg_buf);
            jpeg_buf = NULL;
        } else {
            if (s_preview_state != PREVIEW_STATE_FROZEN) {
                LOGI("photo_task: state changed during jpeg encode, drop\n");
                bk_frame_buffer_free(buf);
                buf = NULL;
                os_free(jpeg_buf);
                jpeg_buf = NULL;
                goto out;
            }
            s_photo_jpeg      = jpeg_buf;
            s_photo_jpeg_size = jpeg_len;
        }
    }

    s_photo_buf  = buf;
    s_photo_size = PREVIEW_SP_NV12_BYTES;
    s_photo_w    = PREVIEW_SP_W;
    s_photo_h    = PREVIEW_SP_H;
    LOGI("photo_task: NV12 %ux%u %u bytes @ %p, JPEG %u bytes @ %p\n",
         s_photo_w, s_photo_h, (unsigned)s_photo_size, s_photo_buf,
         (unsigned)s_photo_jpeg_size, s_photo_jpeg);

out:
    s_photo_thread = NULL;
    s_photo_in_progress = 0;
    LOGI("photo_task: done (have_nv12=%d, have_jpeg=%d)\n",
         (s_photo_buf != NULL), (s_photo_jpeg != NULL));
    rtos_delete_thread(NULL);
}

int camera_preview_take_photo(void)
{
    if (s_preview_state != PREVIEW_STATE_RUNNING) {
        LOGI("camera_preview_take_photo: state=%d, ignore\n", (int)s_preview_state);
        return 0;
    }

    if (s_photo_buf != NULL || s_photo_in_progress) {
        LOGW("take_photo: stale photo state (buf=%p inprog=%d), reset\n",
             s_photo_buf, s_photo_in_progress);
        camera_preview_wait_photo_worker(PREVIEW_PHOTO_WAIT_MS);
        camera_preview_release_photo();
    }

    s_preview_state = PREVIEW_STATE_FROZEN;
    if (media_gpu_arm_snapshot() != AVDK_ERR_OK) {
        LOGE("media_gpu_arm_snapshot failed; revert to RUNNING\n");
        s_preview_state = PREVIEW_STATE_RUNNING;
        return -1;
    }

    s_photo_in_progress = 1;
    bk_err_t ret = rtos_core1_create_thread(&s_photo_thread,
                                            BEKEN_DEFAULT_WORKER_PRIORITY,
                                            PREVIEW_PHOTO_TASK_NAME,
                                            (beken_thread_function_t)camera_preview_photo_task,
                                            PREVIEW_PHOTO_TASK_STACK_SIZE,
                                            NULL);
    if (ret != BK_OK) {
        LOGE("rtos_core1_create_thread(photo) failed: %d; freeze without hi-res\n",
             (int)ret);
        s_photo_in_progress = 0;
        s_photo_thread = NULL;
    }

    LOGI("camera_preview_take_photo: armed, freezing (photo worker=%d)\n",
         s_photo_in_progress);
    return 0;
}

int camera_preview_resume_live(void)
{
    if (s_preview_state != PREVIEW_STATE_FROZEN) {
        LOGI("camera_preview_resume_live: state=%d, ignore\n", (int)s_preview_state);
        return 0;
    }

    if (media_gpu_resume_live() != AVDK_ERR_OK) {
        LOGE("media_gpu_resume_live failed\n");
        return -1;
    }

    camera_preview_wait_photo_worker(PREVIEW_PHOTO_WAIT_MS);
    camera_preview_release_photo();

    s_preview_state = PREVIEW_STATE_RUNNING;
    LOGI("camera_preview_resume_live: live\n");
    return 0;
}

int camera_preview_get_photo_nv12(void **buf, uint32_t *size,
                                  uint16_t *w, uint16_t *h)
{
    if (buf == NULL || size == NULL || w == NULL || h == NULL) {
        return -1;
    }

    if (s_preview_state != PREVIEW_STATE_FROZEN ||
        s_photo_in_progress ||
        s_photo_buf == NULL) {
        return -1;
    }

    *buf  = s_photo_buf;
    *size = s_photo_size;
    *w    = s_photo_w;
    *h    = s_photo_h;
    return 0;
}

int camera_preview_get_photo_jpeg(void **buf, uint32_t *size)
{
    if (buf == NULL || size == NULL) {
        return -1;
    }
    if (s_preview_state != PREVIEW_STATE_FROZEN ||
        s_photo_in_progress ||
        s_photo_jpeg == NULL) {
        return -1;
    }
    *buf  = s_photo_jpeg;
    *size = s_photo_jpeg_size;
    return 0;
}

int camera_preview_start(void)
{
    if (s_preview_state != PREVIEW_STATE_IDLE) {
        LOGI("camera_preview_start: state=%d, ignore\n", (int)s_preview_state);
        return 0;
    }

    s_preview_state = PREVIEW_STATE_STARTING;
    bk_err_t ret = rtos_core1_create_thread(&s_preview_thread,
                                            BEKEN_DEFAULT_WORKER_PRIORITY,
                                            PREVIEW_START_TASK_NAME,
                                            (beken_thread_function_t)camera_preview_start_task,
                                            PREVIEW_TASK_STACK_SIZE,
                                            NULL);
    if (ret != BK_OK) {
        s_preview_state = PREVIEW_STATE_IDLE;
        s_preview_thread = NULL;
        LOGE("rtos_core1_create_thread(start) failed: %d\n", (int)ret);
        return -1;
    }
    return 0;
}

static void camera_preview_stop_task(void *arg)
{
    (void)arg;
    LOGI("camera_preview_stop_task: begin\n");

    camera_preview_wait_photo_worker(PREVIEW_PHOTO_WAIT_MS);

    (void)media_gpu_drop_snapshot();

    camera_preview_release_photo();

    if (media_gpu_close() != AVDK_ERR_OK) {
        LOGE("media_gpu_close failed\n");
    }
    if (media_camera_close() != AVDK_ERR_OK) {
        LOGE("media_camera_close failed\n");
    }
    (void)camera_preview_reopen_lvgl_panel();

    camera_preview_resume_lvgl();

    s_preview_state = PREVIEW_STATE_IDLE;
    s_preview_thread = NULL;
    LOGI("camera_preview_stop_task: done\n");
    rtos_delete_thread(NULL);
}

int camera_preview_stop(void)
{
    if (s_preview_state != PREVIEW_STATE_RUNNING &&
        s_preview_state != PREVIEW_STATE_FROZEN) {
        LOGI("camera_preview_stop: state=%d, ignore\n", (int)s_preview_state);
        return 0;
    }

    s_preview_state = PREVIEW_STATE_STOPPING;
    bk_err_t ret = rtos_core1_create_thread(&s_preview_thread,
                                            BEKEN_DEFAULT_WORKER_PRIORITY,
                                            PREVIEW_STOP_TASK_NAME,
                                            (beken_thread_function_t)camera_preview_stop_task,
                                            PREVIEW_TASK_STACK_SIZE,
                                            NULL);
    if (ret != BK_OK) {
        LOGE("rtos_core1_create_thread(stop) failed: %d, sync rollback on caller stack\n",
             (int)ret);
        camera_preview_wait_photo_worker(PREVIEW_PHOTO_WAIT_MS);
        (void)media_gpu_drop_snapshot();
        camera_preview_release_photo();
        (void)media_gpu_close();
        (void)media_camera_close();
        (void)camera_preview_reopen_lvgl_panel();
        camera_preview_resume_lvgl();
        s_preview_state = PREVIEW_STATE_IDLE;
        s_preview_thread = NULL;
        return -1;
    }
    return 0;
}
