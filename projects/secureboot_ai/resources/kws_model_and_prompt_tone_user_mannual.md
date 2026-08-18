# Beken Robot：KWS 模型与提示音文件使用说明

本文说明如何将本目录（`resources`）中的 **KWS 模型** 与 **提示音** 部署到设备存储，供 `beken_robot` 方案运行时加载。

> 适用工程：`ai_solution/projects/beken_robot`  
> 存储介质：板载 SD-NAND（FatFS 盘符一般为 `1:`，VFS 挂载点为 `/sd0`）

---

## 1. 目录内容说明

本目录交付给客户后的推荐结构如下（请保持文件名与层级不变）：

```text
resources/
├── kws_model/
│   ├── bk_kws_wakeup.tflite      # 唤醒词模型
│   └── bk_kws_commands.tflite    # 命令词模型
├── asr_wakeup_16k_mono_16bit_en.mp3
├── asr_standby_16k_mono_16bit_en.mp3
├── network_provision_16k_mono_16bit_en.mp3
├── network_provision_success_16k_mono_16bit_en.mp3
├── network_provision_fail_16k_mono_16bit_en.mp3
├── reconnect_network_16k_mono_16bit_en.mp3
├── reconnect_network_success_16k_mono_16bit_en.mp3
├── reconnect_network_fail_16k_mono_16bit_en.mp3
├── rtc_connection_lost_16k_mono_16bit_en.mp3
├── agent_joined_16k_mono_16bit_en.mp3
├── agent_offline_16k_mono_16bit_en.mp3
├── agent_start_fail_16k_mono_16bit_en.mp3
├── low_voltage_16k_mono_16bit_en.mp3
├── ota_update_success_16k_mono_16bit_en.mp3
├── ota_update_fail_16k_mono_16bit_en.mp3
└── kws_model_and_prompt_tone_user_mannual.md   # this guide
```

| 类型 | 路径（本包内） | 设备上目标路径（VFS） | 说明 |
|------|----------------|----------------------|------|
| 唤醒模型 | `kws_model/bk_kws_wakeup.tflite` | `/sd0/kws_model/bk_kws_wakeup.tflite` | 对应 FatFS：`1:/kws_model/bk_kws_wakeup.tflite` |
| 命令模型 | `kws_model/bk_kws_commands.tflite` | `/sd0/kws_model/bk_kws_commands.tflite` | 对应 FatFS：`1:/kws_model/bk_kws_commands.tflite` |
| 提示音 | 根目录下 `*_16k_mono_16bit_en.mp3` | `/sd0/<同名文件>` | 对应 FatFS：`1:/<同名文件>`，必须放在存储根目录 |

---

## 2. 固件配置要求（出厂/编译侧）

`beken_robot` 默认 defconfig 已开启从外部存储加载，客户使用预编译固件时通常无需再改：

| 配置项 | 期望值 | 作用 |
|--------|--------|------|
| `CONFIG_BEKEN_KWS` | `y` | 启用 Beken KWS |
| `CONFIG_BEKEN_KWS_MODEL_FROM_UDISK` | `y` | 从 `/sd0/kws_model/` 加载 `.tflite`，不把模型编进 flash |
| `CONFIG_AE_SUPPORT_PROMPT_TONE` | `y` | 启用提示音 |
| `CONFIG_AE_PROMPT_TONE_SOURCE_VFS` | `y` | 提示音从文件系统播放（非编译进固件的数组） |
| `CONFIG_AE_PROMPT_TONE_DECODER_MP3` | `y` | MP3 解码 |
| `CONFIG_BOARD_SD_NAND_ENABLE` | `y` | 板载 SD-NAND |
| `CONFIG_BOARD_USB_SWITCH_ENABLE` | `y` | Type-C 可切换到 USB MSC，方便 PC 拷贝文件 |

若自行编译且关闭了 `CONFIG_BEKEN_KWS_MODEL_FROM_UDISK`，则不会使用本目录中的 KWS 模型文件，而使用库内置模型。

---

## 3. 部署到设备（推荐：U 盘模式）

目标：把本 `resources` 目录的**内容**拷到 SD-NAND **根目录**，使设备上呈现：

```text
/sd0/
├── kws_model/
│   ├── bk_kws_wakeup.tflite
│   └── bk_kws_commands.tflite
├── asr_wakeup_16k_mono_16bit_en.mp3
├── asr_standby_16k_mono_16bit_en.mp3
└── ...（其余提示音）
```

### 3.1 操作步骤

1. 设备上电，进入 Robot UI 的 **U 盘 / U-disk** 功能（将 Type-C 切换为 BK7259 USB，枚举板载 SD-NAND 为可移动磁盘）。
2. 在 PC 上打开该可移动磁盘。
3. 将本目录中的文件按上述结构复制到磁盘**根目录**：
   - 整个 `kws_model` 文件夹拷到根目录；
   - 所有 `*.mp3` 提示音拷到根目录（不要多套一层 `resources` 文件夹）。
4. **安全弹出** USB 磁盘后，退出 U 盘模式（切回 UART/正常运行），必要时重启设备。
5. 重启后，ASR / 唤醒 / 配网等业务触发时，会从 `/sd0` 读取模型与提示音。

### 3.2 正确示例 vs 错误示例

**正确（PC 磁盘根目录）：**

```text
E:\
├── kws_model\bk_kws_wakeup.tflite
├── kws_model\bk_kws_commands.tflite
└── asr_wakeup_16k_mono_16bit_en.mp3
```

**错误（多套了一层目录，固件找不到）：**

```text
E:\resources\kws_model\...
E:\resources\asr_wakeup_....mp3
```

或把提示音误放到 `kws_model/` 下、或改名。

---

## 4. KWS 模型使用说明

### 4.1 文件职责

| 文件 | 用途 |
|------|------|
| `bk_kws_wakeup.tflite` | 唤醒词识别（如「你好博通」等） |
| `bk_kws_commands.tflite` | 命令词识别（前进/后退/转弯等） |

运行时由 `audio_engine` 在 ASR 启动前加载：

- `/sd0/kws_model/bk_kws_wakeup.tflite`
- `/sd0/kws_model/bk_kws_commands.tflite`

### 4.2 注意点

1. **文件名固定**，不可随意重命名；路径必须为 `kws_model/` 子目录。
2. 模型须为当前方案配套的 **Vela / Ethos-U** 版本，与固件中的 KWS 库匹配；请使用本包提供的文件，不要混用其他版本。
3. 加载失败时，串口日志中会出现类似  
   `load kws wakeup model from /sd0/kws_model/... failed`  
   或 `prepare kws udisk failed`，请检查是否已拷贝、路径是否正确、SD-NAND 是否已挂载。
4. 更新模型后，建议重启设备再进命令词/唤醒相关界面。

---

## 5. 提示音使用说明

### 5.1 格式要求

本包提示音文件名已标明格式约定，建议客户替换时保持一致：

| 项目 | 要求 |
|------|------|
| 容器/编码 | MP3 |
| 采样率 | 16 kHz |
| 声道 | 单声道（mono） |
| 位深（命名约定） | 16-bit PCM 源转码 |
| 命名 | 必须与下表文件名**完全一致** |

固件通过路径 `/sd0/<文件名>.mp3` 播放，例如：

`/sd0/asr_wakeup_16k_mono_16bit_en.mp3`

### 5.2 文件与业务事件对应关系

| 文件名 | 典型场景 |
|--------|----------|
| `asr_wakeup_16k_mono_16bit_en.mp3` | 唤醒成功 |
| `asr_standby_16k_mono_16bit_en.mp3` | 进入待机/退出唤醒 |
| `network_provision_16k_mono_16bit_en.mp3` | 开始配网 |
| `network_provision_success_16k_mono_16bit_en.mp3` | 配网成功 |
| `network_provision_fail_16k_mono_16bit_en.mp3` | 配网失败 |
| `reconnect_network_16k_mono_16bit_en.mp3` | 开始重连网络 |
| `reconnect_network_success_16k_mono_16bit_en.mp3` | 重连成功 |
| `reconnect_network_fail_16k_mono_16bit_en.mp3` | 重连失败 |
| `rtc_connection_lost_16k_mono_16bit_en.mp3` | RTC 连接断开 |
| `agent_joined_16k_mono_16bit_en.mp3` | Agent 加入成功 |
| `agent_offline_16k_mono_16bit_en.mp3` | Agent 离线 |
| `agent_start_fail_16k_mono_16bit_en.mp3` | Agent 启动失败 |
| `low_voltage_16k_mono_16bit_en.mp3` | 低电量提示 |
| `ota_update_success_16k_mono_16bit_en.mp3` | OTA 成功 |
| `ota_update_fail_16k_mono_16bit_en.mp3` | OTA 失败 |

---

## 6. 快速自检清单

部署完成后建议按下列项检查：

- [ ] PC 磁盘根目录可见 `kws_model` 文件夹（内含两个 `.tflite`）
- [ ] PC 磁盘根目录可见上表全部 `*_en.mp3`（文件名无拼写错误）
- [ ] 退出 U 盘模式 / 重启后，进入命令词识别界面可正常启动 ASR
- [ ] 说唤醒词可听到唤醒提示音，进入待机可听到待机提示音
- [ ] 配网成功/失败等事件有对应提示音

若 ASR 起不来：优先查模型路径与 `CONFIG_BEKEN_KWS_MODEL_FROM_UDISK`。  
若无提示音：优先查 mp3 是否在 `/sd0` 根目录、文件名是否一致、是否仍停留在 USB MSC 模式导致设备侧无法访问存储。

---

## 7. 常见问题

**Q1：拷贝后仍然加载失败？**  
确认没有多一层 `resources` 目录；确认已从 U 盘模式切回正常模式并重启；确认文件名大小写与本文一致。

**Q2：能否把提示音放进 `kws_model` 目录？**  
不能。提示音必须在存储根目录；只有 KWS 模型在 `kws_model/` 下。

**Q3：能否只用内置模型、不拷 `.tflite`？**  
需要关闭 `CONFIG_BEKEN_KWS_MODEL_FROM_UDISK` 并重新编译/使用对应固件。当前默认配置为外部加载，必须部署本包中的模型文件。

**Q4：`1:` 和 `/sd0` 是什么关系？**  
同一块 SD-NAND：`1:` 为 FatFS 盘符写法，`/sd0` 为 VFS 挂载路径；客户在 PC 上看到的是磁盘根目录，对应设备上的 `/sd0` / `1:`。
