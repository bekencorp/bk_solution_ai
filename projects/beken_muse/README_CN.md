# BK7259 Muse Gadget 示例工程

- [English](./README.md)

## 1. 工程简介

`beken_muse` 是面向 BK7259 的独立 Muse Gadget 产品工程。它在
[`facebookincubator/muse-gadget-sdk`](https://github.com/facebookincubator/muse-gadget-sdk)
（pinned commit `812c46f9fe42b69d916613a9dc702cbac5ff7b54`）的协议基础上，
实现"AI 语音伴侣"的完整链路：

BLE 配对 / Wi-Fi 配网 → Noise 加密建链 → 唤醒与免提语音 → 云端理解与回复
→ 屏幕文本 + 语音播报。

工程由 `beken_robot` 应用底座裁剪而来，只保留 Muse 所需的 UI、音频与 demo，
可独立编译，不依赖 `secureboot_ai`。

- 协议契约（已实现范围、命令、语音线上格式）：[docs/muse_protocol_contract.md](docs/muse_protocol_contract.md)
- 语音功能设计：[docs/muse_voice_design.md](docs/muse_voice_design.md)

## 2. 主要特性

- **BLE community pairing v5**：P-256 ECDH + HKDF-SHA256 + AES-256-GCM，
  加密记录传输 Wi-Fi / 账号信息，物理按键（S4）确认。
- **Noise 加密会话**：`Noise_XX_25519_AESGCM_SHA256` over WSS；控制会话负责
  `link.register` / `link.invoke`，独立 chat 会话负责
  `POST /chat/stream` + `POST /chat/subscribe`。
- **免提语音回合**：KWS 唤醒 + VAD 自动断句，录音后上传语音 note。
- **可选火山语音**：流式 ASR（`CONFIG_MUSE_ASR_VOLC`）与流式 TTS
  （`CONFIG_MUSE_TTS_VOLC`）。
- **LVGL 语音界面**：`/sd0/jolly/{a,b,question}.mp4` 背景视频 + 回复气泡 +
  配对/连接状态页；开机/重连期间原地显示"正在连接…"。
- **云端显示指令**：`display.draw_url`、`display.draw_text`、
  `display.show_animation`。
- **SD-NAND 资源**：KWS 模型、提示音、视频素材、TTS 输出。

## 3. 主要配置

- **目标芯片**：BK7259，编译目标 `bk7259`。
- **显示与输入**：LVGL v9、CST9217 触摸屏、S2～S5 实体按键。
- **SD-NAND**：板载 SD-NAND 接 **SDIO1**（GPIO14～GPIO19），电源门控
  GPIO53；FatFS 盘符 `1:`，VFS 挂载点 `/sd0`。
- **Type-C**：UART 日志 / USB U 盘模式切换（GPIO54）。
- **音频**：麦克风 + 扬声器，语音回合使用 16 kHz mono PCM16。
- **Muse 开关**（`menuconfig`，本工程 defconfig 默认已开启）：

  ```text
  CONFIG_MUSE_GADGET=y
  CONFIG_MUSE_GADGET_VOICE=y
  CONFIG_MUSE_ASR_VOLC=y
  CONFIG_MUSE_TTS_VOLC=y
  CONFIG_BEKEN_KWS=y
  CONFIG_BOARD_SD_NAND_ENABLE=y
  ```

## 4. 编译工程

先安装 Armino SMP 编译环境，并准备与方案代码版本一致的 `bk_avdk_smp` SDK。

```bash
cd ~/armino/bk_solution_ai/projects/beken_muse
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

重新完整编译：

```bash
make clean SDK_DIR=~/armino/bk_avdk_smp
make bk7259 SDK_DIR=~/armino/bk_avdk_smp
```

也可以使用仓库的 Docker 编译脚本：

```bash
export SDK_DIR=~/armino/bk_avdk_smp
./dbuild.sh make bk7259
```

编译成功后的烧录文件为：

```text
build/bk7259/beken_muse/package/all-app.bin
```

烧录方法与编译环境安装请参考仓库根目录的[中文说明](../../README_CN.md)。

## 5. 使用前准备

1. 编译并烧录 `all-app.bin`，连接 LCD、触摸屏、麦克风、扬声器。
2. 将 `resources` 目录中的**内容**复制到 SD-NAND 根目录（不要多套一层
   `resources`）：

   ```text
   /sd0/
   ├── kws_model/
   │   ├── bk_kws_wakeup.tflite
   │   └── bk_kws_commands.tflite
   ├── asr_wakeup_16k_mono_16bit_en.mp3
   ├── asr_standby_16k_mono_16bit_en.mp3
   └── ...其他提示音
   ```

   KWS 模型与提示音的文件名、目录要求见
   [资源使用说明](resources/kws_model_and_prompt_tone_user_mannual.md)。

3. 放入 Jolly 视频素材（H.264、无音轨）：`a.mp4`（语音输入）、`b.mp4`
   （回复气泡，320x320）、`question.mp4`（等待回复，384x320）：

   ```text
   /sd0/jolly/a.mp4
   /sd0/jolly/b.mp4
   /sd0/jolly/question.mp4
   ```

   素材缺失时页面会自动落回普通文字布局。

4. 放入 Muse TLS 信任锚（PEM）。默认路径：

   ```text
   /sd0/muse_ca.pem
   ```

   也可在运行时用 `muse ca-load <path>` 指定。未配置信任锚时 TLS 会直接
   失败关闭（不会降级为不校验）。

5. 配置火山语音凭据（仅运行期，写入 easyflash，不进入固件）：

   ```text
   muse volcappid <APP-ID>
   muse asrkey <Access-Token>
   muse ttskey <Access-Token>
   # 可选：muse asrres <resource-id> / muse ttsvoice <speaker-id>
   ```

6. 通过 Muse App 完成 BLE 配对与 Wi-Fi 配网。

## 6. 运行与操作

- 开机时工程会自动调用 `muse_service_start()` 启动 Muse 服务；也可在串口 CLI
  手动 `muse start`。
- **已配网设备**：开机直接进入语音页（`a.mp4`），连接中显示"正在连接…"，
  连上后自动进入免提语音回合。
- **未配网设备**：停在 Muse 状态页等待配对。

### 实体按键

- **S2 / S5 短按**：上一个 / 下一个条目。
- **S3 短按**：返回上一级；语音页按 S3 回主页。
- **S4 短按**：确认 / 进入当前条目；配对等待时确认 Muse 配对。
- **S4 长按**：语音页内切换录音（PTT，需 `CONFIG_ROBOT_V2_ADC_KEYS`）。

### 触屏

- 点击进入；列表可上下滑动；从左边缘右滑返回。

### 常用 CLI

```text
muse start | stop | status | confirm | clear | newid
muse ca-load [path]
muse volcappid <id> | asrkey <key> | ttskey <key>
muse asrres <rid> | ttsvoice <spk>
muse voice [start|stop|toggle]
muse register-json | mem | time
```

## 7. 相关文档

- [Muse 协议契约](docs/muse_protocol_contract.md)：已实现范围、控制/聊天会话、
  命令表、语音/图片 note 线上格式、安全与重连。
- [Muse 语音设计](docs/muse_voice_design.md)：语音回合状态机与最初的设计依据。
- [KWS 模型与提示音使用说明](resources/kws_model_and_prompt_tone_user_mannual.md)

## 8. 说明

- `CONFIG_MUSE_GADGET_SDK_TOKEN` 仅用于构建期加密配对与 token mint/refresh，
  不要提交真实 token，日志也只打印短前缀。
- 语音凭据、CA 证书都按运行时配置处理，不烧进固件。
- KWS 模型、提示音或 Jolly 视频缺失时，对应能力会降级（KWS 初始化失败或
  页面回退文字布局），不影响其余功能。
