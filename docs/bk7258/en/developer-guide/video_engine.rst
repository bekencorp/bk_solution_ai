Video Engine Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

Video Engine provides a concise video engine management interface, supporting initialization, start, stop, and resource cleanup for DVP and UVC cameras. All configurations are automatically set based on CONFIG macros, making it simple and convenient to use. This module encapsulates complex functions such as camera control, video frame queue management, and video transmission, providing a unified video processing interface for upper-layer applications.

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Auto Configuration**: Automatically configures camera parameters based on CONFIG macros
- **One-key Initialization**: ``video_engine_init()`` completes all initialization in one step
- **State Management**: Complete startup state tracking
- **Complete Lifecycle Management**: init/start/stop/deinit complete flow
- **State Query**: ``video_engine_is_running()`` real-time state query
- **Multiple Format Support**: Supports JPEG, H.264, H.265 formats
- **Frame Queue Management**: Automatically manages video frame queues, supports multiple formats

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Video Engine module architecture is as follows:

::

    Application Layer (Application)
            ↓
    Video Engine API (video_engine.h)
            ↓
    Camera Controller (bk_camera_ctlr)
            ↓
    Video Pipeline (Video Pipeline)
            ↓
    Frame Queue (Frame Queue)
            ↓
    Network Transfer
            ↓
    Hardware Layer (DVP/UVC Camera)

Workflow
----------------

Initialization Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Video Engine initialization flow is as follows:

::

    video_engine_init()
        ↓
    1. Check if Already Initialized
        - If initialized and started, return directly
        - If initialized but not started, call start
        ↓
    2. Allocate Context Memory
        - Allocate video_engine_ctx_t structure
        - Initialize all fields to 0
        ↓
    3. Initialize Frame Queue
        - frame_queue_init_all()
        - Initialize frame queues for all formats (MJPEG, H264, YUV)
        ↓
    4. Auto Start Video Engine
        - video_engine_start()
        ↓
    Initialization Complete

Start Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``video_engine_start()``:

::

    1. Parameter Validation
        - Check context validity
        - Check if already started (prevent duplicate start)
        ↓
    2. Build Camera Parameters
        - Read configuration from CONFIG macros
        - camera_parameters_t structure
            * id: 0 (DVP) or 1 (UVC)
            * width: CONFIG_VIDEO_ENGINE_RESOLUTION_WIDTH
            * height: CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT
            * format: 0 (JPEG) or 1 (H.264)
        ↓
    3. Open Camera
        - video_engine_camera_turn_on()
            * Configure DVP (if using DVP)
            * Create camera controller
            * Open camera device
        ↓
    4. Start Video Transfer Task
        - video_engine_transfer_start()
            * Create transfer task thread
            * Set transfer_task_running = true
        ↓
    5. Set Start Flag
        - is_started = true
        ↓
    Start Complete

Video Data Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**Video Capture and Transmission Flow**:

::

    Camera Capture
        ↓
    Video Pipeline Processing
        ↓
    Frame Callback (dvp_camera_cbs.on_frame)
        ↓
    Frame Queue Enqueue (frame_queue_put_frame)
        ↓
    Transfer Task (video_engine_transfer_task)
        ↓
    Get Frame from Queue (frame_queue_get_frame)
        ↓
    network_transfer_send_video()
        ↓
    Network Send

Transfer Task Loop
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``video_engine_transfer_task()`` task loop:

::

    while (transfer_task_running) {
        1. Get Frame from Queue
           - frame_queue_get_frame()
           - Timeout wait: BEKEN_WAIT_FOREVER
        ↓
        2. Check Frame Validity
           - If frame == NULL, continue loop
        ↓
        3. Send Video Frame
           - ntwk_trans_send_video(frame)
        ↓
        4. Release Frame Resources
           - frame_queue_free()
        ↓
        Continue loop
    }

Stop Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``video_engine_stop()``:

::

    1. Check if Started
        - If not started, return directly
        ↓
    2. Stop Transfer Task
        - video_engine_transfer_stop()
            * Set transfer_task_running = false
            * Wait for task to exit
            * Delete task thread
        ↓
    3. Close Camera
        - video_engine_camera_close()
            * bk_camera_close()
            * Release camera controller
        ↓
    4. Clear Start Flag
        - is_started = false
        ↓
    Stop Complete

Important Interfaces
--------------------

Initialization Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Initialize video engine (using default configuration)
     * 
     * This function initializes video engine context, frame queue, opens camera, and starts transfer task.
     * Configuration is read from CONFIG_* macros.
     * 
     * @return int 
     *         - AVDK_ERR_OK: Success
     *         - AVDK_ERR_NOMEM: Out of memory
     *         - Others: Error codes returned by sub-functions
     */
    int video_engine_init(void);

    /**
     * @brief Deinitialize video engine and release resources
     * 
     * This function stops transfer task, closes camera, deinitializes frame queue, and releases all allocated memory.
     * 
     * @return int 
     *         - AVDK_ERR_OK: Success
     */
    int video_engine_deinit(void);

Start and Stop Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Start video engine (using default configuration)
     * 
     * This function starts video engine using configuration defined by CONFIG macros.
     * Will open camera and start transfer task.
     * 
     * This function is automatically called by video_engine_init().
     * 
     * @return int 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    int video_engine_start(void);

    /**
     * @brief Stop video engine and close all related resources
     * 
     * This function stops video transfer task and closes camera.
     * Does not release memory or deinitialize frame queue.
     * Call video_engine_deinit() to completely clean up all resources.
     * 
     * @return int 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    int video_engine_stop(void);

Camera Control Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Open DVP camera
     * 
     * @param config Camera configuration parameters
     * 
     * @return int 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    int video_engine_dvp_camera_open(camera_parameters_t *config);

    /**
     * @brief Close camera
     * 
     * @return int 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    int video_engine_camera_close(void);

    /**
     * @brief Open camera (with parameter initialization and validation)
     * 
     * This function validates and initializes camera parameters with default values, then opens camera device.
     * 
     * @param parameters Camera parameters (will be validated and modified)
     * 
     * @return int 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    int video_engine_camera_turn_on(camera_parameters_t *parameters);

Transfer Control Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Set camera transfer callback
     * 
     * @param cb Transfer callback pointer (media_transfer_cb_t *)
     * 
     * @return int 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    int video_engine_set_camera_transfer_callback(void *cb);

    /**
     * @brief Start video transfer task
     * 
     * This function creates a task that continuously gets frames from frame queue and processes through registered transfer callback.
     * 
     * @return int 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    int video_engine_transfer_start(void);

    /**
     * @brief Stop video transfer task
     * 
     * @return int 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    int video_engine_transfer_stop(void);

State Query Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Check if video engine is running
     * 
     * @return bool 
     *         - true: Video engine is running
     *         - false: Video engine is not running
     */
    bool video_engine_is_running(void);

Data Structures
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**camera_parameters_t**: Camera parameter structure

.. code-block:: C

    typedef struct {
        uint16_t id;        // Camera ID: 0 = DVP, 1 = UVC
        uint16_t width;     // Image width
        uint16_t height;    // Image height
        uint16_t format;    // Format: 0 = JPEG, 1 = H.264, 2 = H.265
        uint16_t protocol; // Protocol (reserved field)
        uint16_t rotate;    // Rotation angle
    } camera_parameters_t;

Main Macro Definitions
-----------------------

Kconfig Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**Basic Configuration**:

.. code-block:: C

    // Enable video engine
    CONFIG_BK_VIDEO_ENGINE=y

**Camera Type Selection** (mutually exclusive):

.. code-block:: C

    // Use DVP camera
    CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA=y

    // Or use UVC camera
    // CONFIG_VIDEO_ENGINE_USE_UVC_CAMERA=y

**Image Format Selection** (mutually exclusive):

.. code-block:: C

    // Use JPEG format
    CONFIG_VIDEO_ENGINE_JPEG_FORMAT=y

    // Or use H.264 format
    // CONFIG_VIDEO_ENGINE_H264_FORMAT=y

**Resolution Configuration**:

.. code-block:: C

    // Default width
    CONFIG_VIDEO_ENGINE_RESOLUTION_WIDTH=1280  // Range: 1-2000

    // Default height
    CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT=720  // Range: 1-2000

Custom Camera Parameters
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    #include "video_engine.h"

    int custom_video_start(void)
    {
        camera_parameters_t params = {
            .id = 0,              // DVP camera
            .width = 640,        // Width
            .height = 480,       // Height
            .format = 1,         // H.264 format
            .protocol = 0,       // Reserved
            .rotate = 0,         // No rotation
        };

        // Open camera
        int ret = video_engine_camera_turn_on(&params);
        if (ret != BK_OK) {
            LOGE("Camera turn on failed\n");
            return ret;
        }

        // Start transfer task
        ret = video_engine_transfer_start();
        if (ret != BK_OK) {
            LOGE("Transfer start failed\n");
            video_engine_camera_close();
            return ret;
        }

        return BK_OK;
    }

Notes
----------------

1. **Initialization Order**: Must call ``video_engine_init()`` first before using other interfaces

2. **Configuration Dependency**: Camera parameters are read from CONFIG macros, ensure correct configuration in Kconfig

3. **Frame Queue Management**: Frame queue is automatically managed by module, application layer does not need to directly operate

4. **Transfer Task**: Transfer task runs in independent thread with priority 5 and stack size 4KB

5. **Resource Release**: Must call ``video_engine_deinit()`` to release all resources after use

6. **Duplicate Start**: If already started, calling ``video_engine_start()`` again will return success directly

7. **Format Support**:
   - JPEG format: Suitable for low bandwidth scenarios
   - H.264 format: Suitable for high-quality video transmission
   - Format selection affects frame queue type

8. **Resolution Limitation**: Resolution range 1-2000, recommend using standard resolutions (e.g., 640x480, 1280x720)

9. **Memory Management**: Frame queue uses dynamic memory allocation, ensure system has sufficient memory

