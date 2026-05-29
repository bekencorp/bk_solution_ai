BK7259 Robot Solution Introduction
===========================================

:link_to_translation:`zh_CN:[中文]`

Overview
---------------------------------

The BK7259 Robot Solution is a reference design from Beken built on the BK7259 SoC and the Armino SMP (BK AVDK SMP) v4.0.x architecture. It provides:

- An LVGL UI on a 360x390 MIPI display;
- Local voice wake words ("Ni Hao Bo Tong" / "Zai Jian Bo Tong") plus Agora RTC AI dialog and vision Q&A;
- BLE provisioning via the BK App;
- Reference drivers for 4 keys, ToF, ambient light, G-Sensor, NFC, LEDs, vibration motor and servo;
- WiFi STA and an optional 4G cellular fallback.

.. note::

   For environment setup, source download, build, flash, run and debug on the V1 board, see :doc:`../get-started/index`.
   For the ``beken_robot`` project layout, Kconfig, key map, LVGL pages and debug CLIs, see :doc:`../projects/beken_robot/index`.

Design Philosophy
---------------------------------

The BK7259 Robot Solution follows a three-tier "device - cloud - model" architecture:

- **Device**: BK7259 SoC handles UI, keys, mic capture, speaker playback, camera capture, sensors and local wake-word.
- **Cloud**: Agora RTC carries low-latency real-time audio / video between the device and the AI Agent.
- **AI Model**: Mainstream LLMs (OpenAI, Doubao, DeepSeek, Volc Ark, etc.) integrated via Agora's Conversational AI Agent.

Core Features
---------------------------------

1. Multimodal interaction
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Voice dialog**: local wake + cloud streaming ASR / LLM / TTS, low end-to-end latency.
- **Vision Q&A**: MIPI CSI camera + Agora vision mode, query objects in front of the camera.
- **Sound source localization**: dual-mic array estimates the speaker direction; the on-screen eyes follow.
- **Command words**: local commands trigger UI actions (work in progress).

2. Real-time A/V communication
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- Built on the **Agora RTC SDK** for low-latency duplex A/V.
- Default audio: OPUS 16 kHz duplex; video: H.264 or JPEG.
- Supports jitter estimation and adaptive bitrate.

3. On-device audio processing
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- AEC (acoustic echo cancellation), NS (noise suppression).
- KWS (keyword wake-up).
- Prompt tone playback (boot, provisioning, wake, low battery, disconnect, etc.).

4. Full peripheral support
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Display**: 360x390 MIPI LCD (default panel ``jd9855_mipi_360x390``).
- **Input**: dual-mic array + 4 user keys (S2 / S4 / S5 are functional on the V1 board; S1 is reset and S3 is unused due to a wiring error).
- **Sensors**: ToF, ambient light, G-Sensor, NFC.
- **Output**: speaker, LEDs, vibration motor, servo.
- **Storage**: SD-NAND for assets plus on-chip partitions.
- **Networking**: on-board WiFi/BLE; a slot is reserved for a 4G Cat.1 module.
- **Power**: USB Type-C 5V; battery + charging supported on suitable boards.

System Architecture
---------------------------------

.. rubric:: Relationship between BK7259 Robot Solution and BK AVDK SMP

The BK7259 Robot Solution and the underlying BK AVDK SMP (Armino SMP SDK v4.0.x) split the work as follows:

#. **Scope**: this repo is a scenario-specific solution focused on robot UI, AI agent integration and peripheral composition; SoC, RTOS, drivers and network stacks all live in BK AVDK SMP.
#. **Build**: this repo does not ship its own build system. Toolchain, Kconfig and project generation are provided by BK AVDK SMP; point at it via ``SDK_DIR`` (or ``make bk7259 SDK_DIR=...``).
#. **Code boundary**: this repo (``bk_solution_ai``) provides solution and business code (``components/``, ``projects/beken_robot/``, mounted directly at the repo root with **no extra "solution/" wrapper**); HW drivers, RTOS, memory management and Wi-Fi/BLE stacks live in SMP.

On BK7259, Armino SMP follows an AP (application processor) + CP (communication processor) split:

- **AP (CPU1 + CPU2)**: LVGL UI, AI business logic, A/V engines, Agora SDK.
- **CP (CPU0)**: Wi-Fi, BLE, networking stacks and low-power management.

Software stack (illustrative)::

    Application (LVGL UI / robot main flow / state machine)
            ↓
    Services (audio_engine / video_engine / network_transfer / bk_smart_config / bk_app_event)
            ↓
    RTC (Agora RTC SDK)
            ↓
    OS (RTOS)
            ↓
    Hardware (BK7259 AP+CP)

Typical Use Cases
---------------------------------

1. **Desktop / companion robot**: LCD facial expressions, sound source localization and AI dialog.
2. **Education / interactive toys**: local wake plus multimodal recognition.
3. **Service / guide robots**: camera vision plus voice Q&A, suitable for customer support or guidance.
4. **Smart-home hub**: trigger smart-home devices via cloud AI agents.

Technical Highlights
---------------------------------

1. **End-to-end solution paired with SMP**: this repo focuses on the robot business and UI; HW drivers / RTOS / stacks ship with SMP.
2. **Modular components/**: ``audio_engine`` / ``video_engine`` / ``network_transfer`` / ``bk_smart_config`` / ``bk_app_event`` / ``bk_factory_config`` / ``bk_key_app`` / ``bk_led_blink`` / ``bk_motor`` / ``bk_servo`` / ``bk_countdown`` / ``avdk_nn_module`` / ``multimedia_device_service`` are independent and trimmable.
3. **Real LVGL demo**: 10 pages cover boot logo, main menu, provisioning, sound source, AI dialog, vision, command words, music and volume.
