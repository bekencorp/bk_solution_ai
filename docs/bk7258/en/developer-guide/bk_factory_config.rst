Factory Config Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

Factory Config is a factory configuration management module that provides persistent storage and management functions for configuration items. This module supports read/write operations, synchronization to Flash, factory reset, and provides an SRAM cache mechanism to improve read performance. Configuration items are stored in Flash, supporting power-off preservation, while also providing SRAM cache to accelerate frequently accessed configuration items.

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Persistent Storage**: Configuration items stored in Flash, supporting power-off preservation
- **SRAM Cache**: Supports SRAM cache for configuration items to improve read performance
- **Auto Sync**: Supports automatic synchronization of configuration items to Flash
- **Factory Reset**: Supports one-key factory default configuration restore
- **Flexible Extension**: Supports user-defined configuration items
- **Thread Safety**: Uses mutex locks to protect configuration operations

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Factory Config module architecture is as follows:

::

    Application Layer (Application)
            ↓
    Factory Config API (bk_factory_config.h)
            ↓
    SRAM Cache (Factory Cache)
            ↓
    Easy Flash (Flash Storage)
            ↓
    Flash Partition

Workflow
----------------

Initialization Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``bk_factory_init()``:

::

    1. Check Initialization Flag
        - Read "sys_initialized" configuration item
        ↓
    2. First Initialization
        - If not initialized, call bk_factory_reset()
        - Write all default configurations to Flash
        ↓
    3. Already Initialized
        - If initialized, call bk_factory_cache_init()
        - Load configurations from Flash to SRAM cache
        ↓
    4. Register Reboot Callback
        - bk_reboot_callback_register(bk_reboot_sync_config)
        - Ensure configuration sync before reboot
        ↓
    5. Register CLI Commands
        - factory_read, factory_write, factory_sync, factory_reset
        ↓
    Initialization Complete

Configuration Read Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``bk_config_read()``:

::

    1. Find Cache Index
        - find_cache_index(key)
        - Find configuration item in SRAM cache
        ↓
    2. Read from Cache
        - memcpy(value, cache_ptr, value_len)
        ↓
    3. Return Read Length
        - Return valid_len
        ↓
    Read Complete

Configuration Write Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``bk_config_write()``:

::

    1. Find Cache Index
        - find_cache_index(key)
        ↓
    2. Validate Length
        - Check if value_len is less than or equal to max length
        ↓
    3. Write to Cache
        - memcpy(cache_ptr, value, value_len)
        - Update valid_len
        ↓
    4. Return Result
        - 0: Success
        - -1: Failure
        ↓
    Write Complete (only writes to cache, need to call sync to Flash)

Configuration Sync Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``bk_config_sync_flash()``:

::

    1. Check Cache Validity
        - If cache not initialized, return directly
        ↓
    2. Traverse All Configuration Items
        - Platform configuration items
        - User-registered configuration items
        ↓
    3. Check if Update Needed
        - Compare Flash value and cache value
        - is_config_update()
        ↓
    4. Write to Flash
        - If value changed, call bk_set_env_enhance()
        - Write to Easy Flash
        ↓
    5. Sync Complete
        ↓
    Sync Complete

Factory Reset Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``bk_factory_reset()``:

::

    1. Traverse All Configuration Items
        - Platform configuration items
        - User-registered configuration items
        ↓
    2. Write Default Values
        - Use default values from configuration structure
        - bk_set_env_enhance()
        ↓
    3. Initialize Cache
        - bk_factory_cache_init()
        - Load from Flash to cache
        ↓
    Reset Complete

Important Interfaces
--------------------

Initialization Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Initialize factory configuration module
     * 
     * Checks if first initialization, if yes then factory reset, otherwise load cache
     */
    void bk_factory_init(void);

    /**
     * @brief Factory reset
     * 
     * Resets all configuration items to default values
     */
    void bk_factory_reset(void);

Configuration Operation Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Read configuration item
     * 
     * @param key Configuration item key name
     * @param value Value buffer pointer
     * @param value_len Value buffer length
     * @return int 
     *         - > 0: Actual read length
     *         - 0: Not found or failure
     */
    int bk_config_read(const char *key, void *value, int value_len);

    /**
     * @brief Write configuration item
     * 
     * @param key Configuration item key name
     * @param value Value pointer
     * @param value_len Value length
     * @return int 
     *         - 0: Success
     *         - -1: Failure
     * 
     * @note Write only updates cache, need to call bk_config_sync_flash() to sync to Flash
     */
    int bk_config_write(const char *key, const void *value, int value_len);

Sync Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Sync configuration to Flash
     * 
     * Syncs all modified configuration items from cache to Flash
     */
    void bk_config_sync_flash(void);

    /**
     * @brief Safely sync configuration to Flash
     * 
     * Thread-safe version of sync function
     * 
     * @return bk_err_t 
     *         - BK_OK: Success
     *         - BK_FAIL: Failure
     */
    bk_err_t bk_config_sync_flash_safely(void);

User Configuration Registration Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Register user-defined configuration items
     * 
     * @param config Configuration item array pointer
     * @param config_len Number of configuration items
     * 
     * @note Configuration structure must be defined in Flash (using const)
     */
    void bk_regist_factory_user_config(const struct factory_config_t *config, 
                                       uint16_t config_len);

Configuration Structure Definition
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    struct factory_config_t {
        char *key;              // Configuration item key name
        void *value;            // Default value pointer
        uint8_t defval_size;    // Default value size
        uint8_t need_sram_cache; // Whether SRAM cache is needed
        uint16_t value_max_size; // Maximum value size
    };

Main Macro Definitions
-----------------------

Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Enable Factory Config module
    CONFIG_BK_FACTORY_CONFIG=y

    // Dependencies
    CONFIG_EASY_FLASH=y         // Easy Flash support

Platform Configuration Items
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Default platform configuration items:

.. code-block:: C

    // System initialization flag
    {"sys_initialized", "1", 1, BK_FALSE, 1}

    // Volume configuration (SRAM cache)
    {"volume", &s_factory_volume, 4, BK_TRUE, 4}

    // Agent information (SRAM cache)
    {"d_agent_info", "\0", 1, BK_TRUE, 588}

Usage Examples
----------------

Basic Usage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    #include "bk_factory_config.h"

    void example_usage(void)
    {
        // Initialize
        bk_factory_init();
        
        // Read configuration
        uint32_t volume = 0;
        int ret = bk_config_read("volume", &volume, sizeof(volume));
        if (ret > 0) {
            // Use configuration value
        }
        
        // Write configuration
        volume = 10;
        bk_config_write("volume", &volume, sizeof(volume));
        
        // Sync to Flash
        bk_config_sync_flash();
    }

Register User Configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Define user configuration (must be in Flash)
    static const uint32_t s_default_timeout = 5000;
    static const struct factory_config_t s_user_config[] = {
        {
            .key = "timeout",
            .value = (void *)&s_default_timeout,
            .defval_size = 4,
            .need_sram_cache = BK_TRUE,
            .value_max_size = 4,
        },
    };

    void register_user_config(void)
    {
        // Register user configuration
        bk_regist_factory_user_config(s_user_config, 
                                      sizeof(s_user_config) / sizeof(s_user_config[0]));
        
        // Initialize factory configuration
        bk_factory_init();
    }

CLI Commands
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The module provides the following CLI commands:

- ``factory_read``: Read configuration item (example: read volume)
- ``factory_write``: Write configuration item (example: write volume)
- ``factory_sync``: Sync configuration to Flash
- ``factory_reset``: Factory reset

Notes
----------------

1. **Sync Timing**: After writing configuration, need to call ``bk_config_sync_flash()`` to persist to Flash

2. **Thread Safety**: Configuration read/write operations are not thread-safe, multi-thread access requires locking

3. **Configuration Key Names**: Configuration key names must be unique and cannot conflict with platform configuration items

