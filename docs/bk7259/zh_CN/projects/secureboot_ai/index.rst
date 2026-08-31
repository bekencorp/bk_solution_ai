BK7259 Secure Boot AI 工程
=================================

:link_to_translation:`en:[English]`

1. 简介
---------------------------------

    ``projects/secureboot_ai`` 是 BK7259 Robot V1 AI 评估板的安全启动参考工程。工程在
    ``beken_robot`` 的机器人 AI 应用能力上集成 BL1、BL2/MCUboot 和 TF-M，演示从可信启动链
    进入 CP Non-Secure 应用，并通过受控的 NSC 接口准备和启动 AP Non-Secure 应用。

    AP 侧提供 LVGL Demo 中心、BLE 配网、WiFi、音频、视频、端侧 AI 和 Agora 云端 AI
    等功能；CP 侧负责安全启动、TF-M、安全服务以及 AP 上电启动流程。

1.1 硬件参考
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * 主控：BK7259（CP CPU0，AP CPU1/CPU2 SMP）。
    * 评估板：BK7259 Robot V1 AI kit。
    * 显示：320x385 MIPI LCD，默认面板 ``jd9855_mipi_320x385``。
    * 触摸：CST9217。
    * 摄像头：MIPI CSI。
    * 音频：板载麦克风和扬声器链路。
    * 存储：板载 SD-NAND，通过 SDIO1 连接。
    * 联网：WiFi、BLE，并支持经典蓝牙音频。
    * Type-C：由 GPIO54 控制开关，在 CH340 UART 与 BK7259 USB 之间切换。

    BK7259 datasheet 请参考 :doc:`../../hw-reference/index`。

1.2 软件特性
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * BL1 + BL2/MCUboot + TF-M 安全启动。
    * EC-P256 镜像签名和固定密钥 Flash AES 加密。
    * CP/AP Non-Secure 应用与 TF-M Secure 服务隔离。
    * LVGL Demo 中心和触摸交互。
    * BLE 配网、WiFi STA、Agora RTC 和云端 AI Agent。
    * 本地 KWS、音频处理、H.264/JPEG、TFLite Micro 和 NPU。
    * 手掌跟随、人脸检测、手势识别、人脸识别等端侧 AI Demo。
    * SD-NAND、USB MSC、机器人图传和控制服务。

    本工程当前配置未启用 NFC、马达、LED 闪烁和电池监控，不能将这些功能视为默认能力。

2. 工程使用
---------------------------------

2.1 编译
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    ``Makefile`` 默认使用仓库中的 ``bk7259`` SDK：

    .. code:: bash

        cd bk7259_ai_solution/projects/secureboot_ai
        make clean
        make bk7259

    也可以使用 Docker 构建环境：

    .. code:: bash

        ./dbuild.sh make clean
        ./dbuild.sh make bk7259

    如果 SDK 不在默认位置，请在构建前设置 ``SDK_DIR``。通用环境安装和工具链配置请参考
    :doc:`../../get-started/index`。

2.2 构建产物与烧录
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    主要产物位于：

    .. code:: text

        projects/secureboot_ai/build/bk7259/secureboot_ai/package/

    其中：

    * ``all-app.bin``：完整烧录镜像。
    * ``bootloader.bin``：安全启动相关 Bootloader 包。
    * ``ota.bin``：加密 OTA 包。
    * ``otp_efuse_config.json``：构建生成的 OTP/eFuse 配置参考。

    开发阶段可使用 BKFIL 或 ``bk_loader`` 通过 UART 烧录 ``all-app.bin``。安全启动量产还涉及
    Root of Trust 公钥哈希、Flash AES 密钥等不可逆 OTP/eFuse 配置，必须按照芯片量产流程操作。

    .. warning::

        OTP/eFuse 烧写通常不可逆。仓库中的私钥和对称密钥只能用于开发验证，量产必须替换为受控密钥，
        并通过安全的密钥生成、保存和注入流程管理。

2.3 启动验证
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    正常启动顺序为：

    .. code:: text

        BootROM
          -> BL1
          -> BL2 / MCUboot
          -> TF-M Secure
          -> CP Non-Secure
          -> AP Secure prepare
          -> AP Non-Secure

    CP Non-Secure 成功启动后会输出：

    .. code:: text

        secureboot_ai: CP NS world reached (secure boot OK)

    AP 启动并初始化显示后会输出：

    .. code:: text

        LVGL ready on 320x385 MIPI (first page pending)
        LVGL started, page_1 loaded

    BL2/TF-M 日志和 NS 应用日志使用不同的 UART 路径；调试时应同时确认安全启动日志和 AP/CP
    Non-Secure 日志。

3. 安全架构
---------------------------------

3.1 AP/CP 分工
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * **CP Secure**：运行 TF-M，提供 Crypto、安全资源配置和 AP Secure prepare。
    * **CP Non-Secure**：完成系统初始化，并通过 PM vote 启动 AP。
    * **AP Secure Shim**：AP 复位后在 Secure 状态运行，配置安全属性和必要的共享硬件，再切换至 NS。
    * **AP Non-Secure**：运行 LVGL、音视频、网络和 AI 应用。

    CP Non-Secure 不直接执行 Secure 特权配置。AP 启动过程中需要的 Secure 操作通过
    ``psa_ap_secure_prepare`` NSC 接口完成。

3.2 TF-M 配置
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    本工程主要配置如下：

    * ``CONFIG_TFM=y``。
    * ``CONFIG_TFM_BL2="ON"``。
    * ``CONFIG_TFM_PROFILE="profile_medium"``。
    * ``CONFIG_TFM_ISOLATION_LEVEL=2``。
    * ``CONFIG_TFM_CRYPTO=y``。
    * ``CONFIG_TFM_AP_BOOT_NSC=y``。
    * ``CONFIG_TFM_REG_ACCESS_NSC=y``。

    Persistent Storage、Firmware Update 和 Initial Attestation 默认未启用。

3.3 Flash 分区
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    工程使用 8 MB Flash。主要分区包括：

    +--------------------------+----------+----------------------------------+
    | 分区                     | 大小     | 说明                             |
    +==========================+==========+==================================+
    | ``bl1_control``          | 4 KB     | BL1 控制信息                     |
    +--------------------------+----------+----------------------------------+
    | ``primary_manifest``     | 4 KB     | 主镜像 Manifest                  |
    +--------------------------+----------+----------------------------------+
    | ``bl2``                  | 96 KB    | BL2/MCUboot                      |
    +--------------------------+----------+----------------------------------+
    | ``primary_tfm_s``        | 320 KB   | TF-M Secure 镜像                 |
    +--------------------------+----------+----------------------------------+
    | ``primary_cpu0_app``     | 1644 KB  | CP Non-Secure 应用               |
    +--------------------------+----------+----------------------------------+
    | ``primary_ap_app``       | 5000 KB  | AP AI 应用                       |
    +--------------------------+----------+----------------------------------+
    | ``ota``                  | 496 KB   | 加密 OTA 数据区                  |
    +--------------------------+----------+----------------------------------+

    当前策略为 ``XIP_FORCE_A``。Secondary 分区仅为占位，不能视为完整的 A/B 双槽升级空间。

3.4 签名、加密与安全计数器
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    ``partitions/bk7259/security.csv`` 配置安全启动、EC-P256 Root Key 和固定密钥 Flash AES；
    ``ota.csv`` 配置加密 OTA 及应用安全计数器。构建工具据此签名、加密并生成 OTP/eFuse 配置。

    调整签名密钥、加密密钥或安全计数器时，必须同步考虑：

    * 芯片中已烧写的 Root of Trust。
    * 已发布镜像的版本和回滚策略。
    * 开发密钥与量产密钥的隔离。
    * OTA 包与设备侧密钥的一致性。

4. AI 应用
---------------------------------

4.1 UI 与 Demo 中心
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    上电后 AP 初始化 320x385 MIPI LCD 和 CST9217 触摸屏，加载启动页；触摸进入 Demo 中心。
    Demo 按端侧 AI、云端 AI、娱乐互动和设备设置组织，不使用 ``beken_robot`` 文档中的固定
    page_1 到 page_10 导航模型。

    默认 Demo 包括命令词识别、声源定位、手掌/人脸/手势识别、AI 对话、视觉识别、音乐播放、
    图传、蓝牙音乐、音量和 USB 模式切换等。实际可用项由 AP ``defconfig`` 决定。

4.2 BLE 配网与云端 AI
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    1. 从 Demo 中心进入配网页并启动 BLE 配网。
    2. 在 BK App 中选择设备，配置 WiFi SSID 和密码。
    3. 配网成功后设备连接 WiFi。
    4. 配置有效的 Agora App ID 和鉴权信息后，可启动 RTC 与云端 AI Agent。

    Agora 调试命令：

    .. code:: bash

        agora_rtc start
        agora_rtc stop
        agora_rtc start_agora
        agora_rtc stop_agora
        agora_rtc start_agent
        agora_rtc stop_agent

4.3 存储与 USB 切换
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    板载 SD-NAND 连接到 SDIO1，GPIO53 控制其供电。Type-C 默认连接 CH340，用于日志和烧录；
    ``usbsw`` 命令可以通过 GPIO54 将 Type-C 切换到 BK7259 USB，用于 USB MSC 等功能。

5. 调试与常见问题
---------------------------------

Q：没有看到 CP Non-Secure 启动成功日志？

A：先检查 BL1、BL2 和 TF-M 日志。如果出现签名验证、Manifest、Flash AES 或安全计数器错误，
请核对烧录镜像与 OTP/eFuse 配置是否匹配。

Q：出现 ``AP boot vote failed``？

A：表示 CP 无法完成 AP 上电启动流程。检查 AP 电源域、AP MPC/PPHS 配置以及
``psa_ap_secure_prepare`` NSC 调用。

Q：LCD 没有进入 Demo 中心？

A：确认日志中已出现 ``LVGL ready`` 和 ``LVGL started``，然后检查 MIPI LCD、CST9217 触摸配置
和 AP 镜像是否正确烧录。

Q：配网成功后云端 AI 仍无法连接？

A：检查 WiFi 是否能够访问外网，并确认 Agora App ID、Token 和 PC/云端 Agent 已正确配置。

Q：可以直接复用开发工程中的密钥进行量产吗？

A：不可以。量产必须替换开发密钥，并使用受控的密钥生命周期和 OTP/eFuse 注入流程。
