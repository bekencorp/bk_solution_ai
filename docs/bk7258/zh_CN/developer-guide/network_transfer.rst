Network Transfer 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

Network Transfer 是统一网络传输抽象层组件，用于多媒体数据（音频、视频）和控制命令的网络传输。该组件采用 **分层架构** 和 **模块化设计**，通过标准化的接口将上层多媒体应用与底层传输协议解耦，使开发者能够灵活切换或自定义传输方案（如 Agora RTC、VolcEngine RTC 等）。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **统一抽象接口**: 为上层应用提供一致的 API，屏蔽底层传输协议差异
- **协议可插拔**: 支持动态注册传输方案，方便集成自定义协议
- **多后端支持**: 支持 Agora RTC、VolcEngine RTC 等多种 RTC 后端
- **事件驱动机制**: 通过回调机制实时通知应用层状态变化和数据接收
- **自动路由**: 音频数据自动路由到音频引擎，视频数据自动路由到网络

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Network Transfer 模块架构如下：

::

    应用层 (Application)
            ↓
    Network Transfer API (network_transfer.h)
            ↓
    RTC 后端适配层
        ├── Agora RTC (bk_agora_api.c)
        └── VolcEngine RTC (bk_volc_api.c)
            ↓
    RTC SDK
        ├── Agora RTC SDK
        └── VolcEngine RTC SDK
            ↓
    网络层

工作流程
---------------------------------

初始化流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Network Transfer 的初始化流程如下：

::

    ntwk_trans_init()
        ↓
    1. 检查是否已初始化
        - 如果已初始化，直接返回
        ↓
    2. 根据配置选择 RTC 后端
        #if CONFIG_AGORA_IOT_SDK
            - 设置 Agora RTC 回调函数
                * audio_tx_cb = bk_agora_rtc_audio_data_send
                * video_tx_cb = bk_agora_rtc_video_data_send
                * start_cb = bk_agora_start
                * stop_cb = bk_agora_stop
                * pre_config_cb = bk_agora_pre_config
                * update_cb = bk_agora_update_agent
            - network_type = NETWORK_TYPE_AGORA_RTC
        #elif CONFIG_VOLC_RTC_EN
            - 设置 VolcEngine RTC 回调函数
                * audio_tx_cb = bk_byte_rtc_audio_data_send
                * video_tx_cb = bk_byte_rtc_video_data_send
                * start_cb = bk_byte_start
                * stop_cb = bk_byte_stop
                * pre_config_cb = bk_byte_pre_config
                * update_cb = bk_byte_update_agent
            - network_type = NETWORK_TYPE_VOLC_RTC
        #endif
        ↓
    3. 执行预配置回调
        - pre_config_cb(user_data)
        - 注册 CLI 命令等
        ↓
    4. 设置初始化标志
        - initialized = true
        ↓
    初始化完成

启动流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``ntwk_trans_start()`` 详细流程：

::

    1. 检查初始化状态
        - 如果未初始化，返回错误
        - 如果已启动，直接返回
        ↓
    2. 调用启动回调
        - start_cb(user_data)
        - 对于 Agora: bk_agora_start()
            * 启动 Agent
            * 启动 RTC
        - 对于 VolcEngine: bk_byte_start()
        ↓
    3. 设置启动标志
        - is_started = true
        ↓
    启动完成

音频发送流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``ntwk_trans_send_audio()`` 详细流程：

::

    1. 参数验证
        - 检查初始化状态
        - 检查数据指针和大小
        - 检查音频类型有效性
        ↓
    2. 调用音频发送回调
        - audio_tx_cb(data, size, audio_type)
        - 对于 Agora: bk_agora_rtc_audio_data_send()
            * 检查连接状态
            * 映射编码类型
            * agora_rtc_send_audio_data()
        - 对于 VolcEngine: bk_byte_rtc_audio_data_send()
        ↓
    3. 返回发送结果
        ↓
    发送完成

视频发送流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``ntwk_trans_send_video()`` 详细流程：

::

    1. 参数验证
        - 检查初始化状态
        - 检查帧缓冲区有效性
        - 检查帧数据指针和长度
        ↓
    2. 调用视频发送回调
        - video_tx_cb(frame)
        - 对于 Agora: bk_agora_rtc_video_data_send()
            * 检查连接状态
            * I 帧过滤（仅 H.264）
            * 帧率控制（500ms 间隔）
            * agora_rtc_send_video_data()
        - 对于 VolcEngine: bk_byte_rtc_video_data_send()
        ↓
    3. 返回发送结果
        ↓
    发送完成

音频接收流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``ntwk_trans_recv_audio()`` 详细流程：

::

    1. 参数验证
        - 检查初始化状态
        - 检查数据指针和大小
        ↓
    2. 写入音频引擎
        - audio_engine_write_data(data, size, 0)
        - 音频引擎自动解码和播放
        ↓
    接收完成

停止流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``ntwk_trans_stop()`` 详细流程：

::

    1. 检查状态
        - 如果未初始化，返回错误
        - 如果未启动，返回错误
        ↓
    2. 清除启动标志
        - is_started = false
        ↓
    3. 调用停止回调
        - stop_cb(user_data)
        - 对于 Agora: bk_agora_stop()
            * 停止 RTC
            * 停止 Agent
        - 对于 VolcEngine: bk_byte_stop()
        ↓
    停止完成

重要接口
---------------------------------

初始化接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 初始化网络传输模块
     * 
     * 根据配置选择 RTC 后端（Agora 或 VolcEngine），
     * 设置相应的回调函数，并执行预配置。
     * 
     * @return int 
     *         - 0: 成功
     *         - < 0: 失败
     */
    int ntwk_trans_init(void);

    /**
     * @brief 反初始化网络传输模块
     * 
     * 停止网络传输，清空全局上下文。
     * 
     * @return int 
     *         - 0: 成功
     *         - < 0: 失败
     */
    int ntwk_trans_deinit(void);

启动和停止接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 启动网络传输
     * 
     * 调用底层 RTC 后端的启动回调函数。
     * 
     * @param user_data 用户数据指针，传递给启动回调函数
     * @return int 
     *         - 0: 成功
     *         - < 0: 失败
     */
    int ntwk_trans_start(void *user_data);

    /**
     * @brief 停止网络传输
     * 
     * 调用底层 RTC 后端的停止回调函数。
     * 
     * @param user_data 用户数据指针，传递给停止回调函数
     * @return int 
     *         - 0: 成功
     *         - < 0: 失败
     */
    int ntwk_trans_stop(void *user_data);

    /**
     * @brief 更新网络传输
     * 
     * 调用底层 RTC 后端的更新回调函数，用于更新 Agent 配置等。
     * 
     * @param user_data 用户数据
     * @param update_info 更新信息
     * @return int 
     *         - 0: 成功
     *         - < 0: 失败
     */
    int ntwk_trans_update(void *user_data, void *update_info);

数据发送接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 发送音频数据到网络
     * 
     * @param data 音频数据指针
     * @param size 音频数据大小
     * @param audio_type 音频编码类型
     * @return int 
     *         - >= 0: 发送的字节数
     *         - < 0: 失败
     */
    int ntwk_trans_send_audio(const uint8_t *data, size_t size, 
                               audio_enc_type_t audio_type);

    /**
     * @brief 发送视频帧到网络
     * 
     * @param frame 视频帧缓冲区指针
     * @return int 
     *         - 0: 成功
     *         - < 0: 失败
     */
    int ntwk_trans_send_video(frame_buffer_t *frame);

数据接收接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 接收音频数据并写入音频引擎
     * 
     * 该函数将接收到的音频数据写入音频引擎进行解码和播放。
     * 
     * @param data 音频数据指针
     * @param size 音频数据大小
     * @return int 
     *         - 0: 成功
     *         - < 0: 失败
     */
    int ntwk_trans_recv_audio(const uint8_t *data, size_t size);

查询接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 获取当前网络传输类型
     * 
     * @return network_type_t 网络类型枚举值
     */
    network_type_t ntwk_trans_get_network_type(void);

    /**
     * @brief 获取音频编码器类型
     * 
     * 从音频引擎获取当前使用的编码器类型。
     * 
     * @return audio_enc_type_t 音频编码器类型枚举值
     */
    audio_enc_type_t ntwk_trans_get_audio_encoder_type(void);

数据结构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**network_type_t**: 网络类型枚举

.. code:: c

    typedef enum {
        NETWORK_TYPE_VOLC_RTC = 0,    // 火山引擎 RTC
        NETWORK_TYPE_AGORA_RTC,       // 声网 Agora RTC
        NETWORK_TYPE_MAX
    } network_type_t;

**ntwk_trans_ctx_t**: 网络传输模块上下文结构

.. code:: c

    typedef struct {
        network_type_t network_type;              // 网络类型
        void *network_config;                     // 网络特定配置
        audio_tx_callback_t audio_tx_cb;          // 音频发送回调
        video_tx_callback_t video_tx_cb;          // 视频发送回调
        ntwk_trans_start_callback_t start_cb;     // 启动回调
        ntwk_trans_stop_callback_t stop_cb;      // 停止回调
        ntwk_trans_pre_config_callback_t pre_config_cb;  // 预配置回调
        ntwk_trans_update_callback_t update_cb;   // 更新回调
        void *user_data;                          // 用户数据
        bool is_started;                          // 是否已启动
        bool initialized;                         // 初始化标志
    } ntwk_trans_ctx_t;

**回调函数类型**:

.. code:: c

    // 音频发送回调
    typedef int (*audio_tx_callback_t)(uint8_t *data_ptr, size_t data_len, 
                                       audio_enc_type_t audio_type);

    // 视频发送回调
    typedef int (*video_tx_callback_t)(frame_buffer_t *frame);

    // 启动回调
    typedef bk_err_t (*ntwk_trans_start_callback_t)(void *user_data);

    // 停止回调
    typedef bk_err_t (*ntwk_trans_stop_callback_t)(void *user_data);

    // 预配置回调
    typedef bk_err_t (*ntwk_trans_pre_config_callback_t)(void *user_data);

    // 更新回调
    typedef bk_err_t (*ntwk_trans_update_callback_t)(void *user_data, 
                                                      void *update_info);

主要宏定义
---------------------------------

Kconfig 配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**基本配置**:

.. code:: c

    // 启用网络传输模块
    CONFIG_BK_NETWORK_TRANSFER=y

**RTC 后端选择** :

.. code:: c

    // 使用 Agora RTC
    CONFIG_AGORA_IOT_SDK=y

    // 或使用 VolcEngine RTC
    // CONFIG_VOLC_RTC_EN=y

**其他配置**:

.. code:: c

    // 启用 WebSocket 传输（AI 场景）
    CONFIG_BK_WSS_TRANS=y

    // WebSocket 传输不使用 PSRAM
    CONFIG_BK_WSS_TRANS_NOPSRAM=y

    // 启用 Sensenova（Agora）
    CONFIG_SENSENOVA_ENABLE=y

    // 从 Beken 服务器启动 Agent（默认）
    CONFIG_STARTUP_AGENT_FROM_BK_SERVER=y
    // 或从自定义服务器启动 Agent
    // CONFIG_STARTUP_AGENT_FROM_BK_SERVER=n

使用示例
---------------------------------

基本使用
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #include "network_transfer.h"
    #include "audio_engine.h"
    #include "video_engine.h"

    // 音频读取回调（从音频引擎接收数据）
    static int audio_read_callback(unsigned char *data, unsigned int len, void *args)
    {
        // 发送音频数据到网络
        return ntwk_trans_send_audio(data, len, 
                                     ntwk_trans_get_audio_encoder_type());
    }

    int main(void)
    {
        // 1. 初始化音频引擎
        audio_engine_init();

        // 2. 初始化视频引擎
        video_engine_init();

        // 3. 初始化网络传输
        int ret = ntwk_trans_init();
        if (ret != 0) {
            LOGE("Network transfer init failed\n");
            return -1;
        }

        // 4. 启动网络传输
        char device_id[65] = "your_device_id";
        ret = ntwk_trans_start(device_id);
        if (ret != 0) {
            LOGE("Network transfer start failed\n");
            return -1;
        }

        // 5. 检查网络类型
        network_type_t type = ntwk_trans_get_network_type();
        if (type == NETWORK_TYPE_AGORA_RTC) {
            LOGI("Using Agora RTC\n");
        } else if (type == NETWORK_TYPE_VOLC_RTC) {
            LOGI("Using VolcEngine RTC\n");
        }

        // 6. 音频数据会自动通过 audio_read_callback 发送
        // 视频数据会自动通过 video_engine 发送

        // 7. 接收到的音频数据会自动播放（通过 ntwk_trans_recv_audio）

        // 8. 停止网络传输
        ntwk_trans_stop(device_id);

        // 9. 反初始化
        ntwk_trans_deinit();

        return 0;
    }

更新 Agent 配置
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 更新 Agent 配置（例如切换多模态）
    void update_agent_mode(const char *mode)
    {
        int ret = ntwk_trans_update(device_id, (void *)mode);
        if (ret != 0) {
            LOGE("Update agent failed\n");
        }
    }

注意事项
---------------------------------

1. **初始化顺序**: 建议先初始化音频引擎和视频引擎，再初始化网络传输

2. **后端选择**: 只能选择一个 RTC 后端（Agora 或 VolcEngine），通过 Kconfig 配置

3. **回调函数**: 回调函数由模块内部根据配置自动设置，应用层无需手动设置

4. **数据路由**: 
   - 音频数据：通过 audio_engine 的 read_callback 自动发送
   - 视频数据：通过 video_engine 的传输任务自动发送
   - 接收音频：通过 ntwk_trans_recv_audio 自动写入 audio_engine

5. **状态检查**: 发送数据前应确保网络传输已启动（通过检查返回值）

6. **错误处理**: 所有接口都有错误返回值，应检查返回值并处理错误

7. **资源管理**: 使用完毕后必须调用 ``ntwk_trans_stop()`` 和 ``ntwk_trans_deinit()``

8. **线程安全**: 网络传输模块内部使用线程安全机制，但回调函数中应避免长时间阻塞

9. **Agent 管理**: Agent 的启动和停止由底层 RTC 后端管理，网络传输模块只负责调用

10. **配置依赖**: 确保对应的 RTC 后端配置正确（Agora 需要配置 App ID、Token 等）


