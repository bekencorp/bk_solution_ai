Armino AI Solution Introduction
=====================================

:link_to_translation:`zh_CN:[中文]`

Overview
----------------

The Armino AI Solution is an intelligent AI device solution developed by Beken Corporation based on the Armino SMP architecture. This solution provides complete end-to-cloud and cloud-to-large-model AI interaction capabilities, supports multiple large language model integrations, and provides developers with a complete development framework for rapidly building intelligent AI devices.

Design Philosophy
------------------

The Armino AI Solution adopts a three-layer architecture design of "Device-Cloud-Model":

- **Device Side**: Intelligent devices based on BK7258 chip, responsible for audio/video capture, local processing, and user interaction
- **Cloud Side**: BK/Agora RTC/VolcEngine RTC servers, responsible for audio/video data transmission and routing
- **AI Model Side**: Supports multiple large language models (OpenAI, Doubao, DeepSeek, etc.), providing AI conversation and image recognition capabilities

Core Features
----------------

1. Multimodal Interaction
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Voice Interaction**: Supports voice wake-up, speech recognition, and speech synthesis, providing natural human-machine dialogue experience
- **Visual Interaction**: Supports dual-screen display (SPI LCD X2), providing rich visual feedback and emotional expression
- **Image Recognition**: Supports real-time image capture and recognition, can switch between large language models and image recognition models

2. Real-time Audio/Video Communication
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Low Latency Transmission**: Supports low-latency audio/video transmission
- **Multiple Encoding Formats**:
  - Video: H.264, JPEG
  - Audio: G.711A/U, G.722, OPUS, PCM
- **Intelligent Bitrate Control**: Supports adaptive bandwidth estimation and bitrate adjustment

3. Device-side Audio Processing
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **AEC (Echo Cancellation)**: Eliminates speaker echo, improves speech recognition accuracy
- **NS (Noise Suppression)**: Suppresses environmental noise, improves voice quality
- **KWS (Keyword Wake-up)**: Supports custom wake words for local wake-up
- **Prompt Tone Playback**: Supports multiple event prompt tones to enhance user experience

4. Multi-model Support
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Large Language Models**: Supports mainstream large language models such as OpenAI, Doubao, DeepSeek
- **Image Recognition Models**: Supports image recognition and analysis
- **Flexible Switching**: Supports runtime switching between different models to meet different application scenarios

5. Complete Peripheral Support
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Display**: Dual SPI LCD screens (GC9D01 160x160)
- **Input**: Microphone, buttons, gyroscope, NFC
- **Output**: Speaker, LED effects, vibration motor
- **Storage**: SD NAND 128MB
- **Power**: Lithium battery, charging management (ETA3422)
- **Camera**: DVP camera (gc2145)

System Architecture
-------------------

.. rubric:: Relationship between BK AI and BK AVDK SMP

The BK AI Solution delivered in this repository relates to the underlying platform BK AVDK SMP (Armino SMP SDK) as follows:

#. **Development and scope**: BK AI is a scenario-oriented solution built on BK AVDK SMP, focusing on AI business logic, RTC, and cloud large-model integration. The chip, RTOS, drivers, and Wi-Fi/BLE stacks are provided by BK AVDK SMP.

#. **Build and compilation**: BK AI does **not** ship a standalone build system. Firmware build, toolchain, Kconfig, and project generation rely on the **BK AVDK SMP** build environment (for example, point ``SDK_DIR`` to the SMP SDK tree).

#. **Code boundary**: This repository mainly contains **solution and business implementation** code. It does **not** include hardware drivers, RTOS, memory management, or Wi-Fi/BLE stacks; use the interfaces and components provided by **BK AVDK SMP** when you need those capabilities.

The following describes the typical processor split and software layering on BK7258 from the **BK AVDK SMP** platform perspective (BK AI business code runs at the application and service layers and depends on OS, drivers, and networking from SMP).

The Armino SMP architecture uses AP (Application Processor) + CP (Communication Processor):

- **AP (CPU1 + CPU2)**: Runs multimedia applications, AI interaction, audio/video processing, and related core functions
- **CP (CPU0)**: Runs Wi-Fi, BLE, and low-power protocol stacks

Software architecture layers (illustrative):

::

    Application Layer (AI Interaction, UI Display)
            ↓
    Service Layer (Media Service, Network Transfer, Audio Engine)
            ↓
    RTC Layer (Agora RTC SDK)
            ↓
    OS Layer (RTOS)
            ↓
    Hardware Layer (BK7258)

Main Application Scenarios
--------------------------

1. **Intelligent Companion Devices**: Provides voice dialogue, emotional expression, and visual feedback, suitable for scenarios such as children's companionship and elderly care

2. **Intelligent Education Devices**: Supports voice Q&A and image recognition, can be used for learning assistance and knowledge Q&A

3. **Smart Home Control**: Controls smart home devices through voice interaction, providing convenient control methods

4. **Enterprise Service Robots**: Supports multimodal interaction, can be used for customer service, guidance, and other scenarios

Technical Advantages
--------------------

1. **Solution stack with SMP**: BK AI provides scenario solutions and reference projects with documentation; hardware drivers, RTOS, and stacks are provided by BK AVDK SMP, together forming a complete development path

2. **Modular Design**: Each functional module is independent, facilitating customization and expansion

3. **Rich Reference Designs**: Includes reference implementations of common peripherals to accelerate product development

4. **Flexible Configuration**: Supports Kconfig configuration system, can flexibly tailor functions according to requirements

5. **Comprehensive Documentation**: Provides detailed development documentation, API references, and usage examples

