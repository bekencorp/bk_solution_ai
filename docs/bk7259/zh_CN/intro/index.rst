BK7259 机器人方案简介
===========================================

:link_to_translation:`en:[English]`

概述
---------------------------------

BK7259 机器人方案是博通集成电路（上海）股份有限公司基于 BK7259 主控、Armino SMP（BK AVDK SMP）v4.0.x 架构开发的智能机器人参考设计。该方案提供：

- LVGL UI 与 360x390 MIPI 显示屏；
- 本地语音唤醒（"你好博通" / "再见博通"）+ 声网 RTC AI 对话与视觉问答；
- BLE 配网（搭配 BK App）；
- 4 路按键、ToF、环境光、G-Sensor、NFC、LED、震动马达、舵机等机器人常用外设；
- WiFi STA 与可选 4G 蜂窝兜底联网。

.. note::

   V1 评估板的环境搭建、源码下载、编译、烧录、运行与调试，请参阅 :doc:`../get-started/index`；
   ``beken_robot`` 工程的目录结构、Kconfig、按键映射、LVGL 页面与调试 CLI，请参阅 :doc:`../projects/beken_robot/index`。

设计理念
---------------------------------

BK7259 机器人方案采用"端-云-模型"三层架构：

- **端侧（Device）**：BK7259 主控，负责 UI、按键、麦克风采集、扬声器播放、摄像头采集、传感器读取与本地唤醒；
- **云端（Cloud）**：声网（Agora）RTC 实时音视频通道，负责设备与 AI Agent 之间的低延迟数据传输；
- **模型侧（AI Model）**：通过声网 Conversational AI Agent 接入主流大模型（OpenAI、豆包、DeepSeek、火山方舟等），提供语音对话与图像识别能力。

核心特性
---------------------------------

1. 多模态交互
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **语音对话**：本地唤醒 + 云端流式 ASR / LLM / TTS，端到端延迟低；
- **视觉问答**：MIPI CSI 摄像头采集 + Agora 视觉模式，对镜头物体进行问答；
- **声源定位**：双麦阵列估算声源方位，UI 上眼神跟随转向；
- **命令词识别**：本地命令词触发 UI 行为（持续完善中）。

2. 实时音视频通信
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- 基于 **Agora RTC SDK**，提供低延迟双向音视频；
- 音频默认 OPUS 16k 双向，视频可选 H.264 / JPEG；
- 支持网络抖动估计、码率自适应。

3. 端侧音频处理
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- AEC（回声消除）、NS（噪声抑制）；
- KWS（关键词唤醒）；
- 提示音播放（开机、配网、唤醒、低电、断连等场景）。

4. 完整的外设支持
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

- **显示**：360x390 MIPI LCD（默认面板 ``jd9855_mipi_360x390``）；
- **输入**：双 mic 阵列 + 4 路用户按键（V1 板有效按键 S2/S4/S5；S1 为复位、S3 因接线错误暂不可用）；
- **传感器**：ToF 距离感应、环境光、G-Sensor、NFC；
- **输出**：扬声器、LED 指示灯、震动马达、舵机；
- **存储**：SD-NAND（资源文件）+ 内置 partitions；
- **联网**：板载 WiFi/BLE，预留 4G Cat.1 模组接口；
- **电源**：USB Type-C 5V 供电，支持锂电池 + 充电管理（视硬件版本而定）。

系统架构
---------------------------------

.. rubric:: BK7259 机器人方案与 BK AVDK SMP 的关系

本仓库交付的 BK7259 机器人方案与底层 BK AVDK SMP（Armino SMP SDK v4.0.x）的分工如下：

#. **开发与定位**：本方案是在 BK AVDK SMP 之上实现的场景化解决方案，聚焦机器人 UI、AI Agent 接入、外设组合等；芯片、RTOS、驱动、网络协议栈由 BK AVDK SMP 提供。
#. **构建与编译**：本方案不包含独立构建系统。固件编译、工具链、Kconfig 与工程生成依赖 BK AVDK SMP；通过 ``SDK_DIR`` 环境变量或 ``make bk7259 SDK_DIR=...`` 指向 SMP 工程根目录。
#. **代码边界**：本仓库（``bk_solution_ai``）主要提供方案与业务代码（``components/``、``projects/beken_robot/``，仓库根目录直接挂载，**无外层 solution/ 包装**）；硬件驱动、RTOS、内存管理、Wi-Fi/BLE 协议栈在 SMP 中。

Armino SMP 在 BK7259 上采用 AP（应用处理器） + CP（通信处理器）划分：

- **AP（CPU1 + CPU2）**：运行 LVGL UI、AI 业务、音视频引擎、Agora SDK；
- **CP（CPU0）**：运行 Wi-Fi、BLE、协议栈与低功耗管理。

软件架构层次（示意）::

    应用层 (LVGL UI / 机器人主流程 / 状态机)
            ↓
    服务层 (audio_engine / video_engine / network_transfer / bk_smart_config / bk_app_event)
            ↓
    RTC 层 (Agora RTC SDK)
            ↓
    OS 层 (RTOS)
            ↓
    硬件层 (BK7259 AP+CP)

主要应用场景
---------------------------------

1. **桌面 / 陪伴机器人**：通过 LCD 表情、声源定位、AI 对话提供陪伴体验；
2. **教育 / 互动玩具**：本地唤醒 + 多模态识别，适配教育与互动场景；
3. **服务 / 导览机器人**：摄像头识别 + 语音问答，可作为客服 / 导览终端；
4. **智能家居控制中枢**：通过云端 AI Agent 联动家居设备。

技术优势
---------------------------------

1. **与 SMP 配套的完整解决方案**：本方案聚焦机器人业务与 UI；硬件驱动 / RTOS / 协议栈统一由 BK AVDK SMP 提供，二者形成完整开发路径。
2. **模块化的 components/**：``audio_engine`` / ``video_engine`` / ``network_transfer`` / ``bk_smart_config`` / ``bk_app_event`` / ``bk_factory_config`` / ``bk_key_app`` / ``bk_led_blink`` / ``bk_motor`` / ``bk_servo`` / ``bk_countdown`` / ``avdk_nn_module`` / ``multimedia_device_service`` 等模块独立可裁剪。
3. **真实可跑的 LVGL Demo**：开机首页 / 主菜单 / 配网 / 声源定位 / AI 对话 / 视觉识别 / 命令词 / 音乐 / 音量等 10 个 LVGL 页面，覆盖典型机器人 UI 场景。