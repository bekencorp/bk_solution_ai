Audio Engine Module
=====================================

:link_to_translation:`zh_CN:[中文]`

Module Introduction
-------------------

Audio Engine is a high-level audio engine module built on the ``bk_voice_service`` infrastructure, providing simplified audio start/stop operation APIs. This module encapsulates complex audio processing flows, including audio capture, encoding, decoding, AEC (Echo Cancellation), NS (Noise Suppression), and other functions, providing a unified audio processing interface for upper-layer applications.

Core Features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Simplified API**: Easy-to-use ``audio_engine_start()`` and ``audio_engine_stop()`` functions
- **Complete Error Handling**: Detailed error codes for easy debugging
- **Flexible Configuration**: Supports multiple microphone/speaker types and audio processing functions
- **Thread Safety**: Proper resource management and state tracking
- **Callback Support**: Event and data read callbacks for real-time audio processing
- **Volume Control**: Supports volume adjustment and persistent storage
- **Prompt Tone Support**: Supports prompt tone playback function (optional)

Module Architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Audio Engine module architecture is as follows:

::

    Application Layer (Application)
            ↓
    Audio Engine API (audio_engine.h)
            ↓
    Voice Service (bk_voice_service)
            ↓
    Audio Pipeline (Audio Pipeline)
            ↓
    Hardware Layer (ADC/DAC)

Workflow
----------------

Initialization Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The Audio Engine initialization flow is as follows:

::

    audio_engine_init()
        ↓
    1. Volume Initialization (audio_engine_volume_init)
        - Read volume level from configuration
        - Calculate volume gain table
        ↓
    2. Build Configuration Structure (audio_engine_cfg_t)
        - Sample rate configuration
        - Encoder/decoder type
        - AEC/NS configuration
        - PA control configuration
        - Callback function settings
        ↓
    3. Start Audio Engine (audio_engine_start)
        - Initialize Voice Service
        - Configure microphone stream
        - Configure speaker stream
        - Configure encoder/decoder
        - Configure AEC/NS algorithms
        - Start audio service
        ↓
    4. Initialize Prompt Tone (optional)
        - audio_engine_prompt_tone_init
        ↓
    Initialization Complete

Start Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Detailed flow of ``audio_engine_start()``:

::

    1. Parameter Validation
        - Check configuration pointer validity
        - Validate sample rate (8000 or 16000)
        - Check if already started
        ↓
    2. Configure Voice Service
        - Microphone configuration (onboard_mic_stream_cfg_t)
            * ADC sample rate
            * Digital gain/analog gain
            * Frame size (20ms)
            * AEC mode (hardware/software)
        - Encoder configuration
            * G.711A/U: 160/320 byte frames
            * G.722: 80/160 byte frames
            * OPUS: Default configuration
            * PCM: Raw data
        - Decoder configuration
            * Corresponding decoder configuration for encoder
        - Speaker configuration (onboard_speaker_stream_cfg_t)
            * DAC sample rate
            * PA control (GPIO, delay, etc.)
            * Digital gain/analog gain
        - AEC configuration (if enabled)
            * AEC mode selection
            * Multiple output ports
        - EQ configuration (if enabled)
        ↓
    3. Initialize Voice Service
        - bk_voice_init(&voice_cfg)
        ↓
    4. Initialize Voice Read Service
        - bk_voice_read_init()
        - Register read callback
        ↓
    5. Initialize Voice Write Service
        - bk_voice_write_init()
        ↓
    6. Start Voice Service
        - bk_voice_start()
        - bk_voice_read_start()
        - bk_voice_write_start()
        ↓
    7. ASR Initialization (if enabled)
        - Configure ASR service
        - Start ASR
        ↓
    Start Complete

Audio Data Flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**Uplink Audio Stream (Microphone → Network)**:

::

    Microphone Capture (ADC)
        ↓
    Audio Preprocessing (AEC/NS)
        ↓
    Audio Encoding (G.711/G.722/OPUS/PCM)
        ↓
    Read Callback (audio_engine_read_callback_t)
        ↓
    network_engine
        ↓
    Network Send

**Downlink Audio Stream (Network → Speaker)**:

::

    Network Receive
        ↓
    audio_engine_write_data()
        ↓
    Audio Decoding (G.711/G.722/OPUS/PCM)
        ↓
    Audio Post-processing (EQ)
        ↓
    Speaker Playback (DAC)

Important Interfaces
--------------------

Initialization Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Initialize and start audio engine
     * 
     * @param cfg Audio engine configuration structure pointer
     * @return int 
     *         - 0 (AUDIO_ENGINE_SUCCESS): Success
     *         - < 0: Error code (see audio_engine_err_t)
     */
    int audio_engine_start(audio_engine_cfg_t *cfg);

    /**
     * @brief Stop audio engine and clean up resources
     * 
     * @return int 
     *         - 0: Success
     *         - < 0: Error code
     */
    int audio_engine_stop(void);

    /**
     * @brief Initialize audio engine (using default configuration)
     * 
     * This function automatically initializes the audio engine using default parameters from Kconfig
     * 
     * @return int 
     *         - 0: Success
     *         - < 0: Error code
     */
    int audio_engine_init(void);

    /**
     * @brief Deinitialize audio engine
     * 
     * @return int 
     *         - 0: Success
     */
    int audio_engine_deinit(void);

Data Operation Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Write audio data to speaker
     * 
     * @param data Audio data pointer
     * @param size Audio data size (bytes)
     * @param timeout_ms Timeout (milliseconds)
     * @return int 
     *         - 0: Success
     *         - < 0: Error code
     */
    int audio_engine_write_data(const uint8_t *data, uint32_t size, uint32_t timeout_ms);

    /**
     * @brief Check if audio engine is running
     * 
     * @return bool 
     *         - true: Audio engine is running
     *         - false: Audio engine is not running
     */
    bool audio_engine_is_running(void);

Configuration and Query Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Get audio engine encoder type
     * 
     * @return audio_enc_type_t Encoder type
     */
    audio_enc_type_t audio_engine_get_encoder_type(void);

    /**
     * @brief Get audio engine decoder type
     * 
     * @return audio_dec_type_t Decoder type
     */
    audio_dec_type_t audio_engine_get_decoder_type(void);

    /**
     * @brief Convert string to encoder type
     * 
     * @param enc_str Encoder type string ("PCM", "G711A", "G711U", "G722", "OPUS")
     * @return audio_enc_type_t Encoder type enumeration value
     */
    audio_enc_type_t audio_engine_str_to_enc_type(const char *enc_str);

    /**
     * @brief Convert string to decoder type
     * 
     * @param dec_str Decoder type string ("PCM", "G711A", "G711U", "G722", "OPUS")
     * @return audio_dec_type_t Decoder type enumeration value
     */
    audio_dec_type_t audio_engine_str_to_dec_type(const char *dec_str);

    /**
     * @brief Get audio engine error string
     * 
     * @param err Error code
     * @return const char* Error description string
     */
    const char *audio_engine_err_to_str(int err);

Volume Control Interface
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. code-block:: C

    /**
     * @brief Increase volume
     */
    void audio_engine_volume_increase(void);

    /**
     * @brief Decrease volume
     */
    void audio_engine_volume_decrease(void);

Data Structures
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**audio_engine_cfg_t**: Audio engine configuration structure

.. code-block:: C

    typedef struct {
        uint32_t mic_sample_rate;      // Microphone sample rate (8000 or 16000)
        uint32_t spk_sample_rate;      // Speaker sample rate (8000 or 16000)
        
        /* Audio processing */
        uint8_t aec_enable;            // 0: Disable, 1: Enable AEC
        uint8_t eq_enable;             // 0: Disable, 1: Mono EQ, 2: Stereo EQ

        /* Encoding/Decoding */
        audio_enc_type_t enc_type;     // Encoder type
        audio_dec_type_t dec_type;     // Decoder type
        
        /* Audio gain */
        uint8_t dig_gain;              // DAC digital gain
        uint8_t ana_gain;              // DAC analog gain
        
        /* PA control */
        uint8_t pa_enable;             // 0: Disable, 1: Enable PA control
        uint8_t pa_gpio;               // PA control GPIO number
        uint8_t pa_on_level;           // PA on level
        uint32_t pa_on_delay;          // PA on delay (milliseconds)
        uint32_t pa_off_delay;         // PA off delay (milliseconds)
        
        /* Callback functions */
        voice_event_callback_t event_cb;        // Voice event callback
        audio_engine_read_callback_t read_cb;   // Audio read callback
        void *user_data;                        // User data
    } audio_engine_cfg_t;

**audio_engine_err_t**: Error code enumeration

.. code-block:: C

    typedef enum {
        AUDIO_ENGINE_SUCCESS = 0,            // Success
        AUDIO_ENGINE_ERR_INIT_FAILED = -1,   // Initialization failed
        AUDIO_ENGINE_ERR_INVALID_PARAM = -2,  // Invalid parameter
        AUDIO_ENGINE_ERR_NOT_STARTED = -3,    // Audio engine not started
        AUDIO_ENGINE_ERR_VOICE_INIT = -4,     // Voice initialization failed
        AUDIO_ENGINE_ERR_READ_INIT = -5,      // Voice Read initialization failed
        AUDIO_ENGINE_ERR_WRITE_INIT = -6,     // Voice Write initialization failed
        AUDIO_ENGINE_ERR_VOICE_START = -7,    // Voice start failed
        AUDIO_ENGINE_ERR_READ_START = -8,     // Voice Read start failed
        AUDIO_ENGINE_ERR_WRITE_START = -9,    // Voice Write start failed
        AUDIO_ENGINE_ERR_VOICE_STOP = -10,    // Voice stop failed
        AUDIO_ENGINE_ERR_READ_STOP = -11,     // Voice Read stop failed
        AUDIO_ENGINE_ERR_WRITE_STOP = -12,    // Voice Write stop failed
        AUDIO_ENGINE_ERR_ASR_INIT = -13,       // ASR initialization failed
        AUDIO_ENGINE_ERR_ASR_START = -14,      // ASR start failed
        AUDIO_ENGINE_ERR_ASR_STOP = -15,       // ASR stop failed
    } audio_engine_err_t;

Main Macro Definitions
-----------------------

Kconfig Configuration Macros
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**Basic Configuration**:

.. code-block:: C

    // Enable audio engine
    CONFIG_BK_AUDIO_ENGINE=y

    // Audio frame duration (milliseconds)
    CONFIG_AE_AUDIO_FRAME_DURATION_MS=20  // Range: 20-60

    // ADC sample rate
    CONFIG_AE_AUDIO_ADC_SAMP_RATE=16000

    // DAC sample rate
    CONFIG_AE_AUDIO_DAC_SAMP_RATE=16000

**Encoder Configuration**:

.. code-block:: C

    // Encoder type selection (mutually exclusive)
    CONFIG_AE_AUDIO_ENCODER_G722=y      // G.722 encoding
    // or
    CONFIG_AE_AUDIO_ENCODER_G711A=y     // G.711A encoding
    // or
    CONFIG_AE_AUDIO_ENCODER_G711U=y     // G.711U encoding
    // or
    CONFIG_AE_AUDIO_ENCODER_OPUS=y      // OPUS encoding
    // or
    CONFIG_AE_AUDIO_ENCODER_PCM=y       // PCM (no encoding)

    // Encoder type string (auto-generated)
    CONFIG_AE_AUDIO_ENCODER_TYPE="G722"  // or "G711A", "G711U", "OPUS", "PCM"

**Decoder Configuration**:

.. code-block:: C

    // Decoder type selection (mutually exclusive)
    CONFIG_AE_AUDIO_DECODER_G722=y      // G.722 decoding
    // or
    CONFIG_AE_AUDIO_DECODER_G711A=y     // G.711A decoding
    // or
    CONFIG_AE_AUDIO_DECODER_G711U=y     // G.711U decoding
    // or
    CONFIG_AE_AUDIO_DECODER_OPUS=y      // OPUS decoding
    // or
    CONFIG_AE_AUDIO_DECODER_PCM=y       // PCM (no decoding)

    // Decoder type string (auto-generated)
    CONFIG_AE_AUDIO_DECODER_TYPE="G722"  // or "G711A", "G711U", "OPUS", "PCM"

**Prompt Tone Configuration** (optional):

.. code-block:: C

    // Enable prompt tone support
    CONFIG_AE_SUPPORT_PROMPT_TONE=y

    // Prompt tone source selection
    CONFIG_AE_PROMPT_TONE_SOURCE_ARRAY=y    // Array storage
    // or
    CONFIG_AE_PROMPT_TONE_SOURCE_VFS=y      // File system storage

    // Prompt tone decoder type
    CONFIG_AE_PROMPT_TONE_DECODER_WAV=y     // WAV format
    // or
    CONFIG_AE_PROMPT_TONE_DECODER_MP3=y     // MP3 format
    // or
    CONFIG_AE_PROMPT_TONE_DECODER_PCM=y     // PCM format

Notes
----------------

1. **Sample Rate Limitation**: Microphone and speaker sample rates must be 8000 or 16000 Hz

2. **Frame Size**: Frame size is automatically calculated based on sample rate (20ms per frame)
   - 8kHz: 160 samples/frame
   - 16kHz: 320 samples/frame

3. **AEC Mode**: Hardware AEC mode requires dual-channel microphone input

4. **Encoder Dependency**: When selecting encoder, need to ensure corresponding ADK and Voice Service encoder are enabled

5. **Thread Safety**: Audio engine uses thread-safe mechanisms internally, but callback functions should avoid long blocking

6. **Resource Management**: Must call ``audio_engine_stop()`` and ``audio_engine_deinit()`` to release resources after use

7. **Volume Persistence**: Volume level is saved to configuration and automatically restored on next startup

