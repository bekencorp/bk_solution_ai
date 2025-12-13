Countdown 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

Countdown 是一个倒计时管理模块，提供了多票源倒计时机制。该模块支持多个倒计时源（配网、网络错误、待机、OTA等），根据优先级自动选择活跃的倒计时，并在倒计时结束时自动进入深度睡眠模式。该模块封装了定时器管理和倒计时裁决逻辑，为上层应用提供统一的倒计时管理接口。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **多票源支持**: 支持多个倒计时源同时存在
- **优先级管理**: 根据优先级自动选择活跃倒计时
- **自动睡眠**: 倒计时结束时自动进入深度睡眠
- **OTA 暂停**: OTA 事件时自动暂停倒计时
- **灵活配置**: 支持自定义各票源的倒计时时长
- **线程安全**: 使用定时器机制，线程安全

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Countdown 模块架构如下：

::

    应用层 (Application)
            ↓
    Countdown API (countdown.h, countdown_app.h)
            ↓
    倒计时裁决 (update_countdown)
            ↓
    定时器管理 (start_countdown/stop_countdown)
            ↓
    RTOS 定时器 (oneshot timer)
            ↓
    深度睡眠 (Deep Sleep)

工作流程
---------------------------------

倒计时更新流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``update_countdown()`` 详细流程：

::

    1. 检查 OTA 暂停票
        - 如果存在 OTA 票，停止倒计时并返回
        ↓
    2. 遍历票源（按优先级）
        - COUNTDOWN_TICKET_PROVISIONING (最高优先级)
        - COUNTDOWN_TICKET_NETWORK_ERROR
        - COUNTDOWN_TICKET_STANDBY (最低优先级)
        ↓
    3. 选择第一个活跃的票源
        - 检查 s_active_tickets 位掩码
        ↓
    4. 应用倒计时
        - 如果票源改变，停止旧定时器
        - 启动新定时器，使用对应时长
        ↓
    倒计时更新完成

倒计时启动流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``start_countdown()`` 详细流程：

::

    1. 检查定时器状态
        - 如果定时器未初始化，创建新定时器
        - 如果定时器已存在，重新加载
        ↓
    2. 初始化定时器
        - rtos_init_oneshot_timer()
        - 设置回调函数 CountdownCallback
        ↓
    3. 启动定时器
        - rtos_start_oneshot_timer()
        - 或 rtos_oneshot_reload_timer_ex()
        ↓
    定时器运行中

倒计时停止流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``stop_countdown()`` 详细流程：

::

    1. 检查定时器状态
        - rtos_is_oneshot_timer_init()
        ↓
    2. 停止定时器
        - 如果正在运行，调用 rtos_stop_oneshot_timer()
        ↓
    3. 销毁定时器
        - rtos_deinit_oneshot_timer()
        ↓
    定时器已停止

倒计时到期处理
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``CountdownCallback()`` 回调流程：

::

    1. 定时器到期触发回调
        ↓
    2. 记录日志
        - "Standy time is up, start enter deepsleep"
        ↓
    3. 进入深度睡眠
        - bk_reboot_ex(RESET_SOURCE_FORCE_DEEPSLEEP)
        ↓
    系统进入深度睡眠

重要接口
---------------------------------

倒计时控制接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 启动倒计时定时器
     * 
     * 创建并启动一个单次定时器。当倒计时到期时，系统将自动进入深度睡眠模式。
     * 如果定时器已存在，将使用新的超时值重新加载。
     * 
     * @param time_ms 倒计时时长（毫秒）
     * 
     * @note 当倒计时到期时，会触发强制深度睡眠 (RESET_SOURCE_FORCE_DEEPSLEEP)
     */
    void start_countdown(uint32_t time_ms);

    /**
     * @brief 停止倒计时定时器
     * 
     * 停止并删除倒计时定时器，释放相关资源。
     * 即使定时器未初始化或未运行，也可以安全调用此函数。
     * 
     * @note 调用此函数后，定时器完全销毁。需要再次调用 start_countdown() 来重新创建。
     */
    void stop_countdown(void);

倒计时更新接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 更新倒计时状态
     * 
     * 根据活跃票源更新倒计时。该函数会根据优先级选择活跃的倒计时源，
     * 并启动或更新相应的倒计时定时器。
     * 
     * @param s_active_tickets 活跃票源的位掩码
     * 
     * @note OTA 票具有特殊逻辑，会暂停所有倒计时
     */
    void update_countdown(uint32_t s_active_tickets);

票源类型定义
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    typedef enum {
        COUNTDOWN_TICKET_PROVISIONING,   // 配网倒计时(5分钟), 最高优先级
        COUNTDOWN_TICKET_NETWORK_ERROR,  // 网络错误倒计时(5分钟), 中优先级
        COUNTDOWN_TICKET_STANDBY,        // 待机倒计时(3分钟), 低优先级
        COUNTDOWN_TICKET_OTA,            // OTA 事件，特殊逻辑，不参与比较
        COUNTDOWN_TICKET_MAX
    } countdown_ticket_t;

主要宏定义
---------------------------------

配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 启用 Countdown 模块
    CONFIG_COUNTDOWN=y

倒计时时长配置
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

倒计时时长在 ``countdown_app.c`` 中的 ``s_ticket_durations[]`` 数组中配置：

.. code:: c

    static const uint32_t s_ticket_durations[COUNTDOWN_TICKET_MAX] = {
        [COUNTDOWN_TICKET_PROVISIONING] = 5 * 60 * 1000,  // 5分钟
        [COUNTDOWN_TICKET_NETWORK_ERROR] = 5 * 60 * 1000,  // 5分钟
        [COUNTDOWN_TICKET_STANDBY] = 3 * 60 * 1000,        // 3分钟
        [COUNTDOWN_TICKET_OTA] = COUNTDOWN_INFINITE,        // 无限（暂停倒计时）
    };

特殊值
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 无限倒计时（用于暂停）
    #define COUNTDOWN_INFINITE 0xFFFFFFFF

使用示例
---------------------------------

基本使用
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #include "countdown_app.h"

    void example_usage(void)
    {
        uint32_t active_tickets = 0;
        
        // 设置配网倒计时票
        active_tickets |= (1 << COUNTDOWN_TICKET_PROVISIONING);
        
        // 更新倒计时（将启动 5 分钟倒计时）
        update_countdown(active_tickets);
        
        // 清除配网票，设置待机票
        active_tickets &= ~(1 << COUNTDOWN_TICKET_PROVISIONING);
        active_tickets |= (1 << COUNTDOWN_TICKET_STANDBY);
        
        // 更新倒计时（将启动 3 分钟倒计时）
        update_countdown(active_tickets);
        
        // OTA 事件，暂停倒计时
        active_tickets |= (1 << COUNTDOWN_TICKET_OTA);
        update_countdown(active_tickets);  // 倒计时停止
        
        // 清除所有票，停止倒计时
        active_tickets = 0;
        update_countdown(active_tickets);  // 倒计时停止
    }

优先级示例
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    void priority_example(void)
    {
        uint32_t active_tickets = 0;
        
        // 同时设置多个票源
        active_tickets |= (1 << COUNTDOWN_TICKET_PROVISIONING);  // 5分钟
        active_tickets |= (1 << COUNTDOWN_TICKET_STANDBY);        // 3分钟
        
        // 更新倒计时
        // 由于配网票优先级更高，将使用 5 分钟倒计时
        update_countdown(active_tickets);
        
        // 清除配网票后，待机票生效，倒计时变为 3 分钟
        active_tickets &= ~(1 << COUNTDOWN_TICKET_PROVISIONING);
        update_countdown(active_tickets);
    }

注意事项
---------------------------------

1. **优先级顺序**: 票源按枚举顺序确定优先级，PROVISIONING > NETWORK_ERROR > STANDBY

2. **OTA 特殊处理**: OTA 票存在时会暂停所有倒计时

3. **深度睡眠**: 倒计时到期时会强制进入深度睡眠，无法取消

4. **时长配置**: 修改倒计时时长需要修改 ``s_ticket_durations[]`` 数组

