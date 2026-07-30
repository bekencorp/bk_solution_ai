# 博通集成 BK7259 机器人方案

* [English](./README.md)

## 概述

**BK7259 机器人方案**是博通集成电路（上海）股份有限公司基于 BK7259 主控、**Armino SMP（BK AVDK SMP）** v4.0.x 架构开发的智能机器人参考设计，提供 LCD 显示、本地语音唤醒、AI 语音 / 视觉对话、BLE 配网、多种传感器与外设的完整端到端示例工程。

机器人方案对应的代码仓库为 **AI Solution**（`bk_solution_ai`），仓库根目录下直接是 `projects/` / `components/` / `docs/`，没有外层 `solution/` 包装。当前发布的参考工程为 `projects/beken_robot`，该工程使用 **声网 Agora RTC** 接入云端 AI Agent，完成语音 / 视觉双模态对话。

## 文档

- [BK7259 机器人方案在线文档（首页）](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/index.html)
- [快速入门](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/get-started/index.html)（含代码获取、编译环境、编译与烧录）
- [Armino SMP（BK AVDK SMP）](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/index.html)

本地仓库中文文档源码目录：`docs/bk7259/zh_CN/`（含简介、快速入门、H/W 参考、开发者指南、参考工程、第三方工程等）。文档目录说明与本地构建见 [docs/README_CN.md](docs/README_CN.md)。

## 获取代码

### 1. Armino SMP SDK（BK7259 v4.0.1）

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

### 2. BK7259 机器人方案（本仓库 AI Solution）

**GitLab**

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://gitlab.bekencorp.com/armino/smp_solution/bk_solution_ai.git -b release/v4.0.1
```

**GitHub**

```bash
mkdir -p ~/armino
cd ~/armino
git clone https://github.com/bekencorp/bk_solution_ai.git -b release/v4.0.1
```

## 编译环境

`Armino SMP` 提供 **本地编译**（推荐，Windows / Linux）和 **Docker 编译**（Linux / macOS / Windows）两套部署方案。环境部署细节（安装脚本、Armino Bash、Docker 镜像、`dbuild` 等）请参阅 [快速入门](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/get-started/index.html) 中的「环境部署及编译」，以及 [SMP 快速入门（BK7259）](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/get-started/index.html)。

## 编译项目

以下以 `beken_robot` 工程为例（仓库根 = `~/armino/bk_solution_ai`，工程位于 `projects/beken_robot`）。

**方式一：命令行直接指定 SDK 路径**

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

**方式二：通过环境变量指定 SDK 路径**

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
export SDK_DIR=~/armino/bk_avdk_smp
make clean
make bk7259
```

**方式三：Docker（Linux / macOS）**

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make clean
./dbuild.sh make bk7259
```

**方式四：Docker（Windows PowerShell）**

```powershell
cd C:\armino\bk_solution_ai\projects\beken_robot
$env:SDK_DIR = "C:\armino\bk_avdk_smp"
.\dbuild.ps1 make clean
.\dbuild.ps1 make bk7259
```

## 参考工程简介

当前公开的 BK7259 机器人参考工程为：

### `beken_robot`（声网 Agora RTC 版本）

基于 BK7259 + Armino SMP v4.0.x，提供机器人产品形态的完整参考实现：

- **显示**：360x390 MIPI LCD（`jd9855` 面板），LVGL 图形界面（10 个 Demo 页面，覆盖开机首页 / 主菜单 / 配网 / 声源定位 / AI 对话 / 视觉识别 / 命令词 / 音乐 / 音量等场景）。
- **音频**：板载双麦克风 + 扬声器；支持 AEC / NS、本地唤醒词「你好博通 / 再见博通」、提示音播放，AI 对话使用 Agora RTC + OPUS。
- **视频**：MIPI CSI 摄像头采集（默认 `jd9855` 配套），通过 Agora RTC 上传，支持视觉问答模式。
- **网络**：WiFi STA + BLE 配网（BK App），并预留 4G 蜂窝模组接口；配网完成后主菜单右上角自动显示 WiFi 图标。
- **传感器与外设**：4 路按键（V1 板有效按键 S2 / S4 / S5）、ToF、环境光、G-Sensor、NFC、LED 指示灯、震动马达、舵机；电池充电管理预留。
- **存储与升级**：partitions/SD-NAND 资源；支持 OTA / 工厂配置。

更详细的工程说明、按键映射、UI 流程与配置参数见在线文档 [BK7259 机器人工程](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/projects/beken_robot/index.html)。

## 烧录固件

编译完成后，固件位于（路径相对仓库根 `bk_solution_ai/`）：

```
projects/beken_robot/build/bk7259/beken_robot/package/all-app.bin
```

使用博通 `BKFIL` 烧录工具（UART）写入开发板即可。烧录流程与工具说明请参考 [SMP 快速入门（BK7259）](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/get-started/index.html) 及 [SMP 文档首页](https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7259/zh_CN/v4.0.1/index.html)。

## 上电与配网

- 上电后 LCD 显示「BK7259 机器人方案」蓝色 LOGO 开机首页；按 V1 板按键映射进入主菜单（详见在线文档 [BK7259 机器人工程](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/projects/beken_robot/index.html)）。
- 进入「配网」→「开始」短按【确认】键，使用 BK App 完成 BLE 配网；成功后主菜单右上角显示 WiFi 图标。

## APP

- BK App 下载：[应用下载说明](https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_download/index.html)（使用邮箱注册登录）。
- 配网与设备管理操作步骤：见 [机器人方案简介](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/intro/index.html) 与 [快速入门](https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7259/zh_CN/v4.0.1/get-started/index.html)。
