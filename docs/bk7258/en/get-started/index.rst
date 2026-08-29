Quick Start
=================================

:link_to_translation:`zh_CN:[中文]`

.. rubric:: Before you start

- **BK AI vs SMP**: This repository contains solution and application code. Firmware builds depend on **BK AVDK SMP** via ``SDK_DIR``. Drivers, RTOS, Wi-Fi/BLE, etc. are provided by SMP. See :doc:`../intro/index`.
- **Choosing a project**: For ``beken_genie``, ``volc_rtc``, ``volc_rtc_ab`` (OTA A/B), see **Choosing a project** in :doc:`../projects/index`.

.. note::

   If you cannot access GitLab (enterprise-only), use the **GitHub** clone commands below or contact your Beken representative.

1. Environment Setup
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1.1 **Download Armino SMP SDK**:

**GitLab**: You can download the Armino SMP code from GitLab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1

**GitHub**: You can also clone from `bekencorp/bk_avdk_smp on GitHub <https://github.com/bekencorp/bk_avdk_smp>`_::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://github.com/bekencorp/bk_avdk_smp.git -b release/v3.1.1

1.2 **Download AI Solution Code**:

**GitLab**: You can download the Armino AI Solution code from GitLab::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v3.1.1

**GitHub**: You can also clone from `bekencorp/bk_solution_ai on GitHub <https://github.com/bekencorp/bk_solution_ai>`_::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://github.com/bekencorp/bk_solution_ai.git -b release/v3.1.1

.. note::

   On **Windows**, run ``git config --global core.autocrlf false`` before cloning to avoid line-ending issues that break the build.


1.3 Environment Deployment and Build
----------------------------------------

We provide two deployment options: a local build environment and a Docker-based build environment. You may choose either based on your hardware and habits; we generally recommend the local build environment.

The local deployment option currently supports building on Windows and Linux only.

Docker deployment uses containers for environment setup and builds. It supports efficient compilation on Linux, macOS, and Windows.
With Docker, you do not need to manually install the libraries and toolchains required for building, which greatly simplifies setup and compilation.
This option suits users who are familiar with Docker. If you are not, or your network prevents using Docker, use the local deployment option instead.

.. toctree::
    :maxdepth: 1

    Local Deployment <env-manual>
    Docker Deployment <env-docker>


2. Build Project
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The examples below use ``beken_genie``. For **volc_rtc**, replace ``beken_genie`` in the path with ``volc_rtc``; output is under ``build/bk7258/<project>/package``.

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

After compilation, ``all-app.bin`` is generated under ``build/bk7258/beken_genie/package`` (replace ``beken_genie`` with your project folder name if different). Flash it to the board with the Beken tool.

3.1 **Resource File Flashing**

    - 1. Armino supports firmware flashing on Windows/Linux platforms. For flashing methods, please refer to the guide in the flashing tool. Taking Windows platform as an example, Armino currently supports UART flashing.

      For the flashing flow, see `SMP Quick Start (English) <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html>`_ or `SMP Quick Start (中文) <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html>`_, and the `SMP documentation home <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html>`_.


4. APP Registration and Download
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    APP Download: https://docs.bekencorp.com/arminodoc/bk_app/app/en/v2.0.1/app_download/index.html (Chinese: https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_download/index.html)

    Registration and Login: Use email to register and login


5. FAQ
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Where should ``SDK_DIR`` point?** To the root of the cloned **BK AVDK SMP** tree (where ``Makefile`` and ``tools`` live), not the BK AI repository root.
- **Missing toolchain or script errors?** Complete SMP setup in :doc:`env-manual` or :doc:`env-docker` first; for Docker, ensure Docker is installed and running.
- **Slow Docker image pull?** Use the mirror / download options described in :doc:`env-docker`.


