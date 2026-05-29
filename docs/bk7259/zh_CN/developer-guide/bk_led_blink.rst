LED Blink 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

LED Blink 是一个 LED 灯效控制模块，提供了丰富的 LED 状态控制和闪烁效果。该模块支持多个 LED 的独立控制，包括常亮、常灭、快闪、慢闪、交替闪烁等多种状态，并支持错误优先级管理和闪烁时长控制。该模块封装了 GPIO 控制、定时器管理和状态机逻辑，为上层应用提供简单的 LED 控制接口。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **多 LED 支持**: 支持最多 4 个 LED 独立控制
- **多种状态**: 支持常亮、常灭、快闪、慢闪、交替闪烁
- **错误优先级**: 支持错误类型优先级管理（严重错误 > 警告）
- **时长控制**: 支持设置闪烁持续时间
- **状态投票**: 支持多个状态源投票，自动选择最高优先级状态
- **线程安全**: 使用互斥锁保护 LED 操作

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

LED Blink 模块架构如下：

::

    应用层 (Application)
            ↓
    LED App API (led_app.h)
            ↓
    LED Blink API (led_blink.h)
            ↓
    LED 控制块 (LedControlBlock)
            ↓
    GPIO 控制 (bk_gpio)
            ↓
    LED 硬件

工作流程
---------------------------------

初始化流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``led_driver_init()`` 详细流程：

::

    1. 初始化互斥锁
        - rtos_init_mutex(&led_mutex)
        ↓
    2. 清零 LED 控制块数组
        - memset(led_pool, 0, sizeof(led_pool))
        ↓
    3. 注册红色 LED
        - led_register(RED_LED)
        ↓
    4. 注册绿色 LED
        - led_register(GREEN_LED)
        ↓
    初始化完成

LED 状态设置流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``led_app_set()`` 详细流程：

::

    1. 解析操作类型
        - 根据 led_operate_t 确定 LED 和状态
        ↓
    2. 获取 LED 句柄
        - 根据操作类型获取对应的 LED 句柄
        ↓
    3. 设置 LED 状态
        - led_set_state(led_handle, new_state, time)
        ↓
    4. 更新控制块
        - 更新 LedControlBlock 结构
        - 设置闪烁间隔和持续时间
        ↓
    5. 启动定时器
        - 如果为闪烁状态，启动定时器
        ↓
    状态设置完成

LED 闪烁流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

定时器回调流程：

::

    1. 定时器到期触发回调
        - timer_callback(led_handle)
        ↓
    2. 检查持续时间
        - 如果设置了持续时间且已到期，停止闪烁
        ↓
    3. 检查错误状态
        - 如果存在错误，按错误优先级闪烁
        ↓
    4. 切换 LED 状态
        - 翻转 led_status
        - 更新 GPIO 输出
        ↓
    5. 继续定时器
        - 如果未到期，继续定时器
        ↓
    循环步骤 1-5

LED 状态投票流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``led_blink()`` 详细流程：

::

    1. 处理指示状态（绿色 LED）
        - 检查 indicates_state 位掩码
        - 按优先级选择状态
        ↓
    2. 处理警告状态（红色 LED）
        - 检查 warning_state 位掩码
        - 按优先级选择状态
        ↓
    3. 应用状态
        - 调用 led_app_set() 设置 LED 状态
        ↓
    状态更新完成

重要接口
---------------------------------

初始化接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 初始化 LED 驱动
     * 
     * 初始化互斥锁，注册红色和绿色 LED
     */
    void led_driver_init(void);

LED 控制接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 设置 LED 状态
     * 
     * @param oper LED 操作类型
     * @param time 持续时间（毫秒），0xFFFFFFFF 表示永久
     */
    void led_app_set(led_operate_t oper, uint32_t time);

    /**
     * @brief LED 状态投票
     * 
     * 根据警告状态和指示状态自动选择 LED 状态
     * 
     * @param warning_state 警告状态位掩码指针
     * @param indicates_state 指示状态位掩码
     */
    void led_blink(uint32_t *warning_state, uint32_t indicates_state);

LED 状态类型
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    typedef enum {
        LED_OFF = 0,        // 关闭
        LED_ON,             // 常亮
        LED_FAST_BLINK,      // 快闪
        LED_SLOW_BLINK,      // 慢闪
        LED_ALTERNATE       // 交替闪烁
    } LedState;

LED 操作类型
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    typedef enum {
        // 绿色 LED
        LED_OFF_GREEN,
        LED_ON_GREEN,
        LED_FAST_BLINK_GREEN,
        LED_SLOW_BLINK_GREEN,
        
        // 红色 LED
        LED_OFF_RED,
        LED_ON_RED,
        LED_FAST_BLINK_RED,
        LED_SLOW_BLINK_RED,
        
        // 交替闪烁
        LED_REG_GREEN_ALTERNATE,
        LED_REG_GREEN_ALTERNATE_OFF,
        
        // 错误状态
        LED_ERROR_CRITICAL,
        LED_ERROR_WARNING,
        LED_ERROR_CRITICAL_CLOSE,
        LED_ERROR_WARNING_CLOSE,
    } led_operate_t;

警告状态定义
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    enum {
        WARNING_PROVIOSION_FAIL,      // 配网失败（快闪红色）
        WARNING_WIFI_FAIL,            // Wi-Fi 失败（快闪红色）
        WARNING_RTC_CONNECT_LOST,     // RTC 连接丢失（快闪红色）
        WARNING_AGENT_OFFLINE,        // Agent 离线（快闪红色）
        WARNING_AGENT_AGENT_START_FAIL, // Agent 启动失败（快闪红色）
        WARNING_LOW_BATTERY,          // 低电量（慢闪红色）
    };

指示状态定义
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    enum {
        INDICATES_WIFI_RECONNECT,     // Wi-Fi 重连（快闪绿色）
        INDICATES_PROVISIONING,       // 配网中（红绿交替）
        INDICATES_POWER_ON,           // 上电（常亮绿色）
        INDICATES_STANDBY,             // 待机（慢闪绿色）
        INDICATES_AGENT_CONNECT,      // Agent 连接（快闪绿色）
    };

主要宏定义
---------------------------------

配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 启用 LED Blink 模块
    CONFIG_LED_BLINK=y

硬件配置
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // LED GPIO 定义
    #define RED_LED   40   // 红色 LED GPIO
    #define GREEN_LED 41   // 绿色 LED GPIO

    // LED 数量限制
    #define MAX_LED_NUM 4

闪烁时间配置
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 快闪间隔（毫秒）
    #define LED_FAST_BLINK_TIME 200

    // 慢闪间隔（毫秒）
    #define LED_SLOW_BLINK_TIME 1000

    // 永久闪烁
    #define LED_LAST_FOREVER 0xFFFFFFFF

使用示例
---------------------------------

基本使用
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #include "led_app.h"

    void example_usage(void)
    {
        // 初始化 LED
        led_driver_init();
        
        // 设置绿色 LED 快闪
        led_app_set(LED_FAST_BLINK_GREEN, LED_LAST_FOREVER);
        
        // 设置红色 LED 慢闪 5 秒
        led_app_set(LED_SLOW_BLINK_RED, 5000);
        
        // 关闭绿色 LED
        led_app_set(LED_OFF_GREEN, 0);
    }

状态投票使用
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    void state_voting_example(void)
    {
        uint32_t warning_state = 0;
        uint32_t indicates_state = 0;
        
        // 设置警告状态
        warning_state |= (1 << WARNING_WIFI_FAIL);
        
        // 设置指示状态
        indicates_state |= (1 << INDICATES_STANDBY);
        
        // 自动选择 LED 状态
        led_blink(&warning_state, indicates_state);
        // 结果：红色快闪（警告优先级高于指示）
    }

注意事项
---------------------------------

1. **状态优先级**: 警告状态优先级高于指示状态，严重错误优先级高于警告

2. **闪烁间隔**: 快闪间隔 200ms，慢闪间隔 1000ms，可在头文件中修改

3. **状态投票**: 使用 ``led_blink()`` 时，会自动根据优先级选择状态

