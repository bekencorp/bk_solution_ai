# Beken BK AI Solution

* [中文](./README_CN.md)

## Overview

The **BK AI Solution** is an intelligent AI device solution developed by Beken Corporation based on the **Armino SMP (BK AVDK SMP)** architecture. It provides end-to-cloud and cloud-to-large-model AI interaction, supports multiple large language model integrations, and helps you build AI devices quickly. It currently supports integrations such as **Agora** and **VolcEngine** RTC and large-model applications.

## Documentation

- [AI Solution documentation (home)](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/index.html)
- [Quick Start](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/get-started/index.html) (code checkout, build environment, build and flash)
- [Armino SMP (BK AVDK SMP)](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html)

English documentation source in this repository: `docs/bk7258/en/` (introduction, quick start, HW reference, developer guide, reference projects, third-party projects, etc.). See [docs/README.md](docs/README.md) for the `docs/` layout and local HTML build notes.

## Getting the code

### 1. Armino SMP SDK

**GitLab**

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://gitlab.bekencorp.com/armino/bk_avdk_smp.git -b release/v3.1.1
```

**GitHub**

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://github.com/bekencorp/bk_avdk_smp.git -b release/v3.1.1
```

### 2. AI Solution (this repository)

**GitLab**

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v3.1.1
```

**GitHub**

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://github.com/bekencorp/bk_solution_ai.git -b release/v3.1.1
```

## Build environment

Before building, set up the SMP SDK build environment: use a **local** environment (recommended; Windows / Linux) or **Docker** (Linux / macOS / Windows). If you are not familiar with Docker or cannot use it, use the local setup.

For details (install scripts, Armino Bash on Windows, Docker images, `dbuild`, etc.), see **Environment deployment and build** under [Quick Start](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/get-started/index.html).

## Building

The examples below use the `beken_genie` project; for other projects, change the path to `projects/<project_name>`.

**Option 1: Pass SDK path on the command line**

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7258 SDK_DIR=~/armino/bk_avdk_smp
```

**Option 2: `export SDK_DIR`**

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
export SDK_DIR=~/armino/bk_avdk_smp
make clean
make bk7258
```

**Option 3: Docker (Linux / macOS)**

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make clean
./dbuild.sh make bk7258
```

**Option 4: Docker (Windows PowerShell)**

```powershell
cd C:\armino\bk_solution_ai\projects\beken_genie
$env:SDK_DIR = "C:\armino\bk_avdk_smp"
.\dbuild.ps1 make clean
.\dbuild.ps1 make bk7258
```

## Reference projects

The AI Solution includes reference projects such as Agora RTC, VolcEngine RTC, and AI Camera.

### Agora RTC (`beken_genie`)

BK7258 + Agora RTC SDK, end-to-cloud and cloud-to-model AI interaction.

- Agora RTC, audio processing (AEC, NS, KWS)
- OPUS, PCM, prompt tones; multiple LLMs (OpenAI, Doubao, DeepSeek, etc.)
- Dual SPI LCD; peripherals: gyroscope, NFC, keys, vibration motor, NAND Flash, LEDs, charging, DVP camera, etc.

### VolcEngine RTC (`volc_rtc`)

BK7258 + VolcEngine RTC SDK, real-time dialog with cloud AI Agent.

- VolcEngine RTC, audio (AEC, NS), G722 / OPUS / PCM
- VolcEngine AI Agent, voice and vision features, dual SPI LCD and similar peripherals

### AI Camera (`ai_camera`)

AI camera scenario (under active development).

For more detail, see [Reference projects](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/projects/index.html).

## Flashing firmware

After a successful build, `all-app.bin` is produced under `build/bk7258/<project_name>/package` (e.g. `build/bk7258/beken_genie/package` for `beken_genie`). Flash it to the board with the Beken flashing tool.

For UART flashing and tools, see the SMP documentation: [Quick Start](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html) and [SMP home](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html).

## APP and demos

- APP download: [App download](https://docs.bekencorp.com/arminodoc/bk_app/app/en/v2.0.1/app_download/index.html) (register and sign in with email)
- Provisioning, starting the Agent, etc.: see [AI Solution introduction](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/intro/index.html) and [Quick Start](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/get-started/index.html)
