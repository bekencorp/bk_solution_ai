Key App 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

Key App 是一个按键应用模块，提供了 GPIO 按键的配置、事件处理和回调机制。该模块封装了按键扫描、事件识别（单击、双击、长按）和事件分发等功能，为上层应用提供统一的按键处理接口。支持多按键配置，每个按键可独立配置触发事件。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **多按键支持**: 支持多个 GPIO 按键配置
- **事件识别**: 支持单击、双击、长按事件识别
- **灵活配置**: 支持自定义按键 GPIO 和事件映射
- **事件回调**: 支持按键事件回调处理
- **唤醒源注册**: 支持将按键注册为唤醒源
- **音量控制**: 内置音量增减控制功能

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Key App 模块架构如下：

::

    应用层 (Application)
            ↓
    Key App API (key_app_service.h)
            ↓
    按键配置 (key_app_config.h)
            ↓
    按键驱动 (multi_button)
            ↓
    GPIO 驱动 (bk_gpio)
            ↓
    硬件按键

工作流程
---------------------------------

初始化流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``bk_key_service_init()`` 详细流程：

::

    1. 加载按键配置
        - 从 KEY_DEFAULT_CONFIG_TABLE 读取配置
        ↓
    2. 初始化按键驱动
        - 配置 GPIO 引脚
        - 设置触发电平
        - 注册按键事件回调
        ↓
    3. 启动按键扫描任务
        - 创建按键处理线程
        ↓
    初始化完成

按键事件处理流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

按键事件处理流程：

::

    1. GPIO 中断触发
        - 按键按下/释放触发中断
        ↓
    2. 按键驱动扫描
        - multi_button 驱动扫描按键状态
        ↓
    3. 事件识别
        - 识别单击、双击、长按事件
        ↓
    4. 事件分发
        - 根据配置表查找对应事件
        ↓
    5. 调用事件处理函数
        - handle_system_event(event)
        ↓
    6. 执行相应操作
        - 根据事件类型执行操作
        ↓
    事件处理完成

事件处理映射
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

系统事件处理映射：

::

    VOLUME_UP          → audio_engine_volume_increase()
    VOLUME_DOWN        → audio_engine_volume_decrease()
    SHUT_DOWN          → power_off() (长按 >= 9秒)
    POWER_ON           → power_on()
    CONFIG_NETWORK     → bk_sconf_prepare_for_smart_config()
    IR_MODE_SWITCH     → app_event_send_msg(APP_EVT_IR_MODE_SWITCH)
    FACTORY_RESET      → bk_factory_reset() + bk_reboot()
    AUDIO_BUF_APPEND   → app_event_send_msg(APP_EVT_ASR_WAKEUP)

重要接口
---------------------------------

初始化接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 初始化按键服务
     * 
     * 加载按键配置，初始化按键驱动，启动按键扫描任务
     */
    void bk_key_service_init(void);

    /**
     * @brief 注册按键为唤醒源
     * 
     * 将按键注册为系统唤醒源，支持从深度睡眠唤醒
     */
    void bk_key_register_wakeup_source(void);

    /**
     * @brief 初始化音量
     * 
     * 初始化音量控制功能
     */
    void volume_init(void);

按键配置结构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    typedef struct {
        uint8_t gpio_id;           // GPIO 引脚编号
        uint8_t active_level;      // 触发电平 (LOW_LEVEL_TRIGGER/HIGH_LEVEL_TRIGGER)
        uint8_t short_event;       // 单击事件
        uint8_t double_event;      // 双击事件
        uint8_t long_event;        // 长按事件
    } KeyConfig_t;

默认配置表
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #define KEY_DEFAULT_CONFIG_TABLE \
    { \
        { \
            .gpio_id = KEY_GPIO_13, \
            .active_level = LOW_LEVEL_TRIGGER, \
            .short_event = VOLUME_UP, \
            .double_event = VOLUME_UP, \
            .long_event = CONFIG_NETWORK \
        }, \
        { \
            .gpio_id = KEY_GPIO_12, \
            .active_level = LOW_LEVEL_TRIGGER, \
            .short_event = IR_MODE_SWITCH, \
            .double_event = POWER_ON, \
            .long_event = SHUT_DOWN \
        }, \
        { \
            .gpio_id = KEY_GPIO_8, \
            .active_level = LOW_LEVEL_TRIGGER, \
            .short_event = VOLUME_DOWN, \
            .double_event = VOLUME_DOWN, \
            .long_event = FACTORY_RESET \
        } \
    }

系统事件类型
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    enum {
        VOLUME_UP,          // 音量增加
        VOLUME_DOWN,        // 音量减少
        SHUT_DOWN,          // 关机（长按 >= 9秒）
        POWER_ON,           // 开机
        AI_AGENT_CONFIG,    // AI Agent 配置
        CONFIG_NETWORK,     // 网络配网
        IR_MODE_SWITCH,     // 图像识别模式切换
        FACTORY_RESET,      // 恢复出厂设置
        AUDIO_BUF_APPEND,   // 音频缓冲区追加
    };

主要宏定义
---------------------------------

配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 启用按键功能
    CONFIG_BUTTON=y

GPIO 定义
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 默认按键 GPIO（可在 key_app_config.h 中修改）
    #define KEY_GPIO_13 13
    #define KEY_GPIO_12 12
    #define KEY_GPIO_8  8


长按时间配置
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

长按时间在 ``multi_button.h`` 中的 ``LONG_TICKS`` 宏定义：

.. code:: c

    #define LONG_TICKS  (3000 / TICKS_INTERVAL)  // 长按时间（毫秒）

使用示例
---------------------------------

基本使用
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #include "key_app_service.h"

    void app_init(void)
    {
        // 初始化按键服务
        bk_key_service_init();
        
        // 注册唤醒源（可选）
        bk_key_register_wakeup_source();
        
        // 初始化音量（可选）
        volume_init();
    }

自定义按键配置
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 在 key_app_config.h 中定义自定义配置
    #if CONFIG_USR_KEY_CFG_EN
    #define KEY_DEFAULT_CONFIG_TABLE \
    { \
        { \
            .gpio_id = GPIO_10, \
            .active_level = LOW_LEVEL_TRIGGER, \
            .short_event = VOLUME_UP, \
            .double_event = VOLUME_UP, \
            .long_event = CONFIG_NETWORK \
        }, \
        // 添加更多按键配置...
    }
    #endif

注意事项
---------------------------------

1. **GPIO 冲突**: 确保 GPIO 引脚只用于按键，避免功能冲突

2. **事件处理**: 事件处理函数应避免长时间阻塞，否则影响按键响应速度

3. **配置修改**: 修改按键配置需重新编译，不支持运行时修改

4. **硬件差异**: 不同开发板的按键 GPIO 可能不同，需根据硬件设计修改配置

