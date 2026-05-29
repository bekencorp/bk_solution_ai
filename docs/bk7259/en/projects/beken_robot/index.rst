BK7259 Robot Project
=================================

:link_to_translation:`zh_CN:[中文]`

1. Introduction
---------------------------------

    This project (``projects/beken_robot``) is built on the BK7259 SoC and BK AVDK SMP v4.0.x. It uses an **end → Agora RTC → cloud AI Agent** pipeline and provides a complete robot reference implementation.

    Hardware coverage: 360x390 MIPI LCD + LVGL UI (10 demo pages), dual on-board microphones, speaker, MIPI CSI camera, 4 keys, ToF / ambient light / G-Sensor / NFC, LED indicator, vibration motor, servo, etc.

    Software coverage: on-device AEC / NS, local wake words ("Ni Hao Bo Tong" / "Zai Jian Bo Tong"), prompt tones, and Agora's Conversational AI Agent for mainstream LLMs (OpenAI, Doubao, DeepSeek, Volc Ark, etc.).

1.1 Hardware references
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

   * BK7259 Robot V1 schematic: ``docs/SCH-Robot V1_2026-04-07.pdf`` (in this repo).
   * BK7259 datasheet: see :doc:`../../hw-reference/index`.

1.2 Specifications
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * **Hardware**:

        * SoC: BK7259 (AP CPU1+CPU2 / CP CPU0)
        * Display: 360x390 MIPI LCD (default panel ``jd9855_mipi_360x390``)
        * Input: dual mic array + 4 user keys (S2/S4/S5 are functional on V1; S1 is reset, S3 is unused)
        * Camera: MIPI CSI camera
        * Output: speaker, LED indicator, vibration motor, servo
        * Sensors: ToF, ambient light, G-Sensor, NFC (board-dependent)
        * Storage: SD-NAND for assets + on-chip partitions
        * Networking: on-board WiFi/BLE; reserved USB slot for a 4G Cat.1 module

    * **Software**:

        * LVGL with 10 demo pages (boot logo, main menu, provisioning, sound source, AI dialog, vision, command words, music, volume)
        * AEC / NS / KWS (wake words: "Ni Hao Bo Tong", "Zai Jian Bo Tong")
        * Audio: OPUS (recommended), PCM
        * Video: H.264 / JPEG (vision mode)
        * Network: WiFi STA, BLE provisioning (BK App)
        * AI: Agora RTC + Conversational AI Agent (text / vision modes)
        * Factory config, OTA, battery monitor, low-battery alarm, etc.

1.3 Keys
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1.3.1 V1 board key notes (must read)
+++++++++++++++++++++++++++++++++++++++

The V1 board has the following hardware quirks for keys; please be aware before reporting them as software defects:

- **Original design**: 4 ADC keys, each independently supports short-press / double-click / long-press.
- **V1 actual**: only one ADC pair works correctly due to a wiring error. The other pair is replaced by a single GPIO key whose **short-press** and **long-press** cover the missing roles.
- **Result**: only 3 of the 4 physical keys are functional on V1. This is a known HW issue and not a software defect.

So on V1, the **functional keys are S2 / S4 / S5**.

1.3.2 Key map
+++++++++++++++++++++++++++++++++

+--------------+---------------+----------------------+--------------------------------------------+
| Key          | Action        | Function             | Expected feedback                          |
+==============+===============+======================+============================================+
| S4           | Short press   | Confirm / next page  | Highlight is confirmed; UI navigates       |
+--------------+---------------+----------------------+--------------------------------------------+
| S4           | Double click  | Back / previous page | UI returns to previous menu; page resources|
|              |               |                      | are released                               |
+--------------+---------------+----------------------+--------------------------------------------+
| S5           | Short press   | Previous / focus<-   | Highlight moves to previous button         |
+--------------+---------------+----------------------+--------------------------------------------+
| S2           | Short press   | Next / focus->       | Highlight moves to next button             |
+--------------+---------------+----------------------+--------------------------------------------+
| S3 (V1 dead) | Any           | None (HW issue)      | No feedback; not a defect                  |
+--------------+---------------+----------------------+--------------------------------------------+
| S1           | Short press   | Reset (CEN)          | Hard reset                                 |
+--------------+---------------+----------------------+--------------------------------------------+

1.3.3 Key development notes
+++++++++++++++++++++++++++++++++

    1. GPIO keys

        - Configure keys in ``components/bk_key_app/key_app_config.h`` and ``key_app_service.c`` (IO pin + callback event mapping).
        - Long-press duration is set by ``LONG_TICKS`` in ``multi_button.h``.
        - All key events run in a task; blocking or slow callbacks delay key responses.

    2. GPIO key caveats

        - Make sure the GPIO is dedicated to the key; sharing the GPIO will cause the key to misbehave.
        - On a board with a different pinout, reconfigure the GPIO accordingly. See ``bk_avdk_smp/ap/docs/bk7259/en/api-reference/peripheral/bk_gpio.rst`` for GPIO API.
        - The bridge from key events to LVGL focus / navigation is at ``projects/beken_robot/ap/src/ui_key_bridge.c`` and ``ui_nav_router.c``.

1.4 LVGL navigation flow
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The 10 demo pages connect as: boot → main menu → (provisioning / examples submenu) → individual demo pages.

- ``page_1`` boot logo (blue BekenCorp logo + "BK7259 robot solution")
- ``page_2`` main menu ([Provisioning] / [Examples] / WiFi status icon in upper-right)
- ``page_3`` examples submenu (AI Chat / Vision / Command words / Face track / Volume / Sound source / Music)
- ``page_4`` BLE provisioning ([Start] / [Delete] / [Factory reset])
- ``page_5`` sound source localization (lv_arc ring + central eyes + smile)
- ``page_6`` AI dialog (connecting animation, Agora text mode)
- ``page_7`` vision recognition (viewfinder / scan line / REC dot, Agora vision mode)
- ``page_8`` command word recognition (enabled once command-word training is complete)
- ``page_9`` music player ([Play] / [Stop] / [Next])
- ``page_10`` volume settings (slider bound to ``audio_engine_volume_*``)

Source files:

- Auto-generated page skeletons: ``projects/beken_robot/ap/beken_generated/page_N_init.c``, ``beken_ui.c/.h``, ``event_runtime.c/.h``, ``basic_callback.c``.
- Custom business code: ``projects/beken_robot/ap/src/`` — ``ui_nav_router.c``, ``ui_key_bridge.c``, ``page_chat_anim.c``, ``page_5_eyes.c``, ``wifi_status_ui.c``.

1.5 Wake words
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    1. ``Ni Hao Bo Tong`` ("hello Beken"): wake-up. The local engine fires ``APP_EVT_ASR_NIHAOBOTONG``; the UI transitions to LISTENING / SPEAKING and starts cloud dialog.
    2. ``Zai Jian Bo Tong`` ("goodbye Beken"): end the dialog. The local engine fires ``APP_EVT_ASR_ZAIJIANBOTONG``; the UI returns to idle.

1.6 Prompt tones
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The board plays prompt tones on key events:

    Provisioning:
        - Start BLE provisioning: "please use Bluetooth to provision"
        - Failure: "Bluetooth provisioning failed, please retry"
        - Success: "Bluetooth provisioning succeeded"

    Network:
        - Connecting: "connecting to network, please wait"
        - Failed: "network connection failed, please check"
        - Connected: "network connection succeeded"

    AI Agent:
        - Connected: "AI agent connected"
        - Disconnected: "AI agent disconnected"

    Disconnect:
        - "device disconnected"

    Battery:
        - "battery low, please charge"

Default prompt tone resource path: ``<armino_sdk_resource>/bk_avdk_smp/ap/components/ai_audio_engine/resource/``.
For integration details, see the `Audio component developer guide <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7259/en/v4.0.1/developer-guide/audio/voice_service/index.html>`_.

2. Project usage
---------------------------------

2.1 Source download, build and flash
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * Refer to :doc:`../../get-started/index` for the download / build / flash sections.

2.2 BK App
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    Download: https://docs.bekencorp.com/arminodoc/bk_app/app/en/v2.0.1/app_download/index.html

    Sign in with email.

2.3 Provisioning (BLE)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

2.3.1 First-time provisioning
+++++++++++++++++++++++++++++++++

1. From the main menu focus **Provisioning** and short-press *Confirm* to enter the BLE provisioning page (page_4).
2. Focus **Start** and short-press *Confirm*; the log prints ``Start provisioning (short press S4 to trigger)`` and the device starts BLE advertising.
3. The BK App scans the device (``BK_xxxxxx``) within 5 seconds → choose the WiFi SSID → enter password → press *Start provisioning*.
4. Within 60 seconds the device joins WiFi; the log prints ``PROVISIONING_SUCCEED`` and the WiFi icon appears in the upper-right corner of the main menu.
5. On failure the BK App reports the cause (wrong password / SSID missing / weak signal); the device should exit provisioning gracefully.

2.3.2 Re-provisioning
+++++++++++++++++++++++++++++++++

.. warning::

    Before re-provisioning, remove the device from the BK App, then repeat 2.3.1.

2.4 AI dialog and vision Q&A
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- Enter ``page_6`` (AI dialog): the connecting animation shows CONNECTING → LISTENING; speaking transitions the state to SPEAKING / THINKING; the AI reply plays through the speaker; finally the state returns to LISTENING.
- Enter ``page_7`` (vision Q&A): state goes CONNECTING → ANALYZING; show an object to the camera and ask a question; the agent responds.
- Wake words: "Ni Hao Bo Tong" to start, "Zai Jian Bo Tong" to end.

3. AI development guide
---------------------------------

3.1 Module architecture
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    The device uses Agora RTC for duplex audio plus uplink video to the cloud AI Agent. The software stack is described in the *System Architecture* section of :doc:`../../intro/index`.

3.2 Key Kconfig options
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    Enable the Agora SDK on ``cpu0``:

    +----------------------------------------+----------------+---------------+----------------+
    | Kconfig                                |   CPU          |   Format      |      Value     |
    +----------------------------------------+----------------+---------------+----------------+
    | CONFIG_AGORA_IOT_SDK                   |   CPU0         |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+

    Enable BK provisioning + agent on ``cpu0``:

    +----------------------------------------+----------------+---------------+----------------+
    | Kconfig                                |   CPU          |   Format      |      Value     |
    +----------------------------------------+----------------+---------------+----------------+
    | CONFIG_BK_SMART_CONFIG                 |   CPU0         |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+

    Enable LVGL UI (MIPI 360x390) on ``cpu1`` / ``cpu2``:

    +----------------------------------------+----------------+---------------+----------------+
    | Kconfig                                |   CPU          |   Format      |      Value     |
    +----------------------------------------+----------------+---------------+----------------+
    | CONFIG_LVGL                            |   CPU1/2       |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+
    | CONFIG_LV_IMG_UTILITY_CUSTOMIZE        |   CPU1/2       |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+
    | CONFIG_LV_COLOR_DEPTH                  |   CPU1/2       |   int         |        16      |
    +----------------------------------------+----------------+---------------+----------------+
    | CONFIG_LV_COLOR_16_SWAP                |   CPU1/2       |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+

    Project Kconfig (``ap/Kconfig.projbuild``):

    +----------------------------------------+----------------+---------------+----------------+
    | Kconfig                                |   CPU          |   Format      |      Value     |
    +----------------------------------------+----------------+---------------+----------------+
    | CONFIG_LDO3V3_ENABLE                   |   AP           |   bool        | n / y          |
    +----------------------------------------+----------------+---------------+----------------+
    | CONFIG_HARDWARE_SPEAKER_VER            |   AP           |   int (0~8)   | 0              |
    +----------------------------------------+----------------+---------------+----------------+
    | CONFIG_ROBOT_TEST                      |   AP           |   bool        | y              |
    +----------------------------------------+----------------+---------------+----------------+

    With ``CONFIG_ROBOT_TEST=y``, the build defines ``ROBOT_TEST=1``, enabling the ``#if ROBOT_TEST`` blocks in ``page_N_init.c`` (navigation hooks, ``ui_nav_register_screen``, etc.) plus the ``page_5_api`` / ``page_5_eyes`` debug CLIs.

3.3 BLE provisioning and agent customization
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The provisioning and agent code lives mostly under ``components/bk_smart_config``. Customers can customize their own flow by referencing the following entry points.

Path: ``components/bk_smart_config/src/core/bk_smart_config_core.c``.

- **Enter BLE provisioning**: ``bk_sconf_prepare_for_smart_config(void)``.
- **BLE message handler from the phone app**: ``bk_sconf_ble_msg_handler(ble_prov_msg_t *msg)``.
- **Send agent params to the phone**: ``bk_sconf_send_agent_info(char *payload, uint16_t max_len)``.
- **Parse server-issued agent params**: ``bk_sconf_prase_agent_info(char *payload, uint8_t reset)``.
- **Start agent + RTC**: ``bk_sconf_start_network_transfer(char *device_id)``. With Agora as the backend, this calls ``bk_agora_start(device_id)`` (in ``components/network_engine/agora_rtc/agora_rtc_engine.c``) which starts both the RTC stream and the agent.
- **Post-provisioning hooks**: ``bk_sconf_network_provisioning_status_cb(...)`` saves WiFi / agent info and starts the agent after a successful join.
- **Multimodal switch (voice ↔ vision)**: ``bk_sconf_switch_ir_mode_handler(void)`` pairs ``video_engine_init() / video_engine_deinit()`` with ``bk_sconf_upate_agent_info(device_id, "vision"|"text")``.

4. Debug commands
---------------------------------

.. warning::

    These commands are intended for developers already familiar with the codebase. If you are not, please walk through :doc:`../../get-started/index` (power-up and provisioning) first.

4.1 Agora RTC commands
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Source: ``components/network_engine/agora_rtc/agora_rtc_engine.c``.

**Format**:

.. code:: bash

    agora_rtc <subcommand>

**Subcommands**:

    +----------------+--------+-------------------------------------------------+
    | subcommand     | type   | description                                     |
    +----------------+--------+-------------------------------------------------+
    | start          | basic  | start RTC + agent                               |
    +----------------+--------+-------------------------------------------------+
    | stop           | basic  | stop RTC + agent                                |
    +----------------+--------+-------------------------------------------------+
    | start_agora    | extra  | start RTC only                                  |
    +----------------+--------+-------------------------------------------------+
    | stop_agora     | extra  | stop RTC only                                   |
    +----------------+--------+-------------------------------------------------+
    | start_agent    | extra  | start agent only                                |
    +----------------+--------+-------------------------------------------------+
    | stop_agent     | extra  | stop agent only                                 |
    +----------------+--------+-------------------------------------------------+

**Notes**:

- The command derives ``device_id`` from the device's UID (24-byte hex string).
- ``start`` / ``stop`` toggle RTC and agent together.
- Make sure ``AGORA_APPID`` and ``AGORA_RESTFUL_TOKEN`` are configured and the PC-side AI Agent is running before using these commands.

4.2 LVGL debug CLIs (with CONFIG_ROBOT_TEST=y)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- ``arrow``: manually set the arrow angle on ``page_5`` (0=right, 90=down, 180=left, 270=up).
- ``eyes`` subcommands ``blink`` / ``gaze`` / ``show``: drive the central eyes + smile on ``page_5``.

Source: ``projects/beken_robot/ap/include/page_5_api.h`` and ``projects/beken_robot/ap/src/page_5_eyes.c``.

5. FAQ
---------------------------------

Q: No mic data is reported to the application layer?

A: The project enables wake-word ASR by default; mic samples are forwarded only after wake-up. To disable the wake-word path entirely, turn off ``CONFIG_AE_SUPPORT_PROMPT_TONE``.

Q: Pressing S3 does nothing?

A: Known V1 hardware issue (wiring error). Please use S2 / S4 / S5.

Q: ``page_6`` / ``page_7`` is stuck in CONNECTING?

A: Make sure the device has been provisioned (WiFi icon is shown on the main menu); make sure ``AGORA_APPID`` / ``AGORA_RESTFUL_TOKEN`` are set and the PC agent is running; finally, check that the AP can reach the internet.

Q: Which 4G Cat.1 modules over USB are supported?

A: Fibocom LE270/370, Air780E (Hezhou), Quectel EC-800M, Mokoo I511, etc. See the `BK Modem documentation <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/developer-guide/peripheral/bk_modem.html>`_ for adaptation steps.
