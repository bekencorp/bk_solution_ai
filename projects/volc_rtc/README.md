# VolcEngine RTC Solution DEMO Development Guide

* [中文](./README_CN.md)

## 1 Project Overview

This project is a real-time audio/video communication solution based on BK7258 chip and VolcEngine RTC SDK, implementing audio/video data transmission through WiFi and supporting real-time conversation with cloud AI Agent. The project integrates VolcEngine RTC SDK, audio processing engine, network transfer module, and event management system. It is suitable for developing intelligent AI devices, voice assistants, and other application scenarios.

## 2 Features

### 2.1 Real-time Audio/Video Communication
- Supports bidirectional audio/video communication
- Supports multiple audio/video streams
- Supports adaptive bitrate control (BWE)
- Supports key frame request mechanism

### 2.2 Audio Processing
- Supports multiple audio encoding formats:
  - G.722
  - OPUS (recommended)
  - PCM
- Supports AEC (Acoustic Echo Cancellation)
- Supports NS (Noise Suppression)
- Supports audio capture and playback

### 2.3 Video Processing (Optional)
- Supports H264 encoding
- Supports JPEG encoding
- Supports video capture and transmission

### 2.4 Network Functions
- Supports WiFi STA mode connection
- Supports WiFi AP mode hotspot
- Supports TCP/UDP protocols
- Supports HTTP/HTTPS requests

### 2.5 AI Agent Integration
- Supports integration with VolcEngine AI Agent service
- Supports voice conversation and image recognition
- Supports Agent start, stop, and update
- Supports starting Agent from BK server or custom server

### 2.6 Room Management
- Supports joining/leaving RTC rooms
- Supports user online/offline notifications
- Supports Token permission management
- Supports Token expiration warning and automatic refresh

## 3 Quick Start

### 3.1 Compilation and Flashing

Compilation process reference: `AI Solution <../../README_CN.md>`_

Flashing process reference: For specific `flashing procedures <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html>`_, please refer to `SMP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html>`_

The compiled firmware bin file path: ``projects/volc_rtc/build/bk7258/volc_rtc/package/all-app.bin``

**Compilation command example:**

```bash
cd ~/armino/bk_solution_ai/projects/volc_rtc
export SDK_DIR=~/armino/bk_avdk_smp
make clean
make bk7258
```


## 4 API Reference

This section provides API interface descriptions for core functions in the project. These interfaces implement advanced function calls by encapsulating the VolcEngine RTC SDK.

.. note::

   It is recommended that developers do not directly call the following interfaces to implement custom solutions, but rather refer to the implementation of these interfaces and build functional modules that meet their own needs by combining and encapsulating SDK interfaces.


### 4.1 Agent Management API

#### 4.1.1 bk_byte_agent_start
```c
/**
 * @brief Start AI Agent service
 * 
 * @param room_info Room information structure pointer (output parameter)
 *        - Function will fill App ID, Room ID, Token and other information
 * 
 * @param device_id Device ID string
 * 
 * @return int Operation result
 *         - BK_OK: Start successful
 *         - BK_FAIL: Start failed
 * 
 * @note This function will:
 *       1. Select to start from BK server or custom server according to configuration
 *       2. Send HTTP request to server
 *       3. Parse response to get room information
 *       4. Fill room_info structure
 * 
 * @warning Before calling this function, ensure that network connection is normal
 * 
 * @see bk_byte_agent_stop()
 */
int bk_byte_agent_start(byte_rtc_room_info_t *room_info, void *device_id);
```

#### 4.1.2 bk_byte_agent_stop
```c
/**
 * @brief Stop AI Agent service
 * 
 * @param room_info Room information structure pointer
 * @param device_id Device ID string
 * 
 * @return int Operation result
 *         - BK_OK: Stop successful
 *         - BK_FAIL: Stop failed
 * 
 * @note This function will:
 *       1. Select to stop from BK server or custom server according to configuration
 *       2. Send HTTP request to server
 *       3. Clean up local resources
 * 
 * @see bk_byte_agent_start()
 */
int bk_byte_agent_stop(byte_rtc_room_info_t *room_info, void *device_id);
```

### 4.2 Advanced API

#### 4.2.1 bk_byte_start
```c
/**
 * @brief Start complete RTC and Agent service
 * 
 * @param device_id Device ID string
 * 
 * @return int Operation result
 *         - BK_OK: Start successful
 *         - BK_FAIL: Start failed
 * 
 * @note This function will:
 *       1. Mount SD card file system (if License is enabled)
 *       2. Start AI Agent service (get room information)
 *       3. Initialize RTC engine
 *       4. Join RTC room
 *       5. Start audio/video transmission
 * 
 * @warning This is an advanced interface that will start both Agent and RTC
 * 
 * @see bk_byte_stop()
 */
int bk_byte_start(void *device_id);
```

#### 4.2.2 bk_byte_stop
```c
/**
 * @brief Stop complete RTC and Agent service
 * 
 * @param device_id Device ID string
 * 
 * @return int Operation result
 *         - BK_OK: Stop successful
 *         - BK_FAIL: Stop failed
 * 
 * @note This function will:
 *       1. Stop RTC service (leave room, destroy engine)
 *       2. Stop AI Agent service
 *       3. Release room information memory
 *       4. Unmount SD card file system
 * 
 * @see bk_byte_start()
 */
int bk_byte_stop(void *device_id);
```

### 4.3 Audio/Video Sending API

#### 4.3.1 byte_rtc_send_audio
```c
/**
 * @brief Send audio data to RTC room
 * 
 * @param engine RTC engine handle
 * @param room Room ID string
 * @param data Audio data pointer
 * @param data_len Audio data length
 * 
 * @return int Operation result
 *         - 0: Send successful
 *         - <0: Send failed
 * 
 * @note This function will:
 *       1. Check RTC engine and room status
 *       2. Call SDK interface to send audio data
 *       3. Automatically handle encoding format conversion
 * 
 * @warning Before calling this function, ensure that room has been joined
 * 
 * @see byte_rtc_send_video()
 */
int byte_rtc_send_audio(byte_rtc_engine_t engine, 
                        const char *room, 
                        const void *data, 
                        size_t data_len);
```

#### 4.3.2 byte_rtc_send_video
```c
/**
 * @brief Send video data to RTC room
 * 
 * @param engine RTC engine handle
 * @param room Room ID string
 * @param data Video data pointer
 * @param data_len Video data length
 * @param codec Video encoding format
 * 
 * @return int Operation result
 *         - 0: Send successful
 *         - <0: Send failed
 * 
 * @note This function will:
 *       1. Check RTC engine and room status
 *       2. Construct video frame information
 *       3. Call SDK interface to send video data
 * 
 * @warning Before calling this function, ensure that room has been joined
 * 
 * @see byte_rtc_send_audio()
 */
int byte_rtc_send_video(byte_rtc_engine_t engine, 
                        const char *room, 
                        const void *data, 
                        size_t data_len, 
                        video_data_type_e codec);
```


## 5 For detailed project introduction and guide, please refer to the following links

- `Armino SMP SDK Documentation <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html>`_
- `VolcEngine RTC Documentation <https://www.volcengine.com/docs/6348>`_
- `For specific details of VolcEngine DEMO project, please refer to: <https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/projects/volc_rtc/index.html>`_
