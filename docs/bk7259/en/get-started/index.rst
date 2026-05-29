Quick Start
=================================

:link_to_translation:`zh_CN:[中文]`

.. rubric:: Before you start

- **Solution vs SMP**: this repo holds the BK7259 Robot solution and application code. Firmware builds depend on **BK AVDK SMP** via ``SDK_DIR``. Drivers, RTOS, Wi-Fi/BLE, etc. are provided by SMP. See :doc:`../intro/index`.
- **Currently published project**: only ``projects/beken_robot`` (Agora RTC variant). See :doc:`../projects/index`.

.. note::

   The GitLab repo is currently enterprise-only. External users should use the GitHub mirror, or contact your Beken representative for access.

1. Environment Setup
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1.1 **Download Armino SMP SDK (BK7259 v4.0.1)**

**GitLab**::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v4.0.1

**GitHub**::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://github.com/bekencorp/bk_avdk_smp.git -b release/v4.0.1

The SDK currently tracks the ``bk_avdk_smp_release_4.0.1.x`` tag series.

1.2 **Download BK7259 Robot Solution Code (this repo)**

**GitLab** ::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v4.0.1

**GitHub** ::

    mkdir -p ~/armino
    cd ~/armino
    git clone https://github.com/bekencorp/bk_solution_ai.git -b release/v4.0.1

.. note::

   On **Windows**, run ``git config --global core.autocrlf false`` before cloning to avoid line-ending issues that break the build.


1.3 Environment Deployment and Build
----------------------------------------

We provide two deployment options: a local build environment and a Docker-based build environment. We generally recommend the local environment.

The local option supports building on Windows and Linux only.

The Docker option supports Linux, macOS and Windows. Docker images include the toolchain and libraries, which removes manual setup. Use this if you are familiar with Docker; otherwise, use the local option.

.. toctree::
    :maxdepth: 1

    Local Deployment <env-manual>
    Docker Deployment <env-docker>


1. Build Project
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

The examples below use the currently published ``beken_robot`` project (repo root = ``~/armino/bk_solution_ai``, project at ``projects/beken_robot``).

**Method 1: Direct Build**

.. code-block:: bash

    cd ~/armino/bk_solution_ai/projects/beken_robot
    make clean SDK_DIR=~/armino/bk_avdk_smp
    make bk7259 SDK_DIR=~/armino/bk_avdk_smp

**Method 2: Specify SDK Path Using Export**

.. code-block:: bash

    cd ~/armino/bk_solution_ai/projects/beken_robot
    export SDK_DIR=~/armino/bk_avdk_smp
    make clean
    make bk7259

**Method 3: Using Docker (Linux/Mac)**

.. code-block:: bash

    cd ~/armino/bk_solution_ai/projects/beken_robot
    export SDK_DIR=~/armino/bk_avdk_smp
    ./dbuild.sh make clean
    ./dbuild.sh make bk7259

**Method 4: Using Docker (Windows PowerShell)**

.. code-block:: powershell

    cd C:\armino\bk_solution_ai\projects\beken_robot
    $env:SDK_DIR = "C:\armino\bk_avdk_smp"
    .\dbuild.ps1 make clean
    .\dbuild.ps1 make bk7259

3. Flash Firmware to Device
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

After compilation, the binary is at (relative to repo root ``bk_solution_ai/``):

.. code::

    projects/beken_robot/build/bk7259/beken_robot/package/all-app.bin

3.1 **Resource File Flashing**

    - Armino supports flashing on Windows / Linux. On Windows, use BKFIL (``bk_loader``) over UART.

    - For the flashing flow, see `SMP Quick Start (BK7259) <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html>`_ and the `SMP home <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/index.html>`_.


4. APP Registration and Download
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    APP download: https://docs.bekencorp.com/arminodoc/bk_app/app/en/v2.0.1/app_download/index.html

    Sign in with email.

5. Power-up and First Boot
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1. Charge the unit fully or use USB-C power.
2. Slide the power switch on the back to the "ON" position.
3. After ~5 seconds the LCD shows the blue "BK7259 robot solution" logo (page_1).
4. Press any key to enter the main menu (page_2).
5. The full key map, UI flow and per-page behavior live in :doc:`../projects/beken_robot/index` under *Keys* and *LVGL pages*.

6. Provisioning (BLE)
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

1. From the main menu, focus **Provisioning** and short-press *Confirm* to enter the BLE provisioning page (page_4).
2. Focus **Start** and short-press *Confirm*; the device starts BLE advertising.
3. In the BK App on your phone, scan the device and send the WiFi SSID / password (2.4 GHz + WPA2 recommended).
4. The device joins WiFi within 60 s; the WiFi icon appears in the upper-right corner of the main menu.
5. On failure the BK App reports the cause (wrong password / SSID missing / weak signal); the device should exit provisioning gracefully.
6. Troubleshooting and re-provisioning steps live in :doc:`../projects/beken_robot/index` under *BLE provisioning*.

7. FAQ
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **Where should ``SDK_DIR`` point?** To the root of the cloned **BK AVDK SMP** tree (where ``Makefile`` / ``tools`` / ``ap`` / ``cp`` live), not this repo's root.
- **Missing toolchain or script errors?** Complete SMP setup in :doc:`env-manual` or :doc:`env-docker` first; for Docker, ensure Docker is installed and running.
- **Slow Docker image pull from China?** Use the BEKEN mirror in :doc:`env-docker`.
- **BK App can't see the device?** Make sure the device is on the BLE provisioning page (page_4) with **Start** selected and *Confirm* pressed; restart the phone's Bluetooth and try again.
- **WiFi join times out?** Make sure the AP is 2.4 GHz, has no MAC whitelist, and signal strength is sufficient.
