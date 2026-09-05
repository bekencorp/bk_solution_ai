# BK7259 机器人示例工程

- [English](./README.md)

## 1. 工程简介

`beken_robot` 是面向 BK7259 机器人开发套件的综合示例工程，提供 LCD 触控界面、网络连接、本地 AI、云端 AI、音视频和外设控制等可直接体验的 Demo。本文只介绍开始使用该工程所需的步骤；方案代码与 Armino SMP SDK 应使用相同的发布版本。

## 2. 主要配置

- **目标芯片**：BK7259，编译目标为 `bk7259`。
- **显示与输入**：LVGL 图形界面、CST9217 触摸屏，以及 S2～S5 实体按键。
- **SD-NAND**：板载 SD-NAND 连接到 **SDIO1**，使用 GPIO14～GPIO19 的 CLK、CMD 和 DATA0～DATA3；设备侧 FatFS 盘符为 `1:`，VFS 挂载点为 `/sd0`。
- **Type-C 接口**：可在串口日志模式与 USB U 盘模式之间切换。U 盘模式用于从 PC 访问板载 SD-NAND；切换到 U 盘模式后，串口日志会暂时断开。
- **摄像头与音频**：工程包含摄像头、麦克风、扬声器及相关音视频处理示例。

需要修改功能配置时，请使用 SDK 的 `menuconfig`，并确认工程所需的 SD-NAND、LVGL、按键、KWS 和提示音文件系统加载等选项已启用。

## 3. 编译工程

先安装 Armino SMP 编译环境，并准备与方案代码版本一致的 `bk_avdk_smp` SDK。

```bash
cd ~/armino/bk_solution_ai/projects/beken_robot
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

重新完整编译：

```bash
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

也可以使用仓库提供的 Docker 编译脚本：

```bash
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7259
```

编译成功后的烧录文件为：

```text
build/bk7259/beken_robot/package/all-app.bin
```

烧录方法及编译环境安装请参考仓库根目录的[中文说明](../../README_CN.md)。

## 4. 使用前准备

1. 编译并烧录 `all-app.bin`，连接 LCD、触摸屏、摄像头、麦克风、扬声器和其他需要体验的外设。
2. 将配套 `resources` 资源目录中的**内容**复制到 SD-NAND 根目录，不要在磁盘中再套一层 `resources`：

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
   ├── asr_standby_16k_mono_16bit_en.mp3
   └── ...其他提示音
   ```

   `kws_model/` 供命令词识别使用，`tflite/` 供手掌跟随、人脸检测、手势识别、小车跟随和方案示例使用。请使用与当前固件版本配套的模型文件并保持文件名不变。
3. 推荐在设备 UI 中进入“设备设置 > U盘 > USB”后，通过 PC 复制资源。安全弹出磁盘，再切回“UART”并重启设备。
4. 如需体验音乐播放，在 SD-NAND 中创建 `/sd0/music`（FatFS：`1:/music`），放入 MP3、AAC 或 WAV 文件。
5. 进入“连接设置”完成 Wi-Fi/BLE 配网后，再使用需要联网的云端 Demo。

KWS 模型、提示音文件名及目录要求详见[资源文件使用说明](./resources/kws_model_and_prompt_tone_user_mannual.md)。

## 5. UI 操作

### 触屏

- 欢迎页：点击屏幕进入主页。
- 菜单页：点击条目进入；列表较长时可上下滑动。
- 返回：从屏幕左边缘向右滑动。
- AI 相机：点击屏幕可在拍照与恢复实时预览之间切换，右滑退出。

### 实体按键

- **S2 短按**：上一个条目/焦点；AI 相机实时预览时用于拍照。
- **S5 短按**：下一个条目/焦点；AI 相机照片停留时恢复实时预览。
- **S3 短按**：返回上一级；在全屏视觉 Demo 中用于退出。
- **S4 短按**：确认或进入当前条目。
- **S4 长按**：页面相关的长按确认操作；未实现该操作的页面会忽略。

主页提供“连接设置”“Demo中心”和“设备设置”。“设备设置”中可调节音量、切换 Type-C 的 UART/USB 模式、切换中英文以及恢复出厂设置。

## 6. 主要 Demo

- **端侧 AI**：命令词识别、声源定位、手掌跟随、人脸检测、手势识别、小车跟随和摄像头方案示例。
- **云端 AI**：AI 对话、视觉识别和 AI 相机。
- **娱乐互动**：本地音乐播放、实时图传和蓝牙音乐。
- **系统功能**：Wi-Fi/BLE 配网、音量设置、SD-NAND/U 盘访问和中英文界面切换。

部分 Demo 依赖对应的摄像头、舵机、机械手、机器人底盘、网络服务或资源文件；未连接相关硬件时，该 Demo 可能无法完整运行。
