Key App Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

Key App is a key application module that provides GPIO key configuration, event handling, and callback mechanisms. This module encapsulates key scanning, event recognition (single click, double click, long press), and event distribution, providing a unified key processing interface for upper-layer applications. Supports multiple key configurations, each key can be independently configured with trigger events.

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Multiple Key Support**: Supports multiple GPIO key configurations
- **Event Recognition**: Supports single click, double click, and long press event recognition
- **Flexible Configuration**: Supports custom key GPIO and event mapping
- **Event Callback**: Supports key event callback handling
- **Wake-up Source Registration**: Supports registering keys as wake-up sources
- **Volume Control**: Built-in volume increase/decrease control functions

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Key App module architecture is as follows:

::

    Application Layer (Application)
            ↓
    Key App API (key_app_service.h)
            ↓
    Key Configuration (key_app_config.h)
            ↓
    Key Driver (multi_button)
            ↓
    GPIO Driver (bk_gpio)
            ↓
    Hardware Keys

Workflow
----------------

Initialization Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``bk_key_service_init()``:

::

    1. Load Key Configuration
        - Read configuration from KEY_DEFAULT_CONFIG_TABLE
        ↓
    2. Initialize Key Driver
        - Configure GPIO pins
        - Set trigger level
        - Register key event callbacks
        ↓
    3. Start Key Scanning Task
        - Create key processing thread
        ↓
    Initialization Complete

Key Event Processing Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Key event processing flow:

::

    1. GPIO Interrupt Triggered
        - Key press/release triggers interrupt
        ↓
    2. Key Driver Scanning
        - multi_button driver scans key status
        ↓
    3. Event Recognition
        - Recognize single click, double click, long press events
        ↓
    4. Event Distribution
        - Look up corresponding event from configuration table
        ↓
    5. Call Event Handler Function
        - handle_system_event(event)
        ↓
    6. Execute Corresponding Operation
        - Execute operation based on event type
        ↓
    Event Processing Complete

Event Handler Mapping
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

System event handler mapping:

::

    VOLUME_UP          → audio_engine_volume_increase()
    VOLUME_DOWN        → audio_engine_volume_decrease()
    SHUT_DOWN          → power_off() (long press >= 9 seconds)
    POWER_ON           → power_on()
    CONFIG_NETWORK     → bk_sconf_prepare_for_smart_config()
    IR_MODE_SWITCH     → app_event_send_msg(APP_EVT_IR_MODE_SWITCH)
    FACTORY_RESET      → bk_factory_reset() + bk_reboot()
    AUDIO_BUF_APPEND   → app_event_send_msg(APP_EVT_ASR_WAKEUP)

Important Interfaces
--------------------

Initialization Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Initialize key service
     * 
     * Loads key configuration, initializes key driver, starts key scanning task
     */
    void bk_key_service_init(void);

    /**
     * @brief Register key as wake-up source
     * 
     * Registers key as system wake-up source, supports wake-up from deep sleep
     */
    void bk_key_register_wakeup_source(void);

    /**
     * @brief Initialize volume
     * 
     * Initializes volume control function
     */
    void volume_init(void);

Key Configuration Structure
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    typedef struct {
        uint8_t gpio_id;           // GPIO pin number
        uint8_t active_level;      // Trigger level (LOW_LEVEL_TRIGGER/HIGH_LEVEL_TRIGGER)
        uint8_t short_event;       // Single click event
        uint8_t double_event;      // Double click event
        uint8_t long_event;        // Long press event
    } KeyConfig_t;

Default Configuration Table
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

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

System Event Types
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    enum {
        VOLUME_UP,          // Volume increase
        VOLUME_DOWN,        // Volume decrease
        SHUT_DOWN,          // Shutdown (long press >= 9 seconds)
        POWER_ON,           // Power on
        AI_AGENT_CONFIG,    // AI Agent configuration
        CONFIG_NETWORK,     // Network provisioning
        IR_MODE_SWITCH,     // Image recognition mode switch
        FACTORY_RESET,      // Factory reset
        AUDIO_BUF_APPEND,   // Audio buffer append
    };

Main Macro Definitions
-----------------------

Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Enable key function
    CONFIG_BUTTON=y

GPIO Definitions
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Default key GPIO (can be modified in key_app_config.h)
    #define KEY_GPIO_13 13
    #define KEY_GPIO_12 12
    #define KEY_GPIO_8  8

Long Press Time Configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Long press time is defined by the ``LONG_TICKS`` macro in ``multi_button.h``:

.. code-block:: C

    #define LONG_TICKS  (3000 / TICKS_INTERVAL)  // Long press time (milliseconds)

Usage Examples
----------------

Basic Usage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    #include "key_app_service.h"

    void app_init(void)
    {
        // Initialize key service
        bk_key_service_init();
        
        // Register wake-up source (optional)
        bk_key_register_wakeup_source();
        
        // Initialize volume (optional)
        volume_init();
    }

Custom Key Configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Define custom configuration in key_app_config.h
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
        // Add more key configurations...
    }
    #endif

Notes
----------------

1. **GPIO Conflict**: Ensure GPIO pins are only used for keys to avoid function conflicts

2. **Event Handling**: Event handler functions should avoid long blocking, otherwise it will affect key response speed

3. **Configuration Modification**: Modifying key configuration requires recompilation, runtime modification is not supported

4. **Hardware Differences**: Key GPIO may differ on different development boards, need to modify configuration according to hardware design

