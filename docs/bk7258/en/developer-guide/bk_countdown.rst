Countdown Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

Countdown is a countdown management module that provides a multi-ticket countdown mechanism. This module supports multiple countdown sources (provisioning, network error, standby, OTA, etc.), automatically selects active countdown based on priority, and automatically enters deep sleep mode when the countdown expires. This module encapsulates timer management and countdown arbitration logic, providing a unified countdown management interface for upper-layer applications.

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Multi-ticket Support**: Supports multiple countdown sources existing simultaneously
- **Priority Management**: Automatically selects active countdown based on priority
- **Auto Sleep**: Automatically enters deep sleep when countdown expires
- **OTA Pause**: Automatically pauses countdown during OTA events
- **Flexible Configuration**: Supports custom countdown duration for each ticket
- **Thread Safety**: Uses timer mechanism, thread-safe

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Countdown module architecture is as follows:

::

    Application Layer (Application)
            ↓
    Countdown API (countdown.h, countdown_app.h)
            ↓
    Countdown Arbitration (update_countdown)
            ↓
    Timer Management (start_countdown/stop_countdown)
            ↓
    RTOS Timer (oneshot timer)
            ↓
    Deep Sleep (Deep Sleep)

Workflow
----------------

Countdown Update Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``update_countdown()``:

::

    1. Check OTA Pause Ticket
        - If OTA ticket exists, stop countdown and return
        ↓
    2. Traverse Tickets (by priority)
        - COUNTDOWN_TICKET_PROVISIONING (highest priority)
        - COUNTDOWN_TICKET_NETWORK_ERROR
        - COUNTDOWN_TICKET_STANDBY (lowest priority)
        ↓
    3. Select First Active Ticket
        - Check s_active_tickets bitmask
        ↓
    4. Apply Countdown
        - If ticket changed, stop old timer
        - Start new timer with corresponding duration
        ↓
    Countdown Update Complete

Countdown Start Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``start_countdown()``:

::

    1. Check Timer Status
        - If timer not initialized, create new timer
        - If timer exists, reload
        ↓
    2. Initialize Timer
        - rtos_init_oneshot_timer()
        - Set callback function CountdownCallback
        ↓
    3. Start Timer
        - rtos_start_oneshot_timer()
        - or rtos_oneshot_reload_timer_ex()
        ↓
    Timer Running

Countdown Stop Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``stop_countdown()``:

::

    1. Check Timer Status
        - rtos_is_oneshot_timer_init()
        ↓
    2. Stop Timer
        - If running, call rtos_stop_oneshot_timer()
        ↓
    3. Destroy Timer
        - rtos_deinit_oneshot_timer()
        ↓
    Timer Stopped

Countdown Expiry Handling
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Callback flow of ``CountdownCallback()``:

::

    1. Timer Expiry Triggers Callback
        ↓
    2. Log Message
        - "Standy time is up, start enter deepsleep"
        ↓
    3. Enter Deep Sleep
        - bk_reboot_ex(RESET_SOURCE_FORCE_DEEPSLEEP)
        ↓
    System Enters Deep Sleep

Important Interfaces
--------------------

Countdown Control Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Start countdown timer
     * 
     * Creates and starts a oneshot timer. When the countdown expires, the system
     * will automatically enter deep sleep mode. If the timer already exists, it
     * will be reloaded with the new timeout value.
     * 
     * @param time_ms Countdown duration in milliseconds
     * 
     * @note When the countdown expires, it triggers a forced deep sleep 
     *       (RESET_SOURCE_FORCE_DEEPSLEEP)
     */
    void start_countdown(uint32_t time_ms);

    /**
     * @brief Stop countdown timer
     * 
     * Stops and deletes the countdown timer, releasing associated resources.
     * This function is safe to call even if the timer is not initialized or 
     * not running.
     * 
     * @note After calling this function, the timer is completely destroyed. 
     *       You need to call start_countdown() again to recreate it.
     */
    void stop_countdown(void);

Countdown Update Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Update countdown status
     * 
     * Updates countdown based on active tickets. This function selects the active
     * countdown source based on priority and starts or updates the corresponding
     * countdown timer.
     * 
     * @param s_active_tickets Bitmask of active tickets
     * 
     * @note OTA ticket has special logic and will pause all countdowns
     */
    void update_countdown(uint32_t s_active_tickets);

Ticket Type Definition
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    typedef enum {
        COUNTDOWN_TICKET_PROVISIONING,   // Provisioning countdown (5 minutes), highest priority
        COUNTDOWN_TICKET_NETWORK_ERROR,  // Network error countdown (5 minutes), medium priority
        COUNTDOWN_TICKET_STANDBY,        // Standby countdown (3 minutes), lowest priority
        COUNTDOWN_TICKET_OTA,            // OTA event, special logic, not involved in comparison
        COUNTDOWN_TICKET_MAX
    } countdown_ticket_t;

Main Macro Definitions
-----------------------

Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Enable Countdown module
    CONFIG_COUNTDOWN=y

Countdown Duration Configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Countdown duration is configured in the ``s_ticket_durations[]`` array in ``countdown_app.c``:

.. code-block:: C

    static const uint32_t s_ticket_durations[COUNTDOWN_TICKET_MAX] = {
        [COUNTDOWN_TICKET_PROVISIONING] = 5 * 60 * 1000,  // 5 minutes
        [COUNTDOWN_TICKET_NETWORK_ERROR] = 5 * 60 * 1000,  // 5 minutes
        [COUNTDOWN_TICKET_STANDBY] = 3 * 60 * 1000,        // 3 minutes
        [COUNTDOWN_TICKET_OTA] = COUNTDOWN_INFINITE,        // Infinite (pause countdown)
    };

Special Values
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Infinite countdown (for pause)
    #define COUNTDOWN_INFINITE 0xFFFFFFFF

Usage Examples
----------------

Basic Usage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    #include "countdown_app.h"

    void example_usage(void)
    {
        uint32_t active_tickets = 0;
        
        // Set provisioning countdown ticket
        active_tickets |= (1 << COUNTDOWN_TICKET_PROVISIONING);
        
        // Update countdown (will start 5-minute countdown)
        update_countdown(active_tickets);
        
        // Clear provisioning ticket, set standby ticket
        active_tickets &= ~(1 << COUNTDOWN_TICKET_PROVISIONING);
        active_tickets |= (1 << COUNTDOWN_TICKET_STANDBY);
        
        // Update countdown (will start 3-minute countdown)
        update_countdown(active_tickets);
        
        // OTA event, pause countdown
        active_tickets |= (1 << COUNTDOWN_TICKET_OTA);
        update_countdown(active_tickets);  // Countdown stops
        
        // Clear all tickets, stop countdown
        active_tickets = 0;
        update_countdown(active_tickets);  // Countdown stops
    }

Priority Example
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    void priority_example(void)
    {
        uint32_t active_tickets = 0;
        
        // Set multiple tickets simultaneously
        active_tickets |= (1 << COUNTDOWN_TICKET_PROVISIONING);  // 5 minutes
        active_tickets |= (1 << COUNTDOWN_TICKET_STANDBY);        // 3 minutes
        
        // Update countdown
        // Since provisioning ticket has higher priority, will use 5-minute countdown
        update_countdown(active_tickets);
        
        // After clearing provisioning ticket, standby ticket takes effect, countdown becomes 3 minutes
        active_tickets &= ~(1 << COUNTDOWN_TICKET_PROVISIONING);
        update_countdown(active_tickets);
    }

Notes
----------------

1. **Priority Order**: Ticket priority is determined by enumeration order, PROVISIONING > NETWORK_ERROR > STANDBY

2. **OTA Special Handling**: OTA ticket will pause all countdowns when present

3. **Deep Sleep**: When countdown expires, system will force enter deep sleep, cannot be cancelled

4. **Duration Configuration**: Modify countdown duration by modifying the ``s_ticket_durations[]`` array

