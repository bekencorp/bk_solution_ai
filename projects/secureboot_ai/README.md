# Beken Genie AI Solution DEMO Development Guide

* [中文](./README_CN.md)

## 1 Project Overview

This project is a general-purpose AI device solution framework based on the BK7258 chip, providing complete end-to-cloud and cloud-to-large-model AI interaction capabilities. The project supports Agora RTC solution, integrating audio processing engine, network transfer module, event management system, and rich peripheral support. It is suitable for developing intelligent AI devices, voice assistants, smart speakers, and other application scenarios.

## 2 Features

### 2.1 Real-time Audio/Video Communication
- Supports bidirectional audio/video communication
- Supports multiple audio/video streams
- Supports adaptive bitrate control (BWE)
- Supports key frame request mechanism

### 2.2 Audio Processing
- Supports multiple audio encoding formats:
  - OPUS (recommended)
  - PCM
- Supports AEC (Acoustic Echo Cancellation)
- Supports NS (Noise Suppression)
- Supports KWS (Keyword Wake-up)
- Supports audio capture and playback
- Supports prompt tone playback

### 2.3 Video Processing (Optional)
- Supports H264 encoding
- Supports JPEG encoding
- Supports video capture and transmission
- Supports image recognition

### 2.4 Network Functions
- Supports WiFi STA mode connection
- Supports WiFi AP mode hotspot
- Supports Bluetooth network configuration
- Supports TCP/UDP protocols
- Supports HTTP/HTTPS requests

### 2.5 AI Agent Integration
- Supports integration with multiple AI Agent services
- Supports voice conversation and image recognition
- Supports Agent start, stop, and update
- Supports starting Agent from BK server or custom server
- Supports multiple large language models (OpenAI, Doubao, DeepSeek, etc.)

### 2.6 Room Management
- Supports joining/leaving RTC rooms
- Supports user online/offline notifications
- Supports Token permission management
- Supports Token expiration warning and automatic refresh

### 2.7 Peripheral Support
- **Display**: Supports dual SPI LCD screens (GC9D01 160x160)
- **Input**: Microphone, buttons, gyroscope, NFC
- **Output**: Speaker, LED effects, vibration motor
- **Storage**: SD NAND 128MB
- **Power**: Lithium battery, charging management (ETA3422)
- **Camera**: DVP camera (gc2145)

## 3 Quick Start

### 3.1 Compilation and Flashing

Compilation process reference: `AI Solution <../../README_CN.md>`_

Flashing process reference: For specific `flashing procedures <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html>`_, please refer to `SMP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html>`_

The compiled firmware bin file path: ``projects/beken_genie/build/bk7258/beken_genie/package/all-app.bin``

**Compilation command example:**

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
export SDK_DIR=~/armino/bk_avdk_smp
make clean
make bk7258
```

## 4 API Reference

This section provides API interface descriptions for core functions in the project.

### 4.1 Agora RTC API

If Agora RTC is enabled (`CONFIG_AGORA_RTC_EN=y`), the following APIs can be used:

#### 4.1.1 bk_agora_start
```c
/**
 * @brief Start complete Agora RTC and Agent service
 * 
 * @param device_id Device ID string
 * 
 * @return int Operation result
 *         - BK_OK: Start successful
 *         - BK_FAIL: Start failed
 * 
 * @see bk_agora_stop()
 */
int bk_agora_start(void *device_id);
```

#### 4.1.2 bk_agora_stop
```c
/**
 * @brief Stop complete Agora RTC and Agent service
 * 
 * @param device_id Device ID string
 * 
 * @return int Operation result
 *         - BK_OK: Stop successful
 *         - BK_FAIL: Stop failed
 * 
 * @see bk_agora_start()
 */
int bk_agora_stop(void *device_id);
```

### 4.2 General APIs

#### 4.2.1 Audio Engine API
```c
/**
 * @brief Initialize audio engine
 * 
 * @return bk_err_t Operation result
 */
bk_err_t audio_engine_init(void);
```

#### 4.2.2 Network Transfer API
```c
/**
 * @brief Initialize network transfer module
 * 
 * @return bk_err_t Operation result
 */
bk_err_t ntwk_trans_init(void);
```

#### 4.2.3 Application Event API
```c
/**
 * @brief Initialize application event system
 * 
 * @return bk_err_t Operation result
 */
bk_err_t app_event_init(void);
```


## 5 For detailed project introduction and guide, please refer to the following links

- `Armino SMP SDK Documentation <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html>`_
- `Agora RTC Documentation <https://docs.agora.io/>`_
- `For specific details of Agora DEMO project, please refer to: <https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/projects/beken_genie/index.html>`_
