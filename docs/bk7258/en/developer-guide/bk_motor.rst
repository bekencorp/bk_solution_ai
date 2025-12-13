Motor Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

Motor is a motor control module that provides PWM-based motor vibration control functionality. This module encapsulates PWM initialization, LDO power control, and duty cycle adjustment, providing a simple motor control interface for upper-layer applications. Supports controlling motor vibration strength through PWM duty cycle, suitable for haptic feedback scenarios.

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **PWM Control**: Controls motor vibration based on PWM signals
- **LDO Power Management**: Automatically controls motor LDO power switch
- **Duty Cycle Adjustment**: Supports adjusting vibration strength through duty cycle
- **Simple Interface**: Provides simple start/stop interface
- **Resource Management**: Automatically manages PWM and LDO resources

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Motor module architecture is as follows:

::

    Application Layer (Application)
            ↓
    Motor API (motor.h)
            ↓
    PWM Driver (bk_pwm)
            ↓
    LDO Control (bk_pm_module_vote_ctrl_external_ldo)
            ↓
    Motor Hardware

Workflow
----------------

Motor Start Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``motor_open()``:

::

    1. Initialize PWM
        - pwm_init(channel, duty_cycle)
        - Configure PWM parameters
            * Clock source: 26MHz
            * Frequency: 1000Hz
            * Duty cycle: PWM_DUTY_CYCLE (0.3 = 30%)
        ↓
    2. Start PWM
        - bk_pwm_start(channel)
        ↓
    3. Enable LDO Power
        - motor_ldo_power_enable(1)
        - Set GPIO to high level
        ↓
    Motor Starts Vibrating

PWM Initialization Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``pwm_init()``:

::

    1. Configure PWM Parameters
        - psc: 0 (prescaler)
        - period_cycle: s_period (period)
        - duty_cycle: period (duty cycle period)
        ↓
    2. Initialize PWM
        - bk_pwm_init(channel, &init_config)
        ↓
    3. Start PWM
        - bk_pwm_start(channel)
        ↓
    PWM Running

Motor Stop Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``motor_close()``:

::

    1. Stop PWM
        - bk_pwm_stop(channel)
        ↓
    2. Disable LDO Power
        - motor_ldo_power_enable(0)
        - Set GPIO to low level
        ↓
    Motor Stops Vibrating

Important Interfaces
--------------------

Motor Control Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Start motor
     * 
     * Initializes PWM channel, outputs PWM waveform, enables LDO power
     * 
     * @param channel PWM channel number
     */
    void motor_open(pwm_chan_t channel);

    /**
     * @brief Stop motor
     * 
     * Stops PWM output, disables LDO power
     * 
     * @param channel PWM channel number
     */
    void motor_close(pwm_chan_t channel);

PWM Channel Definition
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Default motor PWM channel
    #define PWM_MOTOR_CH_3 PWM_ID_3

Main Macro Definitions
-----------------------

Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Enable Motor module
    CONFIG_MOTOR=y

    // Dependencies
    CONFIG_PWM=y  // PWM driver support

PWM Parameter Configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // PWM clock source (Hz)
    #define PWM_CLOCK_SOURCE 26000000

    // PWM frequency (Hz)
    #define PWM_FREQ 1000

    // PWM duty cycle (0.0 - 1.0)
    #define PWM_DUTY_CYCLE 0.3  // 30% duty cycle

Usage Examples
----------------

Basic Usage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    #include "motor.h"

    void example_usage(void)
    {
        // Start motor (using default channel)
        motor_open(PWM_MOTOR_CH_3);
        
        // Motor vibrating...
        
        // Stop motor
        motor_close(PWM_MOTOR_CH_3);
    }

Vibration Feedback Example
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    void vibration_feedback(void)
    {
        // Short vibration (100ms)
        motor_open(PWM_MOTOR_CH_3);
        rtos_delay_milliseconds(100);
        motor_close(PWM_MOTOR_CH_3);
        
        // Long vibration (500ms)
        motor_open(PWM_MOTOR_CH_3);
        rtos_delay_milliseconds(500);
        motor_close(PWM_MOTOR_CH_3);
    }

Notes
----------------

1. **Power Consumption**: Motor consumes significant power when working, recommend short-time use

2. **Hardware Differences**: LDO control GPIO may differ on different development boards, need to configure according to hardware design

