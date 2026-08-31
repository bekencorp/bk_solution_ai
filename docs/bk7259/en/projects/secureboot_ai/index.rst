BK7259 Secure Boot AI Project
=================================

:link_to_translation:`zh_CN:[中文]`

1. Introduction
---------------------------------

    ``projects/secureboot_ai`` is a secure-boot reference project for the BK7259 Robot V1 AI
    kit. It combines the robot AI application stack from ``beken_robot`` with BL1,
    BL2/MCUboot and TF-M. The project demonstrates a trusted boot chain that enters the CP
    Non-Secure application and then prepares and starts the AP Non-Secure application
    through a controlled NSC interface.

    The AP provides an LVGL demo center, BLE provisioning, WiFi, audio, video, edge AI and
    Agora cloud AI features. The CP owns secure boot, TF-M, secure services and the AP
    power-up sequence.

1.1 Hardware references
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * SoC: BK7259 (CP CPU0 and AP CPU1/CPU2 SMP).
    * Board: BK7259 Robot V1 AI kit.
    * Display: 320x385 MIPI LCD, default panel ``jd9855_mipi_320x385``.
    * Touch controller: CST9217.
    * Camera: MIPI CSI.
    * Audio: on-board microphone and speaker paths.
    * Storage: on-board SD-NAND connected through SDIO1.
    * Connectivity: WiFi, BLE and Classic Bluetooth audio.
    * Type-C: GPIO54 selects between the CH340 UART and BK7259 USB.

    For the BK7259 datasheet, see :doc:`../../hw-reference/index`.

1.2 Software features
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * BL1 + BL2/MCUboot + TF-M secure boot.
    * EC-P256 image signing and fixed-key Flash AES encryption.
    * Isolation between CP/AP Non-Secure applications and TF-M Secure services.
    * LVGL demo center with touch input.
    * BLE provisioning, WiFi STA, Agora RTC and a cloud AI Agent.
    * Local KWS, audio processing, H.264/JPEG, TFLite Micro and NPU.
    * Edge AI demos such as palm tracking, face detection, gesture recognition and face recognition.
    * SD-NAND, USB MSC, robot video and robot control services.

    NFC, motor, LED blinking and battery monitoring are not enabled in the current default
    configuration and must not be treated as default project features.

2. Using the project
---------------------------------

2.1 Build
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    The project ``Makefile`` uses the ``bk7259`` SDK in this repository by default:

    .. code:: bash

        cd bk7259_ai_solution/projects/secureboot_ai
        make clean
        make bk7259

    A Docker build environment can also be used:

    .. code:: bash

        ./dbuild.sh make clean
        ./dbuild.sh make bk7259

    Set ``SDK_DIR`` before building if the SDK is not in the default location. For generic
    environment and toolchain setup, see :doc:`../../get-started/index`.

2.2 Build outputs and flashing
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    The main outputs are generated under:

    .. code:: text

        projects/secureboot_ai/build/bk7259/secureboot_ai/package/

    Important files are:

    * ``all-app.bin``: complete flash image.
    * ``bootloader.bin``: secure-boot bootloader package.
    * ``ota.bin``: encrypted OTA package.
    * ``otp_efuse_config.json``: generated OTP/eFuse configuration reference.

    During development, use BKFIL or ``bk_loader`` to flash ``all-app.bin`` over UART.
    Secure-boot production also requires irreversible OTP/eFuse settings such as the Root
    of Trust public-key hash and Flash AES key. Follow the chip production flow.

    .. warning::

        OTP/eFuse programming is normally irreversible. Keys stored in the repository are
        for development only. Production must use controlled key generation, storage and
        injection procedures.

2.3 Boot verification
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    The expected boot sequence is:

    .. code:: text

        BootROM
          -> BL1
          -> BL2 / MCUboot
          -> TF-M Secure
          -> CP Non-Secure
          -> AP Secure prepare
          -> AP Non-Secure

    After the CP Non-Secure application starts successfully, it prints:

    .. code:: text

        secureboot_ai: CP NS world reached (secure boot OK)

    After the AP initializes the display, it prints:

    .. code:: text

        LVGL ready on 320x385 MIPI (first page pending)
        LVGL started, page_1 loaded

    BL2/TF-M and Non-Secure applications use different UART paths. During debugging, check
    both the secure-boot log and the AP/CP Non-Secure logs.

3. Security architecture
---------------------------------

3.1 AP/CP responsibilities
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * **CP Secure**: runs TF-M and provides Crypto, secure resource configuration and AP Secure prepare.
    * **CP Non-Secure**: initializes the system and starts the AP through a PM vote.
    * **AP Secure Shim**: runs after AP reset in Secure state, configures security attribution and required shared hardware, then enters NS.
    * **AP Non-Secure**: runs LVGL, multimedia, networking and AI applications.

    CP Non-Secure does not perform Secure privileged configuration directly. Secure AP
    preparation is provided through the ``psa_ap_secure_prepare`` NSC interface.

3.2 TF-M configuration
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    The main options are:

    * ``CONFIG_TFM=y``.
    * ``CONFIG_TFM_BL2="ON"``.
    * ``CONFIG_TFM_PROFILE="profile_medium"``.
    * ``CONFIG_TFM_ISOLATION_LEVEL=2``.
    * ``CONFIG_TFM_CRYPTO=y``.
    * ``CONFIG_TFM_AP_BOOT_NSC=y``.
    * ``CONFIG_TFM_REG_ACCESS_NSC=y``.

    Persistent Storage, Firmware Update and Initial Attestation are disabled by default.

3.3 Flash partitions
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    The project uses an 8 MB flash device. Its main partitions are:

    +--------------------------+----------+----------------------------------+
    | Partition                | Size     | Purpose                          |
    +==========================+==========+==================================+
    | ``bl1_control``          | 4 KB     | BL1 control information          |
    +--------------------------+----------+----------------------------------+
    | ``primary_manifest``     | 4 KB     | Primary image manifest           |
    +--------------------------+----------+----------------------------------+
    | ``bl2``                  | 96 KB    | BL2/MCUboot                      |
    +--------------------------+----------+----------------------------------+
    | ``primary_tfm_s``        | 320 KB   | TF-M Secure image                |
    +--------------------------+----------+----------------------------------+
    | ``primary_cpu0_app``     | 1644 KB  | CP Non-Secure application        |
    +--------------------------+----------+----------------------------------+
    | ``primary_ap_app``       | 5000 KB  | AP AI application                |
    +--------------------------+----------+----------------------------------+
    | ``ota``                  | 496 KB   | Encrypted OTA data               |
    +--------------------------+----------+----------------------------------+

    The current strategy is ``XIP_FORCE_A``. Secondary partitions are placeholders and do
    not provide enough space for a complete A/B dual-slot update.

3.4 Signing, encryption and security counter
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    ``partitions/bk7259/security.csv`` enables secure boot, an EC-P256 Root Key and
    fixed-key Flash AES. ``ota.csv`` configures encrypted OTA and the application security
    counter. The build tools sign and encrypt images and generate the OTP/eFuse reference.

    When changing keys or the security counter, consider:

    * The Root of Trust already programmed into each device.
    * Released image versions and rollback policy.
    * Separation of development and production keys.
    * Consistency between OTA packages and device-side keys.

4. AI application
---------------------------------

4.1 UI and demo center
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    The AP initializes the 320x385 MIPI LCD and CST9217 touch controller, loads the splash
    page, and enters the demo center on touch. Demos are grouped into edge AI, cloud AI,
    entertainment and device settings. This is different from the fixed page_1-to-page_10
    navigation documented for ``beken_robot``.

    Default demos include command-word recognition, sound source localization, palm/face/
    gesture recognition, AI dialog, visual recognition, music, robot video, Bluetooth
    music, volume and USB mode switching. The AP ``defconfig`` determines which demos are
    available in a particular build.

4.2 BLE provisioning and cloud AI
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    1. Open provisioning from the demo center and start BLE provisioning.
    2. Select the device in the BK App and provide the WiFi SSID and password.
    3. Wait for the device to join WiFi.
    4. Configure a valid Agora App ID and credentials before starting RTC and the cloud AI Agent.

    Agora debug commands:

    .. code:: bash

        agora_rtc start
        agora_rtc stop
        agora_rtc start_agora
        agora_rtc stop_agora
        agora_rtc start_agent
        agora_rtc stop_agent

4.3 Storage and USB switching
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    The on-board SD-NAND is connected to SDIO1 and powered through GPIO53. Type-C is routed
    to the CH340 by default for logging and flashing. The ``usbsw`` command changes GPIO54
    to route Type-C to BK7259 USB for features such as USB MSC.

5. Debugging and FAQ
---------------------------------

Q: The CP Non-Secure success message is missing.

A: Check the BL1, BL2 and TF-M logs first. For image verification, manifest, Flash AES or
security-counter failures, verify that the flashed image matches the OTP/eFuse settings.

Q: ``AP boot vote failed`` is printed.

A: The CP could not complete the AP power-up sequence. Check the AP power domain, AP
MPC/PPHS configuration and the ``psa_ap_secure_prepare`` NSC call.

Q: The LCD does not enter the demo center.

A: Confirm that ``LVGL ready`` and ``LVGL started`` are present, then check the MIPI LCD,
CST9217 touch configuration and the flashed AP image.

Q: Cloud AI does not connect after provisioning.

A: Verify internet connectivity and confirm that the Agora App ID, token and PC/cloud Agent
are configured correctly.

Q: Can the development keys be reused for production?

A: No. Replace all development keys and use a controlled production key lifecycle and
OTP/eFuse injection process.
