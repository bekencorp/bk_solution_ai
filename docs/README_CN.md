# 博通集成 BK7259 机器人解决方案

- [English](./README.md)

## 概述

**BK7259 机器人解决方案**是 BEKEN 发布并开源的智能机器人软硬件一体化解决方案。该方案基于 **BK7259** 主控芯片，依赖 Armino 基础 SDK **BK_AVDK_SMP**。方案提供完整的端到端示例工程，涵盖 LCD 显示、本地语音唤醒、声源定位、命令词识别、AI 语音与视觉对话、本地人脸识别与手势识别等 NPU 应用，以及 BLE 配网、Wi-Fi 图传、H.264 编解码和多种传感器与外设。

## 文档

- [BK7259 机器人方案在线文档](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/index.html)
- [Armino SMP SDK（BK AVDK SMP）](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/index.html)

## 硬件

本方案硬件使用 BK7259 机器人开发套件。该套件集成了 BK7259 主控芯片、LCD 显示屏、MIPI CSI 摄像头、双麦阵列、扬声器，以及 ToF 距离感应、环境光、G-Sensor、NFC、LED、震动马达、按键、4G 模组、SD NAND 等机器人常用外设。

- [开发套件硬件资料](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/hw-reference/index.html)
- [BK7259 Datasheet](https://docs.bekencorp.com/spec/BK7259/BK7259_Datasheet.pdf)

开发套件购买链接：即将上架。

## 版本策略

本方案采用“维护分支 + 发布标签（Tag）”的版本管理方式：

- `release/v4.0.1` 为持续维护分支，用于该版本系列的功能迭代和问题修复，始终包含最新代码，但其中可能包含尚未完成完整发布测试的改动。
- `release/v4.0.1.x` 为正式发布标签，例如 `release/v4.0.1.1`、`release/v4.0.1.2`。每个标签均经过完整的测试与发布流程，可作为量产版本使用。
- BK7259 机器人方案与 Armino SMP SDK 必须使用完全相同的版本标签。例如，方案代码使用 `release/v4.0.1.4` 时，SDK 也必须使用 `release/v4.0.1.4`。

建议客户选择 `release/v4.0.1.x` 系列中版本号最大的标签进行开发和量产。仅需体验最新功能或参与开发时，才建议使用 `release/v4.0.1` 分支。

## 获取代码

BK7259 机器人方案和 Armino SMP SDK 均同步发布至 GitHub、Gitee 和 GitLab，可根据网络环境及访问权限选择代码源。

BK7259 机器人方案：

- GitHub：<https://github.com/bekencorp/bk_solution_ai>
- Gitee：<https://gitee.com/bekencorp/bk_solution_ai>
- GitLab：<https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai>

Armino SMP SDK：

- GitHub：<https://github.com/bekencorp/bk_avdk_smp>
- Gitee：<https://gitee.com/bekencorp/bk_avdk_smp>
- GitLab：<https://gitlab.bekencorp.com/armino/bk_avdk_smp>

GitHub 和 Gitee 可公开访问。GitLab 仅面向企业客户开放；企业客户如需访问，请联系对接的 FAE 或销售人员申请开通权限。

**Windows 用户注意**：使用 Git for Windows 获取代码时，建议在克隆前关闭自动换行符转换，避免脚本或源文件被转换为 CRLF 而导致编译失败。Linux、macOS 和 WSL 环境无需执行。

```bash
git config --global core.autocrlf false
```

如果代码已经克隆，修改该配置不会自动恢复文件，建议设置后重新克隆。

以下命令以 GitHub 和 `release/v4.0.1.6` 标签为例。**实际获取代码时，请选择最新发布的标签，并确保方案代码与 SDK 使用完全相同的标签**。使用其他代码源时，替换对应的仓库地址即可。

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_avdk_smp.git

# BK7259 机器人方案
git clone --branch release/v4.0.1.6 https://github.com/bekencorp/bk_solution_ai.git
```

代码获取的更多说明见 [快速入门](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/get-started/index.html)。

## 编译环境安装

Armino SMP 支持本地编译和 Docker 编译，可根据开发平台选择以下方式部署编译环境。

### Linux 本地编译

进入 SDK 目录并运行环境安装脚本：

```bash
# 安装脚本位于 bk_avdk_smp 仓库内
cd ~/armino/bk_avdk_smp
sudo bash tools/env_tools/setup/armino_env_setup.sh
```

### Windows 本地编译

下载并安装 [Armino Bash](https://dl.bekencorp.com/tools/arminosdk/WindowsInstaller/Armino-Bash-Setup_0.3.0.exe)。

### Docker 编译

Docker 编译镜像为 [`bekencorp/armino-idk`](https://hub.docker.com/r/bekencorp/armino-idk/tags)，请选择 `1.5` 或更高版本的镜像标签，支持 Windows / Linux / macOS。

详细安装步骤请参阅 [Armino SMP 快速入门：环境部署及编译](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/get-started/index.html)。

## 编译工程

以 `beken_robot` 工程为例（位于 `projects/beken_robot`），通过 `SDK_DIR` 指向 Armino SMP SDK 后本地编译：

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
make bk7259 SDK_DIR=~/armino/bk_avdk_smp        # 也可先 export SDK_DIR 再执行 make bk7259
```

也支持 Docker 编译（Linux / macOS 用 `./dbuild.sh`，Windows PowerShell 用 `.\dbuild.ps1`）：

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7259
```

编译成功后，用于烧录的固件文件位于以下路径（相对于 `bk_solution_ai/` 仓库根目录）：

```text
projects/beken_robot/build/bk7259/beken_robot/package/all-app.bin
```

编译命令的详细说明请参阅 [快速入门](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/get-started/index.html)。

## 烧录固件

可选择以下任一方式烧录固件：

- 下载并使用 [BKFIL 本地烧录工具](https://dl.bekencorp.com/tools/bkfil/v4)
- 使用 [BKFIL 网页烧录工具](https://connect.aclsemi.com/)

烧录时请选择上一节编译生成的 `all-app.bin` 固件文件。

详细烧录流程请参阅 [Armino SMP 快速入门](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/get-started/index.html)。

## 参考工程

| 工程名 | 主要功能 | 详细说明 |
| --- | --- | --- |
| [beken_robot](../projects/beken_robot/) | LCD / LVGL 显示、本地语音唤醒、声源定位、AI 语音与视觉对话、BLE 配网、NPU 视觉识别、多传感器与外设控制、Wi-Fi 图传、SD NAND 存储等。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/projects/beken_robot/index.html) |
| [secureboot_ai](../projects/secureboot_ai/) | 在机器人 AI 应用栈上集成 BL1、BL2/MCUboot、TF-M、镜像签名和 Flash AES 的安全启动参考工程。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/projects/secureboot_ai/index.html) |
| [baf_example](../projects/baf_example/) | BAF 动画播放示例：RAW/LVGL 后端、固件内置与 TF 卡 ``.baf`` 多层合成播放。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/projects/baf_example/index.html) |

## BEKEN 相关资源

- [BEKEN 官网](https://www.bekencorp.com/)
- [ARMINO 开发者论坛](https://armino.bekencorp.com/)
- [BEKEN 文档中心](https://docs.bekencorp.com/)
- 微信视频号: 博通集成电路
