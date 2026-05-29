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

#include <stdio.h>
#include <string.h>

#include "board_usb_switch.h"

/* -----------------------------------------------------------------------
 * Debug-only: persist each captured JPEG to the on-board SD-NAND.
 *
 *   * SD-NAND power + FATFS mount happen ONCE per preview session
 *     (camera_preview_start_task), not per-shot, so successive shots
 *     don't pay the power-on settle / SDIO enumeration / FAT scan tax.
 *   * Each shot only does: scan-next-session-id -> f_mkdir -> f_open ->
 *     chunked f_write -> f_close, while the volume stays mounted.
 *   * Unmount + power-off happen ONCE at stop_task, after the panel /
 *     GPU / camera teardown sequence -- by then the freeze scan-out
 *     is already over and SDIO traffic can no longer underrun the DPU.
 *
 * Toggle off (e.g. -DCONFIG_CAM_PREVIEW_SDNAND_DEBUG=0) when shipping
 * to mass production -- the saved JPEGs are a developer diagnostic, not
 * a product feature. FATFS + board SD-NAND power gate must both be
 * present in defconfig for this code to actually compile in.
 * --------------------------------------------------------------------- */
#ifndef CONFIG_CAM_PREVIEW_SDNAND_DEBUG
#define CONFIG_CAM_PREVIEW_SDNAND_DEBUG 1
#endif

#define CAM_PREV_SDNAND_DEBUG_EFFECTIVE \
    (CONFIG_CAM_PREVIEW_SDNAND_DEBUG && CONFIG_FATFS && CONFIG_BOARD_SD_NAND_ENABLE)

#if CAM_PREV_SDNAND_DEBUG_EFFECTIVE
#include "ff.h"
#endif

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

/* Max wait for photo worker on stop/resume. Covers SP read (~1s) +
 * JPEG encode + safety margin. SD-NAND persist no longer happens on
 * the photo worker (moved to stop_task), so the old 10s budget is
 * unnecessary. */
#define PREVIEW_PHOTO_WAIT_MS       2500
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

/* Forward declarations for the app-scoped SD-NAND debug hooks. mount
 * is called once at app init by camera_preview_sdnand_debug_init() in
 * ap_main; save_jpeg is called per shot from photo_task. There is
 * intentionally no unmount -- see camera_preview.h for the rationale. */
static int cam_prev_sdnand_mount(void);
static int cam_prev_sdnand_save_jpeg(const void *jpeg, uint32_t jpeg_len);

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

/* =======================================================================
 *  Debug-only: persist captured JPEGs to the on-board SD-NAND.
 *
 *  Lifecycle (app-scoped, NO per-preview-session mount/unmount):
 *      ap_main --> camera_preview_sdnand_debug_init()
 *                   --> cam_prev_sdnand_mount()
 *          ONCE at app init: power_on -> settle -> alloc FATFS ->
 *          f_mount (f_mkfs on FR_NO_FILESYSTEM, retry) -> ensure base
 *          dir "1:/photos". Volume stays mounted for the rest of the
 *          app session.
 *      camera_preview_photo_task --> cam_prev_sdnand_save_jpeg()
 *          (assumes mounted) scan next NNNN -> f_mkdir -> f_open(NEW)
 *          -> chunked f_write -> f_close.
 *
 *  Why app-scoped instead of preview-session-scoped: an earlier design
 *  unmounted + powered off SD-NAND inside camera_preview_stop_task,
 *  and that reliably left the LCD panel black after the LVGL panel
 *  reopen + lv_vendor_start. The root cause is somewhere in
 *  {sdio_dwc::sdio_reset, gpio_dev_unmap(GPIO_53) where GPIO_53 is the
 *  LCD_B2 second-func pin in the SDK default pinmux, NAND_VDD HIGH->LOW
 *  transient} and has not been pinpointed. Keeping NAND_VDD HIGH +
 *  volume mounted for the whole app session sidesteps the issue
 *  cleanly, at the cost of ~mA of static current draw -- acceptable
 *  for a developer build.
 *
 *  All large stack-class FatFs objects (FATFS, FIL, DIR, FILINFO) are
 *  heap-allocated -- the photo worker runs on an 8 KB stack and putting
 *  any of them on the stack overflows it (same lesson learned in
 *  sdnand_run_sanity_test).
 * --------------------------------------------------------------------- */
#if CAM_PREV_SDNAND_DEBUG_EFFECTIVE

#define PHOTO_SDNAND_DRIVE_PATH         "1:"
#define PHOTO_SDNAND_BASE_DIR           "1:/photos"
#define PHOTO_SDNAND_FILE_NAME          "photo.jpg"
#define PHOTO_SDNAND_PATH_BUF_LEN       48u
#define PHOTO_SDNAND_POWER_SETTLE_MS    50u
#define PHOTO_SDNAND_POWER_OFF_DELAY_MS 20u
#define PHOTO_SDNAND_MKFS_BUF_SIZE      4096u
/* Hard cap on session scan: covers ~10k photos / one full SD-NAND fill,
 * which is well beyond a debug session, and bounds the worst-case
 * f_readdir loop time. */
#define PHOTO_SDNAND_MAX_SESSION_INDEX  9999u
/* Write in 16 KB chunks: smaller than typical FAT cluster page-cache so
 * f_write never has to spill internally, and small enough that a transient
 * SDIO error doesn't lose the whole frame. */
#define PHOTO_SDNAND_WRITE_CHUNK        (16u * 1024u)

/* Session-scoped FATFS state; both written only from the preview start/
 * stop workers (single-thread w.r.t. each other) and read by photo_task
 * for save. Reads guarded by s_sdnand_mounted check. */
static FATFS *s_sdnand_fs       = NULL;
static int    s_sdnand_mounted  = 0;

/* Parse a decimal directory name (e.g. "0042") into an unsigned int.
 * Returns -1 if the name has any non-digit char or is empty -- those
 * dirs are ignored by the session-id scan. */
static int cam_prev_sdnand_parse_session_id(const char *name, uint32_t *out)
{
    if (name == NULL || name[0] == '\0') {
        return -1;
    }
    uint32_t v = 0;
    for (const char *p = name; *p; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
        v = v * 10u + (uint32_t)(*p - '0');
        if (v > PHOTO_SDNAND_MAX_SESSION_INDEX) {
            return -1;
        }
    }
    *out = v;
    return 0;
}

/* Scan PHOTO_SDNAND_BASE_DIR for existing "NNNN" sub-directories and pick
 * the next free index (max + 1, or 1 if empty). FILINFO is ~570 B with
 * LFN; keep it on the heap. */
static int cam_prev_sdnand_pick_next_session_id(uint32_t *out_id)
{
    DIR     *dir = NULL;
    FILINFO *fno = NULL;
    uint32_t max_id   = 0;
    int      have_any = 0;
    int      rc = -1;

    dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    if (dir == NULL || fno == NULL) {
        LOGE("sdnand: pick_next_session_id alloc FAILED\n");
        goto out;
    }

    FRESULT fr = f_opendir(dir, PHOTO_SDNAND_BASE_DIR);
    if (fr != FR_OK) {
        LOGE("sdnand: f_opendir(%s) fr=%d\n", PHOTO_SDNAND_BASE_DIR, fr);
        goto out;
    }

    while (1) {
        fr = f_readdir(dir, fno);
        if (fr != FR_OK || fno->fname[0] == '\0') {
            break;
        }
        if (!(fno->fattrib & AM_DIR)) {
            continue;
        }
        uint32_t id = 0;
        if (cam_prev_sdnand_parse_session_id(fno->fname, &id) != 0) {
            continue;
        }
        if (!have_any || id > max_id) {
            max_id = id;
            have_any = 1;
        }
    }
    (void)f_closedir(dir);

    uint32_t next = have_any ? (max_id + 1u) : 1u;
    if (next > PHOTO_SDNAND_MAX_SESSION_INDEX) {
        LOGE("sdnand: session index exhausted (max=%u)\n",
             (unsigned)PHOTO_SDNAND_MAX_SESSION_INDEX);
        goto out;
    }
    *out_id = next;
    rc = 0;

out:
    if (dir) { os_free(dir); }
    if (fno) { os_free(fno); }
    return rc;
}

/* Bring SD-NAND up and mount the FATFS volume. Idempotent: if already
 * mounted, return success without touching anything. Failure leaves
 * s_sdnand_mounted = 0 and NAND_VDD off, so the rest of preview keeps
 * working without the debug persist. */
static int cam_prev_sdnand_mount(void)
{
    if (s_sdnand_mounted) {
        return 0;
    }

    LOGI("sdnand: mount BEGIN\n");

    if (board_sd_nand_power_on() != BK_OK) {
        LOGE("sdnand: board_sd_nand_power_on FAILED\n");
        return -1;
    }
    rtos_delay_milliseconds(PHOTO_SDNAND_POWER_SETTLE_MS);

    FATFS *fs = (FATFS *)os_malloc(sizeof(FATFS));
    if (fs == NULL) {
        LOGE("sdnand: FATFS alloc FAILED\n");
        goto err_poweroff;
    }

    FRESULT fr = f_mount(fs, PHOTO_SDNAND_DRIVE_PATH, 1);
    if (fr == FR_NO_FILESYSTEM) {
        LOGW("sdnand: FR_NO_FILESYSTEM -> running f_mkfs ...\n");
        uint8_t *mkfs_buf = (uint8_t *)os_malloc(PHOTO_SDNAND_MKFS_BUF_SIZE);
        if (mkfs_buf == NULL) {
            LOGE("sdnand: mkfs buffer alloc FAILED\n");
            goto err_free_fs;
        }
        fr = f_mkfs(PHOTO_SDNAND_DRIVE_PATH, FM_ANY, 0,
                    mkfs_buf, PHOTO_SDNAND_MKFS_BUF_SIZE);
        os_free(mkfs_buf);
        if (fr != FR_OK) {
            LOGE("sdnand: f_mkfs FAILED fr=%d\n", fr);
            goto err_free_fs;
        }
        fr = f_mount(fs, PHOTO_SDNAND_DRIVE_PATH, 1);
    }
    if (fr != FR_OK) {
        LOGE("sdnand: f_mount FAILED fr=%d\n", fr);
        goto err_free_fs;
    }

    /* Ensure base dir exists once per session. */
    fr = f_mkdir(PHOTO_SDNAND_BASE_DIR);
    if (fr != FR_OK && fr != FR_EXIST) {
        LOGE("sdnand: f_mkdir(%s) FAILED fr=%d\n",
             PHOTO_SDNAND_BASE_DIR, fr);
        (void)f_unmount(1, PHOTO_SDNAND_DRIVE_PATH, 0);
        goto err_free_fs;
    }

    s_sdnand_fs       = fs;
    s_sdnand_mounted  = 1;
    LOGI("sdnand: mount OK (base=%s)\n", PHOTO_SDNAND_BASE_DIR);
    return 0;

err_free_fs:
    os_free(fs);
err_poweroff:
    rtos_delay_milliseconds(PHOTO_SDNAND_POWER_OFF_DELAY_MS);
    (void)board_sd_nand_power_off();
    LOGE("sdnand: mount FAILED\n");
    return -1;
}

/* Save one JPEG into a fresh "NNNN" session sub-dir under the base dir.
 * Caller must have already called cam_prev_sdnand_mount() in this
 * preview session. Failure is non-fatal -- the in-RAM JPEG remains
 * available to RTC / LVM consumers even if persistence fails. */
static int cam_prev_sdnand_save_jpeg(const void *jpeg, uint32_t jpeg_len)
{
    if (!s_sdnand_mounted) {
        LOGW("sdnand: save skipped (volume not mounted)\n");
        return -1;
    }
    if (jpeg == NULL || jpeg_len == 0) {
        LOGW("sdnand: save skipped (buf=%p len=%u)\n",
             jpeg, (unsigned)jpeg_len);
        return -1;
    }

    FIL  *fp   = NULL;
    char *path = NULL;
    int   file_open = 0;
    int   rc = -1;
    FRESULT fr;

    fp   = (FIL *)os_malloc(sizeof(FIL));
    path = (char *)os_malloc(PHOTO_SDNAND_PATH_BUF_LEN);
    if (fp == NULL || path == NULL) {
        LOGE("sdnand: save alloc FAILED\n");
        goto out;
    }

    uint32_t session_id = 0;
    if (cam_prev_sdnand_pick_next_session_id(&session_id) != 0) {
        LOGE("sdnand: pick_next_session_id FAILED\n");
        goto out;
    }

    int n = snprintf(path, PHOTO_SDNAND_PATH_BUF_LEN, "%s/%04u",
                     PHOTO_SDNAND_BASE_DIR, (unsigned)session_id);
    if (n <= 0 || n >= (int)PHOTO_SDNAND_PATH_BUF_LEN) {
        LOGE("sdnand: dir path snprintf overflow (n=%d)\n", n);
        goto out;
    }
    fr = f_mkdir(path);
    if (fr != FR_OK) {
        LOGE("sdnand: f_mkdir(%s) FAILED fr=%d\n", path, fr);
        goto out;
    }
    LOGI("sdnand: created session dir %s\n", path);

    n = snprintf(path, PHOTO_SDNAND_PATH_BUF_LEN, "%s/%04u/%s",
                 PHOTO_SDNAND_BASE_DIR, (unsigned)session_id,
                 PHOTO_SDNAND_FILE_NAME);
    if (n <= 0 || n >= (int)PHOTO_SDNAND_PATH_BUF_LEN) {
        LOGE("sdnand: file path snprintf overflow (n=%d)\n", n);
        goto out;
    }
    /* FA_CREATE_NEW so a stale file in a manually-recovered dir is not
     * silently clobbered. */
    fr = f_open(fp, path, FA_CREATE_NEW | FA_WRITE);
    if (fr != FR_OK) {
        LOGE("sdnand: f_open(%s) FAILED fr=%d\n", path, fr);
        goto out;
    }
    file_open = 1;

    const uint8_t *p = (const uint8_t *)jpeg;
    uint32_t remaining = jpeg_len;
    uint32_t written_total = 0;
    while (remaining > 0) {
        UINT to_write = (remaining > PHOTO_SDNAND_WRITE_CHUNK)
                            ? PHOTO_SDNAND_WRITE_CHUNK : remaining;
        UINT bw = 0;
        fr = f_write(fp, p, to_write, &bw);
        if (fr != FR_OK || bw != to_write) {
            LOGE("sdnand: f_write FAILED fr=%d bw=%u/%u (total=%u/%u)\n",
                 fr, (unsigned)bw, (unsigned)to_write,
                 (unsigned)(written_total + bw), (unsigned)jpeg_len);
            goto out;
        }
        p             += to_write;
        remaining     -= to_write;
        written_total += to_write;
    }
    LOGI("sdnand: wrote %u bytes -> %s\n", (unsigned)written_total, path);
    rc = 0;

out:
    if (file_open) {
        (void)f_close(fp);
    }
    if (fp)   { os_free(fp); }
    if (path) { os_free(path); }
    return rc;
}

#else  /* !CAM_PREV_SDNAND_DEBUG_EFFECTIVE */

static int cam_prev_sdnand_mount(void) { return 0; }
static int cam_prev_sdnand_save_jpeg(const void *jpeg, uint32_t jpeg_len)
{
    (void)jpeg; (void)jpeg_len;
    return -1;
}

#endif /* CAM_PREV_SDNAND_DEBUG_EFFECTIVE */

/* Public init entry: see camera_preview.h for the long-form rationale.
 * Thin wrapper around the static mount helper so ap_main doesn't need
 * to touch board / FATFS / camera_preview internals directly. */
int camera_preview_sdnand_debug_init(void)
{
    return cam_prev_sdnand_mount();
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

    /* Debug-only persist of the JPEG to SD-NAND. Volume is already
     * mounted from start_task, so this is just mkdir + write (~tens
     * of KB on SDIO). The previous mount-in-photo-task design did
     * ~900 ms of SDIO traffic during the single-buffer freeze
     * scan-out and the DPU underran -> panel went black; pre-mounting
     * keeps the per-shot SDIO burst short enough that this no longer
     * happens. Failure here does not invalidate the in-RAM JPEG. */
    if (s_photo_jpeg != NULL && s_photo_jpeg_size > 0) {
        (void)cam_prev_sdnand_save_jpeg(s_photo_jpeg, s_photo_jpeg_size);
    }

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

    /* NOTE: SD-NAND volume is intentionally NOT unmounted here and
     * NAND_VDD is left HIGH. f_unmount + board_sd_nand_power_off
     * reliably caused the LCD panel to go black after this point
     * (root cause not pinned down; suspected to be one of:
     * sdio_dwc::sdio_reset, gpio_dev_unmap(GPIO_53 = LCD_B2 second-
     * func pin) in board_drive_gpio, or NAND_VDD HIGH->LOW transient).
     * Mount happens once at app init via camera_preview_sdnand_debug_init();
     * the volume stays mounted for the rest of the session. */

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
        /* SD-NAND volume stays mounted -- see camera_preview_stop_task
         * for the rationale (f_unmount + power_off caused black LCD). */
        s_preview_state = PREVIEW_STATE_IDLE;
        s_preview_thread = NULL;
        return -1;
    }
    return 0;
}
