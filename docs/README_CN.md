# 博通集成 BK7258 AI 解决方案

- [English](./README.md)

## 概述

**BK7258 AI 解决方案**是 BEKEN 发布并开源的智能 AI 设备解决方案，基于 **BK7258** 主控芯片，并依赖 Armino 基础 SDK **BK_AVDK_SMP**，提供完整的端到云、云到大模型的 AI 交互能力。方案支持语音唤醒、语音与视觉对话、图像识别与双屏显示，集成 RTC 实时音视频、端侧音频处理等能力，并搭配双屏 LCD、摄像头、麦克风等外设。

## 文档

- [BK7258 AI 解决方案在线文档](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/index.html)
- [Armino SMP SDK（BK AVDK SMP）](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html)

## 硬件

本方案硬件使用 BK7258 AI 玩具开发套件。该套件集成了 BK7258 主控芯片、双 SPI LCD 显示屏、DVP 摄像头、麦克风、扬声器，以及陀螺仪、NFC、按键、LED、震动马达、充电管理、锂电池、SD NAND 等 AI 设备常用外设。

- [开发套件硬件资料](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/hw-reference/index.html)
- [BK7258 Datasheet](https://docs.bekencorp.com/spec/BK7258/BK7258%C2%A0Datasheet.pdf)

开发套件购买链接：[AIDK 开发板（淘宝）](https://item.taobao.com/item.htm?id=899826514287)；BK7258 [芯片采购（淘宝）](https://item.taobao.com/item.htm?id=952954434009)。

## 版本策略

本方案采用“维护分支 + 发布标签（Tag）”的版本管理方式：

- `release/v3.1.1` 为持续维护分支，用于该版本系列的功能迭代和问题修复，始终包含最新代码，但其中可能包含尚未完成完整发布测试的改动。
- `release/v3.1.1.x` 为正式发布标签，例如 `release/v3.1.1.1`、`release/v3.1.1.2`。每个标签均经过完整的测试与发布流程，可作为量产版本使用。
- AI 解决方案与 Armino SMP SDK 必须使用完全相同的版本标签。例如，方案代码使用 `release/v3.1.1.4` 时，SDK 也必须使用 `release/v3.1.1.4`。

建议您选择 `release/v3.1.1.x` 系列中版本号最大的标签进行开发和量产。仅需体验最新功能或参与开发时，才建议使用 `release/v3.1.1` 分支。

## 获取代码

BK7258 AI 解决方案和 Armino SMP SDK 均同步发布至 GitHub、Gitee 和 GitLab，可根据网络环境及访问权限选择代码源。

AI 解决方案：

- GitHub：[https://github.com/bekencorp/bk_solution_ai](https://github.com/bekencorp/bk_solution_ai)
- Gitee：[https://gitee.com/bekencorp/bk_solution_ai](https://gitee.com/bekencorp/bk_solution_ai)
- GitLab：[https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai](https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai)

Armino SMP SDK：

- GitHub：[https://github.com/bekencorp/bk_avdk_smp](https://github.com/bekencorp/bk_avdk_smp)
- Gitee：[https://gitee.com/bekencorp/bk_avdk_smp](https://gitee.com/bekencorp/bk_avdk_smp)
- GitLab：[https://gitlab.bekencorp.com/armino/bk_avdk_smp](https://gitlab.bekencorp.com/armino/bk_avdk_smp)

GitHub 和 Gitee 可公开访问。GitLab 仅面向企业客户开放；企业客户如需访问，请联系对接的 FAE 或销售人员申请开通权限。

**Windows 用户注意**：使用 Git for Windows 获取代码时，建议在克隆前关闭自动换行符转换，避免脚本或源文件被转换为 CRLF 而导致编译失败。Linux、macOS 和 WSL 环境无需执行。

```bash
git config --global core.autocrlf false
```

如果代码已经克隆，修改该配置不会自动恢复文件，建议设置后重新克隆。

以下命令以 GitHub 和 `release/v3.1.1.8` 标签为例。**实际获取代码时，请选择最新发布的标签，并确保方案代码与 SDK 使用完全相同的标签**。使用其他代码源时，替换对应的仓库地址即可。

```bash
mkdir -p ~/armino && cd ~/armino

# Armino SMP SDK
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_avdk_smp.git

# AI 解决方案
git clone --branch release/v3.1.1.8 https://github.com/bekencorp/bk_solution_ai.git
```

代码获取的更多说明请参阅 [快速开始](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/get-started/index.html)。

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

Docker 编译镜像为 `[bekencorp/armino-idk](https://hub.docker.com/r/bekencorp/armino-idk/tags)`，请选择 `1.5` 或更高版本的镜像标签，支持 Windows / Linux / macOS。

详细安装步骤请参阅 [Armino SMP 快速入门：环境部署及编译](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html)。

## 编译工程

以 `beken_genie` 工程为例（位于 `projects/beken_genie`），通过 `SDK_DIR` 指向 Armino SMP SDK（即已克隆的 `bk_avdk_smp` 仓库根目录）后本地编译：

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
make bk7258 SDK_DIR=~/armino/bk_avdk_smp        # 也可先 export SDK_DIR 再执行 make bk7258
```

也支持 Docker 编译（Linux / macOS 用 `./dbuild.sh`，Windows PowerShell 用 `.\dbuild.ps1`）：

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7258
```

编译成功后，用于烧录的固件文件位于以下路径（相对于 `bk_solution_ai/` 仓库根目录）：

```text
projects/beken_genie/build/bk7258/beken_genie/package/all-app.bin
```

其他工程请将命令中的路径替换为对应的 `projects/<工程名>`。编译命令的详细说明请参阅 [快速开始](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/get-started/index.html)。

## 烧录固件

可选择以下任一方式烧录固件：

- 下载并使用 [BKFIL 本地烧录工具](https://dl.bekencorp.com/tools/flash/)
- 使用 [BKFIL 网页烧录工具](https://connect.aclsemi.com/)

烧录时请选择上一节编译生成的 `all-app.bin` 固件文件。

详细烧录流程请参阅 [Armino SMP 快速入门](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html)。

## 参考工程


| 工程名                                     | 主要功能                                                                                                                                      | 详细说明                                                                                                                |
| --------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------- |
| [beken_genie](../projects/beken_genie/) | 声网 Agora RTC 版本：双屏显示、语音唤醒与 AI 对话、图像识别、AEC / NS / KWS、多种大语言模型接入（OpenAI、豆包、DeepSeek 等），以及陀螺仪 / NFC / 按键 / 震动马达 / LED / 充电管理 / DVP 摄像头等外设参考。 | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/projects/beken_genie/index.html) |
| [volc_rtc](../projects/volc_rtc/)       | 火山引擎 RTC 版本：功能与 `beken_genie` 类似，RTC 后端替换为火山引擎（VolcEngine）。                                                                               | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/projects/volc_rtc/index.html)    |
| [volc_rtc_ab](../projects/volc_rtc_ab/) | 火山引擎 RTC OTA A/B 版本：功能与 `volc_rtc` 一致，在其基础上增加 OTA A/B 双分区升级能力（区别在于分区布局与固件升级流程）。                                                             | [详细说明及使用说明在线文档](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/projects/volc_rtc_ab/index.html) |


工程选型说明请参阅在线文档 [参考工程](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/projects/index.html)。

## BEKEN 相关资源

- [BEKEN 官网](https://www.bekencorp.com/)
- [ARMINO 开发者论坛](https://armino.bekencorp.com/)
- [BEKEN 文档中心](https://docs.bekencorp.com/)
- 哔哩哔哩：博通集成产品解决方案

