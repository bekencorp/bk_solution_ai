# BK7259 Secure Boot AI Example Project

- [中文](./README_CN.md)

## 1. Introduction

`secureboot_ai` extends the robot AI application stack from [`beken_robot`](../beken_robot/) with BL1, BL2/MCUboot, TF-M, image signing, and Flash AES encryption. It demonstrates how a BK7259 device enters the robot AI application through a trusted boot chain.

In addition to secure boot, the project provides the same LCD touch UI, BLE/Wi-Fi provisioning, edge AI, cloud AI, audio/video, and peripheral demos as `beken_robot`. The solution and Armino SMP SDK must use matching release versions.

## 2. Main Configuration

### Security

- **Trusted boot chain**: BootROM → BL1 → BL2/MCUboot → TF-M Secure → CP Non-Secure → AP Non-Secure.
- **Image protection**: EC-P256 image signing and fixed-key Flash AES encryption are enabled.
- **Isolation**: TF-M uses `profile_medium`, isolation level 2, and the Crypto service. Secure operations are exposed to Non-Secure applications through controlled interfaces.
- **Flash layout**: an 8 MB flash device is used with the `XIP_FORCE_A` single-slot strategy. Secondary partitions are placeholders and do not provide a complete A/B update slot.
- **Key configuration**: development signing keys are under `config/key/`. Secure boot, encryption, and version-counter settings are in `partitions/bk7259/security.csv` and `ota.csv`.

### Board

- **Target chip**: BK7259; use the `bk7259` build target.
- **Display and input**: 320×385 MIPI LCD, CST9217 touch panel, and S2-S5 physical buttons.
- **SD-NAND**: the onboard SD-NAND uses **SDIO1** on GPIO14-GPIO19. Its device-side FatFS drive is `1:` and its VFS mount point is `/sd0`.
- **Type-C port**: GPIO54 switches the port between the CH340 UART and BK7259 USB mass-storage mode. UART logs are temporarily unavailable in USB mode.

## 3. Build

Install the Armino SMP build environment and prepare a `bk_avdk_smp` SDK whose release matches the solution. With the current directory layout, pass `SDK_DIR` explicitly on the command line:

```bash
cd ~/armino/bk_solution_ai/projects/secureboot_ai
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

Docker build:

```bash
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make clean
./dbuild.sh make bk7259
```

The main outputs are generated under:

```text
build/bk7259/secureboot_ai/package/
```

- `all-app.bin`: complete flash image.
- `bootloader.bin`: secure-boot bootloader package.
- `ota.bin`: encrypted OTA package.
- `otp_efuse_config.json`: generated OTP/eFuse configuration reference.

During development, use BKFIL to flash `all-app.bin`. See the repository-level [English guide](../../README.md) for environment setup and flashing instructions.

> **Security warning**: OTP/eFuse programming is normally irreversible. Signing private keys and symmetric keys stored in the repository are for development only and must not be used in production. Production requires controlled key generation, storage, and injection procedures.

## 4. Preparation

1. Build and flash `all-app.bin`, then connect the LCD, touch panel, camera, microphone, speaker, and any robot peripherals required by the demos.
2. Copy the supplied resources to the SD-NAND root. Do not create an extra `resources` directory on the disk:

   ```text
   /sd0/
   ├── kws_model/
   │   ├── bk_kws_wakeup.tflite
   │   └── bk_kws_commands.tflite
   ├── tflite/
   │   ├── palm_detection_builtin_256_integer_quant_vela.tflite
   │   ├── yoloface_int8_vela.tflite
   │   ├── hand_gesture_detection_vela.tflite
   │   ├── face_detection_int8_vela.tflite
   │   └── face_verify_int8_vela.tflite
   ├── asr_wakeup_16k_mono_16bit_en.mp3
   └── ...other prompt tones
   ```

3. The recommended method is to select **Settings > USB > USB**, then copy the files from a PC. Safely eject the disk, switch back to **UART**, and restart the device.
4. To use the music demo, place MP3, AAC, or WAV files under `/sd0/music`.
5. Complete Wi-Fi/BLE provisioning under **Connect** before using cloud demos that require network access.

See the [resource file guide](./resources/kws_model_and_prompt_tone_user_mannual.md) for exact KWS model paths and prompt-tone file names.

## 5. Boot Verification

After a successful secure boot, the UART output should contain BL1, BL2/MCUboot, TF-M, and Non-Secure application logs. Check for:

```text
secureboot_ai: CP NS world reached (secure boot OK)
LVGL ready on 320x385 MIPI (first page pending)
LVGL started, page_1 loaded
```

If boot stops during signature verification, manifest processing, Flash AES, or security-counter validation, verify that the flashed image matches the device OTP/eFuse configuration. Secure-boot and application logs may use different UART paths and should be checked separately.

## 6. UI Controls

### Touchscreen

- Tap the welcome screen to open the home screen. Tap menu items to enter; swipe vertically to scroll long lists.
- Swipe right from the left edge to go back.
- In AI Camera, tap to switch between taking a photo and resuming live preview; swipe right to exit.

### Physical Buttons

- **S2 short press**: previous item/focus; takes a photo during AI Camera live preview.
- **S5 short press**: next item/focus; resumes live preview when an AI Camera photo is displayed.
- **S3 short press**: go back; exits full-screen vision demos.
- **S4 short press**: confirm or open the selected item.
- **S4 long press**: page-specific long-confirm action.

The home screen contains **Connect**, **Demo Center**, and **Settings**. Settings provides volume control, UART/USB switching, UI language selection, and factory reset.

## 7. Main Demos

- **Edge AI**: keyword recognition, sound localization, palm following, face detection, hand-gesture recognition, car following, and a camera solution example.
- **Cloud AI**: AI chat, vision recognition, and AI Camera.
- **Fun**: local music playback, live video streaming, and Bluetooth music.
- **System features**: Wi-Fi/BLE provisioning, volume control, SD-NAND/USB mass storage, and Chinese/English UI switching.

Some demos require the corresponding hardware, network service, or model resources and may not work fully until those prerequisites are available.

## 8. Secure Development Notes

- After changing a signing key, Flash AES key, application version, or security counter, regenerate and flash a matching complete image.
- Increase the security counter only according to the product update policy to avoid rejecting existing images or mismatched OTA packages.
- TF-M Persistent Storage, Firmware Update, and Initial Attestation are disabled in the current default configuration.
- Before production, replace all development keys and review the Root of Trust, OTP/eFuse injection process, and rollback policy.
