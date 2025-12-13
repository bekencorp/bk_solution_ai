Beken Genie AI
=================================

:link_to_translation:`zh_CN:[中文]`

1. Introduction
----------------

    This project is based on an end-to-cloud, cloud-to-large-model design solution.

    Supports dual-screen display, providing visual and voice companionship experience and emotional value.

    Supports end-side integration with various general large model design solutions, directly connecting to Open AI, Doubao, DeepSeek, etc.

    And can effectively utilize cloud distributed deployment to reduce network latency and improve interaction experience.

    Supports end-side AEC, NS and other audio processing algorithms, supports G711/G722 encoding formats, supports KWS keyword interruption wake-up, supports prompt tone playback.

    Includes reference designs and demos for common peripherals, such as gyroscope, NFC, buttons, vibration motor, Nand Flash, LED effects, charging management, DVP camera, dual QPSI screens.


1.1 Hardware Schematics
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

   * `AI Toy Development Board Schematic <https://docs.bekencorp.com/HW/BK7258/AIDK_AI%E7%8E%A9%E5%85%B7%E5%BC%80%E5%8F%91%E6%9D%BF_%E5%8E%9F%E7%90%86%E5%9B%BE.pdf>`_
   * `AI Toy Development Board_Bottom Layer Reference <https://docs.bekencorp.com/HW/BK7258/AIDK_AI%E7%8E%A9%E5%85%B7%E5%BC%80%E5%8F%91%E6%9D%BF_%E5%BA%95%E5%B1%82%E4%BD%8D%E5%8F%B7%E5%9B%BE.pdf>`_
   * `AI Toy Development Board_Top Layer Reference <https://docs.bekencorp.com/HW/BK7258/AIDK_AI%E7%8E%A9%E5%85%B7%E5%BC%80%E5%8F%91%E6%9D%BF_%E9%A1%B6%E5%B1%82%E4%BD%8D%E5%8F%B7%E5%9B%BE.pdf>`_

1.2 Specifications
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

1.3 Keys
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
1.3.1 Key Function Description
+++++++++++++++++++++++++++++++++

There are three keys on the right side of the bottom of the development board, corresponding to silkscreen S1, S2, S3; one key K1 on the right side

    Power On/Off
        - 1. Power On: Long press (>=3 seconds) ``S2`` to power on
        - 2. Power Off: When the system is in power-on state, long press (>=3 seconds) ``S2`` to power off

    Multimodal Switch
        - 1. When the system is first powered on, it uses large language model by default
        - 2. When the system is in wake-up state, click ``S2`` to switch to image recognition large model
        - 3. When the system is in wake-up state, click ``S2`` again to switch to large language model
        - 4. If the system is not powered off and is in wake-up state, repeatedly click ``S2`` to repeat steps 2 and 3

    Network Provisioning
        - 1. Network Provisioning: When the system is in power-on state, long press (>=3 seconds) ``S1`` to enter network provisioning waiting state

    Speaker Volume Control
        - 1. Increase Volume: Click ``S1`` button to increase volume
        - 2. Decrease Volume: Click ``S3`` button to decrease volume

    Factory Reset
        - 1. Factory Reset: Long press ``S3`` button to factory reset

    Reset Key K1
        - 1. Power-off State Reset: Click ``K1`` button, system powers on from power-off state
        - 2. Power-on State Reset: Click ``K1`` button, system hard resets from power-on state

1.3.2 Key Development Instructions
++++++++++++++++++++++++++++++++++
    1. GPIO Keys
        - Key function configuration, refer to projects /components/bk_key_app/key_app_config.h and key_app_service.c. Developers can fill in the corresponding IO pins and key callback function events in this table
        - Long key press duration configuration refers to LONG_TICKS macro definition in multi_button.h
        - Currently all key events are transferred to tasks for execution. If key event execution program is blocked or execution time is too long, it will affect key response speed
    2. GPIO Key Notes
        - Please confirm that GPIO pins are only used for keys, otherwise GPIO pin function conflicts will cause key invalidation problems
        - If the developer's development board is different from the beken_genie development board, please reconfigure GPIO according to the development board hardware design. For GPIO usage methods, please refer to bk_avdk_smp/ap/docs/bk7258/zh_CN/api-reference/peripheral/bk_gpio.rst.


1.4 LED Effects
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
There are red and green status indicator lights on the top of the development board. Important information is indicated by red light blinking, general prompts by green light blinking, and special reminders by red and green lights alternating blinking.
LED effect development reference code: led_blink.c.

    Green Light Always On/Off Prompt Information
        - 1. When powering on, green light stays on, waiting for user operation or next event to start
        - 2. When conversation starts, green light turns off

    Red and Green Lights Alternating Blink Prompt Information
        - 1. User Network Provisioning: User network provisioning

    Green Light Blink Prompt Information
        - 1. Power-on Network Connecting: Green light fast blink
        - 2. Large Model Server Connection Success: Green light slow blink
        - 3. Conversation Stopped: Green light slow blink

    Red Light Blink Prompt Information
        - 1. Network Provisioning Failure/Network Reconnection Failure: Red light fast blink.
        - 2. WEBRTC Connection Disconnected: Red light fast blink.
        - 3. Large Model Server Connection Disconnected: Red light fast blink.
        - 4. Battery Level Below 20%: Red light slow blink for 30 seconds then automatically stops blinking; if charging, red light does not blink.
        - 5. No Important Reminder Events: When there are no important reminder events, red light is in off state.

1.5 SD-NAND Storage
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
        - 1. SD-NAND stores local resource files, such as image resource files on the display screen..
        - 2. SD-NAND storage uses FAT32 file system by default, providing file access through VFS interface indirectly calling FATFS open source program interface.
        - 3. PC side, through USB interface, read and write access to development board SD-NAND files (USB interface on the left side of development board).
        - 4. Please note that files deleted on PC side should not be used by local applications at the same time to prevent system exceptions.

1.6 Gyroscope-Gsensor
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
        - 1. Local Gsensor supports system wake-up function. Users can shake the development board in an S-shaped trajectory to wake up the system

1.7 Charging Management
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
        - 1. The current development board uses charging management chip model (ETA3422)
        - 2. When fully charged, the red light next to the charging port will turn off and green light will turn on; red light on indicates charging
        - 3. Note: During charging or when external power is connected, the system switches to external input voltage source for voltage detection instead of using battery voltage. At this time, the voltage obtained by command is the external input voltage.
        - 4. Charging status monitoring depends on GPIO51 and GPIO26.
            - GPIO51 is responsible for detecting whether it is charging state. When GPIO51 is high, there is external power supply input, otherwise there is not.
            - When GPIO26 is high, it indicates the battery is charging. When low, it indicates the battery is fully charged.
            - Note: This function requires confirmation that R14 resistor is soldered on hardware. If not soldered, additional soldering is required.

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
        - 13. Since the current hardware only supports battery level detection in non-charging state, if users need to detect voltage during charging, hardware modification is required.
            - Only remove D6 diode and R21 resistor.

        - 14. The USB port next to the key is both a charging port and a serial port.
        - 15. The ADC interface for battery level sampling is the internal ADC0 of the chip. External resistor voltage divider circuit plus ADC channel acquisition is not required. ADC0 is directly connected to the VBAT monitoring channel. This interface is a dedicated internal interface of the chip with no external connections.
        - 16. Note: The maximum battery detection voltage is 4.35V. Voltages above this may risk burning the system.

1.8 ASR
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    1. ``Hi Armino`` is used to wake up, enabling interaction between local and cloud AI, while the LCD lights up and displays eye animations.

        Response phrase: ``A Ha``

    2. ``byebye armino`` is used to turn off, enabling interaction between local and cloud AI, while closing the LCD and no longer displaying eye animations.

        Response phrase: ``Byebye``

1.9 MOTOR
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
        - 1. LDO is connected to the positive pole of the motor, PWM is connected to the negative pole of the motor. The vibration strength of the motor is controlled by adjusting the PWM duty cycle.
        - 2. When powering on by long pressing the key, the motor will vibrate.
        - 3. For detailed PWM usage examples, see cli_pwm.c.

1.10 Prompt Tones
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
1.10.1 Prompt Tone Function Description
+++++++++++++++++++++++++++++++++++++++++

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


1.10.2 Prompt Tone Development Guide
+++++++++++++++++++++++++++++++++++++

    Please refer to documentation: `Audio Component Development Guide <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/ap_doc/bk7258/zh_CN/v3.1.1/developer-guide/audio/voice_service/index.html>`_

    Prompt tone file resources are currently stored in the armino_sdk_resource repository on git. Customers can download them themselves.

    Default prompt tone file resource path: ``<armino_sdk_resource>/bk_avdk_smp/ap/components/ai_audio_engine/resource/``
    
    Download method::

        git clone "ssh://$USER@192.168.0.46:29418/armino_sdk_resource/bk_avdk_smp" && scp -p -P 29418 $USER@192.168.0.46:hooks/commit-msg "bk_avdk_smp/.git/hooks/"

1.11 Countdown
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
        - 1. Network provisioning event countdown is 5 minutes. If not provisioned within 5 minutes, the chip will enter deep sleep (similar to power off).
        - 2. Network error countdown is 5 minutes. If network error occurs and network is not restored within 5 minutes, the chip will enter deep sleep.
        - 3. Standby state countdown is 3 minutes. The default state after system power-on is standby state. After you say ``byebye armino`` or ``拜拜阿米诺``, the system will also be in standby state. If no other events occur, the chip will enter deep sleep after 3 minutes.
        - 4. You can modify the countdown time in the s_ticket_durations[COUNTDOWN_TICKET_MAX] array in countdown_app.c.

2. AI Engineering Guide
------------------------

2.1 Module Architecture Diagram
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,


    This AI demo solution is similar to the door lock solution. The device side and AI large model side have bidirectional voice calls, while the device side sends one-way image transmission to the AI large model side. The peer APK in the door lock solution becomes an AI Agent robot.
	The software module architecture is shown in the following figure:

.. figure:: ../../../_static/agora_wanson_ai_arch.png
    :align: center
    :alt: module architecture Overview
    :figclass: align-center

    Figure 2. software module architecture

..

    * In the solution, the device side captures mic voice and sends voice data to Agora server through Agora SDK. Agora server is responsible for interaction with AI Agent large model, sends mic voice to AI Agent and gets reply, then sends voice reply to device side speaker for playback.
    * In the solution, the device side captures images and sends each frame image to Agora server through Agora SDK. Agora server then sends the image to AI Agent large model for recognition.


2.2 Network Provisioning and Conversation Sequence Diagram
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

.. figure:: ../../../_static/demo_flow_sequence_en.png
    :align: center
    :alt: State Machine Overview
    :figclass: align-center

    Figure 3. Operation Flow Sequence


2.3 Working State Machine
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


2.4 Main Configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    To enable Agora function library, the following configuration needs to be enabled on ``cpu0``:

    +----------------------------------------+----------------+---------------+----------------+
    |Kconfig                                 |   CPU          |   Format      |      Value     |
    +----------------------------------------+----------------+---------------+----------------+
    |CONFIG_AGORA_IOT_SDK                    |   CPU0         |   bool        |        y       |
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

2.5 BLE Network Provisioning and Agent Customization Guide
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

 BLE network provisioning and agent related code is mainly distributed in components/bk_smart_config directory. Customers can refer to the following instructions to customize their own solutions

1. bk_sconf_prepare_for_smart_config Enter BLE Network Provisioning Mode

  Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

.. code::

    void bk_sconf_prepare_for_smart_config(void)
    {
        int ret = 0;
        char device_id[128] = {0};

        smart_config_running = true;

        ret = bk_sconf_get_channel_name(device_id);
        if ((ret == 0) && (os_strlen(device_id) > 0)) {
            #if CONFIG_BK_NETWORK_TRANSFER
            ntwk_trans_stop(device_id);
            #endif
        }

        bk_wifi_sta_stop();

    #if CONFIG_BK_MODEM
        bk_modem_deinit();
    #endif

    #if CONFIG_NET_PAN
        bk_bt_enter_pairing_mode(1);
    #endif

        bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
    }


2. bk_sconf_ble_msg_handler is responsible for interacting with mobile app through BLE for network provisioning information. Customers can disable the following code and use their own solution

  Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

.. code::

    void bk_sconf_ble_msg_handler(ble_prov_msg_t *msg)
    {
        LOGI("bk_sconf_ble_msg_handler, event:%d\n", msg->event);
        switch (msg->event)
        {
            case BOARDING_OP_STATION_START:
            {
                bk_ble_provisioning_info_t *bk_ble_provisioning_info = bk_ble_provisioning_get_boarding_info();
                bk_sconf_wifi_sta_connect(bk_ble_provisioning_info->ble_prov_info.ssid_value,
                                            bk_ble_provisioning_info->ble_prov_info.password_value);
                // ... other processing
            }
            break;
            // ... other event handling
            case BOARDING_OP_AGENT_RSP:
            {
                LOGI("BOARDING_OP_AGENT_RSP\n");
                //Receive channel name and other information assigned by server. If customers build their own server, they can not use this code, or refer to section 4 of this subsection to modify bk_sconf_prase_agent_info implementation
                bk_sconf_prase_agent_info((char *)msg->param, 1);
            }
            break;
            // ... other event handling
        }
    }


3. bk_sconf_send_agent_info is responsible for sending agent configuration parameters to APK during network provisioning stage

  Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

.. code::

    static uint16_t bk_sconf_send_agent_info(char *payload, uint16_t max_len)
    {
        unsigned char uid[32] = {0};
        char uid_str[65] = {0};
        uint16 len = 0;

        bk_uid_get_data(uid);
        for (int i = 0; i < 24; i++)
        {
            sprintf(uid_str + i * 2, "%02x", uid[i]);
        }
        len = os_snprintf(payload, max_len, "{\"channel\":\"%s\"}", uid_str);
        BK_LOGI(TAG, "ori channel name:%s, %d\r\n", uid_str, len);
        return len;
    }


4. bk_sconf_prase_agent_info is responsible for parsing return parameters (such as channel_name, etc.) after server starts agent during network provisioning stage, used to start device-side RTC

  Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

.. code::

    void bk_sconf_prase_agent_info(char *payload, uint8_t reset)
    {
        cJSON *json = NULL;
        char *tmp_channel = NULL;

        json = cJSON_Parse(payload);
        if (!json)
        {
            BK_LOGE(TAG, "Error before: [%s]\n", cJSON_GetErrorPtr());
            return;
        }

        cJSON *channel_name = cJSON_GetObjectItem(json, "channel_name");
        if (channel_name && ((channel_name->type & 0xFF) == cJSON_String))
        {
            tmp_channel = channel_name->valuestring;
            BK_LOGI(TAG, "real channel name:%s\r\n", tmp_channel);
        }
        else
        {
            BK_LOGE(TAG, "[Error] not find msg\n");
        }

        LOGI("%s %d, bk_sconf_prase_agent_info, tmp_channel:%s\r\n", __func__, __LINE__, tmp_channel);
        bk_sconf_start_network_transfer(tmp_channel);
        bk_sconf_save_channel_name(tmp_channel);
        #if CONFIG_APP_EVT
        app_event_send_msg(APP_EVT_CLOSE_BLUETOOTH, 0);
        #endif
        cJSON_Delete(json);
    }


5. bk_sconf_network_provisioning_status_cb is responsible for starting agent after WiFi connection, saving WiFi and agent information, and agent wake-up after network provisioning. Customers can replace with their own solution

  Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

  This function handles network provisioning status change events, including provisioning success, failure, reconnection and other statuses. Customers can modify the implementation of this function according to their own needs.


6. bk_sconf_switch_ir_mode_handler is responsible for switching multimodal. For customer customization, need to implement bk_sconf_upate_agent_info function by themselves, or refer to Beken solution and replace server connection bk_get_bk_server_url() with their own server address

  Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

.. code::

    void bk_sconf_switch_ir_mode_handler(void)
    {
        static bool is_enable_ir_mode = false;
        static int ir_mode_switching = 0;

        char device_id[128] = {0};
        int ret = 0;

        if (ir_mode_switching == 1) {
            LOGI("%s %d, ir mode switching ongoing\r\n", __func__, __LINE__);
            goto exit;
        }

        ir_mode_switching = 1;

        ret = bk_sconf_get_channel_name(device_id);
        if ((ret == 0) && (os_strlen(device_id) > 0)) {
            LOGI("device_id:%s\n", device_id);
        }
        else {
            LOGW("No device_id, do nothing\r\n");
            goto exit;
        }

        if (is_enable_ir_mode == false) {
            #if CONFIG_BK_VIDEO_ENGINE
            ret = video_engine_init();
            if (ret != BK_OK) {
                LOGE("%s: Failed to initialize video engine, ret=%d\n", __func__, ret);
                goto exit;
            }
            #endif
            
            ret = bk_sconf_upate_agent_info(device_id, "vision");
            if (ret != 0) {
                LOGW("%s %d ret:%d, Failed to update agent info\r\n", __func__, __LINE__, ret);
                goto exit;
            }

            #if (CONFIG_DUAL_SCREEN_AVI_PLAY)
            //TODO: switch to vision screen
            #endif
            is_enable_ir_mode = true;
        }
        else {
            #if CONFIG_BK_VIDEO_ENGINE
            ret = video_engine_deinit();
            if (ret != BK_OK) {
                LOGE("%s: Failed to deinitialize video engine, ret=%d\n", __func__, ret);
                goto exit;
            } 
            #endif

            ret = bk_sconf_upate_agent_info(device_id, "text");
            if (ret != 0) {
                LOGW("%s %d ret:%d, Failed to update agent info\r\n", __func__, __LINE__, ret);
                goto exit;
            }
            #if (CONFIG_DUAL_SCREEN_AVI_PLAY)
            //TODO: switch to text screen
            #endif
            is_enable_ir_mode = false;
        }

    exit:
        config_ir_mode_switch_thread_handle = NULL;
        ir_mode_switching = 0;
        BK_LOGI(TAG, "ir_mode_switch_main end\r\n");
        rtos_delete_thread(NULL);
    }


7. bk_sconf_start_network_transfer is responsible for starting agent and device-side RTC.

  Code path: components/bk_smart_config/src/core/bk_smart_config_core.c

  This function will call ``ntwk_trans_start(device_id)``, which will call the corresponding startup function based on the configured RTC backend Agora. For Agora backend, it will call ``bk_agora_start(device_id)``, which will start both Agent and RTC.


3. Use Case Demonstration
--------------------------

3.1 Code Download, Compilation, and Flashing
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * `Please refer to Quick Start chapter <../get-started/index.html>`_

3.2 UI Resource Replacement
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    - 1. Convert the AVI video file to be used through the conversion tool in SDK ``<bk_aidk source code path>/bk_avdk/components/multimedia/tools/aviconvert/bk_avi.7z``. For specific usage, please refer to the readme.txt instructions in the tool

    - 2. Put the converted file back into SD NAND and rename it to a name containing only English letters or numbers

    - 3. Modify the file name passed to function ``bk_avi_play_open()`` in ``<bk_aidk source code path>/project/common_components/dual_screen_avi_play/lvgl_ui.c`` file.

3.3 Multiple UI Resource Switching
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    - 1. According to the instructions in section 3.2, convert AVI video files and put them into SD NAND;

    - 2. Call function ``bk_avi_play_stop()`` and function ``bk_avi_play_close()`` to stop playing AVI file;

    - 3. Call function ``bk_avi_play_open()`` again to open new AVI file;

    - 4. Call function ``bk_avi_play_start()`` to start playing new AVI file;

3.4 APP Registration and Download
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    APP Download: https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_download/index.html

    Registration and Login: Use email to register and login

3.5 Operation Steps
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

3.5.1 Beken App Network Provisioning Method
+++++++++++++++++++++++++++++++++++++++++++

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


    d) Say the wake-up word ``hi armino`` to the onboard mic, the device will play the prompt tone ``aha`` after waking up,
    and then you can have an AI conversation

      Say the key word ``byebye armino`` to the onboard mic, the device will play the prompt tone ``byebye`` after detecting it,
      then go to sleep and stop talking to the AI


3.5.2 Re-provisioning
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

..


4. Debug Commands
-----------------
.. warning::

    Debug Commands Section, Reading Notes:

    Debug commands are only suitable for developers with certain understanding of the code.

    If you are not familiar with the code and flow, it is recommended to first follow the demonstration steps below, familiarize and understand the code, then consider as appropriate.

..


4.1 Command List:
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

Code path: components/network_transfer/agora_rtc/bk_agora_api.c

    +--------------------------------------------------------------------------+-------------------------------------+
    |Command                                                                   |    Description                      |
    +--------------------------------------------------------------------------+-------------------------------------+
    |agora_rtc {start\|stop\|start_agora\|stop_agora\|start_agent\|stop_agent} | Agora RTC Debug Command             |
    +--------------------------------------------------------------------------+-------------------------------------+


4.2 Command Description
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**Basic Format**:

.. code-block:: bash

    agora_rtc <subcommand>

**Subcommand Description**:

    +----------------+-----------+-------------------------------------------------+
    |Subcommand      |  Note     | Function Description                            |
    +----------------+-----------+-------------------------------------------------+
    |start           |  Required | Start RTC and Agent (start simultaneously)      |
    +----------------+-----------+-------------------------------------------------+
    |stop            |  Required | Stop RTC and Agent (stop simultaneously)        |
    +----------------+-----------+-------------------------------------------------+
    |start_agora     |  Optional | Start RTC only (do not start Agent)             |
    +----------------+-----------+-------------------------------------------------+
    |stop_agora      |  Optional | Stop RTC only (do not stop Agent)               |
    +----------------+-----------+-------------------------------------------------+
    |start_agent     |  Optional | Start Agent only (do not start RTC)             |
    +----------------+-----------+-------------------------------------------------+
    |stop_agent      |  Optional | Stop Agent only (do not stop RTC)               |
    +----------------+-----------+-------------------------------------------------+

**Usage Instructions**:

- Command automatically uses device UID as device_id (automatically generated from device UID, format is hexadecimal string of 24-byte UID)
- When using ``start`` or ``stop`` command, RTC and Agent will be started/stopped simultaneously
- If you need to control RTC or Agent separately, you can use corresponding subcommands
- Before using this command, you need to start AI Agent on PC side yourself

**Usage Examples**:

.. code-block:: bash

    # Start RTC and Agent
    agora_rtc start

    # Stop RTC and Agent
    agora_rtc stop

    # Start RTC only
    agora_rtc start_agora

    # Stop RTC only
    agora_rtc stop_agora

    # Start Agent only
    agora_rtc start_agent

    # Stop Agent only
    agora_rtc stop_agent

.. note::

    Before using this command, you need to start AI Agent on PC side yourself

    - 1. Register on Agora official website and obtain the following parameters `Beken Agora Registration Documentation <../../thirdparty/agora/index.html#id1>`_

        * AGORA_APPID
        * AGORA_RESTFUL_TOKEN

    - 2. According to the operation manual provided by Agora AI Agent, start the server-side AI Agent.

        a. Currently using the method of starting Agora AI Agent on PC side. For POST instructions, please refer to the usage manual provided by Agora ``<bk_smp source code path>/docs/thirdparty/agora_ai_agent``

        b. You can also refer to `Beken AI Agent Startup Documentation <../../thirdparty/agora/index.html#ai-agent>`_

5. Questions & Answers
----------------------

Q: Application layer has no mic data reporting?

A: Currently beken_genie supports voice wake-up function based on command words by default. Only after wake-up will mic captured data be reported to the application layer, and the application layer then sends data to AI for conversation. If customers do not need voice wake-up function, they can disable this function through macro ``CONFIG_AE_SUPPORT_PROMPT_TONE``.


Q: UI resource playback display abnormal?

A: UI resources used in this project must be AVI format videos with resolution of 320x160, and must be converted using AVI conversion tool before use. You can first check whether UI resources meet the above requirements.

Q: What 4G Cat1 modules have been adapted based on USB interface?

A: Fibocom LE270\370, Hezhou Air780E, Quectel EC-800M, Yike I511

Note: When using Quectel EC-800M, the adaptation code requires some additional modifications. Specific patch can be obtained from this link: https://armino.bekencorp.com/article/34.html

Q: How to connect 4G module through USB interface using beken genie?

A: Please refer to https://docs.bekencorp.com/arminodoc/bk_idk/bk7258/zh_CN/v_ai_2.0.1/developer-guide/peripheral/bk_modem.html#modem-griver-usage-guide

Q: How to confirm that 4G module has been successfully connected?

A: Please refer to https://docs.bekencorp.com/arminodoc/bk_idk/bk7258/zh_CN/v_ai_2.0.1/api-reference/peripheral/bk_modem.html. After successful connection, you can verify through ping packets.

