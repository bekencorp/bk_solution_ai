Quick Start
=================================

1. Environment Setup
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1.1 **Download Armino SMP SDK**:

You can download the Armino SMP code from gitlab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1

1.2 **Download AI Solution Code**:

You can download the Armino AI Solution code from gitlab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v3.1.1


2. Build Project
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

**Method 1: Direct Build**

.. code-block:: bash

    cd ~/armino/bk_solution_ai/projects/beken_genie
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7258 SDK_DIR=~/armino/bk_avdk_smp

**Method 2: Specify SDK Path Using Export**

.. code-block:: bash

    cd ~/armino/bk_solution_ai/projects/beken_genie
    export SDK_DIR=~/armino/bk_avdk_smp
    make clean
    make bk7258

**Method 3: Using Docker (Linux/Mac)**

.. code-block:: bash

    cd ~/armino/bk_solution_ai/projects/beken_genie
        export SDK_DIR=~/armino/bk_avdk_smp
        ./dbuild.sh make clean
        ./dbuild.sh make bk7258

**Method 4: Using Docker (Windows PowerShell)**

.. code-block:: powershell

    cd C:\armino\bk_solution_ai\projects\beken_genie
        $env:SDK_DIR = "C:\armino\bk_avdk_smp"
        .\dbuild.ps1 make clean
        .\dbuild.ps1 make bk7258

3. Flash Firmware to Device
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

After compilation, the all-app.bin file will be generated in the /build/bk7258/beken_genie/package directory of the AI Solution code. Use the flashing tool to flash it to the development board.

3.1 **Resource File Flashing**

    - 1. Armino supports firmware flashing on Windows/Linux platforms. For flashing methods, please refer to the guide in the flashing tool. Taking Windows platform as an example, Armino currently supports UART flashing.

      For specific `flashing process <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html>`_, please refer to `SMP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>`_


4. APP Registration and Download
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    APP Download: https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_download/index.html

    Registration and Login: Use email to register and login


