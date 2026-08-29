# Beken BK7258 AI Solution

- [中文](./README_CN.md)

## Overview

The **BK7258 AI Solution** is an open-source, intelligent AI device solution released by Beken. It is based on the **BK7258** SoC and depends on the Armino base SDK **BK_AVDK_SMP**, providing complete end-to-cloud and cloud-to-large-model AI interaction. The solution supports voice wake-up, voice and vision dialog, image recognition, and dual-screen display, and integrates real-time RTC audio/video and on-device audio processing, together with peripherals such as dual LCD screens, a camera, and a microphone.

## Documentation

- [BK7258 AI Solution online documentation](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/index.html)
- [Armino SMP SDK (BK AVDK SMP)](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/index.html)

## Hardware

The hardware for this solution is the BK7258 AI Toy Development Kit. The kit integrates the BK7258 SoC, dual SPI LCD displays, a DVP camera, a microphone, and a speaker, along with common AI-device peripherals such as a gyroscope, NFC, buttons, LEDs, a vibration motor, charging management, a lithium battery, and SD NAND.

- [Development kit hardware materials](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/hw-reference/index.html)
- [BK7258 Datasheet](https://docs.bekencorp.com/spec/BK7258/BK7258%C2%A0Datasheet.pdf)

Development kit purchase links: [AIDK development board (Taobao)](https://item.taobao.com/item.htm?id=899826514287); BK7258 [chip purchase (Taobao)](https://item.taobao.com/item.htm?id=952954434009).

## Version Strategy

This solution uses a "maintenance branch + release tag" version management approach:

- `release/v3.1.1` is the ongoing maintenance branch, used for feature iteration and bug fixes of this version series. It always contains the latest code, but may include changes that have not yet completed full release testing.
- `release/v3.1.1.x` are formal release tags, for example `release/v3.1.1.1` and `release/v3.1.1.2`. Each tag has gone through the complete test and release process and can be used for mass production.
- The AI Solution and the Armino SMP SDK must use exactly the same version tag. For example, when the solution code uses `release/v3.1.1.4`, the SDK must also use `release/v3.1.1.4`.

We recommend that you choose the tag with the highest version number in the `release/v3.1.1.x` series for development and mass production. Only use the `release/v3.1.1` branch when you need to try the latest features or participate in development.

## Get the Code

The BK7258 AI Solution and the Armino SMP SDK are published simultaneously on GitHub, Gitee, and GitLab. Choose a code source based on your network environment and access permissions.

AI Solution:

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

The commands below use GitHub and the `release/v3.1.1.8` tag as an example. **When actually getting the code, choose the latest released tag, and make sure the solution code and the SDK use exactly the same tag.** When using another code source, simply replace the corresponding repository URL.

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_avdk_smp.git

# AI Solution
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_solution_ai.git
```

For more details on getting the code, see [Quick Start](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/get-started/index.html).

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

For detailed setup steps, see [Armino SMP Quick Start: Environment deployment and build](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html).

## Build the Project

Using the `beken_genie` project as an example (located at `projects/beken_genie`), build locally by pointing `SDK_DIR` to the Armino SMP SDK (the root directory of the cloned `bk_avdk_smp` repository):

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
make bk7258 SDK_DIR=~/armino/bk_avdk_smp        # or export SDK_DIR first, then run make bk7258
```

Docker builds are also supported (use `./dbuild.sh` on Linux / macOS, and `.\dbuild.ps1` in Windows PowerShell):

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7258
```

After a successful build, the firmware file used for flashing is located at the following path (relative to the `bk_solution_ai/` repository root):

```text
projects/beken_genie/build/bk7258/beken_genie/package/all-app.bin
```

For other projects, replace the path in the commands with the corresponding `projects/<project_name>`. For detailed build commands, see [Quick Start](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/get-started/index.html).

## Flash the Firmware

You can flash the firmware using either of the following methods:

- Download and use the [BKFIL local flashing tool](https://dl.bekencorp.com/tools/flash/)
- Use the [BKFIL web flashing tool](https://connect.aclsemi.com/)

When flashing, select the `all-app.bin` firmware file generated in the previous section.

For the detailed flashing process, see [Armino SMP Quick Start](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/en/v3.1.1/get-started/index.html).

## Reference Projects

| Project | Main features | Details |
| --- | --- | --- |
| [beken_genie](../projects/beken_genie/) | Agora RTC version: dual-screen display, voice wake-up and AI dialog, image recognition, AEC / NS / KWS, integration with multiple large language models (OpenAI, Doubao, DeepSeek, etc.), and peripheral references such as gyroscope / NFC / buttons / vibration motor / LED / charging management / DVP camera. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/projects/beken_genie/index.html) |
| [volc_rtc](../projects/volc_rtc/) | VolcEngine RTC version: similar to `beken_genie`, with the RTC backend replaced by VolcEngine. | [Detailed and usage documentation (online)](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/projects/volc_rtc/index.html) |

For project selection guidance, see [Reference Projects](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/en/v3.1.1/projects/index.html) in the online documentation.

## Beken Resources

- [Beken official website](https://www.bekencorp.com/)
- [Armino developer forum](https://armino.bekencorp.com/)
- [Beken documentation center](https://docs.bekencorp.com/)
- Bilibili: Beken product solutions
