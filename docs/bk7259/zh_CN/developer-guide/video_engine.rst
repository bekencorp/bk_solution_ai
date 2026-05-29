Video Engine 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

Video Engine 提供了简洁的视频引擎管理接口，支持 DVP 和 UVC 摄像头的初始化、启动、停止和资源清理。所有配置基于 CONFIG 宏自动设置，使用简单方便。该模块封装了摄像头控制、视频帧队列管理、视频传输等复杂功能，为上层应用提供统一的视频处理接口。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **自动配置**: 基于 CONFIG 宏自动配置摄像头参数
- **一键初始化**: ``video_engine_init()`` 一步完成所有初始化
- **状态管理**: 完善的启动状态跟踪
- **完整的生命周期管理**: init/start/stop/deinit 完整流程
- **状态查询**: ``video_engine_is_running()`` 实时状态查询
- **多格式支持**: 支持 JPEG、H.264、H.265 格式
- **帧队列管理**: 自动管理视频帧队列，支持多格式

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Video Engine 模块架构如下：

::

    应用层 (Application)
            ↓
    Video Engine API (video_engine.h)
            ↓
    Camera Controller (bk_camera_ctlr)
            ↓
    Video Pipeline (视频管道)
            ↓
    Frame Queue (帧队列)
            ↓
    Network Transfer
            ↓
    硬件层 (DVP/UVC Camera)

工作流程
---------------------------------

初始化流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Video Engine 的初始化流程如下：

::

    video_engine_init()
        ↓
    1. 检查是否已初始化
        - 如果已初始化且已启动，直接返回
        - 如果已初始化但未启动，调用 start
        ↓
    2. 分配上下文内存
        - 分配 video_engine_ctx_t 结构
        - 初始化所有字段为 0
        ↓
    3. 初始化帧队列
        - frame_queue_init_all()
        - 初始化所有格式的帧队列（MJPEG, H264, YUV）
        ↓
    4. 自动启动视频引擎
        - video_engine_start()
        ↓
    初始化完成

启动流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``video_engine_start()`` 详细流程：

::

    1. 参数验证
        - 检查上下文有效性
        - 检查是否已启动（防止重复启动）
        ↓
    2. 构建摄像头参数
        - 从 CONFIG 宏读取配置
        - camera_parameters_t 结构
            * id: 0 (DVP) 或 1 (UVC)
            * width: CONFIG_VIDEO_ENGINE_RESOLUTION_WIDTH
            * height: CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT
            * format: 0 (JPEG) 或 1 (H.264)
        ↓
    3. 打开摄像头
        - video_engine_camera_turn_on()
            * 配置 DVP（如果使用 DVP）
            * 创建摄像头控制器
            * 打开摄像头设备
        ↓
    4. 启动视频传输任务
        - video_engine_transfer_start()
            * 创建传输任务线程
            * 设置 transfer_task_running = true
        ↓
    5. 设置启动标志
        - is_started = true
        ↓
    启动完成

视频数据流
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**视频采集和传输流程**:

::

    摄像头采集
        ↓
    视频管道处理
        ↓
    帧回调 (dvp_camera_cbs.on_frame)
        ↓
    帧队列入队 (frame_queue_put_frame)
        ↓
    传输任务 (video_engine_transfer_task)
        ↓
    从队列取帧 (frame_queue_get_frame)
        ↓
    network_transfer_send_video()
        ↓
    网络发送

传输任务循环
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``video_engine_transfer_task()`` 任务循环：

::

    while (transfer_task_running) {
        1. 从帧队列取帧
           - frame_queue_get_frame()
           - 超时等待: BEKEN_WAIT_FOREVER
        ↓
        2. 检查帧有效性
           - 如果 frame == NULL，继续循环
        ↓
        3. 发送视频帧
           - ntwk_trans_send_video(frame)
        ↓
        4. 释放帧资源
           - frame_queue_free()
        ↓
        继续循环
    }

停止流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``video_engine_stop()`` 详细流程：

::

    1. 检查是否已启动
        - 如果未启动，直接返回
        ↓
    2. 停止传输任务
        - video_engine_transfer_stop()
            * 设置 transfer_task_running = false
            * 等待任务退出
            * 删除任务线程
        ↓
    3. 关闭摄像头
        - video_engine_camera_close()
            * bk_camera_close()
            * 释放摄像头控制器
        ↓
    4. 清除启动标志
        - is_started = false
        ↓
    停止完成

重要接口
---------------------------------

初始化接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 初始化视频引擎（使用默认配置）
     * 
     * 该函数初始化视频引擎上下文、帧队列，打开摄像头，并启动传输任务。
     * 配置从 CONFIG_* 宏读取。
     * 
     * @return int 
     *         - AVDK_ERR_OK: 成功
     *         - AVDK_ERR_NOMEM: 内存不足
     *         - 其他: 子函数返回的错误代码
     */
    int video_engine_init(void);

    /**
     * @brief 反初始化视频引擎并释放资源
     * 
     * 该函数停止传输任务，关闭摄像头，反初始化帧队列，并释放所有分配的内存。
     * 
     * @return int 
     *         - AVDK_ERR_OK: 成功
     */
    int video_engine_deinit(void);

启动和停止接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 启动视频引擎（使用默认配置）
     * 
     * 该函数使用 CONFIG 宏定义的配置启动视频引擎。
     * 会打开摄像头并启动传输任务。
     * 
     * 该函数由 video_engine_init() 自动调用。
     * 
     * @return int 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    int video_engine_start(void);

    /**
     * @brief 停止视频引擎并关闭所有相关资源
     * 
     * 该函数停止视频传输任务并关闭摄像头。
     * 不会释放内存或反初始化帧队列。
     * 调用 video_engine_deinit() 以完全清理所有资源。
     * 
     * @return int 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    int video_engine_stop(void);

摄像头控制接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 打开 DVP 摄像头
     * 
     * @param config 摄像头配置参数
     * 
     * @return int 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    int video_engine_dvp_camera_open(camera_parameters_t *config);

    /**
     * @brief 关闭摄像头
     * 
     * @return int 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    int video_engine_camera_close(void);

    /**
     * @brief 打开摄像头（带参数初始化和验证）
     * 
     * 该函数验证并使用默认值初始化摄像头参数，然后打开摄像头设备。
     * 
     * @param parameters 摄像头参数（会被验证和修改）
     * 
     * @return int 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    int video_engine_camera_turn_on(camera_parameters_t *parameters);

传输控制接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 设置摄像头传输回调
     * 
     * @param cb 传输回调指针 (media_transfer_cb_t *)
     * 
     * @return int 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    int video_engine_set_camera_transfer_callback(void *cb);

    /**
     * @brief 启动视频传输任务
     * 
     * 该函数创建一个任务，持续从帧队列中取帧并通过注册的传输回调处理。
     * 
     * @return int 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    int video_engine_transfer_start(void);

    /**
     * @brief 停止视频传输任务
     * 
     * @return int 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    int video_engine_transfer_stop(void);

状态查询接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 检查视频引擎是否正在运行
     * 
     * @return bool 
     *         - true: 视频引擎正在运行
     *         - false: 视频引擎未运行
     */
    bool video_engine_is_running(void);

数据结构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**camera_parameters_t**: 摄像头参数结构

.. code:: c

    typedef struct {
        uint16_t id;        // 摄像头 ID: 0 = DVP, 1 = UVC
        uint16_t width;     // 图像宽度
        uint16_t height;    // 图像高度
        uint16_t format;    // 格式: 0 = JPEG, 1 = H.264, 2 = H.265
        uint16_t protocol; // 协议（保留字段）
        uint16_t rotate;    // 旋转角度
    } camera_parameters_t;

主要宏定义
---------------------------------


Kconfig 配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**基本配置**:

.. code:: c

    // 启用视频引擎
    CONFIG_BK_VIDEO_ENGINE=y

**摄像头类型选择** (互斥):

.. code:: c

    // 使用 DVP 摄像头
    CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA=y

    // 或使用 UVC 摄像头
    // CONFIG_VIDEO_ENGINE_USE_UVC_CAMERA=y

**图像格式选择** (互斥):

.. code:: c

    // 使用 JPEG 格式
    CONFIG_VIDEO_ENGINE_JPEG_FORMAT=y

    // 或使用 H.264 格式
    // CONFIG_VIDEO_ENGINE_H264_FORMAT=y

**分辨率配置**:

.. code:: c

    // 默认宽度
    CONFIG_VIDEO_ENGINE_RESOLUTION_WIDTH=1280  // 范围: 1-2000

    // 默认高度
    CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT=720  // 范围: 1-2000


自定义摄像头参数
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #include "video_engine.h"

    int custom_video_start(void)
    {
        camera_parameters_t params = {
            .id = 0,              // DVP 摄像头
            .width = 640,        // 宽度
            .height = 480,       // 高度
            .format = 1,         // H.264 格式
            .protocol = 0,       // 保留
            .rotate = 0,         // 不旋转
        };

        // 打开摄像头
        int ret = video_engine_camera_turn_on(&params);
        if (ret != BK_OK) {
            LOGE("Camera turn on failed\n");
            return ret;
        }

        // 启动传输任务
        ret = video_engine_transfer_start();
        if (ret != BK_OK) {
            LOGE("Transfer start failed\n");
            video_engine_camera_close();
            return ret;
        }

        return BK_OK;
    }

注意事项
---------------------------------

1. **初始化顺序**: 必须先调用 ``video_engine_init()``，然后才能使用其他接口

2. **配置依赖**: 摄像头参数从 CONFIG 宏读取，确保在 Kconfig 中正确配置

3. **帧队列管理**: 帧队列由模块自动管理，应用层无需直接操作

4. **传输任务**: 传输任务在独立线程中运行，优先级为 5，栈大小为 4KB

5. **资源释放**: 使用完毕后必须调用 ``video_engine_deinit()`` 释放所有资源

6. **重复启动**: 如果已启动，再次调用 ``video_engine_start()`` 会直接返回成功

7. **格式支持**:
   - JPEG 格式：适合低带宽场景
   - H.264 格式：适合高质量视频传输
   - 格式选择会影响帧队列类型

8. **分辨率限制**: 分辨率范围 1-2000，建议使用标准分辨率（如 640x480, 1280x720）

9. **内存管理**: 帧队列使用动态内存分配，确保系统有足够内存


