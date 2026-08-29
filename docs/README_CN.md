# 博通集成 AI 解决方案

## 概述

**BK AI 解决方案**是博通集成电路（上海）股份有限公司基于 **Armino SMP（BK AVDK SMP）** 架构开发的智能 AI 设备解决方案，提供端到云、云到大模型的 AI 交互能力，支持多种大语言模型接入，便于快速构建智能 AI 设备。当前支持声网（Agora）、火山引擎（VolcEngine）等大模型与 RTC 应用。

## 文档

- [AI 解决方案在线文档（首页）](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/index.html)
- [快速入门](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/get-started/index.html)（含代码获取、编译环境、编译与烧录）
- [Armino SMP（BK AVDK SMP）](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html)

本地仓库中文文档源码目录：`docs/bk7258/zh_CN/`（含简介、快速入门、H/W 参考、开发者指南、参考工程、第三方工程等）。文档目录说明与本地构建见 [docs/README_CN.md](docs/README_CN.md)。

## 获取代码

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

### 2. AI 解决方案（本仓库）

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

## 编译环境

在编译工程前，需完成 SMP SDK 侧编译环境部署：可使用 **本地编译环境**（推荐，适用于 Windows / Linux）或 **Docker 编译环境**（Linux / macOS / Windows）。不熟悉 Docker 或网络受限时，请使用本地部署。

详细步骤（本地安装脚本、Windows Armino Bash、Docker 镜像与 `dbuild` 等）见在线文档 [快速入门](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/get-started/index.html) 中的「环境部署及编译」及子章节。

## 编译项目

以下以 `beken_genie` 工程为例（其他工程请将路径改为对应 `projects/<工程名>`）。

**方式一：命令行直接指定 SDK 路径**

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7258 SDK_DIR=~/armino/bk_avdk_smp
```

**方式二：通过环境变量指定 SDK 路径**

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
export SDK_DIR=~/armino/bk_avdk_smp
make clean
make bk7258
```

**方式三：Docker（Linux / macOS）**

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make clean
./dbuild.sh make bk7258
```

**方式四：Docker（Windows PowerShell）**

```powershell
cd C:\armino\bk_solution_ai\projects\beken_genie
$env:SDK_DIR = "C:\armino\bk_avdk_smp"
.\dbuild.ps1 make clean
.\dbuild.ps1 make bk7258
```

## 参考工程简介

BK AI 解决方案主要包含声网 RTC 版本、火山 RTC 版本、AI Camera 版本等参考工程。

### 声网 RTC 版本（`beken_genie`）

基于 BK7258 与声网 RTC SDK，提供端到云、云到大模型的 AI 交互能力。

- 声网 RTC 实时音视频，集成音频处理（AEC、NS、KWS）
- 支持 OPUS、PCM 等音频编码，支持提示音
- 支持多种大语言模型（OpenAI、豆包、DeepSeek 等）
- 支持双 SPI LCD，外设参考：陀螺仪、NFC、按键、震动马达、NAND Flash、LED、充电管理、DVP 摄像头等

### 火山 RTC 版本（`volc_rtc`）

基于 BK7258 与火山引擎 RTC SDK，支持与云端 AI Agent 实时对话。

- 火山 RTC、音频处理（AEC、NS），G722 / OPUS / PCM 等
- 火山 AI Agent、语音与图像相关能力，双 SPI LCD 及类似外设参考设计

### AI Camera 版本（`ai_camera`）

AI 相机类方案（持续演进中）。

更完整的工程说明见在线文档 [参考工程](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/projects/index.html)。

## 烧录固件

编译完成后，在 AI 解决方案目录下 `build/bk7258/<工程名>/package` 中生成 `all-app.bin`（示例工程为 `beken_genie` 时路径为 `build/bk7258/beken_genie/package`），使用烧录工具写入开发板。

固件烧录流程与工具说明请参考 [SMP 文档 - 快速入门 / 烧录相关章节](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html) 及 [SMP 文档首页](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html)。

## APP 与演示

- APP 下载：[应用下载说明](https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_download/index.html)（使用邮箱注册登录）
- 配网、启动 Agent 等操作步骤：见 [AI 解决方案简介](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/intro/index.html) 与 [快速入门](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/get-started/index.html)
