Network Transfer Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

Network Transfer is a unified network transmission abstraction layer component for network transmission of multimedia data (audio, video) and control commands. This component adopts **layered architecture** and **modular design**, decoupling upper-layer multimedia applications from underlying transmission protocols through standardized interfaces, enabling developers to flexibly switch or customize transmission solutions (such as Agora RTC, VolcEngine RTC, etc.).

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Unified Abstract Interface**: Provides consistent API for upper-layer applications, shielding underlying transmission protocol differences
- **Pluggable Protocol**: Supports dynamic registration of transmission solutions, convenient for integrating custom protocols
- **Multiple Backend Support**: Supports multiple RTC backends such as Agora RTC, VolcEngine RTC
- **Event-driven Mechanism**: Real-time notification of state changes and data reception to application layer through callback mechanism
- **Auto Routing**: Audio data automatically routed to audio engine, video data automatically routed to network

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Network Transfer module architecture is as follows:

::

    Application Layer (Application)
            ↓
    Network Transfer API (network_engine.h)
            ↓
    RTC Backend Adapter Layer
        ├── Agora RTC (bk_agora_api.c)
        └── VolcEngine RTC (bk_volc_api.c)
            ↓
    RTC SDK
        ├── Agora RTC SDK
        └── VolcEngine RTC SDK
            ↓
    Network Layer

Workflow
----------------

Initialization Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Network Transfer initialization flow is as follows:

::

    ntwk_trans_init()
        ↓
    1. Check if Already Initialized
        - If initialized, return directly
        ↓
    2. Select RTC Backend Based on Configuration
        #if CONFIG_AGORA_IOT_SDK
            - Set Agora RTC callback functions
                * audio_tx_cb = bk_agora_rtc_audio_data_send
                * video_tx_cb = bk_agora_rtc_video_data_send
                * start_cb = bk_agora_start
                * stop_cb = bk_agora_stop
                * pre_config_cb = bk_agora_pre_config
                * update_cb = bk_agora_update_agent
            - network_type = NETWORK_TYPE_AGORA_RTC
        #elif CONFIG_VOLC_RTC_EN
            - Set VolcEngine RTC callback functions
                * audio_tx_cb = bk_byte_rtc_audio_data_send
                * video_tx_cb = bk_byte_rtc_video_data_send
                * start_cb = bk_byte_start
                * stop_cb = bk_byte_stop
                * pre_config_cb = bk_byte_pre_config
                * update_cb = bk_byte_update_agent
            - network_type = NETWORK_TYPE_VOLC_RTC
        #endif
        ↓
    3. Execute Pre-configuration Callback
        - pre_config_cb(user_data)
        - Register CLI commands, etc.
        ↓
    4. Set Initialization Flag
        - initialized = true
        ↓
    Initialization Complete

Start Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``ntwk_trans_start()``:

::

    1. Check Initialization Status
        - If not initialized, return error
        - If already started, return directly
        ↓
    2. Call Start Callback
        - start_cb(user_data)
        - For Agora: bk_agora_start()
            * Start Agent
            * Start RTC
        - For VolcEngine: bk_byte_start()
        ↓
    3. Set Start Flag
        - is_started = true
        ↓
    Start Complete

Audio Send Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``ntwk_trans_send_audio()``:

::

    1. Parameter Validation
        - Check initialization status
        - Check data pointer and size
        - Check audio type validity
        ↓
    2. Call Audio Send Callback
        - audio_tx_cb(data, size, audio_type)
        - For Agora: bk_agora_rtc_audio_data_send()
            * Check connection status
            * Map encoder type
            * agora_rtc_send_audio_data()
        - For VolcEngine: bk_byte_rtc_audio_data_send()
        ↓
    3. Return Send Result
        ↓
    Send Complete

Video Send Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``ntwk_trans_send_video()``:

::

    1. Parameter Validation
        - Check initialization status
        - Check frame buffer validity
        - Check frame data pointer and length
        ↓
    2. Call Video Send Callback
        - video_tx_cb(frame)
        - For Agora: bk_agora_rtc_video_data_send()
            * Check connection status
            * I-frame filtering (H.264 only)
            * Frame rate control (500ms interval)
            * agora_rtc_send_video_data()
        - For VolcEngine: bk_byte_rtc_video_data_send()
        ↓
    3. Return Send Result
        ↓
    Send Complete

Audio Receive Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``ntwk_trans_recv_audio()``:

::

    1. Parameter Validation
        - Check initialization status
        - Check data pointer and size
        ↓
    2. Write to Audio Engine
        - audio_engine_write_data(data, size, 0)
        - Audio engine automatically decodes and plays
        ↓
    Receive Complete

Stop Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``ntwk_trans_stop()``:

::

    1. Check Status
        - If not initialized, return error
        - If not started, return error
        ↓
    2. Clear Start Flag
        - is_started = false
        ↓
    3. Call Stop Callback
        - stop_cb(user_data)
        - For Agora: bk_agora_stop()
            * Stop RTC
            * Stop Agent
        - For VolcEngine: bk_byte_stop()
        ↓
    Stop Complete

Important Interfaces
--------------------

Initialization Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Initialize network transfer module
     * 
     * Selects RTC backend (Agora or VolcEngine) based on configuration,
     * sets corresponding callback functions, and executes pre-configuration.
     * 
     * @return int 
     *         - 0: Success
     *         - < 0: Failure
     */
    int ntwk_trans_init(void);

    /**
     * @brief Deinitialize network transfer module
     * 
     * Stops network transfer and clears global context.
     * 
     * @return int 
     *         - 0: Success
     *         - < 0: Failure
     */
    int ntwk_trans_deinit(void);

Start and Stop Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Start network transfer
     * 
     * Calls the start callback function of underlying RTC backend.
     * 
     * @param user_data User data pointer, passed to start callback function
     * @return int 
     *         - 0: Success
     *         - < 0: Failure
     */
    int ntwk_trans_start(void *user_data);

    /**
     * @brief Stop network transfer
     * 
     * Calls the stop callback function of underlying RTC backend.
     * 
     * @param user_data User data pointer, passed to stop callback function
     * @return int 
     *         - 0: Success
     *         - < 0: Failure
     */
    int ntwk_trans_stop(void *user_data);

    /**
     * @brief Update network transfer
     * 
     * Calls the update callback function of underlying RTC backend, used for updating Agent configuration, etc.
     * 
     * @param user_data User data
     * @param update_info Update information
     * @return int 
     *         - 0: Success
     *         - < 0: Failure
     */
    int ntwk_trans_update(void *user_data, void *update_info);

Data Send Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Send audio data to network
     * 
     * @param data Audio data pointer
     * @param size Audio data size
     * @param audio_type Audio encoder type
     * @return int 
     *         - >= 0: Number of bytes sent
     *         - < 0: Failure
     */
    int ntwk_trans_send_audio(const uint8_t *data, size_t size, 
                               audio_enc_type_t audio_type);

    /**
     * @brief Send video frame to network
     * 
     * @param frame Video frame buffer pointer
     * @return int 
     *         - 0: Success
     *         - < 0: Failure
     */
    int ntwk_trans_send_video(frame_buffer_t *frame);

Data Receive Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Receive audio data and write to audio engine
     * 
     * This function writes received audio data to audio engine for decoding and playback.
     * 
     * @param data Audio data pointer
     * @param size Audio data size
     * @return int 
     *         - 0: Success
     *         - < 0: Failure
     */
    int ntwk_trans_recv_audio(const uint8_t *data, size_t size);

Query Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Get current network transfer type
     * 
     * @return network_type_t Network type enumeration value
     */
    network_type_t ntwk_trans_get_network_type(void);

    /**
     * @brief Get audio encoder type
     * 
     * Gets the encoder type currently used from audio engine.
     * 
     * @return audio_enc_type_t Audio encoder type enumeration value
     */
    audio_enc_type_t ntwk_trans_get_audio_encoder_type(void);

Data Structures
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**network_type_t**: Network type enumeration

.. code-block:: C

    typedef enum {
        NETWORK_TYPE_VOLC_RTC = 0,    // VolcEngine RTC
        NETWORK_TYPE_AGORA_RTC,       // Agora RTC
        NETWORK_TYPE_MAX
    } network_type_t;

**ntwk_trans_ctx_t**: Network transfer module context structure

.. code-block:: C

    typedef struct {
        network_type_t network_type;              // Network type
        void *network_config;                     // Network-specific configuration
        audio_tx_callback_t audio_tx_cb;          // Audio send callback
        video_tx_callback_t video_tx_cb;          // Video send callback
        ntwk_trans_start_callback_t start_cb;     // Start callback
        ntwk_trans_stop_callback_t stop_cb;      // Stop callback
        ntwk_trans_pre_config_callback_t pre_config_cb;  // Pre-configuration callback
        ntwk_trans_update_callback_t update_cb;   // Update callback
        void *user_data;                          // User data
        bool is_started;                          // Whether started
        bool initialized;                         // Initialization flag
    } ntwk_trans_ctx_t;

**Callback Function Types**:

.. code-block:: C

    // Audio send callback
    typedef int (*audio_tx_callback_t)(uint8_t *data_ptr, size_t data_len, 
                                       audio_enc_type_t audio_type);

    // Video send callback
    typedef int (*video_tx_callback_t)(frame_buffer_t *frame);

    // Start callback
    typedef bk_err_t (*ntwk_trans_start_callback_t)(void *user_data);

    // Stop callback
    typedef bk_err_t (*ntwk_trans_stop_callback_t)(void *user_data);

    // Pre-configuration callback
    typedef bk_err_t (*ntwk_trans_pre_config_callback_t)(void *user_data);

    // Update callback
    typedef bk_err_t (*ntwk_trans_update_callback_t)(void *user_data, 
                                                      void *update_info);

Main Macro Definitions
-----------------------

Kconfig Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**Basic Configuration**:

.. code-block:: C

    // Enable network transfer module
    CONFIG_BK_NETWORK_ENGINE=y

**RTC Backend Selection**:

.. code-block:: C

    // Use Agora RTC
    CONFIG_AGORA_IOT_SDK=y

    // Or use VolcEngine RTC
    // CONFIG_VOLC_RTC_EN=y

**Other Configuration**:

.. code-block:: C

    // Enable WebSocket transmission (AI scenario)
    CONFIG_BK_WSS_TRANS=y

    // WebSocket transmission does not use PSRAM
    CONFIG_BK_WSS_TRANS_NOPSRAM=y

    // Enable Sensenova (Agora)
    CONFIG_SENSENOVA_ENABLE=y

    // Start Agent from Beken server (default)
    CONFIG_STARTUP_AGENT_FROM_BK_SERVER=y
    // Or start Agent from custom server
    // CONFIG_STARTUP_AGENT_FROM_BK_SERVER=n

Usage Examples
----------------

Basic Usage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    #include "network_engine.h"
    #include "audio_engine.h"
    #include "video_engine.h"

    // Audio read callback (receive data from audio engine)
    static int audio_read_callback(unsigned char *data, unsigned int len, void *args)
    {
        // Send audio data to network
        return ntwk_trans_send_audio(data, len, 
                                     ntwk_trans_get_audio_encoder_type());
    }

    int main(void)
    {
        // 1. Initialize audio engine
        audio_engine_init();

        // 2. Initialize video engine
        video_engine_init();

        // 3. Initialize network transfer
        int ret = ntwk_trans_init();
        if (ret != 0) {
            LOGE("Network transfer init failed\n");
            return -1;
        }

        // 4. Start network transfer
        char device_id[65] = "your_device_id";
        ret = ntwk_trans_start(device_id);
        if (ret != 0) {
            LOGE("Network transfer start failed\n");
            return -1;
        }

        // 5. Check network type
        network_type_t type = ntwk_trans_get_network_type();
        if (type == NETWORK_TYPE_AGORA_RTC) {
            LOGI("Using Agora RTC\n");
        } else if (type == NETWORK_TYPE_VOLC_RTC) {
            LOGI("Using VolcEngine RTC\n");
        }

        // 6. Audio data will be automatically sent through audio_read_callback
        // Video data will be automatically sent through video_engine

        // 7. Received audio data will be automatically played (through ntwk_trans_recv_audio)

        // 8. Stop network transfer
        ntwk_trans_stop(device_id);

        // 9. Deinitialize
        ntwk_trans_deinit();

        return 0;
    }

Update Agent Configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    // Update Agent configuration (e.g., switch multimodal)
    void update_agent_mode(const char *mode)
    {
        int ret = ntwk_trans_update(device_id, (void *)mode);
        if (ret != 0) {
            LOGE("Update agent failed\n");
        }
    }

Notes
----------------

1. **Initialization Order**: Recommend initializing audio engine and video engine first, then network transfer

2. **Backend Selection**: Only one RTC backend can be selected (Agora or VolcEngine), configured through Kconfig

3. **Callback Functions**: Callback functions are automatically set by module internally based on configuration, application layer does not need to manually set

4. **Data Routing**: 
   - Audio data: Automatically sent through audio_engine's read_callback
   - Video data: Automatically sent through video_engine's transfer task
   - Receive audio: Automatically written to audio_engine through ntwk_trans_recv_audio

5. **State Check**: Should ensure network transfer is started before sending data (by checking return value)

6. **Error Handling**: All interfaces have error return values, should check return values and handle errors

7. **Resource Management**: Must call ``ntwk_trans_stop()`` and ``ntwk_trans_deinit()`` after use

8. **Thread Safety**: Network transfer module uses thread-safe mechanisms internally, but callback functions should avoid long blocking

9. **Agent Management**: Agent start and stop are managed by underlying RTC backend, network transfer module only responsible for calling

10. **Configuration Dependency**: Ensure corresponding RTC backend is correctly configured (Agora requires App ID, Token, etc.)

