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

#ifndef _VIDEO_ENGINE_H_
#define _VIDEO_ENGINE_H_

#include <stdbool.h>
#include <common/bk_include.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef struct
{
	uint16_t id;
	uint16_t width;
	uint16_t height;
	uint16_t format;
	uint16_t protocol;
	uint16_t rotate;
} camera_parameters_t;

typedef void (*video_engine_preview_sink_t)(const uint8_t *rgb565,
                                            uint16_t width,
                                            uint16_t height,
                                            void *user_data);

typedef struct
{
    uint16_t width;      /**< ISP SP input width (NV12). */
    uint16_t height;     /**< ISP SP input height (NV12). */
    uint8_t fps;         /**< Local preview frame rate, 1-15 fps recommended. */
    uint16_t rotate;     /**< Software output rotation: 0/90/180/270. */
    uint16_t out_width;  /**< Target RGB565 output width; 0 = rotated input width. */
    uint16_t out_height; /**< Target RGB565 output height; 0 = rotated input height. */
    video_engine_preview_sink_t sink;
    void *user_data;
} video_engine_preview_config_t;

// extern camera_parameters_t camera_parameters;


/**
 * @brief Open DVP camera
 * 
 * @param config Camera configuration
 * 
 * @return
 *    - BK_OK: Success
 *    - BK_FAIL: Failed
 */
int video_engine_dvp_camera_open(camera_parameters_t *config);

/**
 * @brief Close camera
 * 
 * @return
 *    - BK_OK: Success
 *    - BK_FAIL: Failed
 */
int video_engine_camera_close(void);

/**
 * @brief Start video engine with default configuration
 * 
 * This function starts the video engine using the configuration
 * defined by CONFIG macros. It will open the camera and start the transfer task.
 * 
 * This function is called automatically by video_engine_init().
 * 
 * @return
 *    - BK_OK: Success
 *    - BK_FAIL: Failed
 */
int video_engine_start(void);

/**
 * @brief Stop video engine and close all related resources
 * 
 * This function stops the video transfer task and closes the camera.
 * It does not free memory or deinitialize frame queues.
 * Call video_engine_deinit() to completely cleanup all resources.
 * 
 * @return
 *    - BK_OK: Success
 *    - BK_FAIL: Failed
 */
int video_engine_stop(void);

/**
 * @brief Turn on camera with parameter initialization and validation
 * 
 * This function validates and initializes camera parameters with default values,
 * then opens the camera device.
 * 
 * @param parameters Camera parameters (will be validated and modified if needed)
 * 
 * @return
 *    - BK_OK: Success
 *    - BK_FAIL: Failed
 */
int video_engine_camera_turn_on(camera_parameters_t *parameters);

/**
 * @brief Set camera transfer callback
 * 
 * @param cb Transfer callback pointer (media_transfer_cb_t *)
 * 
 * @return
 *    - BK_OK: Success
 *    - BK_FAIL: Failed
 */
int video_engine_set_camera_transfer_callback(void *cb);

/**
 * @brief Start video transfer task
 * 
 * This function creates a task that continuously pops frames from
 * the frame queue using rtos_pop_from_queue() and processes them
 * through the registered transfer callback.
 * 
 * @return
 *    - BK_OK: Success
 *    - BK_FAIL: Failed
 */
int video_engine_transfer_start(void);

/**
 * @brief Stop video transfer task
 * 
 * @return
 *    - BK_OK: Success
 *    - BK_FAIL: Failed
 */
int video_engine_transfer_stop(void);


/**
 * @brief Initialize video engine with default configuration
 * 
 * This function initializes the video engine context, frame queues,
 * opens the camera, and starts the transfer task automatically.
 * Configuration is read from CONFIG_* macros.
 * 
 * @return
 *    - AVDK_ERR_OK: Success
 *    - AVDK_ERR_NOMEM: Out of memory
 *    - Other: Error codes from sub-functions
 */
int video_engine_init(void);

/**
 * @brief Deinitialize video engine and release resources
 * 
 * This function stops the transfer task, closes the camera,
 * deinitializes frame queues, and frees all allocated memory.
 * 
 * @return
 *    - AVDK_ERR_OK: Success
 */
int video_engine_deinit(void);

/**
 * @brief Check if video engine is currently running
 * 
 * @return bool 
 *         - true: Video engine is running
 *         - false: Video engine is not running
 */
bool video_engine_is_running(void);

/**
 * @brief Start local RGB565 preview from the running MIPI camera SP channel.
 *
 * The preview path is a secondary local consumer. It does not own the camera
 * lifecycle and must be stopped before the UI object receiving frames is
 * destroyed. H.264 uplink continues to use the MP flexa path.
 */
int video_engine_preview_start(const video_engine_preview_config_t *config);

/**
 * @brief Stop local preview worker and release its temporary buffers.
 *
 * This does not close the MIPI camera; video_engine_deinit() owns that.
 */
int video_engine_preview_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* _VIDEO_ENGINE_H_ */

