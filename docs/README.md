Armino AI Solution Introduction
------------------------------------

:link_to_translation:`zh_CN:[中文]`

Overview
-----------------------------------

Armino AI Solution is an intelligent AI device solution developed by Beken Corporation based on the Armino SMP architecture. This solution provides complete end-to-cloud and cloud-to-large-model AI interaction capabilities, supports multiple large language model integrations, and offers developers a complete development framework for rapidly building intelligent AI devices. Currently supports large model applications such as VolcEngine and Agora.


Project Compilation
------------------------------------

1. Environment Setup
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1.1  **Download Armino SMP SDK**:

You can download Armino SMP code from gitlab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1

1.2 **Download AI Solution Code**:

You can download Armino AI Solution code from gitlab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v3.1.1


2. Project Introduction
--------------------------------
Armino AI Solution mainly includes Agora RTC version project, VolcEngine RTC version project, and AI camera version project.

Agora RTC Version Project (beken_genie)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

An AI device solution based on BK7258 chip and Agora RTC SDK, providing complete end-to-cloud and cloud-to-large-model AI interaction capabilities.
- Supports Agora RTC real-time audio/video communication, integrated audio processing engine (AEC, NS, KWS)
- Supports OPUS, PCM audio encoding formats, supports prompt tone playback
- Supports multiple large language model integrations (OpenAI, Doubao, DeepSeek, etc.)
- Supports dual SPI LCD screen display, providing visual and voice interaction experience
- Includes rich peripheral reference designs: gyroscope, NFC, buttons, vibration motor, NAND Flash, LED effects, charging management, DVP camera, etc.

VolcEngine RTC Version Project (volc_rtc)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

A real-time audio/video communication solution based on BK7258 chip and VolcEngine RTC SDK, supporting real-time conversation with cloud AI Agent.
- Supports VolcEngine RTC real-time audio/video communication, integrated audio processing engine (AEC, NS)
- Supports G722, OPUS, PCM audio encoding formats
- Supports VolcEngine AI Agent service integration, supports voice conversation and image recognition
- Supports dual SPI LCD screen display, providing visual and voice interaction experience
- Includes rich peripheral reference designs: gyroscope, NFC, buttons, vibration motor, NAND Flash, LED effects, charging management, DVP camera, etc.

AI Camera Version Project (ai_camera)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

AI camera solution, currently under development.


3. Compile Project
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**Method 1: Direct Compilation**

.. code:: bash

    cd ~/armino/bk_solution_ai/projects/beken_genie
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7258 SDK_DIR=~/armino/bk_avdk_smp

**Method 2: Specify SDK Path via export**

.. code:: bash

    cd ~/armino/bk_solution_ai/projects/beken_genie
    export SDK_DIR=~/armino/bk_avdk_smp
    make clean
    make bk7258

**Method 3: Using Docker (Linux/Mac)**

.. code:: bash

    cd ~/armino/bk_solution_ai/projects/beken_genie
        export SDK_DIR=~/armino/bk_avdk_smp
        ./dbuild.sh make clean
        ./dbuild.sh make bk7258

**Method 4: Using Docker (Windows PowerShell)**

.. code:: powershell

    cd C:\armino\bk_solution_ai\projects\beken_genie
        $env:SDK_DIR = "C:\armino\bk_avdk_smp"
        .\dbuild.ps1 make clean
        .\dbuild.ps1 make bk7258

4. Flash Firmware to Device
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

After compilation, the all-app.bin file will be generated in the /build/bk7258/beken_genie/package directory of the AI Solution code. Use the flashing tool to flash it to the development board.

4.1 **Resource File Flashing**

    - 1. Armino supports firmware flashing on Windows/Linux platforms. Refer to the flashing tool documentation for flashing methods. For Windows platform, Armino currently supports UART flashing.

      For specific `flashing procedures <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html>`_, please refer to `SMP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html>`_


5. Project Demo and Operation Steps
----------------------------------

5.1  APP Download: `Download <https://docs.bekencorp.com/arminodoc/bk_app/app/en/v2.0.1/app_download/index.html>`_

    Registration and Login: Register and login using email

5.2  Operation Steps: Mainly includes APP network configuration methods and procedures, how to properly start Agent. For detailed procedures, please refer to `AI Solution <https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/intro/index.html>`_
