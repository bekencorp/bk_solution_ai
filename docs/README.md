# Beken BK7259 Robot Solution

## Overview

The **BK7259 Robot Solution** is a reference design developed by Beken Corporation on top of the BK7259 SoC and the **Armino SMP (BK AVDK SMP)** v4.0.x architecture. It provides a turn-key robot example covering LCD UI, local voice wake-word, AI voice / vision dialog, BLE provisioning, and a rich set of sensors and peripherals.

The robot solution is delivered through the **AI Solution** repository (`bk_solution_ai`); its repo root contains `projects/` / `components/` / `docs/` directly, with no extra `solution/` wrapper. The currently published reference project is `projects/beken_robot`, which uses **Agora RTC** to connect to a cloud AI Agent for voice / vision multimodal dialog.


## Documentation

- Chinese sources: `docs/bk7259/zh_CN/` (intro, quick start, HW reference, developer guide, reference projects, third-party).
- English sources: `docs/bk7259/en/`.

## Getting the code

### 1. Armino SMP SDK (BK7259 v4.0.1)

**GitLab**

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v4.0.1
```

**GitHub**

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://github.com/bekencorp/bk_avdk_smp.git -b release/v4.0.1
```

### 2. BK7259 Robot solution (the AI Solution repo)

Browse the repo at: [bk_solution_ai (release/v4.0.1)](https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai/-/tree/release/v4.0.1?ref_type=heads).

**GitLab**

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v4.0.1
```

**GitHub** (when made public)

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://github.com/bekencorp/bk_solution_ai.git -b release/v4.0.1
```

## Build environment

`Armino SMP` provides both **local** (recommended; Windows / Linux) and **Docker** (Linux / macOS / Windows) build environments. Refer to the SMP *Quick Start* online docs for environment scripts, the Armino Bash on Windows, Docker images and `dbuild` usage.

## Building

The examples below use the `beken_robot` project (repo root = `~/armino/bk_solution_ai`, project at `projects/beken_robot`).

**Option 1: pass `SDK_DIR` on the command line**

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

**Option 2: `export SDK_DIR`**

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
export SDK_DIR=~/armino/bk_avdk_smp
make clean
make bk7259
```

**Option 3: Docker (Linux / macOS)**

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make clean
./dbuild.sh make bk7259
```

**Option 4: Docker (Windows PowerShell)**

```powershell
cd C:\armino\bk_solution_ai\projects\beken_robot
$env:SDK_DIR = "C:\armino\bk_avdk_smp"
.\dbuild.ps1 make clean
.\dbuild.ps1 make bk7259
```

## Reference project

### `beken_robot` (Agora RTC variant)

A complete robot reference implementation on BK7259 + Armino SMP v4.0.x:

- **Display**: 360x390 MIPI LCD (`jd9855` panel), LVGL UI with 10 demo pages (boot logo, main menu, provisioning, sound source localization, AI dialog, vision recognition, command words, music, volume, etc.).
- **Audio**: dual on-board microphones + speaker; AEC / NS, local wake words "Ni Hao Bo Tong / Zai Jian Bo Tong", prompt tones; AI dialog over Agora RTC + OPUS.
- **Vision**: MIPI CSI camera capture, uploaded over Agora RTC for vision question-answer mode.
- **Network**: WiFi STA + BLE provisioning (BK App), with a 4G cellular module slot reserved; once provisioned, the WiFi icon appears in the main menu.
- **Sensors / peripherals**: 4 push buttons (S2 / S4 / S5 are functional on the V1 board), ToF, ambient light, G-Sensor, NFC, LED indicator, vibration motor, servo; battery / charger reserved.
- **Storage / OTA**: partitions and SD-NAND assets; factory config and OTA hooks.

Full project notes, key map, UI flow and Kconfig settings live in the online docs under *Reference projects → BK7259 Robot project*.

## Flashing firmware

After a successful build, the binary is at (relative to the repo root `bk_solution_ai/`):

```
projects/beken_robot/build/bk7259/beken_robot/package/all-app.bin
```

Flash it with the Beken `BKFIL` tool over UART.

## Power-up & provisioning

- After power-up, the LCD shows the blue "BK7259 robot solution" logo. Use the V1 board key map (see the online docs under *Reference projects → BK7259 Robot project*) to enter the main menu.
- From the main menu choose **Provisioning → Start** (short press the *Confirm* key) and complete BLE provisioning with BK App. Once successful, the WiFi icon appears in the upper-right corner.

## App

- BK App download: [app download notes](https://docs.bekencorp.com/arminodoc/bk_app/app/en/v2.0.1/app_download/index.html) (sign in with email).
- Provisioning and device-management steps: see *Quick Start* and *Reference projects* in the online docs.
