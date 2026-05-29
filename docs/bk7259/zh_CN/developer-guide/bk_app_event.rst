App Event 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

App Event 是一个应用层事件系统模块，提供了统一的事件分发和处理机制。该模块封装了事件队列、事件处理线程和事件回调注册等功能，为上层应用提供解耦的事件通信接口。通过 App Event 模块，不同模块之间可以异步通信，实现松耦合的架构设计。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **事件驱动架构**: 基于消息队列的异步事件处理机制
- **多事件类型支持**: 支持 ASR、网络、RTC、Agent、OTA 等多种事件类型
- **事件回调注册**: 支持为特定事件类型注册回调函数
- **线程安全**: 使用互斥锁保护事件处理流程
- **解耦设计**: 模块间通过事件通信，降低耦合度
- **灵活扩展**: 易于添加新的事件类型和处理逻辑

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

App Event 模块架构如下：

::

    应用层 (Application)
            ↓
    App Event API (app_event.h)
            ↓
    事件队列 (Message Queue)
            ↓
    事件处理线程 (Event Thread)
            ↓
    事件回调分发 (Callback Dispatch)
            ↓
    各模块事件处理 (Module Handlers)

工作流程
---------------------------------

初始化流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

App Event 的初始化流程如下：

::

    app_event_init()
        ↓
    1. 创建事件处理线程
        - 创建消息队列
        - 创建互斥锁
        - 启动事件处理线程
        ↓
    2. 初始化事件处理器链表
        - s_event_handlers = NULL
        ↓
    初始化完成

事件发送流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``app_event_send_msg()`` 详细流程：

::

    1. 参数验证
        - 检查事件类型有效性
        ↓
    2. 创建事件消息
        - 分配 app_evt_msg_t 结构
        - 设置事件类型和参数
        ↓
    3. 发送到消息队列
        - rtos_push_to_queue()
        ↓
    4. 事件处理线程接收
        - 从队列中取出消息
        ↓
    5. 事件分发
        - 遍历事件处理器链表
        - 调用匹配的回调函数
        ↓
    6. 执行模块特定处理
        - 根据事件类型执行相应逻辑
        ↓
    事件处理完成

事件注册流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``app_event_register_handler()`` 详细流程：

::

    1. 分配事件处理器结构
        - os_malloc(sizeof(app_event_handler_t))
        ↓
    2. 设置处理器信息
        - event_type: 事件类型
        - callback: 回调函数
        - user_data: 用户数据
        ↓
    3. 添加到处理器链表
        - 加锁保护
        - 插入到链表头部
        - 解锁
        ↓
    注册完成

重要接口
---------------------------------

初始化接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 初始化应用事件系统
     * 
     * 创建事件处理线程和消息队列，启动事件处理循环
     * 
     * @return void
     */
    void app_event_init(void);

事件发送接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 发送应用事件消息
     * 
     * @param event 事件类型 (app_evt_type_t)
     * @param param 事件参数
     * @return bk_err_t 
     *         - BK_OK: 成功
     *         - BK_FAIL: 失败
     */
    bk_err_t app_event_send_msg(uint32_t event, uint32_t param);

事件注册接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 注册事件处理器
     * 
     * @param event_type 事件类型
     * @param callback 回调函数指针
     * @param user_data 用户数据指针
     * @return int 
     *         - BK_OK: 成功
     *         - BK_ERR_NO_MEM: 内存分配失败
     */
    int app_event_register_handler(app_evt_type_t event_type, 
                                   app_event_callback_t callback,
                                   void *user_data);

事件类型定义
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    typedef enum {
        APP_EVT_ASR_WAKEUP = 0,              // ASR 唤醒事件
        APP_EVT_ASR_STANDBY,                 // ASR 待机事件
        APP_EVT_IR_MODE_SWITCH,               // 图像识别模式切换
        APP_EVT_NETWORK_PROVISIONING,         // 网络配网事件
        APP_EVT_NETWORK_PROVISIONING_SUCCESS, // 网络配网成功
        APP_EVT_NETWORK_PROVISIONING_FAIL,    // 网络配网失败
        APP_EVT_RECONNECT_NETWORK,            // 网络重连事件
        APP_EVT_RECONNECT_NETWORK_SUCCESS,    // 网络重连成功
        APP_EVT_RECONNECT_NETWORK_FAIL,       // 网络重连失败
        APP_EVT_RTC_REJOIN_SUCCESS,           // RTC 重连成功
        APP_EVT_RTC_CONNECTION_LOST,          // RTC 连接丢失
        APP_EVT_AGENT_JOINED,                  // Agent 加入
        APP_EVT_AGENT_OFFLINE,                 // Agent 离线
        APP_EVT_AGENT_START_FAIL,              // Agent 启动失败
        APP_EVT_AGENT_DEVICE_REMOVE,           // Agent 设备移除
        APP_EVT_CLOSE_BLUETOOTH,               // 关闭蓝牙
        APP_EVT_LOW_VOLTAGE,                   // 低电压
        APP_EVT_CHARGING,                      // 充电中
        APP_EVT_SHUTDOWN_LOW_BATTERY,          // 低电量关机
        APP_EVT_OTA_START,                     // OTA 开始
        APP_EVT_OTA_SUCCESS,                   // OTA 成功
        APP_EVT_OTA_FAIL,                      // OTA 失败
        APP_EVT_SYNC_FLASH,                    // 同步 Flash
        APP_EVT_POWER_ON,                      // 上电事件
    } app_evt_type_t;

回调函数类型
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 事件回调函数类型
     * 
     * @param msg 事件消息指针
     * @param user_data 用户数据指针
     */
    typedef void (*app_event_callback_t)(app_evt_msg_t *msg, void *user_data);

主要宏定义
---------------------------------

配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 启用 App Event 模块
    CONFIG_APP_EVT=y

使用示例
---------------------------------

基本使用
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #include "app_event.h"

    // 事件回调函数
    void my_event_handler(app_evt_msg_t *msg, void *user_data)
    {
        switch (msg->event) {
            case APP_EVT_ASR_WAKEUP:
                // 处理唤醒事件
                break;
            case APP_EVT_NETWORK_PROVISIONING_SUCCESS:
                // 处理配网成功事件
                break;
            default:
                break;
        }
    }

    // 初始化
    void app_init(void)
    {
        // 初始化事件系统
        app_event_init();
        
        // 注册事件处理器
        app_event_register_handler(APP_EVT_ASR_WAKEUP, 
                                   my_event_handler, 
                                   NULL);
    }

    // 发送事件
    void some_function(void)
    {
        // 发送唤醒事件
        app_event_send_msg(APP_EVT_ASR_WAKEUP, 0);
    }

注意事项
---------------------------------

1. **回调函数**: 回调函数应避免长时间阻塞，否则影响事件响应速度

2. **初始化时机**: 应在系统初始化早期调用 ``app_event_init()``

3. **事件参数**: 事件参数类型为 uint32_t，传递复杂数据时需使用指针

