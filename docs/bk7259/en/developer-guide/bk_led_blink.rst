LED Blink Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

LED Blink is an LED effect control module that provides rich LED state control and blinking effects. This module supports independent control of multiple LEDs, including always on, always off, fast blink, slow blink, alternate blink, and other states, and supports error priority management and blink duration control. This module encapsulates GPIO control, timer management, and state machine logic, providing a simple LED control interface for upper-layer applications.

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Multiple LED Support**: Supports up to 4 independent LED controls
- **Multiple States**: Supports always on, always off, fast blink, slow blink, alternate blink
- **Error Priority**: Supports error type priority management (critical error > warning)
- **Duration Control**: Supports setting blink duration
- **State Voting**: Supports multiple state source voting, automatically selects highest priority state
- **Thread Safety**: Uses mutex locks to protect LED operations

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The LED Blink module architecture is as follows:

::

    Application Layer (Application)
            ↓
    LED App API (led_app.h)
            ↓
    LED Blink API (led_blink.h)
            ↓
    LED Control Block (LedControlBlock)
            ↓
    GPIO Control (bk_gpio)
            ↓
    LED Hardware

Workflow
----------------

Initialization Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``led_driver_init()``:

::

    1. Initialize Mutex
        - rtos_init_mutex(&led_mutex)
        ↓
    2. Clear LED Control Block Array
        - memset(led_pool, 0, sizeof(led_pool))
        ↓
    3. Register Red LED
        - led_register(RED_LED)
        ↓
    4. Register Green LED
        - led_register(GREEN_LED)
        ↓
    Initialization Complete

LED State Setting Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``led_app_set()``:

::

    1. Parse Operation Type
        - Determine LED and state based on led_operate_t
        ↓
    2. Get LED Handle
        - Get corresponding LED handle based on operation type
        ↓
    3. Set LED State
        - led_set_state(led_handle, new_state, time)
        ↓
    4. Update Control Block
        - Update LedControlBlock structure
        - Set blink interval and duration
        ↓
    5. Start Timer
        - If blinking state, start timer
        ↓
    State Setting Complete

LED Blink Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Timer callback flow:

::

    1. Timer Expiry Triggers Callback
        - timer_callback(led_handle)
        ↓
    2. Check Duration
        - If duration set and expired, stop blinking
        ↓
    3. Check Error State
        - If error exists, blink by error priority
        ↓
    4. Toggle LED State
        - Flip led_status
        - Update GPIO output
        ↓
    5. Continue Timer
        - If not expired, continue timer
        ↓
    Loop steps 1-5

LED State Voting Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``led_blink()``:

::

    1. Process Indication State (Green LED)
        - Check indicates_state bitmask
        - Select state by priority
        ↓
    2. Process Warning State (Red LED)
        - Check warning_state bitmask
        - Select state by priority
        ↓
    3. Apply State
        - Call led_app_set() to set LED state
        ↓
    State Update Complete

Important Interfaces
--------------------

Initialization Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Initialize LED driver
     * 
     * Initializes mutex, registers red and green LEDs
     */
    void led_driver_init(void);

LED Control Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Set LED state
     * 
     * @param oper LED operation type
     * @param time Duration (milliseconds), 0xFFFFFFFF means permanent
     */
    void led_app_set(led_operate_t oper, uint32_t time);

    /**
     * @brief LED state voting
     * 
     * Automatically selects LED state based on warning state and indication state
     * 
     * @param warning_state Warning state bitmask pointer
     * @param indicates_state Indication state bitmask
     */
    void led_blink(uint32_t *warning_state, uint32_t indicates_state);

LED State Types
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    typedef enum {
        LED_OFF = 0,        // Off
        LED_ON,             // Always on
        LED_FAST_BLINK,      // Fast blink
        LED_SLOW_BLINK,      // Slow blink
        LED_ALTERNATE       // Alternate blink
    } LedState;

LED Operation Types
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    typedef enum {
        // Green LED
        LED_OFF_GREEN,
        LED_ON_GREEN,
        LED_FAST_BLINK_GREEN,
        LED_SLOW_BLINK_GREEN,
        
        // Red LED
        LED_OFF_RED,
        LED_ON_RED,
        LED_FAST_BLINK_RED,
        LED_SLOW_BLINK_RED,
        
        // Alternate blink
        LED_REG_GREEN_ALTERNATE,
        LED_REG_GREEN_ALTERNATE_OFF,
        
        // Error states
        LED_ERROR_CRITICAL,
        LED_ERROR_WARNING,
        LED_ERROR_CRITICAL_CLOSE,
        LED_ERROR_WARNING_CLOSE,
    } led_operate_t;

Warning State Definitions
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    enum {
        WARNING_PROVIOSION_FAIL,      // Provisioning failure (fast blink red)
        WARNING_WIFI_FAIL,            // Wi-Fi failure (fast blink red)
        WARNING_RTC_CONNECT_LOST,     // RTC connection lost (fast blink red)
        WARNING_AGENT_OFFLINE,        // Agent offline (fast blink red)
        WARNING_AGENT_AGENT_START_FAIL, // Agent start failure (fast blink red)
        WARNING_LOW_BATTERY,          // Low battery (slow blink red)
    };

Indication State Definitions
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    enum {
        INDICATES_WIFI_RECONNECT,     // Wi-Fi reconnection (fast blink green)
        INDICATES_PROVISIONING,       // Provisioning (red-green alternate)
        INDICATES_POWER_ON,           // Power on (always on green)
        INDICATES_STANDBY,             // Standby (slow blink green)
        INDICATES_AGENT_CONNECT,      // Agent connection (fast blink green)
    };

Main Macro Definitions
-----------------------

Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Enable LED Blink module
    CONFIG_LED_BLINK=y

Hardware Configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // LED GPIO definitions
    #define RED_LED   40   // Red LED GPIO
    #define GREEN_LED 41   // Green LED GPIO

    // LED number limit
    #define MAX_LED_NUM 4

Blink Time Configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Fast blink interval (milliseconds)
    #define LED_FAST_BLINK_TIME 200

    // Slow blink interval (milliseconds)
    #define LED_SLOW_BLINK_TIME 1000

    // Permanent blink
    #define LED_LAST_FOREVER 0xFFFFFFFF

Usage Examples
----------------

Basic Usage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    #include "led_app.h"

    void example_usage(void)
    {
        // Initialize LED
        led_driver_init();
        
        // Set green LED fast blink
        led_app_set(LED_FAST_BLINK_GREEN, LED_LAST_FOREVER);
        
        // Set red LED slow blink for 5 seconds
        led_app_set(LED_SLOW_BLINK_RED, 5000);
        
        // Turn off green LED
        led_app_set(LED_OFF_GREEN, 0);
    }

State Voting Usage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    void state_voting_example(void)
    {
        uint32_t warning_state = 0;
        uint32_t indicates_state = 0;
        
        // Set warning state
        warning_state |= (1 << WARNING_WIFI_FAIL);
        
        // Set indication state
        indicates_state |= (1 << INDICATES_STANDBY);
        
        // Automatically select LED state
        led_blink(&warning_state, indicates_state);
        // Result: Red fast blink (warning priority higher than indication)
    }

Notes
----------------

1. **State Priority**: Warning state priority is higher than indication state, critical error priority is higher than warning

2. **Blink Interval**: Fast blink interval 200ms, slow blink interval 1000ms, can be modified in header file

3. **State Voting**: When using ``led_blink()``, state is automatically selected based on priority

