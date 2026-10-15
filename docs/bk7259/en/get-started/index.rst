Quick Start
=================================

:link_to_translation:`zh_CN:[中文]`

This section explains how to get the BK7259 Robot Solution code, set up the build environment, build a project, and flash the firmware, so you can get started quickly.

.. rubric:: Before you start

- **Solution vs SMP**: this repo holds the BK7259 Robot solution and application code. Firmware builds depend on **BK AVDK SMP** via ``SDK_DIR``. Drivers, RTOS, Wi-Fi/BLE, etc. are provided by SMP. See :doc:`../intro/index`.
- **Currently published project**: only ``projects/beken_robot`` (Agora RTC variant). See :doc:`../projects/index`.

.. include:: ../_readme_body.rst

APP Registration and Download
--------------------------------

APP download: https://docs.bekencorp.com/arminodoc/bk_app/app/en/v2.0.1/app_download/index.html

Sign in with email.

Power-up and First Boot
--------------------------------

1. Charge the unit fully or use USB-C power.
2. Slide the power switch on the back to the "ON" position.
3. After ~5 seconds the LCD shows the blue "BK7259 robot solution" logo (page_1).
4. Press any key to enter the main menu (page_2).
5. The full key map, UI flow and per-page behavior live in :doc:`../projects/beken_robot/index` under *Keys* and *LVGL pages*.

Provisioning (BLE)
--------------------------------

1. From the main menu, focus **Provisioning** and short-press *Confirm* to enter the BLE provisioning page (page_4).
2. Focus **Start** and short-press *Confirm*; the device starts BLE advertising.
3. In the BK App on your phone, scan the device and send the WiFi SSID / password (2.4 GHz + WPA2 recommended).
4. The device joins WiFi within 60 s; the WiFi icon appears in the upper-right corner of the main menu.
5. On failure the BK App reports the cause (wrong password / SSID missing / weak signal); the device should exit provisioning gracefully.
6. Troubleshooting and re-provisioning steps live in :doc:`../projects/beken_robot/index` under *BLE provisioning*.

FAQ
--------------------------------

- **Where should ``SDK_DIR`` point?** To the root of the cloned **BK AVDK SMP** tree (where ``Makefile`` / ``tools`` / ``ap`` / ``cp`` live), not this repo's root.
- **Missing toolchain or script errors?** Complete the environment setup in `Environment Deployment and Compilation <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html#smp-get-started-env>`_ first; for Docker, ensure Docker is installed and running.
- **Slow Docker image pull from China?** See the mirror / download options in `Environment Deployment and Compilation <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html#smp-get-started-env>`_.
- **BK App can't see the device?** Make sure the device is on the BLE provisioning page (page_4) with **Start** selected and *Confirm* pressed; restart the phone's Bluetooth and try again.
- **WiFi join times out?** Make sure the AP is 2.4 GHz, has no MAC whitelist, and signal strength is sufficient.
