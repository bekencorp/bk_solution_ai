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
- **RAW 模式**：上电后自动轮播 TF 卡 `1:/baf/` 下的所有 `.baf` 文件——按文件名排序，
  每个循环播放 **2 遍**后切下一个，播完最后一个回到第一个，无限循环。若 `1:/baf/`
  下没有可用 `.baf`（或无卡），则回退播放编译进固件的
  `ap/assets/sample_bk_baf_asset.c`。用 `baf_display play <path>` 可临时插播指定
  文件（见 §4），播完后自动回到轮播。
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

RAW 模式（默认）：

RAW 模式上电即自动轮播 `1:/baf/*.baf`（见 §1）；下列命令用于临时干预：

| 命令 | 说明 |
| --- | --- |
| `ap_cmd baf_display play <path>` | 插播 TF 卡上的 `.baf`（如 `1:/baf/dizzy.baf`），播 2 遍后回到轮播；加载失败则直接回到轮播 |
| `ap_cmd baf_display freerun <0\|1>` | 开/关最高速播放（忽略每帧时长） |

LVGL 模式：

| 命令 | 说明 |
| --- | --- |
| `ap_cmd baf_display rot <0\|90\|180\|270>` | 设置 LVGL 显示旋转角度 |

示例：

```text
ap_cmd tf ls baf
ap_cmd baf_display play 1:/baf/fine.baf
ap_cmd baf_display freerun 1
```

## 5. 目录结构

```text
baf_example/
├── CMakeLists.txt
├── Makefile
├── ap/                              # AP 侧
│   ├── ap_main.c                    # 入口：上电、TF 挂载、按模式启动 RAW/LVGL、baf_display CLI
│   ├── baf_raw.c / .h               # RAW 后端：解码 + GPU/CPU 合成 + DPU flush + /baf 自动轮播 + 插播/freerun
│   ├── baf_page.c / .h              # LVGL 后端：lv_baf 控件页面
│   ├── baf_file.c / .h              # 解析 .baf 文件为 bk_baf_source_t（文件播放）
│   ├── tf_card.c / .h               # TF/FATFS 挂载 + `tf` CLI
│   ├── assets/sample_bk_baf_asset.c # 编译进固件的示例动画
│   ├── lv_conf_custom.h             # LVGL 工程级配置（仅 LVGL 模式）
│   ├── Kconfig.projbuild            # 模式/后端选择
│   └── config/bk7259_ap/
│       ├── defconfig                # 功能开关（SDCARD/FATFS/SDIO1、模式等）
│       └── usr_gpio_cfg.h           # GPIO 表（含 SDIO1 引脚）
├── cp/                              # CP 侧
└── partitions/                      # Flash/RAM 分区
```
