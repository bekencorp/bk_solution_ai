Beken Genie VolcEngine RTC Version
==================================

:link_to_translation:`zh_CN:[中文]`

**1. Introduction**
--------------------

    * This project is based on an end-to-cloud, cloud-to-large-model design solution.
    * Supports dual-screen display, providing visual and voice companionship experience and emotional value.
    * Supports end-side integration with various general large model design solutions, directly connecting to Open AI, Doubao, DeepSeek, etc.
    * And can effectively utilize cloud distributed deployment to reduce network latency and improve interaction experience.
    * Supports end-side AEC, NS and other audio processing algorithms, supports G711/G722 encoding formats, supports KWS keyword interruption wake-up, supports prompt tone playback.
    * Includes reference designs and demos for common peripherals, such as gyroscope, NFC, buttons, vibration motor, Nand Flash, LED effects, charging management, DVP camera, dual QPSI screens.
    * **This project uses VolcEngine RTC**


**1.1 Hardware Schematics**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

   * `AI Toy Development Board Schematic <https://docs.bekencorp.com/HW/BK7258/AIDK_AI%E7%8E%A9%E5%85%B7%E5%BC%80%E5%8F%91%E6%9D%BF_%E5%8E%9F%E7%90%86%E5%9B%BE.pdf>`_
   * `AI Toy Development Board_Bottom Layer Reference <https://docs.bekencorp.com/HW/BK7258/AIDK_AI%E7%8E%A9%E5%85%B7%E5%BC%80%E5%8F%91%E6%9D%BF_%E5%BA%95%E5%B1%82%E4%BD%8D%E5%8F%B7%E5%9B%BE.pdf>`_
   * `AI Toy Development Board_Top Layer Reference <https://docs.bekencorp.com/HW/BK7258/AIDK_AI%E7%8E%A9%E5%85%B7%E5%BC%80%E5%8F%91%E6%9D%BF_%E9%A1%B6%E5%B1%82%E4%BD%8D%E5%8F%B7%E5%9B%BE.pdf>`_

**1.2 Specifications**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * Hardware Configuration:
        * SPI LCD X2 (GC9D01 160x160)
        * Microphone
        * Speaker
        * SD NAND 128MB
        * NFC (MFRC522)
        * Gyroscope (SC7A20H)
        * Charging Management Chip (ETA3422)
        * Lithium Battery
        * DVP (gc2145)

    * Software Features:
        * AEC
        * NS
        * G722 / G711u
        * Wake Word Customization
        * WIFI Station
        * BLE
        * BT PAN

.. figure:: ../../../_static/beken_genie_pic.jpg
    :align: center
    :alt: Hardware Development Board
    :figclass: align-center

    Figure 1. Hardware Development Board

**1.3 Keys**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

There are three keys on the right side of the bottom of the development board, corresponding to silkscreen S1, S2, S3; one key K1 on the right side

Power On/Off
    - Power On: Long press (>=3 seconds) ``S2`` to power on
    - Power Off: When the system is in power-on state, long press (>=3 seconds) ``S2`` to power off

Network Provisioning
    - Network Provisioning: When the system is in power-on state, long press (>=3 seconds) ``S1`` to enter network provisioning waiting state

Speaker Volume Control
    - Increase Volume: Click ``S1`` button to increase volume
    - Decrease Volume: Click ``S3`` button to decrease volume

Factory Reset
    - Factory Reset: Long press ``S3`` button to factory reset

Reset Key K1
    - Power-off State Reset: Click ``K1`` button, system powers on from power-off state
    - Power-on State Reset: Click ``K1`` button, system hard resets from power-on state

For key function development, please refer to :doc:`../../thirdparty/volc/index`

**1.4 LED Effects**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

There are red and green status indicator lights on the top of the development board. Important information is indicated by red light blinking, general prompts by green light blinking, and special reminders by red and green lights alternating blinking.

    Green Light Always On/Off Prompt Information
        - When powering on, green light stays on, waiting for user operation or next event to start
        - When conversation starts, green light turns off

    Red and Green Lights Alternating Blink Prompt Information
        - User Network Provisioning: User network provisioning

    Green Light Blink Prompt Information
        - Power-on Network Connecting: Green light fast blink
        - Large Model Server Connection Success: Green light slow blink
        - Conversation Stopped: Green light slow blink

    Red Light Blink Prompt Information
        - Network Provisioning Failure/Network Reconnection Failure: Red light fast blink
        - RTC Connection Disconnected: Red light fast blink
        - Large Model Server Connection Disconnected: Red light fast blink
        - Battery Level Below 20%: Red light slow blink for 30 seconds then automatically stops blinking; if charging, red light does not blink
        - No Important Reminder Events: When there are no important reminder events, red light is in off state

LED effect development reference code: led_blink.c.

**1.5 SD-NAND Storage**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
    - SD-NAND stores local resource files, such as image resource files on the display screen
    - SD-NAND storage uses FAT32 file system by default, providing file access through VFS interface indirectly calling FATFS open source program interface
    - PC side, through USB interface, read and write access to development board SD-NAND files (USB interface on the left side of development board)
    - Please note that files deleted on PC side should not be used by local applications at the same time to prevent system exceptions

**1.6 Gyroscope-Gsensor**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
    - Local Gsensor supports system wake-up function. Users can shake the development board in an S-shaped trajectory to wake up the system

**1.7 Charging Management**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
    - 1. The current development board uses charging management chip model (ETA3422)
    - 2. When fully charged, the red light next to the charging port will turn off and green light will turn on; red light on indicates charging
    - 3. Note: During charging or when external power is connected, the system switches to external input voltage source for voltage detection instead of using battery voltage. At this time, the voltage obtained by command is the external input voltage.
    - 4. Charging status monitoring depends on GPIO51 and GPIO26.
        - GPIO51 is responsible for detecting whether it is charging state. When GPIO51 is high, there is external power supply input, otherwise there is not.
        - When GPIO26 is high, it indicates the battery is charging. When low, it indicates the battery is fully charged.
        - Note: This function requires confirmation that R14 resistor is soldered on hardware. If not soldered, additional soldering is required.
        - Specific hardware information is subject to the project schematic.

    - 5. To enable charging management function, configure CONFIG_BAT_MONITOR=y. To enable charging management test cases, configure CONFIG_BATTERY_TEST=y.
    - 6. After enabling battery test command configuration, battery information can be obtained through battery command.
        - For example, "battery init" can initialize the battery monitoring task.
        - "battery get_battery_info" can view basic battery information.
        - "battery get_voltage" can view the current voltage value of the battery.
        - "battery get_level" can view the current battery level.
        - When using the above commands, note whether it is under external power supply, otherwise the detected voltage information is the external power supply voltage.
        - For other specific commands, you can just send "battery" to print related command support. For further information, please refer to the definition in cli_battery.c.

    - 7. When battery level is equal to or less than 20%, a low battery warning event will be sent.
        - Only triggered when not plugged in. External power supply or charging will not send low battery warning.
        - Only sent once each time entering low battery state.
        - At this time, the red light on the other side slow blinks for 30s.

    - 8. The charging management task will print "Device is charging..." information when charging.
    - 9. When fully charged, it will print "Battery is full." information.
    - 10. Although the battery has low voltage protection function, it is recommended that users charge in time when battery is low to extend battery life.
    - 11. If users use batteries from other manufacturers, they need to modify the battery level lookup table s_chargeLUT and battery basic information content in iot_battery_open according to specific battery information.
    - 12. In our SDK, we provide API functions for current, voltage, and battery level. Currently, the battery only supports voltage and battery level detection functions. It should be noted that although the current detection API interface is reserved, it is not yet implemented. Therefore, if the user's device supports current detection, the current API needs to be implemented by the user.
    - 13. Since the current hardware only supports battery level detection in non-charging state, if users need to detect voltage during charging, hardware modification is required. Remove D6 diode and R21 resistor.
    - 14. The USB port next to the key is both a charging port and a serial port.
    - 15. The ADC interface for battery level sampling is the internal ADC0 of the chip. External resistor voltage divider circuit plus ADC channel acquisition is not required. ADC0 is directly connected to the VBAT monitoring channel. This interface is a dedicated internal interface of the chip with no external connections.
    - 16. Note: The maximum battery detection voltage is 4.35V. Voltages above this may risk burning the system.

**1.8 ASR**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


    1. ``Hi Armino`` is used to wake up, enabling interaction between local and cloud AI, while the LCD lights up and displays eye animations.

        Response phrase: ``A Ha``

    2. ``byebye armino`` is used to turn off, enabling interaction between local and cloud AI, while closing the LCD and no longer displaying eye animations.

        Response phrase: ``Byebye``


**1.9 Motor**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
    - 1. LDO is connected to the positive pole of the motor, PWM is connected to the negative pole of the motor. The vibration strength of the motor is controlled by adjusting the PWM duty cycle.
    - 2. When powering on by long pressing the key, the motor will vibrate.
    - 3. For detailed PWM usage examples, see cli_pwm.c.

**1.10 Prompt Tone**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


    The development board plays corresponding prompt tones during operation based on different events. Below are the prompt tones associated with each event:

    Provision network Over Bluetooth LE
        - 1.Provision Network Over Bluetooth LE: ``Please use Bluetooth LE for network provision``
        - 2.Provision Network fail: ``Network provision over Bluetooth LE failed, please reprovision the network.``
        - 3.Provision Network success: ``Network provision over Bluetooth LE successed``

    Reconnect Network
        - 1.Connecting to Network: ``The network is connecting, please wait.``
        - 2.Reconnect Network fail: ``The network connection has failed, please check the network.``
        - 3.Reconnect Network success: ``The network connection successful.``

    Wake-Up and Shutdown
        - 1.Wake-Up: ``A Ha``
        - 2.Shutdown: ``Byebye``

    AI Entity
        - 1.AI Entity Connected Successfully: ``AI entity has been connected``
        - 2.AI Entity Disconnected: ``AI entity has been disconnected``

    Device Disconnection
        - 1.Device Disconnected: ``Device disconnected``

    Battery Level
        - 1.Low Battery: ``Battery level is low. Please charge.``



    Please refer to documentation: `Audio Component Development Guide <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.1.1/developer-guide/audio/voice_service/index.html>`_
    Default prompt tone file resource path: ``/projects/common_componets/resource/``

**1.11 Countdown**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
    - Network provisioning event countdown is 5 minutes. If not provisioned within 5 minutes, the chip will enter deep sleep (similar to power off).
    - Network error countdown is 5 minutes. If network error occurs and network is not restored within 5 minutes, the chip will enter deep sleep.
    - Standby state countdown is 3 minutes. The default state after system power-on is standby state. After you say ``byebye armino`` or ``拜拜阿米诺``, the system will also be in standby state. If no other events occur, the chip will enter deep sleep after 3 minutes.
    - You can modify the countdown time in the s_ticket_durations[COUNTDOWN_TICKET_MAX] array in countdown_app.c.

**1.12 VolcEngine RTC**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

For VolcEngine RTC, please refer to documentation :doc:`../../thirdparty/volc/index`


**2. Project Usage Introduction**
-----------------------------------

**2.1 Code Download, Compilation, and Flashing**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * `Please refer to Quick Start chapter <../get-started/index.html>`_

**2.2 APP Registration and Download**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    APP Download: https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_download/index.html

    Registration and Login: Use email to register and login


**2.3 Resource File Flashing**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

2.3.1 Copy Resource Files
+++++++++++++++++++++++++++++++++

Copy audio and video files from ``<armino_sdk_resource>/bk_avdk_smp/ap/components/ai_audio_engine/resource/`` directory to SD NAND of AIDK development board.
For specific usage of SD NAND, please refer to `Nand Disk Usage Notes <../../api-reference/nand_disk_note.html>`_

2.3.2 Power On
+++++++++++++++++++++++++++++++++

Flash the compiled all-app.bin file and power on to execute.


**2.4 Network Provisioning**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

2.4.1 First-time Network Provisioning
++++++++++++++++++++++++++++++++++++++++++++++ 

    a) Enter the following interface on the phone and follow the picture steps

    .. figure:: ../../../_static/add_device_en.png
        :scale: 30%

    .. figure:: ../../../_static/add_devcie_ai_toy_en.png
        :scale: 30%

    .. figure:: ../../../_static/device_info_en.png
        :scale: 30%

    b) After the phone starts BLE scanning, long press the network provisioning key shown below for 3s, the board enters network provisioning mode

    .. figure:: ../../../_static/add_ai_device_8.png
        :scale: 70%

    c) The phone scans the following device, click the device to start network provisioning


    .. figure:: ../../../_static/ble_scan_en.png
        :scale: 30%

    .. figure:: ../../../_static/select_model_en.png
        :scale: 30%

    .. figure:: ../../../_static/ai_activate_type_en.png
        :scale: 30%

    .. figure:: ../../../_static/wifi_select_en.png
        :scale: 30%

    .. figure:: ../../../_static/activating_en.png
        :scale: 30%

    .. figure:: ../../../_static/added_en.png
        :scale: 30%



2.4.1 Re-provisioning
+++++++++++++++++++++++++++++++++

.. warning::

    Before re-provisioning, you need to remove the device from the phone that originally provisioned it, then repeat the operations in the above section.

    Device removal method is as follows:

..

    a) Long press the area shown in the figure, a prompt box will pop up.

    .. figure:: ../../../_static/added_en.png
        :scale: 30%

    b) Click confirm to complete the operation.

    .. figure:: ../../../_static/del_en.png
        :scale: 30%

.. note::

    For more APP operations, please refer to APP documentation:

    https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_usage/app_usage_guide/index.html#ai


**2.5 AI Conversation**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    Say the wake-up word ``hi armino`` to the onboard mic, the device will play the prompt tone ``aha`` after waking up,
    and then you can have an AI conversation

    Say the key word ``byebye armino`` to the onboard mic, the device will play the prompt tone ``byebye`` after detecting it,
    then go to sleep and stop talking to the AI


**3. AI Engineering Guide**
----------------------------

**3.1 Module Architecture Diagram**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

This AI demo solution is similar to the door lock solution. The device side and AI large model side have bidirectional voice calls, while the device side sends one-way image transmission to the AI large model side. The peer APK in the door lock solution becomes an AI Agent robot.
The software module architecture is shown in the following figure:

.. figure:: ../../../_static/sw_arch_beken_genie_volc_en.png
    :align: center
    :alt: module architecture Overview
    :figclass: align-center

    Figure 2. software module architecture

..

    * In the solution, the device side captures mic voice and sends voice data to the server through VolcEngine RTC SDK. The server is responsible for interaction with AI Agent large model, sends mic voice to AI Agent and gets reply, then sends voice reply to device side speaker for playback.
    * In the solution, the device side captures images and sends each frame image to the server through VolcEngine RTC SDK. The server then sends the image to AI Agent large model for recognition.


3.2 Network Provisioning and Conversation Sequence Diagram
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. figure:: ../../../_static/volc_demo_flow_sequence.png
    :align: center
    :alt: State Machine Overview
    :figclass: align-center

    Figure 3. Operation Flow Sequence


**3.3 Working State Machine**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. figure:: ../../../_static/bk_genie_statemachine.png
    :align: center
    :alt: State Machine Overview
    :figclass: align-center

    Figure 4. module state diagram

::

    1/2 Green light stays on.
    3/4 Green and red lights flash alternately
    5/6 Green light flashes quickly.
    7 Green light flashes quickly.
    8 LCD on, LED off.
    9 LCD off
    12/13 Red light flashes quickly


**3.4 Main Configuration**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    To enable VolcEngine RTC function library, the following configuration needs to be enabled on ``cpu0``:

    +----------------------------------------+----------------+---------------+----------------+
    |Kconfig                                 |   CPU          |   Format      |      Value     |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_VOLC_RTC_EN                      |   CPU0         |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+

    To enable Beken network provisioning and agent startup, the following configuration needs to be enabled on ``cpu0``:

    +----------------------------------------+----------------+---------------+----------------+
    |Kconfig                                 |   CPU          |   Format      |      Value     |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_BK_SMART_CONFIG                  |   CPU0         |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+

    To enable dual-screen display and AVI playback, the following configuration needs to be enabled:

    +----------------------------------------+----------------+---------------+----------------+
    |Kconfig                                 |   CPU          |   Format      |      Value     |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_LCD_SPI_GC9D01                   |   CPU1         |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_LCD_SPI_DEVICE_NUM               |   CPU1         |   int         |        2       |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_AVI_PLAY                         |   CPU1         |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_DUAL_SCREEN_AVI_PLAY             |   CPU0 & CPU1  |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_LVGL                             |   CPU1         |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_LV_IMG_UTILITY_CUSTOMIZE         |   CPU1         |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_LV_COLOR_DEPTH                   |   CPU1         |   int         |        16      |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_LV_COLOR_16_SWAP                 |   CPU1         |   bool        |        y       |
    +----------------------------------------+----------------+---------------+----------------+


**3.5 Key Development Instructions**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
GPIO Key Usage Instructions
    - Key function configuration, refer to ``/components/bk_key_app/key_app_config.h`` and ``key_app_service.c``. Developers can fill in the corresponding IO pins and key callback function events in this table
    - Long key press duration configuration refers to LONG_TICKS macro definition in multi_button.h
    - Currently all key events are transferred to tasks for execution. If key event execution program is blocked or execution time is too long, it will affect key response speed

GPIO Key Notes
    - Please confirm that GPIO pins are only used for keys, otherwise GPIO pin function conflicts will cause key invalidation problems
    - If the developer's development board is different from the beken_genie development board, please reconfigure GPIO according to the development board hardware design. For GPIO usage methods, please refer to ``bk_avdk_smp/ap/docs/bk7258/zh_CN/api-reference/peripheral/bk_gpio.rst``


**3.6 BLE Network Provisioning and Agent Customization Guide**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

BLE network provisioning and agent related code is mainly distributed in ``components/bk_smart_config`` directory. Customers can refer to the following instructions to customize their own solutions

Core code is as follows:

Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

 - **Enter BLE Network Provisioning Mode**, please refer to ``bk_sconf_prepare_for_smart_config(void)`` implementation

   Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

 - **Mobile app interacts with network provisioning information through BLE**, refer to code ``bk_sconf_ble_msg_handler(ble_prov_msg_t *msg)``

   Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

 - **Send Agent configuration parameters to mobile app**, refer to code ``bk_sconf_send_agent_info(char *payload, uint16_t max_len)``

   Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

 - **Parse server startup Agent parameters**, refer to code ``bk_sconf_prase_agent_info(char *payload, uint8_t reset)``

   Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

 - **Start Agent and RTC**, refer to code ``bk_sconf_start_network_transfer(char *device_id)``

   This function will call the corresponding startup function based on the configured RTC backend (Agora or VolcEngine). For VolcEngine, it will call ``bk_byte_start(void *device_id)``, which will start both Agent and RTC.

   Code paths:
   - ``bk_sconf_start_network_transfer``: components/bk_smart_config/src/core/bk_smart_config_core.c
   - ``bk_byte_start``: components/network_transfer/volc_rtc/bk_volc_api.c

 - **After WiFi connection, start agent, save WiFi and agent information and network provisioning**, refer to code ``bk_sconf_network_provisioning_status_cb(bk_network_provisioning_status_t status, void *user_data)``

   Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

 - **Save (to flash)/erase/get channel_name information related function interfaces**, refer to code:

   - ``bk_sconf_save_channel_name`` / ``bk_sconf_get_channel_name``: /components/bk_smart_config/src/core/bk_smart_config_core.c
   - ``bk_sconf_erase_channel_name``: Related implementation is located in bk_smart_config module

 - **Key switch multimodal**, refer to code ``bk_sconf_switch_ir_mode_handler(void)``

   Code path: components/bk_smart_config/src/core/bk_smart_config_core.c


**3.7 VolcEngine Related Function Development**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
For VolcEngine related function development, please refer to `VolcEngine RTC Functions <../../thirdparty/volc/index.html#id6>`_


4. Questions & Answers
------------------------

Q: Application layer has no mic data reporting?
A: Currently beken_genie supports voice wake-up function based on command words by default. Only after wake-up will mic captured data be reported to the application layer, and the application layer then sends data to AI for conversation. If customers do not need voice wake-up function, they can disable this function through macro ``CONFIG_AE_SUPPORT_PROMPT_TONE``.


Q: UI resource playback display abnormal?
A: UI resources used in this project must be AVI format videos with resolution of 320x160, and must be converted using AVI conversion tool before use. You can first check whether UI resources meet the above requirements.

