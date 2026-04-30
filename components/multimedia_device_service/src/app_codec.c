// Copyright 2020-2021 Beken
// Ported from doorbell reference project: HW-flexa H.264 encoder bound to the
// ISP MP channel for the beken_robot MIPI camera pipeline.

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <components/log.h>
#include <driver/int.h>
#include <common/bk_err.h>

#include <components/bk_frame_buffer.h>
#include <components/bk_encode/bk_h264_encode_ctlr.h>
#include <components/bk_encode/bk_h264_encode_types.h>

#include <driver/isp.h>

#include "app_camera.h"
#include "app_codec.h"
#include "multimedia_img_manager.h"

#define TAG "db-codec"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/* Per-frame debug print interval (ms). 0 disables. */
#ifndef APP_H264_DEBUG_INTERVAL_MS
#define APP_H264_DEBUG_INTERVAL_MS  (2000)
#endif

static bk_h264_encode_ctlr_handle_t s_doorbell_enc_handler = NULL;

/* HW-flexa output-buffer request callback: encoder asks the pool for a free
 * frame to write the next encoded H.264 unit into. */
static void *encoder_buffer_request(uint32_t buffer_len, void *args)
{
    (void)args;
    void *buf = NULL;
    if (buffer_len > 0)
    {
        buf = bk_encoded_data_request();
    }
    return buf;
}

/* HW-flexa output-buffer complete callback: encoder signals success/failure
 * for the previously-requested frame; we either push it to the ready queue
 * (consumer can take it) or recycle it back to the free queue. */
static uint32_t encoder_buffer_complete(void *buffer, uint32_t result, void *args)
{
    (void)args;
    if (result == BK_OK)
    {
        bk_encoded_data_complete_request(buffer);
    }
    else
    {
        bk_encoded_data_free_request(buffer);
    }
    return BK_OK;
}

int app_h264e_turn_off(void)
{
    if (s_doorbell_enc_handler == NULL)
    {
        return BK_OK;
    }

    bk_h264_encode_ioctl(s_doorbell_enc_handler, BK_H264_ENCODE_IOCTL_DEBUG_STOP, NULL);

    bk_h264_encode_close(s_doorbell_enc_handler);
    bk_h264_encode_deinit(s_doorbell_enc_handler);
    bk_h264_encode_delete(s_doorbell_enc_handler);
    s_doorbell_enc_handler = NULL;

    /* input == 1: producer side (this encoder) stops. The pool is only
     * actually torn down once the consumer also stops (input == 0). */
    bk_encoded_data_manager_deinit(1);
    return BK_OK;
}

int app_h264e_turn_on(void)
{
    bk_err_t ret = BK_OK;

    if (s_doorbell_enc_handler != NULL)
    {
        LOGW("%s already running\n", __func__);
        return BK_OK;
    }

    /* Allocate the H.264 frame pool (idempotent: re-init only flips flags). */
    bk_encoded_data_manager_init();

    void *isp_handle = app_isp_handle_get();
    if (isp_handle == NULL)
    {
        LOGE("%s: ISP handle is NULL, call app_isp_mipi_camera_turn_on() first\n", __func__);
        return BK_FAIL;
    }

    isp_control_t *isp_control = (isp_control_t *)isp_handle;
    const uint8_t chnl_id = ISP_MP_CHN_ID;

    /* HW flexa encoder reads NV12 directly from the ISP MP channel ring buffer.
     * width/height/buf_cnt/y_addr come from the running ISP MP channel; the
     * caller is responsible for ensuring the MP channel is configured with
     * enable_flexa = 1 (see app_isp_mipi_camera_mp_turn_on()). */
    bk_h264_encode_hw_flexa_config_t config = {
        .width  = isp_control->chn[chnl_id].chn_attr.chnFormat.width,
        .height = isp_control->chn[chnl_id].chn_attr.chnFormat.height,
        .input_format    = BK_PIXEL_FORMAT_NV12,
        .pframe_number   = 30,
        .input_flexa_cnt = 3,
        .input_buf       = isp_control->chn[chnl_id].y_addr,
        .input_size      = isp_control->chn[chnl_id].buf_cnt,
        .outbuf_malloc        = encoder_buffer_request,
        .outbuf_malloc_args   = NULL,
        .outbuf_complete      = encoder_buffer_complete,
        .outbuf_complete_args = NULL,
    };

    LOGI("h264 encoder cfg: %ux%u, flexa_cnt=%u, input_buf=0x%08x, ring=%u\n",
         (unsigned)config.width, (unsigned)config.height,
         (unsigned)config.input_flexa_cnt, (unsigned)config.input_buf,
         (unsigned)config.input_size);

    ret = bk_h264_encode_hw_flexa_new(&s_doorbell_enc_handler, &config);
    if (ret != BK_OK)
    {
        LOGE("Create H.264 encoder failed: %d\r\n", ret);
        return ret;
    }

    ret = bk_h264_encode_init(s_doorbell_enc_handler);
    if (ret != BK_OK)
    {
        LOGE("Init H.264 encoder failed: %d\r\n", ret);
        bk_h264_encode_delete(s_doorbell_enc_handler);
        s_doorbell_enc_handler = NULL;
        return ret;
    }

    ret = bk_h264_encode_open(s_doorbell_enc_handler);
    if (ret != BK_OK)
    {
        LOGE("Open H.264 encoder failed: %d\r\n", ret);
        bk_h264_encode_deinit(s_doorbell_enc_handler);
        bk_h264_encode_delete(s_doorbell_enc_handler);
        s_doorbell_enc_handler = NULL;
        return ret;
    }

#if (APP_H264_DEBUG_INTERVAL_MS > 0)
    {
        uint32_t debug_interval = APP_H264_DEBUG_INTERVAL_MS;
        bk_h264_encode_ioctl(s_doorbell_enc_handler, BK_H264_ENCODE_IOCTL_DEBUG_START, &debug_interval);
    }
#endif
    LOGI("%s ok\n", __func__);
    return ret;
}

void *app_h264_encode_handle_get(void)
{
    return s_doorbell_enc_handler;
}
