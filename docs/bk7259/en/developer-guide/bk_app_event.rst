App Event Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

App Event is an application-layer event system module that provides a unified event distribution and processing mechanism. This module encapsulates event queues, event processing threads, and event callback registration, providing a decoupled event communication interface for upper-layer applications. Through the App Event module, different modules can communicate asynchronously, achieving a loosely coupled architecture design.

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Event-driven Architecture**: Asynchronous event processing mechanism based on message queues
- **Multiple Event Type Support**: Supports various event types such as ASR, network, RTC, Agent, OTA, etc.
- **Event Callback Registration**: Supports registering callback functions for specific event types
- **Thread Safety**: Uses mutex locks to protect event processing flow
- **Decoupled Design**: Modules communicate through events, reducing coupling
- **Flexible Extension**: Easy to add new event types and processing logic

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The App Event module architecture is as follows:

::

    Application Layer (Application)
            ↓
    App Event API (app_event.h)
            ↓
    Event Queue (Message Queue)
            ↓
    Event Processing Thread (Event Thread)
            ↓
    Event Callback Dispatch (Callback Dispatch)
            ↓
    Module Event Handlers (Module Handlers)

Workflow
----------------

Initialization Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The App Event initialization flow is as follows:

::

    app_event_init()
        ↓
    1. Create Event Processing Thread
        - Create message queue
        - Create mutex lock
        - Start event processing thread
        ↓
    2. Initialize Event Handler Linked List
        - s_event_handlers = NULL
        ↓
    Initialization Complete

Event Sending Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``app_event_send_msg()``:

::

    1. Parameter Validation
        - Check event type validity
        ↓
    2. Create Event Message
        - Allocate app_evt_msg_t structure
        - Set event type and parameters
        ↓
    3. Send to Message Queue
        - rtos_push_to_queue()
        ↓
    4. Event Processing Thread Receives
        - Retrieve message from queue
        ↓
    5. Event Dispatch
        - Traverse event handler linked list
        - Call matching callback functions
        ↓
    6. Execute Module-specific Processing
        - Execute corresponding logic based on event type
        ↓
    Event Processing Complete

Event Registration Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``app_event_register_handler()``:

::

    1. Allocate Event Handler Structure
        - os_malloc(sizeof(app_event_handler_t))
        ↓
    2. Set Handler Information
        - event_type: Event type
        - callback: Callback function
        - user_data: User data
        ↓
    3. Add to Handler Linked List
        - Lock protection
        - Insert at linked list head
        - Unlock
        ↓
    Registration Complete

Important Interfaces
--------------------

Initialization Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Initialize application event system
     * 
     * Creates event processing thread and message queue, starts event processing loop
     * 
     * @return void
     */
    void app_event_init(void);

Event Sending Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Send application event message
     * 
     * @param event Event type (app_evt_type_t)
     * @param param Event parameter
     * @return bk_err_t 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    bk_err_t app_event_send_msg(uint32_t event, uint32_t param);

Event Registration Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Register event handler
     * 
     * @param event_type Event type
     * @param callback Callback function pointer
     * @param user_data User data pointer
     * @return int 
     *         - BK_OK: Success
     *         - BK_ERR_NO_MEM: Memory allocation failure
     */
    int app_event_register_handler(app_evt_type_t event_type, 
                                   app_event_callback_t callback,
                                   void *user_data);

Event Type Definition
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    typedef enum {
        APP_EVT_ASR_WAKEUP = 0,              // ASR wake-up event
        APP_EVT_ASR_STANDBY,                 // ASR standby event
        APP_EVT_IR_MODE_SWITCH,               // Image recognition mode switch
        APP_EVT_NETWORK_PROVISIONING,         // Network provisioning event
        APP_EVT_NETWORK_PROVISIONING_SUCCESS, // Network provisioning success
        APP_EVT_NETWORK_PROVISIONING_FAIL,    // Network provisioning failure
        APP_EVT_RECONNECT_NETWORK,            // Network reconnection event
        APP_EVT_RECONNECT_NETWORK_SUCCESS,    // Network reconnection success
        APP_EVT_RECONNECT_NETWORK_FAIL,       // Network reconnection failure
        APP_EVT_RTC_REJOIN_SUCCESS,           // RTC rejoin success
        APP_EVT_RTC_CONNECTION_LOST,          // RTC connection lost
        APP_EVT_AGENT_JOINED,                  // Agent joined
        APP_EVT_AGENT_OFFLINE,                 // Agent offline
        APP_EVT_AGENT_START_FAIL,              // Agent start failure
        APP_EVT_AGENT_DEVICE_REMOVE,           // Agent device remove
        APP_EVT_CLOSE_BLUETOOTH,               // Close Bluetooth
        APP_EVT_LOW_VOLTAGE,                   // Low voltage
        APP_EVT_CHARGING,                      // Charging
        APP_EVT_SHUTDOWN_LOW_BATTERY,          // Low battery shutdown
        APP_EVT_OTA_START,                     // OTA start
        APP_EVT_OTA_SUCCESS,                   // OTA success
        APP_EVT_OTA_FAIL,                      // OTA failure
        APP_EVT_SYNC_FLASH,                    // Sync Flash
        APP_EVT_POWER_ON,                      // Power on event
    } app_evt_type_t;

Callback Function Type
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Event callback function type
     * 
     * @param msg Event message pointer
     * @param user_data User data pointer
     */
    typedef void (*app_event_callback_t)(app_evt_msg_t *msg, void *user_data);

Main Macro Definitions
-----------------------

Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Enable App Event module
    CONFIG_APP_EVT=y

Usage Examples
----------------

Basic Usage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    #include "app_event.h"

    // Event callback function
    void my_event_handler(app_evt_msg_t *msg, void *user_data)
    {
        switch (msg->event) {
            case APP_EVT_ASR_WAKEUP:
                // Handle wake-up event
                break;
            case APP_EVT_NETWORK_PROVISIONING_SUCCESS:
                // Handle network provisioning success event
                break;
            default:
                break;
        }
    }

    // Initialization
    void app_init(void)
    {
        // Initialize event system
        app_event_init();
        
        // Register event handler
        app_event_register_handler(APP_EVT_ASR_WAKEUP, 
                                   my_event_handler, 
                                   NULL);
    }

    // Send event
    void some_function(void)
    {
        // Send wake-up event
        app_event_send_msg(APP_EVT_ASR_WAKEUP, 0);
    }

Notes
----------------

1. **Callback Function**: Callback functions should avoid long blocking, otherwise it will affect event response speed

2. **Initialization Timing**: Should call ``app_event_init()`` early in system initialization

3. **Event Parameters**: Event parameter type is uint32_t, use pointers when passing complex data

