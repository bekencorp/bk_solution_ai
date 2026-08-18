// Copyright 2025-2026 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "video_engine.h"
#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include <driver/gpio.h>
#include <driver/flash.h>
#include <components/bk_frame_buffer.h>
#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
#include "video_frame_que.h"
#include <components/bk_camera_ctlr_types.h>
#include <components/dvp_camera_types.h>
#endif
#include "network_engine.h"

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
#include <components/bk_flexa_bond.h>
#include "app_camera.h"
#include "app_camera_types.h"
#include "app_codec.h"
#include "multimedia_img_manager.h"
#endif

#define TAG "video_engine"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define GPIO_INVALID_ID           (0xFF)
#ifdef CONFIG_DVP_CTRL_POWER_GPIO_ID
#define DVP_POWER_GPIO_ID CONFIG_DVP_CTRL_POWER_GPIO_ID
#else
#define DVP_POWER_GPIO_ID GPIO_INVALID_ID
#endif

/* Video frame transfer task configuration */
#define VIDEO_TRANSFER_TASK_NAME        "video_xfer"
#define VIDEO_TRANSFER_TASK_PRIORITY    5
#define VIDEO_TRANSFER_TASK_STACK_SIZE  (4 * 1024)
#define VIDEO_TRANSFER_QUEUE_TIMEOUT    (100) //(BEKEN_WAIT_FOREVER)
#define VIDEO_TRANSFER_STOP_WAIT_MS     (3000) //(1000)
#define VIDEO_TRANSFER_STOP_POLL_MS     (20)
#define VIDEO_MIPI_ENCODER_DRAIN_MS     (80)
#define VIDEO_PREVIEW_TASK_NAME         "ve_preview"
#define VIDEO_PREVIEW_TASK_STACK_SIZE   (4 * 1024)
#define VIDEO_PREVIEW_STOP_WAIT_MS      (1000)
#define VIDEO_PREVIEW_STOP_POLL_MS      (20)
#define VIDEO_PREVIEW_READ_TIMEOUT_MS   (350)
#define VIDEO_PREVIEW_DEFAULT_FPS       (15)
#define VIDEO_PREVIEW_MAX_FPS           (20)

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
#define VIDEO_ENGINE_MIPI_CAM_SCL       GPIO_70
#define VIDEO_ENGINE_MIPI_CAM_SDA       GPIO_71
#define VIDEO_ENGINE_MIPI_CAM_RESET     GPIO_31
#define VIDEO_ENGINE_MIPI_CAM_XCLK      GPIO_59
#define VIDEO_ENGINE_MIPI_CAM_I2C_ID    1

#ifndef CONFIG_VIDEO_ENGINE_MIPI_SENSOR_WIDTH
#define CONFIG_VIDEO_ENGINE_MIPI_SENSOR_WIDTH 1280
#endif
#ifndef CONFIG_VIDEO_ENGINE_MIPI_SENSOR_HEIGHT
#define CONFIG_VIDEO_ENGINE_MIPI_SENSOR_HEIGHT 720
#endif
#ifndef CONFIG_VIDEO_ENGINE_MIPI_SENSOR_FPS
#define CONFIG_VIDEO_ENGINE_MIPI_SENSOR_FPS 25
#endif
#endif

typedef enum {
    VIDEO_ENGINE_CAMERA_UNKNOWN = 0,
    VIDEO_ENGINE_CAMERA_DVP,
    VIDEO_ENGINE_CAMERA_MIPI,
} video_engine_camera_type_t;

/**
 * @brief Video engine internal context structure
 */
typedef struct {
#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
    bk_camera_ctlr_handle_t camera_handle;      /**< Camera controller handle */
#endif
    image_format_t transfer_format;             /**< Transfer format (IMAGE_MJPEG/IMAGE_H264/IMAGE_H265) */
    beken_thread_t transfer_task_handle;        /**< Transfer task handle */
    bool transfer_task_running;                 /**< Transfer task running flag */
    bool camera_opened;                         /**< Camera pipeline opened flag */
    bool use_encoded_manager;                   /**< Frames come from multimedia encoded-frame manager */
#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
    void *h264_bond;                            /**< ISP MP -> H.264 encoder flexa bond */
    uint16_t encoded_width;                     /**< Encoded frame width for metadata fixup */
    uint16_t encoded_height;                    /**< Encoded frame height for metadata fixup */
    bool preview_sp_opened;                     /**< ISP SP channel opened for local preview/capture */
    bool preview_running;                       /**< Preview worker loop flag */
    beken_thread_t preview_task_handle;         /**< Local RGB565 preview worker */
    uint16_t preview_in_width;                  /**< SP NV12 input width */
    uint16_t preview_in_height;                 /**< SP NV12 input height */
    uint16_t preview_out_width;                 /**< RGB565 callback width */
    uint16_t preview_out_height;                /**< RGB565 callback height */
    uint16_t preview_rotate;                    /**< 0/90/180/270 */
    uint8_t preview_fps;                        /**< Local preview frame rate */
    uint8_t *preview_nv12_buf;                  /**< SP NV12 scratch buffer */
    uint8_t *preview_rgb565_buf;                /**< RGB565 output buffer */
    uint32_t preview_nv12_size;
    uint32_t preview_rgb565_size;
    video_engine_preview_sink_t preview_sink;
    void *preview_user_data;
    beken_mutex_t preview_lock;                 /**< Serializes preview start/stop/task lifecycle */
#endif
    /* Engine state */
    bool is_started;                            /**< Video engine started flag */
} video_engine_ctx_t;

/* Global video engine context */
static video_engine_ctx_t *g_video_engine_ctx = NULL;
static video_engine_camera_type_t curr_cam_type = VIDEO_ENGINE_CAMERA_UNKNOWN;
/*
 * Set for the whole video_engine_stop()/deinit() critical section. Preview
 * must not start while this is true: is_started/camera_opened can still look
 * valid during a long MIPI/H.264 close, and a concurrent preview_start was the
 * BK7259SW-2665 UAF (ve_preview blocked on a semaphore torn down by stop).
 *
 * SMP (CPU2/CPU3): a plain store is not enough for cross-core visibility.
 * video_engine_raise_shutting_down() publishes via preview_lock
 * acquire/release so the other core's later lock in preview_start sees the
 * flag before create_thread.
 */
static volatile bool s_video_engine_shutting_down = false;

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
static void video_engine_raise_shutting_down(void)
{
    s_video_engine_shutting_down = true;
    /* Release-acquire publish to the other AP core. */
    if (g_video_engine_ctx != NULL && g_video_engine_ctx->preview_lock != NULL) {
        rtos_lock_mutex(&g_video_engine_ctx->preview_lock);
        rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);
    }
}
#else
static void video_engine_raise_shutting_down(void)
{
    s_video_engine_shutting_down = true;
}
#endif

static void video_engine_transfer_task(void *arg);

#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
/**
 * @brief Frame buffer allocation callback
 */
static frame_buffer_t *video_engine_frame_malloc(image_format_t format, uint32_t size)
{
    return frame_queue_malloc(format, size);

}

/**
 * @brief Frame completion callback
 */
static void video_engine_frame_complete(image_format_t format, frame_buffer_t *frame, int result)
{
    if (result != AVDK_ERR_OK)
    {
        frame_queue_free(format, frame);
    }
    else
    {
        frame_queue_complete(format, frame);
    }
    
}

/**
 * @brief DVP camera callbacks
 */
static const bk_dvp_callback_t dvp_camera_cbs = {
    .malloc = video_engine_frame_malloc,
    .complete = video_engine_frame_complete,
};
#endif

static const char *video_engine_frame_format_name(uint32_t fmt)
{
    if (fmt == IMAGE_H264 || fmt == PIXEL_FMT_H264)
    {
        return "H264";
    }
    if (fmt == IMAGE_MJPEG || fmt == PIXEL_FMT_JPEG)
    {
        return "MJPEG";
    }
    if (fmt == IMAGE_H265 || fmt == PIXEL_FMT_H265)
    {
        return "H265";
    }
    if (fmt == IMAGE_YUV)
    {
        return "YUV";
    }
    return "UNKNOWN";
}

static const char *video_engine_h264_frame_type_name(uint32_t h264_type)
{
    if (h264_type == 0 || (h264_type & (1U << 24)))
    {
        return "I";
    }
    if (h264_type == 1 || (h264_type & (1U << 23)))
    {
        return "P";
    }
    if (h264_type == 2 || (h264_type & (1U << 22)))
    {
        return "B";
    }
    return "UNKNOWN";
}

static void video_engine_log_frame_info(const frame_buffer_t *frame)
{
    static uint32_t s_debug_count = 0;
    bool is_h264 = false;
    bool is_key_frame = false;

    if (frame == NULL)
    {
        return;
    }

    is_h264 = (frame->fmt == IMAGE_H264 || frame->fmt == PIXEL_FMT_H264);
    is_key_frame = is_h264 && (video_engine_h264_frame_type_name(frame->h264_type)[0] == 'I');

    s_debug_count++;
    if ((s_debug_count % 30) != 0 && !is_key_frame)
    {
        //return;
    }

    LOGI("frame info: seq=%u fmt=%s(0x%x) h264=%s(type=0x%x) %ux%u len=%u size=%u ts=%u data=%p\n",
         frame->sequence,
         video_engine_frame_format_name(frame->fmt),
         frame->fmt,
         video_engine_h264_frame_type_name(frame->h264_type),
         frame->h264_type,
         frame->width,
         frame->height,
         frame->length,
         frame->size,
         frame->timestamp,
         frame->frame);
}

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
static void video_engine_fill_encoded_frame_info(frame_buffer_t *frame)
{
    if (g_video_engine_ctx == NULL || frame == NULL)
    {
        return;
    }

    frame->fmt = IMAGE_H264;
    frame->width = g_video_engine_ctx->encoded_width;
    frame->height = g_video_engine_ctx->encoded_height;
    if (frame->timestamp == 0)
    {
        frame->timestamp = rtos_get_time();
    }
}
#endif


/**
 * @brief Video frame transfer task
 * 
 * This task continuously pops frames from the frame queue and processes them.
 * 使用 rtos_pop_from_queue() 方式从 frame_queue 内循环取帧操作
 */
static void video_engine_transfer_task(void *arg)
{
    frame_buffer_t *frame = NULL;
    bk_err_t ret = BK_OK;
    
    LOGI("%s: video transfer task started\n", __func__);

    /* 循环从 frame_queue 中取帧 */
    while (g_video_engine_ctx && g_video_engine_ctx->transfer_task_running)
    {
        /* Double check context validity at the start of each loop */
        if (g_video_engine_ctx == NULL) {
            LOGE("%s: g_video_engine_ctx became NULL, exiting task\n", __func__);
            break;
        }
        
        if (g_video_engine_ctx->use_encoded_manager)
        {
#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
            frame = (frame_buffer_t *)bk_encoded_complete_data_request(100);
#else
            frame = NULL;
#endif
        }
        else
        {
#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
            /* 使用 frame_queue_get_frame 从 frame_queue 取帧
             * 该函数内部调用 rtos_pop_from_queue() 从 ready_queue 中 pop 取帧
             */
            frame = frame_queue_get_frame(g_video_engine_ctx->transfer_format,
                                          VIDEO_TRANSFER_QUEUE_TIMEOUT);
#else
            frame = NULL;
#endif
        }

        if (frame != NULL)
        {
            /* Check context before sending */
            if (g_video_engine_ctx == NULL) {
                LOGE("%s: g_video_engine_ctx became NULL, releasing frame\n", __func__);
                break;
            }

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
            if (g_video_engine_ctx->use_encoded_manager)
            {
                video_engine_fill_encoded_frame_info(frame);
            }
#endif

            /* 直接调用 ntwk_eng_send_video() 发送帧数据 */
            //video_engine_log_frame_info(frame);
            ret = ntwk_eng_send_video(frame);
            if (ret != BK_OK)
            {
                //LOGW("%s: ntwk_eng_send_video failed, ret=%d\n", __func__, ret);
            }

            /* 处理完成后释放帧 */
            if (g_video_engine_ctx->use_encoded_manager)
            {
#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
                bk_encoded_complete_data_free_request((uint8_t *)frame);
#endif
            }
            else
            {
#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
                frame_queue_free(g_video_engine_ctx->transfer_format, frame);
#endif
            }
            frame = NULL;
        }
        else
        {
            continue;
        }
    }
    
    LOGI("%s: video transfer task exit\n", __func__);
    
    if (g_video_engine_ctx != NULL) {
        g_video_engine_ctx->transfer_task_handle = NULL;
    }
    
    rtos_delete_thread(NULL);
}


/* ============================= Public APIs ============================= */

int video_engine_init(void)
{
    bk_err_t ret = BK_OK;

    if (g_video_engine_ctx != NULL) {
        if (g_video_engine_ctx->is_started) {
            LOGD("%s: Video engine already initialized and started\n", __func__);
            return BK_OK;
        } else {
            LOGW("%s: Video engine context exists but not started, restarting\n", __func__);
            return video_engine_start();
        }
    }
    
    g_video_engine_ctx = (video_engine_ctx_t *)os_malloc(sizeof(video_engine_ctx_t));
    if (g_video_engine_ctx == NULL) {
        LOGE("%s: Failed to allocate memory for video_engine_ctx\n", __func__);
        return BK_FAIL;
    }
    
    memset(g_video_engine_ctx, 0, sizeof(video_engine_ctx_t));

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
    if (rtos_init_mutex(&g_video_engine_ctx->preview_lock) != BK_OK) {
        LOGE("%s: preview_lock init failed\n", __func__);
        os_free(g_video_engine_ctx);
        g_video_engine_ctx = NULL;
        return BK_FAIL;
    }
#endif

#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
    ret = frame_queue_init_all();
    if (ret != BK_OK) {
        LOGE("%s: frame_queue_init_all failed, ret=%d\n", __func__, ret);
#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
        rtos_deinit_mutex(&g_video_engine_ctx->preview_lock);
#endif
        os_free(g_video_engine_ctx);
        g_video_engine_ctx = NULL;
        return ret;
    }
    LOGI("%s: frame_queue initialized\n", __func__);
#endif
    
    /* Start video engine with default configuration */
    ret = video_engine_start();
    if (ret != BK_OK) {
        LOGE("%s: video_engine_start failed, ret=%d\n", __func__, ret);
        
#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
        frame_queue_deinit_all();
#endif
#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
        rtos_deinit_mutex(&g_video_engine_ctx->preview_lock);
#endif
        os_free(g_video_engine_ctx);
        g_video_engine_ctx = NULL;
        
        return ret;
    }
    
    LOGI("%s: Video engine initialized successfully\n", __func__);
    return BK_OK;
}

int video_engine_deinit(void)
{
    bk_err_t ret = BK_OK;
    
    LOGI("%s: Deinitializing video engine\n", __func__);
    
    if (g_video_engine_ctx == NULL)
    {
        LOGD("%s: g_video_engine_ctx is NULL, already deinitialized\n", __func__);
        return BK_OK;
    }

    /* Keep the barrier raised across stop + ctx free so a concurrent
     * preview_start cannot observe a half-destroyed engine. Publish to the
     * other SMP core before teardown work begins. */
    video_engine_raise_shutting_down();
    
    ret = video_engine_stop();
    if (ret != BK_OK) {
        LOGE("%s: video_engine_stop failed, ret=%d\n", __func__, ret);
    }
    
#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
    frame_queue_deinit_all();
    LOGI("%s: frame_queue deinitialized\n", __func__);
#endif

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
    rtos_deinit_mutex(&g_video_engine_ctx->preview_lock);
#endif

    os_free(g_video_engine_ctx);
    /* Null the pointer before clearing the flag so unlocked readers that see
     * shutting_down==false cannot observe a stale non-NULL ctx (SMP). */
    g_video_engine_ctx = NULL;
    s_video_engine_shutting_down = false;
    
    LOGI("%s: Video engine deinitialized successfully\n", __func__);
    
    return BK_OK;
}

#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
int video_engine_dvp_camera_open(camera_parameters_t *parameters)
{
    avdk_err_t ret = AVDK_ERR_OK;

    if (parameters == NULL)
    {
        LOGE("video_engine_dvp_camera_open: parameters is NULL");
        return BK_FAIL;
    }

    if (g_video_engine_ctx == NULL)
    {
        LOGE("%s: g_video_engine_ctx is NULL, call video_engine_info_init() first\n", __func__);
        return BK_FAIL;
    }

    if (g_video_engine_ctx->camera_handle != NULL)
    {
        LOGE("%s, dvp camera have been already opened!\n", __func__);
        return ret;
    }

   // power on dvp
   if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
   {
       GPIO_UP(DVP_POWER_GPIO_ID);
       LOGD("%s, power on dvp successful, GPIO_ID: %d\n", __func__, DVP_POWER_GPIO_ID);
   }

    /* Configure DVP camera */
    bk_dvp_config_t dvp_config = BK_DVP_864X480_30FPS_MJPEG_CONFIG();
    
    if (parameters->format == 0) // wifi transfer format 0/1:mjpeg/h264
    {
        dvp_config.img_format = IMAGE_MJPEG;
        g_video_engine_ctx->transfer_format = IMAGE_MJPEG;
    }
    else
    {
        dvp_config.img_format =  IMAGE_H264;
        g_video_engine_ctx->transfer_format = IMAGE_H264;
    }


    dvp_config.width = parameters->width;
    dvp_config.height = parameters->height;
    dvp_config.reset_pin = 28;

    LOGD("%s: DVP config - %dx%d, img_format:%d\n", __func__, 
         dvp_config.width, dvp_config.height, dvp_config.img_format);

    bk_dvp_ctlr_config_t dvp_ctlr_config = {
        .config = dvp_config,
        .cbs = &dvp_camera_cbs,
    };

    ret = bk_camera_dvp_ctlr_new(&g_video_engine_ctx->camera_handle, &dvp_ctlr_config);
    if (ret == BK_OK)
    {
        LOGD("%s: bk_camera_dvp_ctlr_new successful\n", __func__);
        ret = bk_camera_open(g_video_engine_ctx->camera_handle);
        if (ret != BK_OK)
        {
            LOGE("%s: bk_camera_open failed, ret=%d\n", __func__, ret);
            bk_camera_delete(g_video_engine_ctx->camera_handle);
            g_video_engine_ctx->camera_handle = NULL;
        }
        else
        {
            g_video_engine_ctx->camera_opened = true;
            g_video_engine_ctx->use_encoded_manager = false;
            LOGI("%s: Camera opened successfully\n", __func__);
        }
    }
    else
    {
        LOGE("%s: bk_camera_dvp_ctlr_new failed, ret=%d\n", __func__, ret);
    }

    return ret;
}
#else
int video_engine_dvp_camera_open(camera_parameters_t *parameters)
{
    (void)parameters;
    LOGE("%s: DVP camera is not enabled\n", __func__);
    return BK_FAIL;
}
#endif

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
static int video_engine_mipi_camera_open(camera_parameters_t *parameters)
{
#if !(CONFIG_ISP && CONFIG_MIPI_CSI && CONFIG_CSI_CAMERA && CONFIG_MULTIMEDIA_H264_ENCODE)
    (void)parameters;
    LOGE("%s: BK7259 MIPI/H264 camera dependencies are not enabled\n", __func__);
    return BK_FAIL;
#else
    bk_err_t ret = BK_OK;

    if (parameters == NULL)
    {
        LOGE("%s: parameters is NULL\n", __func__);
        return BK_FAIL;
    }

    if (g_video_engine_ctx == NULL)
    {
        LOGE("%s: g_video_engine_ctx is NULL\n", __func__);
        return BK_FAIL;
    }

    if (g_video_engine_ctx->camera_opened)
    {
        LOGW("%s: camera already opened\n", __func__);
        return BK_OK;
    }

    if (parameters->format != 1)
    {
        LOGE("%s: BK7259 MIPI path only supports H264 transfer\n", __func__);
        return BK_FAIL;
    }

    camera_board_config_t cfg = {0};
    cfg.mipi.enable             = true;
    cfg.mipi.pin_scl            = VIDEO_ENGINE_MIPI_CAM_SCL;
    cfg.mipi.pin_sda            = VIDEO_ENGINE_MIPI_CAM_SDA;
    cfg.mipi.i2c_id             = VIDEO_ENGINE_MIPI_CAM_I2C_ID;
    cfg.mipi.pin_reset          = VIDEO_ENGINE_MIPI_CAM_RESET;
    cfg.mipi.pin_pwdn           = -1;
    cfg.mipi.pin_xclk           = VIDEO_ENGINE_MIPI_CAM_XCLK;
    cfg.mipi.sensor_max_width   = CONFIG_VIDEO_ENGINE_MIPI_SENSOR_WIDTH;
    cfg.mipi.sensor_max_height  = CONFIG_VIDEO_ENGINE_MIPI_SENSOR_HEIGHT;
    cfg.mipi.sensor_fps         = CONFIG_VIDEO_ENGINE_MIPI_SENSOR_FPS;
    cfg.mipi.hmirror            = 1;
    cfg.mipi.vflip              = 0;

    cfg.isp.mp_enable = true;
    cfg.isp.mp_flexa  = true;
    cfg.isp.mp_width  = parameters->width;
    cfg.isp.mp_height = parameters->height;
    cfg.isp.mp_format = BK_PIXEL_FORMAT_NV12;

    ret = app_camera_board_config_set(&cfg);
    if (ret != BK_OK)
    {
        LOGE("%s: app_camera_board_config_set failed, ret=%d\n", __func__, ret);
        return ret;
    }

    ret = app_isp_mipi_camera_turn_on(app_camera_board_config_get());
    if (ret != BK_OK)
    {
        LOGE("%s: app_isp_mipi_camera_turn_on failed, ret=%d\n", __func__, ret);
        return ret;
    }

    ret = app_h264e_turn_on();
    if (ret != BK_OK)
    {
        LOGE("%s: app_h264e_turn_on failed, ret=%d\n", __func__, ret);
        app_isp_camera_turn_off();
        return ret;
    }

    void *isp_handle = app_isp_handle_get();
    void *enc_handle = app_h264_encode_handle_get();
    if (isp_handle == NULL || enc_handle == NULL)
    {
        LOGE("%s: isp_handle=%p enc_handle=%p\n", __func__, isp_handle, enc_handle);
        app_h264e_turn_off();
        app_isp_camera_turn_off();
        return BK_FAIL;
    }

    ret = bk_flexa_isp_h264e_bond_start(&g_video_engine_ctx->h264_bond, isp_handle, enc_handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_flexa_isp_h264e_bond_start failed, ret=%d\n", __func__, ret);
        g_video_engine_ctx->h264_bond = NULL;
        app_h264e_turn_off();
        app_isp_camera_turn_off();
        return ret;
    }

    g_video_engine_ctx->transfer_format = IMAGE_H264;
    g_video_engine_ctx->encoded_width = parameters->width;
    g_video_engine_ctx->encoded_height = parameters->height;
    g_video_engine_ctx->use_encoded_manager = true;
    g_video_engine_ctx->camera_opened = true;

    LOGI("%s: MIPI camera opened: sensor %ux%u@%u -> ISP MP %ux%u H264\n",
         __func__,
         CONFIG_VIDEO_ENGINE_MIPI_SENSOR_WIDTH,
         CONFIG_VIDEO_ENGINE_MIPI_SENSOR_HEIGHT,
         CONFIG_VIDEO_ENGINE_MIPI_SENSOR_FPS,
         parameters->width,
         parameters->height);

    return BK_OK;
#endif
}

static int video_engine_mipi_camera_close(void)
{
#if !(CONFIG_ISP && CONFIG_MIPI_CSI && CONFIG_CSI_CAMERA && CONFIG_MULTIMEDIA_H264_ENCODE)
    return BK_OK;
#else
    bk_err_t ret = BK_OK;
    bk_err_t final_ret = BK_OK;

    if (g_video_engine_ctx == NULL)
    {
        return BK_OK;
    }

    if (g_video_engine_ctx->h264_bond != NULL)
    {
        bk_flexa_isp_h264e_bond_stop(g_video_engine_ctx->h264_bond);
        g_video_engine_ctx->h264_bond = NULL;
        rtos_delay_milliseconds(VIDEO_MIPI_ENCODER_DRAIN_MS);
    }

    ret = app_h264e_turn_off();
    if (ret != BK_OK)
    {
        LOGE("%s: app_h264e_turn_off failed, ret=%d\n", __func__, ret);
        final_ret = ret;
    }

    ret = app_isp_camera_turn_off();
    if (ret != BK_OK)
    {
        LOGE("%s: app_isp_camera_turn_off failed, ret=%d\n", __func__, ret);
        final_ret = ret;
    }

    g_video_engine_ctx->use_encoded_manager = false;
    g_video_engine_ctx->encoded_width = 0;
    g_video_engine_ctx->encoded_height = 0;
    g_video_engine_ctx->preview_sp_opened = false;
    g_video_engine_ctx->preview_in_width = 0;
    g_video_engine_ctx->preview_in_height = 0;
    g_video_engine_ctx->camera_opened = false;

    return final_ret;
#endif
}
#endif

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
static uint8_t video_engine_clip_u8(int value)
{
    if (value < 0)
    {
        return 0;
    }
    if (value > 255)
    {
        return 255;
    }
    return (uint8_t)value;
}

static uint16_t video_engine_rgb_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint16_t)(r & 0xF8) << 8) |
                      ((uint16_t)(g & 0xFC) << 3) |
                      ((uint16_t)b >> 3));
}

static void video_engine_preview_map_xy(uint16_t out_x,
                                        uint16_t out_y,
                                        uint16_t in_w,
                                        uint16_t in_h,
                                        uint16_t rotate,
                                        uint16_t *src_x,
                                        uint16_t *src_y)
{
    switch (rotate)
    {
    case 90:
        *src_x = out_y;
        *src_y = (uint16_t)(in_h - 1U - out_x);
        break;
    case 180:
        *src_x = (uint16_t)(in_w - 1U - out_x);
        *src_y = (uint16_t)(in_h - 1U - out_y);
        break;
    case 270:
        *src_x = (uint16_t)(in_w - 1U - out_y);
        *src_y = out_x;
        break;
    default:
        *src_x = out_x;
        *src_y = out_y;
        break;
    }
}

static void video_engine_nv12_to_rgb565(const uint8_t *nv12,
                                        uint8_t *rgb565,
                                        uint16_t in_w,
                                        uint16_t in_h,
                                        uint16_t rotate,
                                        uint16_t out_w,
                                        uint16_t out_h)
{
    /* Defensive: a torn-down preview can hand us freed/NULL buffers. Bailing
     * out here turns a fatal NULL/UAF dereference into a dropped frame
     * (root cause of the BK7259SW-2402 MemFault). */
    if (nv12 == NULL || rgb565 == NULL)
    {
        return;
    }

    /* Logical size after rotation (before scaling to the display size). */
    uint16_t rot_w = (rotate == 90 || rotate == 270) ? in_h : in_w;
    uint16_t rot_h = (rotate == 90 || rotate == 270) ? in_w : in_h;
    uint16_t *dst = (uint16_t *)rgb565;
    const uint8_t *uv = nv12 + ((uint32_t)in_w * in_h);

    for (uint16_t y = 0; y < out_h; y++)
    {
        /* Nearest-neighbor scale from display row to rotated-space row. */
        uint16_t ry = (uint16_t)(((uint32_t)y * rot_h) / out_h);
        if (ry >= rot_h)
        {
            ry = (uint16_t)(rot_h - 1U);
        }
        for (uint16_t x = 0; x < out_w; x++)
        {
            uint16_t rx = (uint16_t)(((uint32_t)x * rot_w) / out_w);
            if (rx >= rot_w)
            {
                rx = (uint16_t)(rot_w - 1U);
            }

            uint16_t sx;
            uint16_t sy;
            video_engine_preview_map_xy(rx, ry, in_w, in_h, rotate, &sx, &sy);

            uint8_t yv = nv12[(uint32_t)sy * in_w + sx];
            uint32_t uv_index = ((uint32_t)(sy >> 1) * in_w) + (uint32_t)(sx & ~1U);
            int u = (int)uv[uv_index] - 128;
            int v = (int)uv[uv_index + 1] - 128;
            int c = (int)yv - 16;
            if (c < 0)
            {
                c = 0;
            }

            uint8_t r = video_engine_clip_u8((298 * c + 409 * v + 128) >> 8);
            uint8_t g = video_engine_clip_u8((298 * c - 100 * u - 208 * v + 128) >> 8);
            uint8_t b = video_engine_clip_u8((298 * c + 516 * u + 128) >> 8);
            dst[(uint32_t)y * out_w + x] = video_engine_rgb_to_rgb565(r, g, b);
        }
    }
}

static void video_engine_preview_task(void *arg)
{
    (void)arg;
    LOGI("%s: started\n", __func__);

    video_engine_ctx_t *ctx = g_video_engine_ctx;
    if (ctx == NULL)
    {
        rtos_delete_thread(NULL);
        return;
    }

    /* This task is the sole owner of the preview buffers for its whole
     * lifetime: they are allocated by preview_start() and freed *only* here on
     * exit. A concurrent preview_stop()/deinit() therefore can never free them
     * out from under an in-flight frame - that use-after-free (buffer freed
     * after the 1000 ms stop timeout while the task kept running) was the root
     * cause of the BK7259SW-2402 NULL/UAF MemFault. */
    rtos_lock_mutex(&ctx->preview_lock);
    uint8_t *my_nv12 = ctx->preview_nv12_buf;
    uint8_t *my_rgb565 = ctx->preview_rgb565_buf;
    rtos_unlock_mutex(&ctx->preview_lock);

    while (1)
    {
        /* Snapshot everything the frame needs under the lock so we never
         * re-read a global that another core may be tearing down mid-iteration. */
        rtos_lock_mutex(&ctx->preview_lock);
        bool running = ctx->preview_running;
        uint8_t *nv12 = ctx->preview_nv12_buf;
        uint8_t *rgb565 = ctx->preview_rgb565_buf;
        uint32_t nv12_size = ctx->preview_nv12_size;
        uint16_t in_w = ctx->preview_in_width;
        uint16_t in_h = ctx->preview_in_height;
        uint16_t rotate = ctx->preview_rotate;
        uint16_t out_w = ctx->preview_out_width;
        uint16_t out_h = ctx->preview_out_height;
        video_engine_preview_sink_t sink = ctx->preview_sink;
        void *user_data = ctx->preview_user_data;
        uint32_t fps = ctx->preview_fps ? ctx->preview_fps : VIDEO_PREVIEW_DEFAULT_FPS;
        rtos_unlock_mutex(&ctx->preview_lock);

        if (!running || nv12 == NULL || rgb565 == NULL)
        {
            break;
        }

        uint32_t frame_interval_ms = 1000U / fps;
        uint32_t loop_start = rtos_get_time();

        if (app_isp_camera_channel_read(APP_ISP_SP_CHN_ID,
                                        nv12,
                                        nv12_size,
                                        VIDEO_PREVIEW_READ_TIMEOUT_MS) == BK_OK)
        {
            video_engine_nv12_to_rgb565(nv12, rgb565, in_w, in_h, rotate, out_w, out_h);

            if (sink != NULL)
            {
                sink(rgb565, out_w, out_h, user_data);
            }
        }

        /* The SP read already blocks at the sensor frame rate, so only pad
         * out the remaining time toward the target interval instead of
         * adding a full fixed delay on top of the capture latency. */
        uint32_t elapsed = rtos_get_time() - loop_start;
        if (elapsed < frame_interval_ms)
        {
            rtos_delay_milliseconds(frame_interval_ms - elapsed);
        }
        else
        {
            rtos_delay_milliseconds(1);
        }
    }

    /* Publish "task gone" and release ownership of the buffers atomically.
     * Only clear the ctx pointers if they still refer to this task's buffers
     * (a fresh preview_start is blocked until preview_task_handle is NULL, so
     * in practice they always match here). */
    rtos_lock_mutex(&ctx->preview_lock);
    if (ctx->preview_nv12_buf == my_nv12)
    {
        ctx->preview_nv12_buf = NULL;
        ctx->preview_nv12_size = 0;
    }
    if (ctx->preview_rgb565_buf == my_rgb565)
    {
        ctx->preview_rgb565_buf = NULL;
        ctx->preview_rgb565_size = 0;
    }
    ctx->preview_task_handle = NULL;
    rtos_unlock_mutex(&ctx->preview_lock);

    if (my_nv12 != NULL)
    {
        bk_frame_buffer_free(my_nv12);
    }
    if (my_rgb565 != NULL)
    {
        bk_frame_buffer_free(my_rgb565);
    }

    LOGI("%s: exit\n", __func__);
    rtos_delete_thread(NULL);
}

int video_engine_preview_start(const video_engine_preview_config_t *config)
{
    bk_err_t ret;

    if (!video_engine_is_preview_ready())
    {
        LOGW("%s: camera not ready (running=%d shutting_down=%d)\n",
             __func__,
             (int)video_engine_is_running(),
             (int)s_video_engine_shutting_down);
        return BK_FAIL;
    }
    if (curr_cam_type != VIDEO_ENGINE_CAMERA_MIPI)
    {
        LOGW("%s: only MIPI camera preview is supported\n", __func__);
        return BK_FAIL;
    }
    if (config == NULL || config->sink == NULL ||
        config->width == 0 || config->height == 0)
    {
        LOGE("%s: invalid config\n", __func__);
        return BK_FAIL;
    }
    if (config->rotate != 0 && config->rotate != 90 &&
        config->rotate != 180 && config->rotate != 270)
    {
        LOGE("%s: unsupported rotate=%u\n", __func__, config->rotate);
        return BK_FAIL;
    }

    /* Serialize with stop()/preview_stop() on the other AP core. */
    rtos_lock_mutex(&g_video_engine_ctx->preview_lock);
    if (s_video_engine_shutting_down ||
        !g_video_engine_ctx->is_started ||
        !g_video_engine_ctx->camera_opened)
    {
        rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);
        LOGW("%s: camera not ready under lock\n", __func__);
        return BK_FAIL;
    }
    if (g_video_engine_ctx->preview_running)
    {
        rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);
        LOGI("%s: already running\n", __func__);
        return BK_OK;
    }
    /* A previous preview task may still be winding down (it owns/frees its own
     * buffers). Never overlap two preview tasks: bail out and let the caller
     * retry once the old one has fully exited (preview_task_handle == NULL). */
    if (g_video_engine_ctx->preview_task_handle != NULL)
    {
        rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);
        LOGW("%s: previous preview task still exiting, retry later\n", __func__);
        return BK_FAIL;
    }
    rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);

    uint16_t rot_w = (config->rotate == 90 || config->rotate == 270) ?
                     config->height : config->width;
    uint16_t rot_h = (config->rotate == 90 || config->rotate == 270) ?
                     config->width : config->height;
    /* Display output size: caller-provided target, or the rotated size. */
    uint16_t out_w = config->out_width ? config->out_width : rot_w;
    uint16_t out_h = config->out_height ? config->out_height : rot_h;

    if (g_video_engine_ctx->preview_sp_opened &&
        (g_video_engine_ctx->preview_in_width != config->width ||
         g_video_engine_ctx->preview_in_height != config->height))
    {
        LOGE("%s: SP already opened as %ux%u, cannot switch to %ux%u without camera restart\n",
             __func__,
             g_video_engine_ctx->preview_in_width,
             g_video_engine_ctx->preview_in_height,
             config->width,
             config->height);
        return BK_FAIL;
    }

    if (!g_video_engine_ctx->preview_sp_opened)
    {
        if (!video_engine_is_preview_ready())
        {
            LOGW("%s: abort SP open, engine shutting down\n", __func__);
            return BK_FAIL;
        }
        camera_board_config_t *board_config = app_camera_board_config_get();
        if (board_config == NULL)
        {
            LOGE("%s: camera board config is NULL\n", __func__);
            return BK_FAIL;
        }
        board_config->isp.sp_enable = 1;
        board_config->isp.sp_flexa = 0;
        board_config->isp.sp_width = config->width;
        board_config->isp.sp_height = config->height;
        board_config->isp.sp_format = BK_PIXEL_FORMAT_NV12;

        ret = app_isp_camera_sp_channel_turn_on(board_config);
        if (ret != BK_OK)
        {
            LOGE("%s: app_isp_camera_sp_channel_turn_on failed ret=%d\n", __func__, ret);
            return ret;
        }
        g_video_engine_ctx->preview_sp_opened = true;
    }

    /* Allocate the buffers before taking the lock (malloc can block). */
    uint32_t nv12_size = (uint32_t)config->width * config->height * 3U / 2U;
    uint32_t rgb565_size = (uint32_t)out_w * out_h * 2U;
    uint8_t *nv12_buf = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, nv12_size + 32U);
    uint8_t *rgb565_buf = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, rgb565_size + 32U);
    if (nv12_buf == NULL || rgb565_buf == NULL)
    {
        LOGE("%s: buffer alloc failed nv12=%p rgb565=%p\n", __func__, nv12_buf, rgb565_buf);
        if (nv12_buf != NULL)
        {
            bk_frame_buffer_free(nv12_buf);
        }
        if (rgb565_buf != NULL)
        {
            bk_frame_buffer_free(rgb565_buf);
        }
        return BK_FAIL;
    }

    /* Commit state and hand buffer ownership to the worker atomically.
     * Re-check shutting_down under the lock: stop() may have begun while we
     * were in malloc / SP open (BK7259SW-2665). */
    rtos_lock_mutex(&g_video_engine_ctx->preview_lock);
    if (s_video_engine_shutting_down ||
        !g_video_engine_ctx->is_started ||
        !g_video_engine_ctx->camera_opened)
    {
        rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);
        LOGW("%s: abort commit, engine went down during start\n", __func__);
        bk_frame_buffer_free(nv12_buf);
        bk_frame_buffer_free(rgb565_buf);
        return BK_FAIL;
    }
    g_video_engine_ctx->preview_in_width = config->width;
    g_video_engine_ctx->preview_in_height = config->height;
    g_video_engine_ctx->preview_out_width = out_w;
    g_video_engine_ctx->preview_out_height = out_h;
    g_video_engine_ctx->preview_rotate = config->rotate;
    g_video_engine_ctx->preview_fps = (config->fps == 0) ? VIDEO_PREVIEW_DEFAULT_FPS : config->fps;
    if (g_video_engine_ctx->preview_fps > VIDEO_PREVIEW_MAX_FPS)
    {
        g_video_engine_ctx->preview_fps = VIDEO_PREVIEW_MAX_FPS;
    }
    g_video_engine_ctx->preview_sink = config->sink;
    g_video_engine_ctx->preview_user_data = config->user_data;
    g_video_engine_ctx->preview_nv12_size = nv12_size;
    g_video_engine_ctx->preview_rgb565_size = rgb565_size;
    g_video_engine_ctx->preview_nv12_buf = nv12_buf;
    g_video_engine_ctx->preview_rgb565_buf = rgb565_buf;
    g_video_engine_ctx->preview_running = true;
    ret = rtos_create_thread(&g_video_engine_ctx->preview_task_handle,
                             VIDEO_TRANSFER_TASK_PRIORITY,
                             VIDEO_PREVIEW_TASK_NAME,
                             video_engine_preview_task,
                             VIDEO_PREVIEW_TASK_STACK_SIZE,
                             NULL);
    if (ret != BK_OK)
    {
        /* Task never took ownership - clear state and free here. */
        g_video_engine_ctx->preview_running = false;
        g_video_engine_ctx->preview_task_handle = NULL;
        g_video_engine_ctx->preview_nv12_buf = NULL;
        g_video_engine_ctx->preview_rgb565_buf = NULL;
        g_video_engine_ctx->preview_nv12_size = 0;
        g_video_engine_ctx->preview_rgb565_size = 0;
        g_video_engine_ctx->preview_sink = NULL;
        g_video_engine_ctx->preview_user_data = NULL;
        rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);
        LOGE("%s: create preview task failed ret=%d\n", __func__, ret);
        bk_frame_buffer_free(nv12_buf);
        bk_frame_buffer_free(rgb565_buf);
        return BK_FAIL;
    }
    rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);

    LOGI("%s: SP %ux%u -> RGB565 %ux%u rotate=%u fps=%u\n",
         __func__, config->width, config->height, out_w, out_h,
         config->rotate, g_video_engine_ctx->preview_fps);
    return BK_OK;
}

int video_engine_preview_stop(void)
{
    if (g_video_engine_ctx == NULL)
    {
        return BK_OK;
    }

    /* Ask the worker to stop and stop delivering frames to the sink. The
     * worker owns the preview buffers and frees them itself on exit, so we
     * must NOT free them here while it may still be running - that free-then-use
     * was the BK7259SW-2402 crash. */
    rtos_lock_mutex(&g_video_engine_ctx->preview_lock);
    g_video_engine_ctx->preview_running = false;
    g_video_engine_ctx->preview_sink = NULL;
    g_video_engine_ctx->preview_user_data = NULL;
    beken_thread_t handle = g_video_engine_ctx->preview_task_handle;
    rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);

    uint32_t waited_ms = 0;
    while (handle != NULL && waited_ms < VIDEO_PREVIEW_STOP_WAIT_MS)
    {
        rtos_delay_milliseconds(VIDEO_PREVIEW_STOP_POLL_MS);
        waited_ms += VIDEO_PREVIEW_STOP_POLL_MS;
        rtos_lock_mutex(&g_video_engine_ctx->preview_lock);
        handle = g_video_engine_ctx->preview_task_handle;
        rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);
    }

    if (handle != NULL)
    {
        /* Still running: leave the buffers to the worker (it frees them the
         * moment it finishes its current frame) rather than freeing under it. */
        LOGW("%s: preview task did not exit within %u ms (task will free its buffers on exit)\n",
             __func__, VIDEO_PREVIEW_STOP_WAIT_MS);
        return BK_FAIL;
    }

    LOGI("%s: stopped\n", __func__);
    return BK_OK;
}

bool video_engine_preview_is_running(void)
{
    bool running;

    if (g_video_engine_ctx == NULL)
    {
        return false;
    }

    rtos_lock_mutex(&g_video_engine_ctx->preview_lock);
    running = (g_video_engine_ctx->preview_running &&
              g_video_engine_ctx->preview_task_handle != NULL);
    rtos_unlock_mutex(&g_video_engine_ctx->preview_lock);

    return running;
}
#else
int video_engine_preview_start(const video_engine_preview_config_t *config)
{
    (void)config;
    LOGW("%s: MIPI camera disabled\n", __func__);
    return BK_FAIL;
}

int video_engine_preview_stop(void)
{
    return BK_OK;
}

bool video_engine_preview_is_running(void)
{
    return false;
}
#endif

int video_engine_camera_close(void)
{
    bk_err_t ret = BK_OK;

    if (g_video_engine_ctx == NULL)
    {
        LOGE("%s: g_video_engine_ctx is NULL\n", __func__);
        return BK_FAIL;
    }

    if (!g_video_engine_ctx->camera_opened)
    {
        LOGE("%s: camera is not opened\n", __func__);
        return ret;
    }

#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
    if (curr_cam_type == VIDEO_ENGINE_CAMERA_MIPI)
    {
        ret = video_engine_mipi_camera_close();
        curr_cam_type = VIDEO_ENGINE_CAMERA_UNKNOWN;
        return ret;
    }
#endif

#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
    if (g_video_engine_ctx->camera_handle == NULL)
    {
        LOGE("%s: camera_handle is NULL\n", __func__);
        return BK_FAIL;
    }

    ret = bk_camera_close(g_video_engine_ctx->camera_handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_camera_close failed\n", __func__);
        return ret;
    }

    ret = bk_camera_delete(g_video_engine_ctx->camera_handle);
    if (ret != BK_OK)
    {
        LOGE("%s: bk_camera_delete failed\n", __func__);
        return ret;
    }
    else
    {
        LOGD("%s: bk_camera_delete successful\n", __func__);
        g_video_engine_ctx->camera_handle = NULL;
    }

    // power off dvp
    if (DVP_POWER_GPIO_ID != GPIO_INVALID_ID)
    {
        GPIO_DOWN(DVP_POWER_GPIO_ID);
    }
#else
    LOGE("%s: unsupported camera type %d\n", __func__, curr_cam_type);
    return BK_FAIL;
#endif

    g_video_engine_ctx->camera_opened = false;
    curr_cam_type = VIDEO_ENGINE_CAMERA_UNKNOWN;

    return BK_OK;
}


/**
 * @brief Start video transfer task
 * 
 * This function creates a task that continuously pops frames from the frame queue
 * and sends them using ntwk_eng_send_video().
 * 
 * @return BK_OK on success, BK_FAIL otherwise
 */
int video_engine_transfer_start(void)
{
    bk_err_t ret = BK_FAIL;

    if (g_video_engine_ctx == NULL)
    {
        LOGE("%s: g_video_engine_ctx is NULL\n", __func__);
        return BK_FAIL;
    }

    if (!g_video_engine_ctx->camera_opened)
    {
        LOGE("%s: camera not open!\n", __func__);
        return BK_FAIL;
    }

    /* Check if task is already running */
    if (g_video_engine_ctx->transfer_task_running)
    {
        LOGW("%s: transfer task already running\n", __func__);
        return BK_OK;
    }

    g_video_engine_ctx->transfer_task_running = true;

    ret = rtos_create_thread(&g_video_engine_ctx->transfer_task_handle,
                            VIDEO_TRANSFER_TASK_PRIORITY,
                            VIDEO_TRANSFER_TASK_NAME,
                            video_engine_transfer_task,
                            VIDEO_TRANSFER_TASK_STACK_SIZE,
                            NULL);
    
    if (ret != BK_OK)
    {
        LOGE("%s: create transfer task failed, ret=%d\n", __func__, ret);
        g_video_engine_ctx->transfer_task_running = false;
        return BK_FAIL;
    }

    LOGI("%s: video transfer task started successfully\n", __func__);
    return BK_OK;
}

/**
 * @brief Stop video transfer task
 * 
 * This function stops the video transfer task gracefully.
 * 
 * @return BK_OK on success, BK_FAIL otherwise
 */
int video_engine_transfer_stop(void)
{
    bk_err_t ret = BK_OK;
    
    if (g_video_engine_ctx == NULL)
    {
        LOGE("%s: g_video_engine_ctx is NULL\n", __func__);
        return BK_FAIL;
    }

    if (!g_video_engine_ctx->transfer_task_running)
    {
        LOGW("%s: transfer task not running\n", __func__);
        return BK_OK;
    }

    g_video_engine_ctx->transfer_task_running = false;

    ntwk_eng_abort_video_send(true);

    for (uint32_t waited_ms = 0; g_video_engine_ctx->transfer_task_handle != NULL; waited_ms += VIDEO_TRANSFER_STOP_POLL_MS)
    {
        rtos_delay_milliseconds(VIDEO_TRANSFER_STOP_POLL_MS);
        if (waited_ms != 0 && (waited_ms % VIDEO_TRANSFER_STOP_WAIT_MS) == 0)
        {
            LOGW("%s: still waiting for transfer task to exit (%u ms)...\n",
                 __func__, waited_ms);
        }
    }

    ntwk_eng_abort_video_send(false);

    LOGI("%s: video transfer task stopped\n", __func__);
    return ret;
}

int video_engine_camera_turn_on(camera_parameters_t *parameters)
{
    bk_err_t ret = BK_FAIL;

    if (parameters == NULL)
    {
        LOGE("%s: parameters is NULL\n", __func__);
        return BK_FAIL;
    }

    LOGI("%s: Camera params - id:%d, %dx%d, format:%d\n", __func__,
        parameters->id, parameters->width, parameters->height, parameters->format);
    
    if (parameters->width == 0 || parameters->height == 0)
    {
        LOGW("%s: invalid resolution, using default 640x480\n", __func__);
        parameters->width = 640;
        parameters->height = 480;
    }
    
    if (parameters->format > 2)
    {
        parameters->format = 0;  
    }
   

    if (parameters->id == 0)
    {
        curr_cam_type = VIDEO_ENGINE_CAMERA_DVP;
        ret = video_engine_dvp_camera_open(parameters);
    }
#if CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA
    else if (parameters->id == 2)
    {
        curr_cam_type = VIDEO_ENGINE_CAMERA_MIPI;
        ret = video_engine_mipi_camera_open(parameters);
    }
#endif
    else
    {
        LOGE("%s: unknown camera id %d\n", __func__, parameters->id);
        curr_cam_type = VIDEO_ENGINE_CAMERA_UNKNOWN;
        ret = BK_FAIL;
    }

    if (ret != BK_OK)
    {
        LOGE("%s: camera open failed, ret=%d\n", __func__, ret);
        return ret;
    }

    LOGI("%s: Camera opened successfully\n", __func__);
    return ret;    
}

/**
 * @brief Start video engine with default configuration
 * 
 * This function initializes and starts the video engine using the
 * configuration defined in camera_parameters (based on CONFIG macros).
 * It will open the camera and start the transfer task.
 * 
 * @return bk_err_t 
 *         - BK_OK: Success
 *         - BK_FAIL: Failed
 */
int video_engine_start(void)
{
    camera_parameters_t camera_parameters= {
        /* Camera ID: 0 = DVP camera, 1 = UVC camera, 2 = MIPI/ISP camera */
    #if (CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA)
        .id = 0,  // DVP camera
        .width = CONFIG_VIDEO_ENGINE_RESOLUTION_WIDTH, //640,
        .height = CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT,//480,
        #if (CONFIG_VIDEO_ENGINE_JPEG_FORMAT)
        .format = 0,//JPEG_MODE,
        #elif (CONFIG_VIDEO_ENGINE_H264_FORMAT)
        .format = 1,    //H264_MODE,
        #endif
    #elif (CONFIG_VIDEO_ENGINE_USE_MIPI_CAMERA)
        .id = 2,  // MIPI/ISP camera
        .width = CONFIG_VIDEO_ENGINE_RESOLUTION_WIDTH,
        .height = CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT,
        .format = 1,    // BK7259 MIPI path produces H264 through HW encoder
    #elif (CONFIG_VIDEO_ENGINE_USE_UVC_CAMERA)
        .id = 1,  // UVC camera
        .width = CONFIG_VIDEO_ENGINE_RESOLUTION_WIDTH,
        .height = CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT,
        #if (CONFIG_VIDEO_ENGINE_JPEG_FORMAT)
        .format = 0,
        #elif (CONFIG_VIDEO_ENGINE_H264_FORMAT)
        .format = 1,
        #endif
    #endif
    };

    bk_err_t ret = BK_OK;
    
    if (g_video_engine_ctx == NULL) {
        LOGE("%s: g_video_engine_ctx is NULL, call video_engine_init() first\n", __func__);
        return BK_FAIL;
    }
    
    if (g_video_engine_ctx->is_started) {
        LOGD("%s: Video engine already started\n", __func__);
        return BK_OK;
    }
    
    LOGI("%s: Starting video engine (id:%d, %dx%d, format:%d)\n", __func__,
         camera_parameters.id, camera_parameters.width, 
         camera_parameters.height, camera_parameters.format);
    
    ret = video_engine_camera_turn_on(&camera_parameters);
    if (ret != BK_OK) {
        LOGE("%s: video_engine_camera_turn_on failed, ret=%d\n", __func__, ret);
        return ret;
    }
    
    /* Start video transfer task */
    ret = video_engine_transfer_start();
    if (ret != BK_OK) {
        LOGE("%s: video_engine_transfer_start failed, ret=%d\n", __func__, ret);
        video_engine_camera_close();
        return ret;
    }
    
    g_video_engine_ctx->is_started = true;
    
    LOGI("%s: Video engine started successfully\n", __func__);
    return BK_OK;
}

/**
 * @brief Stop video engine and close all related resources
 * 
 * This function stops the video transfer task and closes the camera.
 * It does not free memory or deinitialize frame queues.
 * 
 * @return bk_err_t 
 *         - BK_OK: Success
 *         - BK_FAIL: Failed
 */
int video_engine_stop(void)
{
    bk_err_t ret = BK_OK;
    bk_err_t final_ret = BK_OK;
    /* deinit() raises the barrier first and clears it after freeing ctx. */
    bool clear_shutting_down = !s_video_engine_shutting_down;
    
    if (g_video_engine_ctx == NULL) {
        LOGD("%s: g_video_engine_ctx is NULL, already stopped\n", __func__);
        return BK_OK;
    }
    
    if (!g_video_engine_ctx->is_started) {
        LOGD("%s: Video engine not started\n", __func__);
        return BK_OK;
    }
    
    LOGI("%s: Stopping video engine\n", __func__);

    if (clear_shutting_down) {
        video_engine_raise_shutting_down();
    }

    (void)video_engine_preview_stop();
    
    /* Stop video transfer task */
    if (g_video_engine_ctx->transfer_task_running) {
        ret = video_engine_transfer_stop();
        if (ret != BK_OK) {
            LOGE("%s: video_engine_transfer_stop failed, ret=%d\n", __func__, ret);
            final_ret = ret;  
        }
    }

    /* Catch a preview that raced past the first stop before the barrier was
     * visible; must be idle before camera/ISP teardown. */
    (void)video_engine_preview_stop();
    
    if (g_video_engine_ctx->camera_opened) {
        ret = video_engine_camera_close();
        if (ret != BK_OK) {
            LOGE("%s: video_engine_camera_close failed, ret=%d\n", __func__, ret);
            final_ret = ret;  // Record error but continue
        }
    }
    
    g_video_engine_ctx->is_started = false;

    if (clear_shutting_down) {
        s_video_engine_shutting_down = false;
    }
    
    if (final_ret == BK_OK) {
        LOGI("%s: Video engine stopped successfully\n", __func__);
    } else {
        LOGW("%s: Video engine stopped with errors, ret=%d\n", __func__, final_ret);
    }
    
    return final_ret;
}

/**
 * @brief Check if video engine is currently running
 * 
 * @return bool 
 *         - true: Video engine is running
 *         - false: Video engine is not running
 */
bool video_engine_is_running(void)
{
    if (g_video_engine_ctx == NULL) {
        return false;
    }
    return g_video_engine_ctx->is_started;
}

bool video_engine_is_preview_ready(void)
{
    video_engine_ctx_t *ctx;
    bool ready;

    /* Fast reject: must be first so unlocked readers bail out before touching
     * ctx while the other core is inside deinit/free. */
    if (s_video_engine_shutting_down) {
        return false;
    }

    ctx = g_video_engine_ctx;
    if (ctx == NULL) {
        return false;
    }

    ready = ctx->is_started && ctx->camera_opened;

    /* Recheck after field loads: deinit may have started on the other core. */
    if (s_video_engine_shutting_down || g_video_engine_ctx != ctx) {
        return false;
    }

    return ready;
}