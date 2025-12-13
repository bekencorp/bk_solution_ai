Motor 模块
=====================================

:link_to_translation:`en:[English]`

模块简介
---------------------------------

Motor 是一个马达控制模块，提供了基于 PWM 的马达振动控制功能。该模块封装了 PWM 初始化、LDO 电源控制和占空比调节等功能，为上层应用提供简单的马达控制接口。支持通过 PWM 占空比控制马达振动强度，适用于触觉反馈场景。

核心特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **PWM 控制**: 基于 PWM 信号控制马达振动
- **LDO 电源管理**: 自动控制马达 LDO 电源开关
- **占空比调节**: 支持通过占空比调节振动强度
- **简单接口**: 提供简单的启动/停止接口
- **资源管理**: 自动管理 PWM 和 LDO 资源

模块架构
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Motor 模块架构如下：

::

    应用层 (Application)
            ↓
    Motor API (motor.h)
            ↓
    PWM 驱动 (bk_pwm)
            ↓
    LDO 控制 (bk_pm_module_vote_ctrl_external_ldo)
            ↓
    马达硬件

工作流程
---------------------------------

马达启动流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``motor_open()`` 详细流程：

::

    1. 初始化 PWM
        - pwm_init(channel, duty_cycle)
        - 配置 PWM 参数
            * 时钟源: 26MHz
            * 频率: 1000Hz
            * 占空比: PWM_DUTY_CYCLE (0.3 = 30%)
        ↓
    2. 启动 PWM
        - bk_pwm_start(channel)
        ↓
    3. 使能 LDO 电源
        - motor_ldo_power_enable(1)
        - 设置 GPIO 为高电平
        ↓
    马达开始振动

PWM 初始化流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``pwm_init()`` 详细流程：

::

    1. 配置 PWM 参数
        - psc: 0 (预分频器)
        - period_cycle: s_period (周期)
        - duty_cycle: period (占空比周期)
        ↓
    2. 初始化 PWM
        - bk_pwm_init(channel, &init_config)
        ↓
    3. 启动 PWM
        - bk_pwm_start(channel)
        ↓
    PWM 运行中

马达停止流程
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

``motor_close()`` 详细流程：

::

    1. 停止 PWM
        - bk_pwm_stop(channel)
        ↓
    2. 关闭 LDO 电源
        - motor_ldo_power_enable(0)
        - 设置 GPIO 为低电平
        ↓
    马达停止振动

重要接口
---------------------------------

马达控制接口
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    /**
     * @brief 启动马达
     * 
     * 初始化 PWM 通道，输出 PWM 波形，使能 LDO 电源
     * 
     * @param channel PWM 通道号
     */
    void motor_open(pwm_chan_t channel);

    /**
     * @brief 停止马达
     * 
     * 停止 PWM 输出，关闭 LDO 电源
     * 
     * @param channel PWM 通道号
     */
    void motor_close(pwm_chan_t channel);

PWM 通道定义
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 默认马达 PWM 通道
    #define PWM_MOTOR_CH_3 PWM_ID_3

主要宏定义
---------------------------------

配置宏
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // 启用 Motor 模块
    CONFIG_MOTOR=y

    // 依赖配置
    CONFIG_PWM=y  // PWM 驱动支持

PWM 参数配置
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    // PWM 时钟源（Hz）
    #define PWM_CLOCK_SOURCE 26000000

    // PWM 频率（Hz）
    #define PWM_FREQ 1000

    // PWM 占空比（0.0 - 1.0）
    #define PWM_DUTY_CYCLE 0.3  // 30% 占空比


使用示例
---------------------------------

基本使用
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    #include "motor.h"

    void example_usage(void)
    {
        // 启动马达（使用默认通道）
        motor_open(PWM_MOTOR_CH_3);
        
        // 马达振动中...
        
        // 停止马达
        motor_close(PWM_MOTOR_CH_3);
    }

振动反馈示例
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code:: c

    void vibration_feedback(void)
    {
        // 短振动（100ms）
        motor_open(PWM_MOTOR_CH_3);
        rtos_delay_milliseconds(100);
        motor_close(PWM_MOTOR_CH_3);
        
        // 长振动（500ms）
        motor_open(PWM_MOTOR_CH_3);
        rtos_delay_milliseconds(500);
        motor_close(PWM_MOTOR_CH_3);
    }

注意事项
---------------------------------

1. **功耗考虑**: 马达工作时功耗较大，建议短时间使用

2. **硬件差异**: 不同开发板的 LDO 控制 GPIO 可能不同，需根据硬件设计配置

