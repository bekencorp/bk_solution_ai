# BK7259 Secure Boot AI 示例工程

- [English](./README.md)

## 1. 工程简介

`secureboot_ai` 基于 [`beken_robot`](../beken_robot/) 的机器人 AI 应用能力，增加了 BL1、BL2/MCUboot、TF-M、镜像签名和 Flash AES 加密，用于演示 BK7259 从可信启动链进入机器人 AI 应用。

除安全启动功能外，本工程与 `beken_robot` 一样提供 LCD 触控界面、BLE/Wi-Fi 配网、端侧 AI、云端 AI、音视频和外设控制 Demo。方案代码与 Armino SMP SDK 应使用相同的发布版本。

## 2. 主要配置

### 安全配置

- **可信启动链**：BootROM → BL1 → BL2/MCUboot → TF-M Secure → CP Non-Secure → AP Non-Secure。
- **镜像保护**：使用 EC-P256 对镜像签名，并启用固定密钥 Flash AES 加密。
- **安全隔离**：TF-M 使用 `profile_medium`、隔离等级 2，并启用 Crypto 服务；安全操作通过受控接口提供给 Non-Secure 应用。
- **Flash 布局**：使用 8 MB Flash，当前为 `XIP_FORCE_A` 单槽策略；Secondary 分区仅为占位，不是完整的 A/B 双槽升级空间。
- **密钥配置**：开发签名密钥位于 `config/key/`，安全启动、加密与版本计数配置位于 `partitions/bk7259/security.csv` 和 `ota.csv`。

### 板级配置

- **目标芯片**：BK7259，编译目标为 `bk7259`。
- **显示与输入**：320×385 MIPI LCD、CST9217 触摸屏及 S2～S5 实体按键。
- **SD-NAND**：板载 SD-NAND 连接到 **SDIO1**，使用 GPIO14～GPIO19；设备侧 FatFS 盘符为 `1:`，VFS 挂载点为 `/sd0`。
- **Type-C 接口**：GPIO54 控制接口在 CH340 UART 与 BK7259 USB U 盘模式间切换；进入 U 盘模式后，串口日志会暂时断开。

## 3. 编译工程

先安装 Armino SMP 编译环境，并准备与方案代码版本一致的 `bk_avdk_smp` SDK。当前目录布局下应通过命令行显式指定 `SDK_DIR`：

```bash
cd ~/armino/bk_solution_ai/projects/secureboot_ai
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

Docker 编译：

```bash
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make clean
./dbuild.sh make bk7259
```

主要产物位于：

```text
build/bk7259/secureboot_ai/package/
```

- `all-app.bin`：完整烧录镜像。
- `bootloader.bin`：安全启动 Bootloader 包。
- `ota.bin`：加密 OTA 包。
- `otp_efuse_config.json`：构建生成的 OTP/eFuse 配置参考。

开发阶段可使用 BKFIL 烧录 `all-app.bin`。环境安装和烧录方法请参考仓库根目录的[中文说明](../../README_CN.md)。

> **安全警告**：OTP/eFuse 烧写通常不可逆。仓库中的签名私钥和对称密钥仅用于开发验证，禁止直接用于量产。量产时必须替换为受控密钥，并按正式流程完成密钥生成、保管和注入。

## 4. 使用前准备

1. 编译并烧录 `all-app.bin`，连接需要使用的 LCD、触摸屏、摄像头、麦克风、扬声器及机器人外设。
2. 将配套资源复制到 SD-NAND 根目录，不要在磁盘中再套一层 `resources`：

   ```text
   /sd0/
   ├── kws_model/
   │   ├── bk_kws_wakeup.tflite
   │   └── bk_kws_commands.tflite
   ├── tflite/
   │   ├── palm_detection_builtin_256_integer_quant_vela.tflite
   │   ├── yoloface_int8_vela.tflite
   │   ├── hand_gesture_detection_vela.tflite
   │   ├── face_detection_int8_vela.tflite
   │   └── face_verify_int8_vela.tflite
   ├── asr_wakeup_16k_mono_16bit_en.mp3
   └── ...其他提示音
   ```

3. 推荐在设备 UI 中进入“设备设置 > U盘 > USB”，通过 PC 复制资源。安全弹出磁盘，再切回“UART”并重启。
4. 如需体验音乐播放，在 `/sd0/music` 中放入 MP3、AAC 或 WAV 文件。
5. 进入“连接设置”完成 Wi-Fi/BLE 配网后，再使用需要联网的云端 Demo。

KWS 模型和提示音要求详见[资源文件使用说明](./resources/kws_model_and_prompt_tone_user_mannual.md)。

## 5. 启动验证

安全启动成功后，串口应依次出现 BL1、BL2/MCUboot、TF-M 和 Non-Secure 应用日志。重点检查：

```text
secureboot_ai: CP NS world reached (secure boot OK)
LVGL ready on 320x385 MIPI (first page pending)
LVGL started, page_1 loaded
```

若启动停在签名校验、Manifest、Flash AES 或安全计数器阶段，请确认烧录镜像与设备 OTP/eFuse 配置匹配。安全启动日志和应用日志可能使用不同的 UART 路径，调试时需分别检查。

## 6. UI 操作

### 触屏

- 欢迎页点击屏幕进入主页；点击菜单条目进入，长列表可上下滑动。
- 从屏幕左边缘向右滑动返回。
- AI 相机中点击屏幕可在拍照与恢复实时预览之间切换，右滑退出。

### 实体按键

- **S2 短按**：上一个条目/焦点；AI 相机实时预览时拍照。
- **S5 短按**：下一个条目/焦点；AI 相机照片停留时恢复实时预览。
- **S3 短按**：返回上一级；在全屏视觉 Demo 中退出。
- **S4 短按**：确认或进入当前条目。
- **S4 长按**：页面相关的长按确认操作。

主页提供“连接设置”“Demo中心”和“设备设置”。设备设置中可调节音量、切换 UART/USB 模式、切换中英文以及恢复出厂设置。

## 7. 主要 Demo

- **端侧 AI**：命令词识别、声源定位、手掌跟随、人脸检测、手势识别、小车跟随和摄像头方案示例。
- **云端 AI**：AI 对话、视觉识别和 AI 相机。
- **娱乐互动**：本地音乐播放、实时图传和蓝牙音乐。
- **系统功能**：Wi-Fi/BLE 配网、音量设置、SD-NAND/U 盘访问及中英文界面切换。

部分 Demo 依赖对应硬件、网络服务或模型资源；未满足前置条件时可能无法完整运行。

## 8. 安全开发注意事项

- 修改签名密钥、Flash AES 密钥、应用版本或安全计数器后，应重新生成并烧录匹配的完整镜像。
- 安全计数器只能按产品升级策略递增，避免设备拒绝旧版本或不匹配的 OTA 包。
- 当前配置未默认启用 TF-M Persistent Storage、Firmware Update 和 Initial Attestation。
- 量产前必须完成开发密钥替换、Root of Trust 配置、OTP/eFuse 注入方案及回滚策略评审。
