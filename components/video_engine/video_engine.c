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
#include "video_frame_que.h"
#include "network_transfer.h"

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
#define VIDEO_TRANSFER_QUEUE_TIMEOUT    (BEKEN_WAIT_FOREVER)

/**
 * @brief Video engine internal context structure
 */
typedef struct {
    bk_camera_ctlr_handle_t camera_handle;      /**< Camera controller handle */
    image_format_t transfer_format;             /**< Transfer format (IMAGE_MJPEG/IMAGE_H264/IMAGE_H265) */
    beken_thread_t transfer_task_handle;        /**< Transfer task handle */
    bool transfer_task_running;                 /**< Transfer task running flag */
    /* Engine state */
    bool is_started;                            /**< Video engine started flag */
} video_engine_ctx_t;

/* Global video engine context */
static video_engine_ctx_t *g_video_engine_ctx = NULL;

static void video_engine_transfer_task(void *arg);

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
        
        /* 使用 frame_queue_get_frame 从 frame_queue 取帧 
         * 该函数内部调用 rtos_pop_from_queue() 从 ready_queue 中 pop 取帧
         */
        frame = frame_queue_get_frame(g_video_engine_ctx->transfer_format, 
                                      VIDEO_TRANSFER_QUEUE_TIMEOUT);
        
        if (frame != NULL)
        {
            /* Check context before sending */
            if (g_video_engine_ctx == NULL) {
                LOGE("%s: g_video_engine_ctx became NULL, releasing frame\n", __func__);
                frame_queue_free(g_video_engine_ctx->transfer_format, frame);
                break;
            }

            /* 直接调用 ntwk_trans_send_video() 发送帧数据 */
            ret = ntwk_trans_send_video(frame);
            if (ret != BK_OK)
            {
                //LOGW("%s: ntwk_trans_send_video failed, ret=%d\n", __func__, ret);
            }

            /* 处理完成后释放帧 */
            frame_queue_free(g_video_engine_ctx->transfer_format, frame);
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
    
    ret = frame_queue_init_all();
    if (ret != BK_OK) {
        LOGE("%s: frame_queue_init_all failed, ret=%d\n", __func__, ret);
        os_free(g_video_engine_ctx);
        g_video_engine_ctx = NULL;
        return ret;
    }
    LOGI("%s: frame_queue initialized\n", __func__);
    
    /* Start video engine with default configuration */
    ret = video_engine_start();
    if (ret != BK_OK) {
        LOGE("%s: video_engine_start failed, ret=%d\n", __func__, ret);
        
        frame_queue_deinit_all();
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
    
    ret = video_engine_stop();
    if (ret != BK_OK) {
        LOGE("%s: video_engine_stop failed, ret=%d\n", __func__, ret);
    }
    
    frame_queue_deinit_all();
    LOGI("%s: frame_queue deinitialized\n", __func__);
    
    os_free(g_video_engine_ctx);
    g_video_engine_ctx = NULL;
    
    LOGI("%s: Video engine deinitialized successfully\n", __func__);
    
    return BK_OK;
}

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
            LOGI("%s: Camera opened successfully\n", __func__);
        }
    }
    else
    {
        LOGE("%s: bk_camera_dvp_ctlr_new failed, ret=%d\n", __func__, ret);
    }

    return ret;
}

int video_engine_camera_close(void)
{
    bk_err_t ret = BK_OK;

    if (g_video_engine_ctx == NULL)
    {
        LOGE("%s: g_video_engine_ctx is NULL\n", __func__);
        return BK_FAIL;
    }

    if (g_video_engine_ctx->camera_handle == NULL)
    {
        LOGE("%s: camera_handle is NULL\n", __func__);
        return ret;
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

    return BK_OK;
}


/**
 * @brief Start video transfer task
 * 
 * This function creates a task that continuously pops frames from the frame queue
 * and sends them using ntwk_trans_send_video().
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

    if (g_video_engine_ctx->camera_handle == NULL)
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


    if (g_video_engine_ctx->transfer_task_handle != NULL)
    {
        LOGW("%s: transfer task did not exit gracefully, force delete\n", __func__);
        ret = rtos_delete_thread(&g_video_engine_ctx->transfer_task_handle);
        g_video_engine_ctx->transfer_task_handle = NULL;
    }

    LOGI("%s: video transfer task stopped\n", __func__);
    return ret;
}


static camera_type_t curr_cam_type = UNKNOW_CAMERA;
int video_engine_camera_turn_on(camera_parameters_t *parameters)
{
    bk_err_t ret = BK_FAIL;

    LOGI("%s: Camera params - id:%d, %dx%d, format:%d\n", __func__,
        parameters->id, parameters->width, parameters->height, parameters->format);


    if (parameters == NULL)
    {
        LOGE("%s: parameters is NULL\n", __func__);
        return BK_FAIL;
    }
    
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
        curr_cam_type = DVP_CAMERA;
        ret = video_engine_dvp_camera_open(parameters);
    }
    else
    {
        LOGE("%s: unknown camera id %d\n", __func__, parameters->id);
        curr_cam_type = UNKNOW_CAMERA;
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
        /* Camera ID: 0 = DVP camera, 1 = UVC camera */
    #if (CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA)
        .id = 0,  // DVP camera
        .width = CONFIG_VIDEO_ENGINE_RESOLUTION_WIDTH, //640,
        .height = CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT,//480,
        #if (CONFIG_VIDEO_ENGINE_JPEG_FORMAT)
        .format = 0,//JPEG_MODE,
        #elif (CONFIG_VIDEO_ENGINE_H264_FORMAT)
        .format = 1,    //H264_MODE,
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
    
    if (g_video_engine_ctx == NULL) {
        LOGD("%s: g_video_engine_ctx is NULL, already stopped\n", __func__);
        return BK_OK;
    }
    
    if (!g_video_engine_ctx->is_started) {
        LOGD("%s: Video engine not started\n", __func__);
        return BK_OK;
    }
    
    LOGI("%s: Stopping video engine\n", __func__);
    
    /* Stop video transfer task */
    if (g_video_engine_ctx->transfer_task_running) {
        ret = video_engine_transfer_stop();
        if (ret != BK_OK) {
            LOGE("%s: video_engine_transfer_stop failed, ret=%d\n", __func__, ret);
            final_ret = ret;  
        }
    }
    
    if (g_video_engine_ctx->camera_handle != NULL) {
        ret = video_engine_camera_close();
        if (ret != BK_OK) {
            LOGE("%s: video_engine_camera_close failed, ret=%d\n", __func__, ret);
            final_ret = ret;  // Record error but continue
        }
    }
    
    g_video_engine_ctx->is_started = false;
    
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