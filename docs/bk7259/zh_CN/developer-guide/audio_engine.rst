Audio Engine 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

Audio Engine 是基于 ``bk_voice_service`` 基础设施构建的高级音频引擎模块，提供简化的音频启动/停止操作 API。该模块封装了复杂的音频处理流程，包括音频采集、编码、解码、AEC（回声消除）、NS（噪声抑制）等功能，为上层应用提供统一的音频处理接口。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **简化 API**: 易于使用的 ``audio_engine_start()`` 和 ``audio_engine_stop()`` 函数
- **完整错误处理**: 详细的错误代码，便于调试
- **灵活配置**: 支持多种麦克风/扬声器类型和音频处理功能
- **线程安全**: 正确的资源管理和状态跟踪
- **回调支持**: 事件和数据读取回调，用于实时音频处理
- **音量控制**: 支持音量调节和持久化存储
- **提示音支持**: 支持提示音播放功能（可选）

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Audio Engine 模块架构如下：

::

    应用层 (Application)
            ↓
    Audio Engine API (audio_engine.h)
            ↓
    Voice Service (bk_voice_service)
            ↓
    Audio Pipeline (音频管道)
            ↓
    硬件层 (ADC/DAC)

工作流程
---------------------------------

初始化流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Audio Engine 的初始化流程如下：

::

    audio_engine_init()
        ↓
    1. 音量初始化 (audio_engine_volume_init)
        - 从配置读取音量等级
        - 计算音量增益表
        ↓
    2. 构建配置结构 (audio_engine_cfg_t)
        - 采样率配置
        - 编码/解码类型
        - AEC/NS 配置
        - PA 控制配置
        - 回调函数设置
        ↓
    3. 启动音频引擎 (audio_engine_start)
        - 初始化 Voice Service
        - 配置麦克风流
        - 配置扬声器流
        - 配置编码器/解码器
        - 配置 AEC/NS 算法
        - 启动音频服务
        ↓
    4. 初始化提示音（可选）
        - audio_engine_prompt_tone_init
        ↓
    初始化完成

启动流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``audio_engine_start()`` 详细流程：

::

    1. 参数验证
        - 检查配置指针有效性
        - 验证采样率（8000 或 16000）
        - 检查是否已启动
        ↓
    2. 配置 Voice Service
        - 麦克风配置 (onboard_mic_stream_cfg_t)
            * ADC 采样率
            * 数字增益/模拟增益
            * 帧大小（20ms）
            * AEC 模式（硬件/软件）
        - 编码器配置
            * G.711A/U: 160/320 字节帧
            * G.722: 80/160 字节帧
            * OPUS: 默认配置
            * PCM: 原始数据
        - 解码器配置
            * 对应编码器的解码配置
        - 扬声器配置 (onboard_speaker_stream_cfg_t)
            * DAC 采样率
            * PA 控制（GPIO、延迟等）
            * 数字增益/模拟增益
        - AEC 配置（如果启用）
            * AEC 模式选择
            * 多路输出端口
        - EQ 配置（如果启用）
        ↓
    3. 初始化 Voice Service
        - bk_voice_init(&voice_cfg)
        ↓
    4. 初始化 Voice Read Service
        - bk_voice_read_init()
        - 注册读取回调
        ↓
    5. 初始化 Voice Write Service
        - bk_voice_write_init()
        ↓
    6. 启动 Voice Service
        - bk_voice_start()
        - bk_voice_read_start()
        - bk_voice_write_start()
        ↓
    7. ASR 初始化（如果启用）
        - 配置 ASR 服务
        - 启动 ASR
        ↓
    启动完成

音频数据流
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**上行音频流（麦克风 → 网络）**:

::

    麦克风采集 (ADC)
        ↓
    音频预处理 (AEC/NS)
        ↓
    音频编码 (G.711/G.722/OPUS/PCM)
        ↓
    读取回调 (audio_engine_read_callback_t)
        ↓
    network_transfer
        ↓
    网络发送

**下行音频流（网络 → 扬声器）**:

::

    网络接收
        ↓
    audio_engine_write_data()
        ↓
    音频解码 (G.711/G.722/OPUS/PCM)
        ↓
    音频后处理 (EQ)
        ↓
    扬声器播放 (DAC)

重要接口
---------------------------------

初始化接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 初始化并启动音频引擎
     * 
     * @param cfg 音频引擎配置结构指针
     * @return int 
     *         - 0 (AUDIO_ENGINE_SUCCESS): 成功
     *         - < 0: 错误代码（见 audio_engine_err_t）
     */
    int audio_engine_start(audio_engine_cfg_t *cfg);

    /**
     * @brief 停止音频引擎并清理资源
     * 
     * @return int 
     *         - 0: 成功
     *         - < 0: 错误代码
     */
    int audio_engine_stop(void);

    /**
     * @brief 初始化音频引擎（使用默认配置）
     * 
     * 该函数使用 Kconfig 配置的默认参数自动初始化音频引擎
     * 
     * @return int 
     *         - 0: 成功
     *         - < 0: 错误代码
     */
    int audio_engine_init(void);

    /**
     * @brief 反初始化音频引擎
     * 
     * @return int 
     *         - 0: 成功
     */
    int audio_engine_deinit(void);

数据操作接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 向扬声器写入音频数据
     * 
     * @param data 音频数据指针
     * @param size 音频数据大小（字节）
     * @param timeout_ms 超时时间（毫秒）
     * @return int 
     *         - 0: 成功
     *         - < 0: 错误代码
     */
    int audio_engine_write_data(const uint8_t *data, uint32_t size, uint32_t timeout_ms);

    /**
     * @brief 检查音频引擎是否正在运行
     * 
     * @return bool 
     *         - true: 音频引擎正在运行
     *         - false: 音频引擎未运行
     */
    bool audio_engine_is_running(void);

配置和查询接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 获取音频引擎编码器类型
     * 
     * @return audio_enc_type_t 编码器类型
     */
    audio_enc_type_t audio_engine_get_encoder_type(void);

    /**
     * @brief 获取音频引擎解码器类型
     * 
     * @return audio_dec_type_t 解码器类型
     */
    audio_dec_type_t audio_engine_get_decoder_type(void);

    /**
     * @brief 字符串转编码器类型
     * 
     * @param enc_str 编码器类型字符串 ("PCM", "G711A", "G711U", "G722", "OPUS")
     * @return audio_enc_type_t 编码器类型枚举值
     */
    audio_enc_type_t audio_engine_str_to_enc_type(const char *enc_str);

    /**
     * @brief 字符串转解码器类型
     * 
     * @param dec_str 解码器类型字符串 ("PCM", "G711A", "G711U", "G722", "OPUS")
     * @return audio_dec_type_t 解码器类型枚举值
     */
    audio_dec_type_t audio_engine_str_to_dec_type(const char *dec_str);

    /**
     * @brief 获取音频引擎错误字符串
     * 
     * @param err 错误代码
     * @return const char* 错误描述字符串
     */
    const char *audio_engine_err_to_str(int err);

音量控制接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 增加音量
     */
    void audio_engine_volume_increase(void);

    /**
     * @brief 减少音量
     */
    void audio_engine_volume_decrease(void);

数据结构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**audio_engine_cfg_t**: 音频引擎配置结构

.. code:: c

    typedef struct {
        uint32_t mic_sample_rate;      // 麦克风采样率（8000 或 16000）
        uint32_t spk_sample_rate;      // 扬声器采样率（8000 或 16000）
        
        /* 音频处理 */
        uint8_t aec_enable;            // 0: 禁用, 1: 启用 AEC
        uint8_t eq_enable;             // 0: 禁用, 1: 单声道 EQ, 2: 立体声 EQ

        /* 编码/解码 */
        audio_enc_type_t enc_type;     // 编码类型
        audio_dec_type_t dec_type;     // 解码类型
        
        /* 音频增益 */
        uint8_t dig_gain;              // DAC 数字增益
        uint8_t ana_gain;              // DAC 模拟增益
        
        /* PA 控制 */
        uint8_t pa_enable;             // 0: 禁用, 1: 启用 PA 控制
        uint8_t pa_gpio;               // PA 控制 GPIO 编号
        uint8_t pa_on_level;           // PA 开启电平
        uint32_t pa_on_delay;          // PA 开启延迟（毫秒）
        uint32_t pa_off_delay;         // PA 关闭延迟（毫秒）
        
        /* 回调函数 */
        voice_event_callback_t event_cb;        // 语音事件回调
        audio_engine_read_callback_t read_cb;   // 音频读取回调
        void *user_data;                        // 用户数据
    } audio_engine_cfg_t;

**audio_engine_err_t**: 错误代码枚举

.. code:: c

    typedef enum {
        AUDIO_ENGINE_SUCCESS = 0,            // 成功
        AUDIO_ENGINE_ERR_INIT_FAILED = -1,   // 初始化失败
        AUDIO_ENGINE_ERR_INVALID_PARAM = -2,  // 无效参数
        AUDIO_ENGINE_ERR_NOT_STARTED = -3,    // 音频引擎未启动
        AUDIO_ENGINE_ERR_VOICE_INIT = -4,     // Voice 初始化失败
        AUDIO_ENGINE_ERR_READ_INIT = -5,      // Voice Read 初始化失败
        AUDIO_ENGINE_ERR_WRITE_INIT = -6,     // Voice Write 初始化失败
        AUDIO_ENGINE_ERR_VOICE_START = -7,    // Voice 启动失败
        AUDIO_ENGINE_ERR_READ_START = -8,     // Voice Read 启动失败
        AUDIO_ENGINE_ERR_WRITE_START = -9,    // Voice Write 启动失败
        AUDIO_ENGINE_ERR_VOICE_STOP = -10,    // Voice 停止失败
        AUDIO_ENGINE_ERR_READ_STOP = -11,     // Voice Read 停止失败
        AUDIO_ENGINE_ERR_WRITE_STOP = -12,    // Voice Write 停止失败
        AUDIO_ENGINE_ERR_ASR_INIT = -13,       // ASR 初始化失败
        AUDIO_ENGINE_ERR_ASR_START = -14,      // ASR 启动失败
        AUDIO_ENGINE_ERR_ASR_STOP = -15,       // ASR 停止失败
    } audio_engine_err_t;

主要宏定义
---------------------------------

Kconfig 配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**基本配置**:

.. code:: c

    // 启用音频引擎
    CONFIG_BK_AUDIO_ENGINE=y

    // 音频帧时长（毫秒）
    CONFIG_AE_AUDIO_FRAME_DURATION_MS=20  // 范围: 20-60

    // ADC 采样率
    CONFIG_AE_AUDIO_ADC_SAMP_RATE=16000

    // DAC 采样率
    CONFIG_AE_AUDIO_DAC_SAMP_RATE=16000

**编码器配置**:

.. code:: c

    // 编码器类型选择（互斥）
    CONFIG_AE_AUDIO_ENCODER_G722=y      // G.722 编码
    // 或
    CONFIG_AE_AUDIO_ENCODER_G711A=y     // G.711A 编码
    // 或
    CONFIG_AE_AUDIO_ENCODER_G711U=y     // G.711U 编码
    // 或
    CONFIG_AE_AUDIO_ENCODER_OPUS=y      // OPUS 编码
    // 或
    CONFIG_AE_AUDIO_ENCODER_PCM=y       // PCM（无编码）

    // 编码器类型字符串（自动生成）
    CONFIG_AE_AUDIO_ENCODER_TYPE="G722"  // 或 "G711A", "G711U", "OPUS", "PCM"

**解码器配置**:

.. code:: c

    // 解码器类型选择（互斥）
    CONFIG_AE_AUDIO_DECODER_G722=y      // G.722 解码
    // 或
    CONFIG_AE_AUDIO_DECODER_G711A=y     // G.711A 解码
    // 或
    CONFIG_AE_AUDIO_DECODER_G711U=y     // G.711U 解码
    // 或
    CONFIG_AE_AUDIO_DECODER_OPUS=y      // OPUS 解码
    // 或
    CONFIG_AE_AUDIO_DECODER_PCM=y       // PCM（无解码）

    // 解码器类型字符串（自动生成）
    CONFIG_AE_AUDIO_DECODER_TYPE="G722"  // 或 "G711A", "G711U", "OPUS", "PCM"


**提示音配置** (可选):

.. code:: c

    // 启用提示音支持
    CONFIG_AE_SUPPORT_PROMPT_TONE=y

    // 提示音源选择
    CONFIG_AE_PROMPT_TONE_SOURCE_ARRAY=y    // 数组存储
    // 或
    CONFIG_AE_PROMPT_TONE_SOURCE_VFS=y      // 文件系统存储

    // 提示音解码器类型
    CONFIG_AE_PROMPT_TONE_DECODER_WAV=y     // WAV 格式
    // 或
    CONFIG_AE_PROMPT_TONE_DECODER_MP3=y     // MP3 格式
    // 或
    CONFIG_AE_PROMPT_TONE_DECODER_PCM=y     // PCM 格式


注意事项
---------------------------------

1. **采样率限制**: 麦克风和扬声器采样率必须为 8000 或 16000 Hz

2. **帧大小**: 帧大小根据采样率自动计算（20ms 一帧）
   - 8kHz: 160 样本/帧
   - 16kHz: 320 样本/帧

3. **AEC 模式**: 硬件 AEC 模式需要双通道麦克风输入

4. **编码器依赖**: 选择编码器时，需要确保对应的 ADK 和 Voice Service 编码器已启用

5. **线程安全**: 音频引擎内部使用线程安全机制，但回调函数中应避免长时间阻塞

6. **资源管理**: 使用完毕后必须调用 ``audio_engine_stop()`` 和 ``audio_engine_deinit()`` 释放资源

7. **音量持久化**: 音量等级会保存到配置中，下次启动时自动恢复


