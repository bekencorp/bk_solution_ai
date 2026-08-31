# Beken BK7259 Robot Solution

- [中文](./README_CN.md)

## Overview

The **BK7259 Robot Solution** is an open-source, integrated hardware-and-software robot solution released by Beken. It is based on the **BK7259** SoC and depends on the Armino base SDK **BK_AVDK_SMP**. The solution provides complete end-to-end example projects covering LCD display, local voice wake-up, sound source localization, command-word recognition, AI voice and vision dialog, on-device NPU applications such as face recognition and gesture recognition, as well as BLE provisioning, Wi-Fi video transmission, H.264 encoding/decoding, and a variety of sensors and peripherals.

## Documentation

- [BK7259 Robot Solution online documentation](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/en/v4.0.1/index.html)
- [Armino SMP SDK (BK AVDK SMP)](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/index.html)

## Hardware

The hardware for this solution is the BK7259 Robot Development Kit. The kit integrates the BK7259 SoC, an LCD display, a MIPI CSI camera, a dual-microphone array, and a speaker, along with common robot peripherals such as ToF distance sensing, ambient light, G-Sensor, NFC, LED, vibration motor, buttons, a 4G module, and SD NAND.

- [Development kit hardware materials](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/en/v4.0.1/hw-reference/index.html)
- [BK7259 Datasheet](https://docs.bekencorp.com/spec/BK7259/BK7259_Datasheet.pdf)

Development kit purchase link: coming soon.

## Version Strategy

This solution uses a "maintenance branch + release tag" version management approach:

- `release/v4.0.1` is the ongoing maintenance branch, used for feature iteration and bug fixes of this version series. It always contains the latest code, but may include changes that have not yet completed full release testing.
- `release/v4.0.1.x` are formal release tags, for example `release/v4.0.1.1` and `release/v4.0.1.2`. Each tag has gone through the complete test and release process and can be used for mass production.
- The BK7259 Robot Solution and the Armino SMP SDK must use exactly the same version tag. For example, when the solution code uses `release/v4.0.1.4`, the SDK must also use `release/v4.0.1.4`.

We recommend that customers choose the tag with the highest version number in the `release/v4.0.1.x` series for development and mass production. Only use the `release/v4.0.1` branch when you need to try the latest features or participate in development.

## Get the Code

The BK7259 Robot Solution and the Armino SMP SDK are published simultaneously on GitHub, Gitee, and GitLab. Choose a code source based on your network environment and access permissions.

BK7259 Robot Solution:

- GitHub: <https://github.com/bekencorp/bk_solution_ai>
- Gitee: <https://gitee.com/bekencorp/bk_solution_ai>
- GitLab: <https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai>

Armino SMP SDK:

- GitHub: <https://github.com/bekencorp/bk_avdk_smp>
- Gitee: <https://gitee.com/bekencorp/bk_avdk_smp>
- GitLab: <https://gitlab.bekencorp.com/armino/bk_avdk_smp>

GitHub and Gitee are publicly accessible. GitLab is open to enterprise customers only; enterprise customers who need access should contact their FAE or sales representative to apply for permission.

**Note for Windows users:** When using Git for Windows to get the code, we recommend disabling automatic line-ending conversion before cloning, to avoid scripts or source files being converted to CRLF and causing build failures. This is not required on Linux, macOS, or WSL.

```bash
git config --global core.autocrlf false
```

If the code has already been cloned, changing this setting will not automatically restore the files; we recommend re-cloning after applying it.

The commands below use GitHub and the `release/v4.0.1.6` tag as an example. **When actually getting the code, choose the latest released tag, and make sure the solution code and the SDK use exactly the same tag.** When using another code source, simply replace the corresponding repository URL.

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_avdk_smp.git

# BK7259 Robot Solution
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_solution_ai.git
```

For more details on getting the code, see [Quick Start](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/en/v4.0.1/get-started/index.html).

## Set Up the Build Environment

Armino SMP supports both local builds and Docker builds. Choose one of the following methods to set up the build environment based on your development platform.

### Local Build on Linux

Enter the SDK directory and run the environment setup script:

```bash
# The setup script is located in the bk_avdk_smp repository
cd ~/armino/bk_avdk_smp
sudo bash tools/env_tools/setup/armino_env_setup.sh
```

### Local Build on Windows

Download and install [Armino Bash](https://dl.bekencorp.com/tools/arminosdk/WindowsInstaller/Armino-Bash-Setup_0.3.0.exe).

### Docker Build

The Docker build image is [`bekencorp/armino-idk`](https://hub.docker.com/r/bekencorp/armino-idk/tags). Choose an image tag of `1.5` or higher; it supports Windows / Linux / macOS.

For detailed setup steps, see [Armino SMP Quick Start: Environment deployment and build](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html).

## Build the Project

Using the `beken_robot` project as an example (located at `projects/beken_robot`), build locally by pointing `SDK_DIR` to the Armino SMP SDK:

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
make bk7259 SDK_DIR=~/armino/bk_avdk_smp        # or export SDK_DIR first, then run make bk7259
```

Docker builds are also supported (use `./dbuild.sh` on Linux / macOS, and `.\dbuild.ps1` in Windows PowerShell):

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7259
```

After a successful build, the firmware file used for flashing is located at the following path (relative to the `bk_solution_ai/` repository root):

```text
projects/beken_robot/build/bk7259/beken_robot/package/all-app.bin
```

For detailed build commands, see [Quick Start](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/en/v4.0.1/get-started/index.html).

## Flash the Firmware

You can flash the firmware using either of the following methods:

- Download and use the [BKFIL local flashing tool](https://dl.bekencorp.com/tools/bkfil/v4)
- Use the [BKFIL web flashing tool](https://connect.aclsemi.com/)

When flashing, select the `all-app.bin` firmware file generated in the previous section.

For the detailed flashing process, see [Armino SMP Quick Start](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/en/v4.0.1/get-started/index.html).

## Reference Projects

| Project | Main features | Details |
| --- | --- | --- |
| [beken_robot](../projects/beken_robot/) | LCD / LVGL display, local voice wake-up, sound source localization, AI voice and vision dialog, BLE provisioning, NPU vision recognition, multi-sensor and peripheral control, Wi-Fi video transmission, SD NAND storage, and more. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/en/v4.0.1/projects/beken_robot/index.html) |
| [secureboot_ai](../projects/secureboot_ai/) | Secure-boot reference integrating BL1, BL2/MCUboot, TF-M, image signing and Flash AES with the robot AI stack. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/en/v4.0.1/projects/secureboot_ai/index.html) |
| [baf_example](../projects/baf_example/) | BAF animation playback demo: RAW/LVGL backends, firmware-embedded and TF-card ``.baf`` multi-layer compositing. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/en/v4.0.1/projects/baf_example/index.html) |

## Beken Resources

- [Beken official website](https://www.bekencorp.com/)
- [Armino developer forum](https://armino.bekencorp.com/)
- [Beken documentation center](https://docs.bekencorp.com/)
- WeChat Channels: Beken Corporation
