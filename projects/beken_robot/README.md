# BK7259 Robot Example Project

- [中文](./README_CN.md)

## 1. Introduction

`beken_robot` is an integrated example project for the BK7259 robot development kit. It provides ready-to-use demos for the LCD touch UI, network connectivity, on-device AI, cloud AI, audio/video, and peripheral control. This guide covers only the steps needed to start using the project. The solution and Armino SMP SDK must use matching release versions.

## 2. Main Configuration

- **Target chip**: BK7259; use the `bk7259` build target.
- **Display and input**: LVGL GUI, CST9217 touch panel, and S2-S5 physical buttons.
- **SD-NAND**: the onboard SD-NAND uses **SDIO1**, with GPIO14-GPIO19 assigned to CLK, CMD, and DATA0-DATA3. Its device-side FatFS drive is `1:` and its VFS mount point is `/sd0`.
- **Type-C port**: switches between UART log mode and USB mass-storage mode. USB mode allows a PC to access the onboard SD-NAND; UART logs are temporarily unavailable in this mode.
- **Camera and audio**: camera, microphone, speaker, and related audio/video examples are included.

To change features, use the SDK `menuconfig` interface. Make sure that the SD-NAND, LVGL, buttons, KWS, and file-system prompt-tone options required by the project are enabled.

## 3. Build

Install the Armino SMP build environment and prepare a `bk_avdk_smp` SDK whose release version matches the solution.

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

For a clean rebuild:

```bash
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

The repository's Docker build script can also be used:

```bash
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7259
```

The firmware image is generated at:

```text
build/bk7259/beken_robot/package/all-app.bin
```

See the repository-level [English guide](../../README.md) for build-environment setup and flashing instructions.

## 4. Preparation

1. Build and flash `all-app.bin`, then connect the LCD, touch panel, camera, microphone, speaker, and any peripherals required by the demos you want to run.
2. Copy the **contents** of the supplied `resources` directory to the SD-NAND root. Do not create an extra `resources` directory on the disk:

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
   ├── asr_standby_16k_mono_16bit_en.mp3
   └── ...other prompt tones
   ```

   `kws_model/` is used by keyword recognition. `tflite/` is used by the palm-following, face-detection, hand-gesture, car-following, and solution demos. Use model files that match the current firmware release and keep their file names unchanged.
3. The recommended method is to select **Settings > USB > USB** on the device, then copy the files from a PC. Safely eject the disk, switch back to **UART**, and restart the device.
4. To use the music demo, create `/sd0/music` (FatFS: `1:/music`) and place MP3, AAC, or WAV files in it.
5. Complete Wi-Fi/BLE provisioning under **Connect** before using cloud demos that require network access.

See the [resource file guide](./resources/kws_model_and_prompt_tone_user_mannual.md) for exact KWS model paths and prompt-tone file names.

## 5. UI Controls

### Touchscreen

- Welcome screen: tap anywhere to open the home screen.
- Menus: tap an item to open it; swipe vertically to scroll long lists.
- Back: swipe right from the left edge of the screen.
- AI Camera: tap to switch between taking a photo and resuming live preview; swipe right to exit.

### Physical Buttons

- **S2 short press**: previous item/focus; takes a photo during AI Camera live preview.
- **S5 short press**: next item/focus; resumes live preview when an AI Camera photo is displayed.
- **S3 short press**: go back; exits full-screen vision demos.
- **S4 short press**: confirm or open the selected item.
- **S4 long press**: page-specific long-confirm action; ignored on pages that do not implement it.

The home screen contains **Connect**, **Demo Center**, and **Settings**. Settings provides volume control, Type-C UART/USB switching, UI language selection, and factory reset.

## 6. Main Demos

- **Edge AI**: keyword recognition, sound localization, palm following, face detection, hand-gesture recognition, car following, and a camera solution example.
- **Cloud AI**: AI chat, vision recognition, and AI Camera.
- **Fun**: local music playback, live video streaming, and Bluetooth music.
- **System features**: Wi-Fi/BLE provisioning, volume control, SD-NAND/USB mass storage, and Chinese/English UI switching.

Some demos require the corresponding camera, servos, robot hand, robot chassis, network service, or resource files. They may not work fully when the required hardware is not connected.
