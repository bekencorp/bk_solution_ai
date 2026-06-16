// Copyright 2020-2021 Beken
// Ported from doorbell reference project for beken_robot multimedia_device_service component.
// VG-LITE GPU controller helpers wired to ISP MP channel + MIPI LCD flush.

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <driver/int.h>
#include <common/bk_err.h>
#include "sys_driver.h"
#include <components/bk_gpu_ctlr.h>
#include <components/bk_gpu.h>
#include "app_camera.h"
#include "app_display.h"
#include "app_gpu.h"
#include <avdk_check.h>
#include <components/bk_frame_buffer.h>
#include "driver/isp.h"
#include <driver/isp_types.h>

#if (CONFIG_PSRAM_WRITE_THROUGH)
#include <driver/psram_types.h>
#include <driver/psram.h>
#endif

#define TAG "bkmm-gpu"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

static bk_gpu_ctlr_handle_t s_gpu_handle = NULL;

static gpu_board_config_t *gpu_board_config = NULL;

#if (CONFIG_PSRAM_WRITE_THROUGH)
static uint32_t s_psram_cover_area = PSRAM_WRITE_THROUGH_AREA_COUNT;
#endif

/* ---------------------------------------------------------------------------
 * Snapshot / freeze state -- see app_gpu.h for the public contract.
 *
 *  * s_snapshot_arm   : one-shot, "next bkmm_frame_complete should snapshot".
 *  * s_freeze_active  : while true, all GPU frames are dropped and the DPU is
 *                       locked on s_snapshot_buf.
 *  * s_snapshot_buf / _size : the photo, allocated from the same
 *                             MEM_SLAB_HEAP_UNCODED pool as live GPU frames
 *                             (so DPU can scan-out from it directly).
 *
 * volatile because the flags are flipped from a non-GPU thread
 * (camera_preview worker on core 1) but read by the GPU completion path.
 * ------------------------------------------------------------------------- */
static volatile bool  s_snapshot_arm  = false;
static volatile bool  s_freeze_active = false;
static void          *s_snapshot_buf  = NULL;
static uint32_t       s_snapshot_size = 0;

static void *bkmm_frame_malloc(uint32_t size)
{
    void *disp_frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);
    if (disp_frame == NULL)
    {
        LOGE("GPU failed to malloc frame size=%u\n", (unsigned)size);
        return NULL;
    }
#if (CONFIG_PSRAM_WRITE_THROUGH)
    if (s_psram_cover_area < PSRAM_WRITE_THROUGH_AREA_COUNT &&
        bk_psram_enable_write_through(s_psram_cover_area, (uint32_t)disp_frame, (uint32_t)((uint8_t *)disp_frame + size)) != BK_OK)
    {
        LOGE("Failed to enable write through\n");
        bk_frame_buffer_free(disp_frame);
        return NULL;
    }
#endif
    return disp_frame;
}

static avdk_err_t bkmm_frame_free(void *ptr)
{
#if (CONFIG_PSRAM_WRITE_THROUGH)
    if (s_psram_cover_area < PSRAM_WRITE_THROUGH_AREA_COUNT)
    {
        bk_psram_disable_write_through(s_psram_cover_area);
    }
#endif
    bk_frame_buffer_free(ptr);
    return AVDK_ERR_OK;
}

/* Snapshot uses bk_frame_buffer_malloc (not bkmm_frame_malloc/write-through). */
static void *snapshot_buffer_alloc(uint32_t size)
{
    return bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);
}

static avdk_err_t snapshot_buffer_release_cb(void *ptr)
{
    /* Called from DPU on swap or deinit; safe to free snapshot buffer. */
    if (ptr != NULL)
    {
        bk_frame_buffer_free(ptr);
    }
    return AVDK_ERR_OK;
}

static void bkmm_frame_complete(void *frame, uint32_t frame_size, void *args)
{
    (void)args;

    /* Arm path: copy frame to snapshot buf, flush to DPU, freeze display. */
    if (s_snapshot_arm && s_snapshot_buf == NULL)
    {
        s_snapshot_arm = false;
        void *buf = snapshot_buffer_alloc(frame_size);
        if (buf != NULL)
        {
            os_memcpy(buf, frame, frame_size);
            s_snapshot_buf  = buf;
            s_snapshot_size = frame_size;
            LOGI("snapshot taken: buf=%p size=%u\n", buf, (unsigned)frame_size);
            if (app_mipi_lcd_flush(buf, snapshot_buffer_release_cb) == AVDK_ERR_OK)
            {
                s_freeze_active = true;
                bkmm_frame_free(frame);
                return;
            }
            /* flush failed: free snapshot buf, keep live path */
            LOGE("snapshot lcd_flush failed, fall back to live\n");
            bk_frame_buffer_free(buf);
            s_snapshot_buf  = NULL;
            s_snapshot_size = 0;
        }
        else
        {
            LOGE("snapshot malloc(%u) failed; keep live\n", (unsigned)frame_size);
        }
    }

    /* Frozen: drop live GPU frames, DPU holds snapshot */
    if (s_freeze_active)
    {
        bkmm_frame_free(frame);
        return;
    }

    /* Live path: clear local snapshot ref before flushing new frame */
    if (s_snapshot_buf != NULL)
    {
        s_snapshot_buf  = NULL;
        s_snapshot_size = 0;
    }

    avdk_err_t ret = app_mipi_lcd_flush(frame, bkmm_frame_free);
    if (ret != AVDK_ERR_OK)
    {
        LOGD("%s, %d, GPU failed to flush frame %d\n", __func__, __LINE__, ret);
        bkmm_frame_free(frame);
    }
}

int app_gpu_arm_snapshot(void)
{
    if (s_freeze_active || s_snapshot_arm)
    {
        return 0;
    }
    s_snapshot_arm = true;
    return 0;
}

int app_gpu_resume_live(void)
{
    /* Always clear arm and freeze; DPU owns snapshot buffer release */
    s_snapshot_arm  = false;
    s_freeze_active = false;
    return 0;
}

int app_gpu_drop_snapshot(void)
{
    /* Stop path: clear flags only; DPU release_cb frees snapshot buffer */
    s_snapshot_arm  = false;
    s_freeze_active = false;
    s_snapshot_buf  = NULL;
    s_snapshot_size = 0;
    return 0;
}

bool app_gpu_is_frozen(void)
{
    return s_freeze_active;
}

void *app_gpu_get_snapshot_buffer(void)
{
    return s_snapshot_buf;
}

uint32_t app_gpu_get_snapshot_size(void)
{
    return s_snapshot_size;
}

avdk_err_t app_gpu_turn_on(gpu_board_config_t *config)
{
#if (CONFIG_VG_LITE_GPU)
    bk_gpu_ctlr_config_t gpu_config;
    isp_control_t *isp_control = (isp_control_t *)app_isp_handle_get();
    if (isp_control == NULL)
    {
        LOGW("%s, %d, isp handle is NULL\n", __func__, __LINE__);
        return AVDK_ERR_GENERIC;
    }

    AVDK_RETURN_ON_FALSE((s_gpu_handle == NULL), AVDK_ERR_BUSY, TAG, "already turned on");
    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is NULL");

    LOGI("%s: GPU config - src: %dx%d, dst: %dx%d, rotate: %d, scale=%d\n",
         __func__, config->flexa.src_width, config->flexa.src_height,
         config->flexa.dst_width, config->flexa.dst_height,
         config->flexa.degree, config->flexa.scale);

    os_memset(&gpu_config, 0, sizeof(bk_gpu_ctlr_config_t));

#if (CONFIG_PSRAM_WRITE_THROUGH)
    if (s_psram_cover_area >= PSRAM_WRITE_THROUGH_AREA_COUNT)
    {
        s_psram_cover_area = bk_psram_alloc_write_through_channel_with_psram_id(0);
        if (s_psram_cover_area >= PSRAM_WRITE_THROUGH_AREA_COUNT)
        {
            LOGE("alloc write-through channel failed\n");
            return AVDK_ERR_GENERIC;
        }
    }
#endif

    gpu_config.rotate_degree = config->flexa.degree;
    gpu_config.src_width = config->flexa.src_width;
    gpu_config.src_height = config->flexa.src_height;
    gpu_config.dst_width = config->flexa.dst_width;
    gpu_config.dst_height = config->flexa.dst_height;
    gpu_config.src_format = config->flexa.src_format;
    gpu_config.dst_format = config->flexa.dst_format;
    gpu_config.tess_width = config->flexa.tess_width;
    gpu_config.tess_height = config->flexa.tess_height;
    gpu_config.compress = config->flexa.dst_compress;
    gpu_config.scale = config->flexa.scale;
    gpu_config.src_buffer = (uint8_t *)(isp_control->chn[APP_ISP_MP_CHN_ID].y_addr);
    gpu_config.flexa_lines = 16;
    gpu_config.flexa_buff_cnt = isp_control->chn[APP_ISP_MP_CHN_ID].buf_cnt;
    gpu_config.flexa = true;
    gpu_config.frame_malloc = bkmm_frame_malloc;
    gpu_config.frame_free = bkmm_frame_free;
    gpu_config.flexa_line_done = NULL;
    gpu_config.flexa_line_done_args = NULL;
    gpu_config.frame_done = bkmm_frame_complete;
    gpu_config.frame_done_args = NULL;

    avdk_err_t ret = bk_gpu_ctlr_new(&s_gpu_handle, &gpu_config);
    if (ret != BK_OK)
    {
        LOGW("bk_gpu_ctlr_new failed: %d\n", ret);
        return AVDK_ERR_GENERIC;
    }

    ret = bk_gpu_init(s_gpu_handle);
    if (ret != BK_OK)
    {
        LOGW("bk_gpu_init failed: %d\n", ret);
        return AVDK_ERR_GENERIC;
    }

    ret = bk_gpu_open(s_gpu_handle);
    if (ret != BK_OK)
    {
        LOGW("bk_gpu_open failed: %d\n", ret);
        return AVDK_ERR_GENERIC;
    }
    return AVDK_ERR_OK;
#else
    LOGE("CONFIG_VG_LITE_GPU not enabled, GPU pipeline unavailable\n");
    (void)config;
    return AVDK_ERR_UNSUPPORTED;
#endif
}

avdk_err_t app_gpu_turn_off(bk_gpu_ctlr_handle_t ctlr)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (ctlr == NULL)
    {
        LOGW("%s, gpu handle is NULL\n", __func__);
        return AVDK_ERR_GENERIC;
    }

    /* The DPU may still own the last flushed GPU frame after GPU stop.
     * Keep the write-through channel alive so that delayed DPU release
     * callbacks can safely run bkmm_frame_free(). */
    app_gpu_drop_snapshot();

    ret = bk_gpu_close(ctlr);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("close gpu failed: %d\n", ret);
        return ret;
    }

    ret = bk_gpu_deinit(ctlr);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("deinit gpu failed: %d\n", ret);
        return ret;
    }

    ret = bk_gpu_delete(ctlr);
    if (ret != AVDK_ERR_OK)
    {
        LOGW("delete gpu failed: %d\n", ret);
        return ret;
    }

    if (ctlr == s_gpu_handle)
    {
        s_gpu_handle = NULL;
    }

    return AVDK_ERR_OK;
}

bk_gpu_ctlr_handle_t app_gpu_handle_get(void)
{
    return s_gpu_handle;
}

int app_gpu_board_config_set(gpu_board_config_t *config)
{
    AVDK_RETURN_ON_FALSE(config, AVDK_ERR_INVAL, TAG, "config is NULL");

    if (gpu_board_config == NULL)
    {
        gpu_board_config = os_malloc(sizeof(gpu_board_config_t));
        AVDK_RETURN_ON_FALSE(gpu_board_config, AVDK_ERR_GENERIC, TAG, "gpu_board_config malloc failed");
    }

    os_memcpy(gpu_board_config, config, sizeof(gpu_board_config_t));
    return AVDK_ERR_OK;
}

gpu_board_config_t *app_gpu_board_config_get(void)
{
    return gpu_board_config;
}
