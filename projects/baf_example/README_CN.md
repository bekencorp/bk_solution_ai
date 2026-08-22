# baf_example — BAF 动画播放示例（BK7259）

* [English](./README.md)

`baf_example` 演示在 BK7259（Robot V1，jd9855 320×385 MIPI-DSI 屏）上播放
**BAF（Beken Animation Format）** 动画：H.264 编码的 RGB 序列 + A8 alpha 序列，
硬件解码后合成到 ARGB8888 帧缓冲并送 DPU 显示。动画既可编译进固件，也可从 TF 卡按
文件播放。

## 1. 两种播放模式（编译期二选一）

在 menuconfig 的 “BAF example playback backend” 选择（见 `ap/Kconfig.projbuild`）：

| 模式 | Kconfig | 说明 |
| --- | --- | --- |
| **RAW（默认）** | `CONFIG_BAF_EXAMPLE_MODE_RAW` | 直接驱动 bk_baf 解码 + GPU(VG-Lite)/CPU(Helium) 合成 + DPU flush，不经过 LVGL。RAW 的合成后端再由 “BAF RAW render backend”（GPU/CPU）选择 |
| **LVGL** | `CONFIG_BAF_EXAMPLE_MODE_LVGL`（会 `select CONFIG_LVGL`） | 通过 `lv_baf` 控件（`lv_image` 子类）在 LVGL 里播放 |

动画来源：
- **RAW 模式**：多层合成器把最多 3 个 BAF 动画自底向上叠加（底层不透明打底，
  上层用 src-over 混合，透明区域露出下层）。运行时在两种互斥模式间切换（用
  `baf_display`，见 §4）：

  - **场景模式 SCENE** *(默认)* —— 显示编译进固件的预设叠层：

    | 场景 | 层（底 → 顶） |
    | --- | --- |
    | 1 | 仅前景 avatar |
    | 2 *(默认)* | 背景 + avatar |
    | 3 | 背景 + avatar + 帘幕 curtain |

  - **自定义模式 CUSTOM** —— 只显示用户从 TF 卡逐层叠加的 `.baf` 文件。给某层指定
    文件即进入 CUSTOM 并清除所有预设层；清除某层只移除该层；所有层都清空时只显示灰色
    背景。选择场景则切回 SCENE 并清除所有自定义层。
- **LVGL 模式**：播放编译进固件的示例动画。

`.baf` 与 `*_baf_asset.c` 由 `baf_tool/tools/to_baf.py` 从 GIF/APNG/MP4 等素材生成。

## 2. 硬件（Robot V1）

| 项 | 值 |
| --- | --- |
| 芯片 | BK7259（AP + CP 双核） |
| 屏 | jd9855 MIPI-DSI 320×385，ARGB8888 |
| 面板复位 / 背光 | GPIO_5 / GPIO_7（背光低电平有效） |
| 外设 3.3V 电源 | GPIO_53 |
| TF 卡 | SDIO1（GPIO_14–19，4 线），FATFS 盘符 `1:`，供电同 GPIO_53 |

## 3. 编译

本工程是 solution 工程，编译时需指定 SDK 路径：

```bash
cd bk_solution_ai_release_4.0.1/projects/baf_example
make bk7259 SDK_DIR=<path-to>/avdk -j
# CI 用法：./dbuild.sh make bk7259
```

- 默认编 **RAW** 模式；产物在 `build/bk7259/baf_example/package/all-app.bin`。
- 切 **LVGL** 模式：在 `ap/config/bk7259_ap/defconfig` 打开
  `CONFIG_BAF_EXAMPLE_MODE_LVGL=y`，并先 `make clean` 再编（切换模式必须 clean，
  否则会复用旧的 sdkconfig）。

## 4. 串口 CLI 命令

命令从 CP 串口控制台发送；AP 侧命令需加 `ap_cmd` 前缀。

### TF 卡（FATFS，盘符 `1:`）

| 命令 | 说明 |
| --- | --- |
| `ap_cmd tf ls [path]` | 列目录（默认 `1:/`），目录显示 `<DIR>`，文件显示字节数 |
| `ap_cmd tf mount` | 挂载 TF 卡（开机已自动挂载；换卡后可重新挂载） |
| `ap_cmd tf unmount` | 卸载 TF 卡 |

### 播放控制（`baf_display`）

RAW 模式（多层合成器，见 §1）：

| 命令 | 说明 |
| --- | --- |
| `ap_cmd baf_display scene <1\|2\|3>` | 场景模式：选预设叠层（1 = avatar，2 = 背景 + avatar（默认），3 = + 帘幕）；同时清除所有自定义层 |
| `ap_cmd baf_display layer <idx> <sdpath>` | 自定义模式：把 TF 卡 `.baf` 叠到第 `idx` 层（0 = 底），如 `1:/baf/hug.baf`；首次指定会清除所有预设层 |
| `ap_cmd baf_display layer <idx> clear` | 自定义模式：清除第 `idx` 层；所有层清空后只显示背景（场景模式下无效） |
| `ap_cmd baf_display maxlayers` | 打印最大可叠加层数 |
| `ap_cmd baf_display freerun <0\|1>` | 开/关顶层最高速播放（忽略每帧时长） |

LVGL 模式：

| 命令 | 说明 |
| --- | --- |
| `ap_cmd baf_display rot <0\|90\|180\|270>` | 设置 LVGL 显示旋转角度 |

示例：

```text
ap_cmd tf ls baf
ap_cmd baf_display maxlayers
ap_cmd baf_display scene 3                     # 预设 3 层
ap_cmd baf_display layer 0 1:/baf/quiet.baf    # 进入自定义模式，仅 quiet.baf
ap_cmd baf_display layer 1 1:/baf/hug.baf      # 叠加 hug.baf
ap_cmd baf_display layer 0 clear               # 移除底层
ap_cmd baf_display scene 2                      # 切回场景模式（预设）
ap_cmd baf_display freerun 1
```

## 5. 目录结构

```text
baf_example/
├── CMakeLists.txt
├── Makefile
├── ap/                              # AP 侧
│   ├── ap_main.c                    # 入口：上电、TF 挂载、按模式启动 RAW/LVGL、baf_display CLI
│   ├── baf_raw.c / .h               # RAW 后端：多层解码 + GPU/CPU 合成 + DPU flush + scene/layer CLI
│   ├── baf_page.c / .h              # LVGL 后端：lv_baf 控件页面
│   ├── baf_file.c / .h              # 解析 .baf 文件为 bk_baf_source_t（SD 卡层覆盖）
│   ├── tf_card.c / .h               # TF/FATFS 挂载 + `tf` CLI
│   ├── assets/                      # 编译进固件的动画
│   │   ├── hello_bk_baf_asset.c     #   前景 avatar（场景 1/2/3）
│   │   ├── background_bk_baf_asset.c#   背景（场景 2/3）
│   │   └── curtain_baf_asset.c      #   帘幕叠加（场景 3）
│   ├── lv_conf_custom.h             # LVGL 工程级配置（仅 LVGL 模式）
│   ├── Kconfig.projbuild            # 模式/后端选择
│   └── config/bk7259_ap/
│       ├── defconfig                # 功能开关（SDCARD/FATFS/SDIO1、模式等）
│       └── usr_gpio_cfg.h           # GPIO 表（含 SDIO1 引脚）
├── cp/                              # CP 侧
└── partitions/                      # Flash/RAM 分区
```
