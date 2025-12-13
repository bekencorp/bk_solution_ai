VolcEngine RTC
=================================

:link_to_translation:`zh_CN:[中文]`

**1. VolcEngine RTC Introduction**
-----------------------------------
VolcEngine Real-Time Communication (veRTC) provides globally reliable, high-concurrency, low-latency real-time audio/video communication capabilities, enabling various types of real-time communication and interaction.
For detailed information, please refer to: https://www.volcengine.com/product/veRTC

**2. VolcEngine RTC Code Distribution**
---------------------------------------
 - VolcEngine RTC HAL layer and lib code directory: Located in Armino SMP SDK, path is ``components/bk_thirdparty/VolcEngineRTCLite``
 - BK and VolcEngine RTC transmission integration: ``components/network_transfer/volc_rtc``
 - VolcEngine RTC demo project directory: ``projects/volc_rtc``

**3. Prerequisites for Using VolcEngine RTC**
---------------------------------------------
Using VolcEngine RTC with large models for AI conversation requires enabling related services and configuring permissions. For details, please refer to VolcEngine online documentation:
https://www.volcengine.com/docs/6348/1315561

**4. Agent (Intelligent Agent) Startup Methods**
------------------------------------------------
AIDK includes two methods for starting Agent: starting Agent through BK server and starting Agent through developer-deployed server. AIDK defaults to starting Agent through BK server for demonstrating VolcEngine RTC effects.

.. attention::
    *Starting Agent through BK server for AI conversation is a demo, with each conversation limited to 5 minutes.*
    *If you need longer conversation duration, please contact the development board Taobao customer service to obtain a license file. For details, please refer to Chapter 5 license function description.*
    **We strongly recommend developers to set up their own servers for convenient duration control, device management, and differentiated deployment**

**4.1 Starting Agent Through BK Server**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
After network provisioning is completed, the program will call ``bk_sconf_start_network_transfer(char *device_id)`` to start network transfer. This function will call ``bk_byte_start(device_id)`` based on configuration, which in turn calls ``volc_agent_start()`` to request BK server to start Agent and start device-side RTC, then requires the device to join the room where Agent is located for AI conversation.

Code paths:
- ``bk_sconf_start_network_transfer``: components/bk_smart_config/src/core/bk_smart_config_core.c
- ``bk_byte_start``: components/network_transfer/volc_rtc/bk_volc_api.c
- ``volc_agent_start``: components/network_transfer/volc_rtc/volc_agent_engine.c

**4.2 Starting Agent Through Developer-Deployed Server**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,  
To facilitate device management for developers, AIDK also supports developers to start Agent through personally deployed servers. For server deployment, please refer to VolcEngine open source code and guide: https://github.com/volcengine/rtc-aigc-embedded-demo
To use this method to start Agent, the following configuration is required:

4.2.1 Modify VolcEngine RTC Project Configuration File
++++++++++++++++++++++++++++++++++++++++++++++++++++++

Modify the ``projects/volc_rtc/ap/config/bk7258_ap/config`` file by setting ``CONFIG_STARTUP_AGENT_FROM_BK_SERVER=n`` to disable starting Agent from BK server and change to starting Agent from custom server.

.. code::

    # Disable starting Agent from BK server (enabled by default)
    # CONFIG_STARTUP_AGENT_FROM_BK_SERVER is not set

4.2.2 Modify VolcEngine Configuration File
++++++++++++++++++++++++++++++++++++++++++
Modify ``components/network_transfer/volc_rtc/volc_config.h`` to configure parameters required for requesting custom server.

.. code-block:: c

    #if !CONFIG_STARTUP_AGENT_FROM_BK_SERVER
    // RTC APP ID
    #define CONFIG_RTC_APP_ID    "67********************d1"
    // Server address
    #define CONFIG_AGENT_SERVER_HOST   "1**.1**.**.**:8080"
    #endif


**5. License Function Description**
------------------------------------
VolcEngine RTC supports license billing. License billing is a special billing rule. Please communicate and confirm with VolcEngine before use.
AIDK supports License mode starting from v2.0.1.9. To use License billing, ensure that the ``CONFIG_VOLC_RTC_ENABLE_LICENSE`` macro in ``projects/volc_rtc/ap/config/bk7258_ap/config`` file is configured as ``y``.

.. code::

    CONFIG_VOLC_RTC_ENABLE_LICENSE=y

License is bound to device UID by default. Device UID can be obtained through ``bk_byte_rtc_print_finger()``.

After obtaining the License file, please name the License file as VolcEngineRTCLite.lic and copy the License file to the root directory of the file system.

.. note::
    BK7258 AIDK file system uses SD-NAND by default. License file needs to be copied to SD-NAND root directory.
    For specific usage of SD NAND, please refer to `Nand Disk Usage Notes <../../api-reference/nand_disk_note.html>`_.
    Developers can contact staff who provide AIDK development boards to obtain license files for experience.

**6. Customization Configuration**
----------------------------------

**6.1 Audio Codec Configuration**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
VolcEngine supports common audio codec formats. AIDK VolcEngine RTC project uses opus by default. If you want to change to G722 codec, you can modify the audio encoder/decoder configuration in ``projects/volc_rtc/ap/config/bk7258_ap/config`` file.

.. code::

    # Audio encoder type
    CONFIG_AE_AUDIO_ENCODER_TYPE="G722"
    # Audio decoder type
    CONFIG_AE_AUDIO_DECODER_TYPE="G722"

VolcEngine RTC audio codec related operations include the following:

Code path: components/network_transfer/volc_rtc/

 - Call ``byte_rtc_set_audio_codec`` interface to set audio encoding type before ``byte_rtc_join_room``

   Code location: ``__byte_rtc_start()`` function in ``volc_rtc_engine.c``

 - Send audio data through ``byte_rtc_send_audio_data``. For details, please refer to ``bk_byte_rtc_audio_data_send()`` function implementation

   Code location: ``bk_volc_api.c``

 - Configure corresponding audio encoding type through ``audio_codec`` parameter when starting Agent. For details, please refer to ``volc_start_agent_from_bk_server()`` or ``volc_start_agent_from_customer_server()`` function implementation

   Code location: ``volc_agent_engine.c``

**6.2 Vision Recognition Function**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
VolcEngine project supports vision recognition function by default. AIDK VolcEngine project does not enable vision recognition function by default after network provisioning. You can switch between vision recognition and normal AI conversation functions through S2 button.
If you want to enable vision recognition function by default after network provisioning, you can configure ``CONFIG_VOLC_ENABLE_VISION_BY_DEFAULT=y`` in ``projects/volc_rtc/ap/config/bk7258_ap/config`` file.

.. code::

    CONFIG_VOLC_ENABLE_VISION_BY_DEFAULT=y

When enabling vision recognition, note the following:

Code path: components/network_transfer/volc_rtc/

 - When ``byte_rtc_join_room``, the options parameter needs to set ``auto_publish_video`` to ``true``

   Code location: ``__byte_rtc_start()`` function in ``volc_rtc_engine.c``

 - Send video data through ``byte_rtc_send_video_data``. For details, please refer to ``bk_byte_rtc_video_data_send()`` function implementation

   Code location: ``bk_volc_api.c``

 - When starting Agent, set ``mode`` parameter to ``"vision"`` to enable vision understanding and fill in corresponding image configuration. For details, please refer to ``volc_start_agent_from_bk_server()`` or ``volc_start_agent_from_customer_server()`` function implementation

   Code location: ``volc_agent_engine.c``

 - Backend large model needs to support vision recognition function

**6.3 Subtitle Function**
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
VolcEngine project disables subtitle function by default. If you want to enable subtitle function, you can configure ``CONFIG_VOLC_ENABLE_SUBTITLE_BY_DEFAULT=y`` in ``projects/volc_rtc/ap/config/bk7258_ap/config`` file.

.. code::

    CONFIG_VOLC_ENABLE_SUBTITLE_BY_DEFAULT=y

Subtitle receiving function is ``__on_message_received()`` function in ``components/network_transfer/volc_rtc/volc_rtc_engine.c`` file.

Code path: components/network_transfer/volc_rtc/volc_rtc_engine.c

For VolcEngine subtitle format, please refer to VolcEngine documentation: https://www.volcengine.com/docs/6348/1337284

**7. Reference Links**
-----------------------

VolcEngine reference documentation: https://www.volcengine.com/product/veRTC

VolcEngine account application link: https://www.volcengine.com/docs/6348/1315561

VolcEngine Agent server deployment: https://github.com/volcengine/rtc-aigc-embedded-demo

VolcEngine Demo project: `BK7258 VolcEngine RTC Demo <../../projects/volc_rtc/index.html>`_

