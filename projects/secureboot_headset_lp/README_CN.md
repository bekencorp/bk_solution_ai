# BK7259 安全启动低功耗耳机示例工程

- [English](./README.md)

## 1. 工程简介

`secureboot_headset_lp` 是面向蓝牙耳机 / 蓝牙音箱形态的方案工程，在安全启动链之上只保留一条产品主线：**单页面蓝牙音乐**（A2DP Sink 播放 + 频谱可视化），并围绕它做了低功耗适配。

与 `secureboot_ai` 的区别：

- UI 只保留一个页面（`page_bt_music`），不再包含机器人多页面菜单、Demo 中心、配网、云端 AI、摄像头等模块。
- 打开 AP 掉电 + CP 休眠的低功耗通路：PSRAM 数据保留、AP Fast Boot、BT 控制器随 AP 掉电保留。
- 板级外设按耳机 / 音箱裁剪：仅 MIPI LCD + 触摸 + 音频 + SD-NAND + USB 切换。

安全启动部分与 `secureboot_ai` 保持一致：BL1、BL2/MCUboot、TF-M、镜像签名和 Flash AES 加密。

## 2. 主要配置

### 安全配置

- **可信启动链**：BootROM → BL1 → BL2/MCUboot → TF-M Secure → CP Non-Secure → AP Non-Secure。
- **镜像保护**：EC-P256 镜像签名 + 固定密钥 Flash AES 加密。
- **安全隔离**：TF-M 使用 `profile_medium`、隔离等级 2，启用 Crypto 服务。
- **Flash 布局**：8 MB Flash，`OVERWRITE_ONLY` 单槽策略，只有 `primary_all` 执行槽 + `ota_control`，无 OTA 暂存分区。
- **密钥与安全参数**：开发签名密钥在 `config/key/`；安全启动、加密和版本计数配置在 `partitions/bk7259/security.csv`、`ota.csv`、`auto_partitions.csv`。

### 低功耗配置

CP 侧（`cp/config/bk7259/defconfig`）：

- `CONFIG_PM_AP_POWERDOWN_WHEN_LV=y`：AP 空闲时允许整核掉电。
- `CONFIG_DEEP_LV=y`、`CONFIG_OPTIMIZE_AON_CURRENT=y`：深睡 + AON 电流优化。
- `CONFIG_PSRAM_DATA_RETENTION_ENABLE=y`：PSRAM 数据保留（不作为系统内存使用）。
- `CONFIG_PM_AP_FAST_BOOT_ENABLE=y`：AP Fast Boot 恢复通路。
- `CONFIG_BLUETOOTH_SUPPORT_AP_PWD_RETENTION=y`：BT 控制器随 AP 掉电保留。
- `CONFIG_BLE_LV_SUPPORT=y`、`CONFIG_STA_PS=y`：BLE / Wi-Fi 低功耗。

AP 侧（`ap/config/bk7259_ap/defconfig`）：

- `CONFIG_PM_AP_SUBPOWER_DOMAIN_DEFAULT_DISABLE=y`：AP 电源管理初始化时默认关闭 VIDEO_POST / H26E / ISP / NPU 子电源域。
- `CONFIG_PSRAM_DATA_RETENTION_ENABLE=y`、`CONFIG_PM_AP_FAST_BOOT_ENABLE=y`。

> **必须保持为 n 的三项**：CP defconfig 中的 `CONFIG_PM_CURRENT_OPTIMIZE_XTAL_RX_TX_ANABUF`、`CONFIG_PM_CP_PERI_CLK_DEFAULT_OFF`、`CONFIG_PM_XTAL_RX_TX_ANABUF_DEFAULT_OFF`。把它们置 `y` 会破坏 DSI 链路时序，串口持续报 `DSI ERR dpi_pld_full`，画面出现变色、上下滚动。

### 显示与输入

- 320×385 MIPI DSI LCD（`jd9855`），旋转 90°，ARGB8888 + LVGL V9 partial 渲染。
- 输出经 VG-Lite GPU 压缩通路（`CONFIG_VG_LITE_GPU=y`），双 framebuffer。
- 触摸：CST9217（I2C，RST=GPIO29，INT=GPIO30，SDA=GPIO71，SCL=GPIO70）。

### 板级

- **目标芯片**：BK7259，编译目标 `bk7259`。
- **板级电源**：外设 3.3V 由 GPIO53 使能，2.8V 由 GPIO32 使能；背光 GPIO7（低有效）；屏复位 GPIO5。
- **SD-NAND**：板载 SD-NAND 接 **SDIO1**（GPIO14～GPIO19），VFS 挂载点 `/sd0`。
- **Type-C**：GPIO54 在 CH340 UART 与 BK7259 USB 之间切换；USB 模式下串口日志会暂时断开。

## 3. 编译工程

`Makefile` 中 `SDK_DIR` 默认指向 `../../../bk_avdk_smp_release_4.0.1`，SDK 放在其他位置时显式传入：

```bash
cd projects/secureboot_headset_lp
make clean
make bk7259                    # 或 make bk7259 SDK_DIR=/path/to/bk_avdk_smp
```

Docker 编译：

```bash
export SDK_DIR=/path/to/bk_avdk_smp
./dbuild.sh make clean
./dbuild.sh make bk7259
```

产物目录：

```text
build/bk7259/secureboot_headset_lp/package/
```

- `all-app.bin`：完整烧录镜像。
- `bootloader.bin`：安全启动 Bootloader 包。
- `app.elf`：AP 调试符号。
- `otp_efuse_config.json`：OTP/eFuse 配置参考。

开发阶段用 BKFIL 烧录 `all-app.bin` 即可。环境安装和烧录方法见仓库根目录[中文说明](../../README_CN.md)。

> **修改 defconfig 后**：先删除 `build/bk7259/secureboot_headset_lp/bk7259/config/config` 与 `.../bk7259_ap/config/config`，否则旧的 `.config` 会覆盖新的 defconfig 改动。也不要通过 menuconfig 反向生成 defconfig，会丢失与 SoC 默认值相反的显式配置行。

> **安全警告**：OTP/eFuse 烧写通常不可逆。仓库中的签名私钥和对称密钥仅用于开发验证，禁止用于量产。

## 4. 启动验证

烧录后串口应依次出现安全启动链日志和 AP 应用日志，重点检查：

```text
secureboot_headset_lp: CP NS world reached (secure boot OK)
media_dev: panel opened: fmt=... decompress=1
ap_main: LVGL ready on 320x385 MIPI (first page pending)
```

屏幕应点亮并显示蓝牙音乐页；手机蓝牙搜索 `soundbar_xxxxxx` 并连接后即可推送 A2DP 音频。

若停在签名校验、Manifest、Flash AES 或安全计数器阶段，说明烧录镜像与设备 OTP/eFuse 配置不匹配。安全启动日志和应用日志可能走不同 UART 路径，需要分别确认。

## 5. 界面操作

单页面产品，没有多级菜单，也没有实体按键导航（页面导航接口在本工程中是 stub）。

### 触摸

- 底部操作栏：`音量-` / `上一曲` / `播放暂停` / `下一曲` / `音量+`。
- 顶部状态区：连接状态点、状态文本；右上角 `PAIR` 按钮让 Sink 重新进入可连接 / 可发现状态。
- 中间 6 根霓虹柱：随 SBC 子带能量实时律动（低音→高音）。
- 播放中约 5 秒无触摸会自动进入沉浸模式（隐藏状态栏与操作栏、柱体放大），点击屏幕任意位置退出。

## 6. 低功耗验证

AP 的关 / 开全部在 **CP 侧**用 CLI 驱动，AP 侧只注册电源回调（与 SDK 的 `pm_secureboot_ap_fast_boot` 示例一致）。上电时 `cp_main.c` 的 `user_app_main()` 会投一次 ON 把 AP 拉起，之后在 CP 控制台执行：

| 命令 | 作用 |
|---|---|
| `ap_fast_boot on` | 投 ON，拉起 AP，阻塞到 AP full ready |
| `ap_fast_boot off` | 投 OFF，关 AP，阻塞到 `ap power off!!!` |
| `ap_fast_boot cycle` | OFF → 延时 1 s → ON，一条命令走完一次掉电唤醒 |

等价的底层命令（模块号 9 = `PM_BOOT_AP_MODULE_NAME_APP`；state 0 = ON、1 = OFF，容易记反）：

```text
pm_boot_ap 9 0      # 打开 AP，等同 ap_fast_boot on
pm_boot_ap 9 1      # 关闭 AP，等同 ap_fast_boot off
```

`ap_fast_boot off` 的串口现象：

```text
pm:I:ap_close: send recovery seq=1
ap0:AP:D:AP close request waiting prepare seq=1
ap0:ap_main:display quiesce: lvgl stopped + panel powered off in X ms
pm:I:ap power off!!!
```

此时屏幕熄灭（背光关 + 面板断电），AP 掉电但 SRAM / PSRAM 数据保留。面板 3.3V（GPIO53，同时是 SD-NAND 的 NAND_VDD）、camera 2.8V（GPIO32）和显示 1.8V AUX LDO 都在掉电前主动关掉——它们挂在常开域，不关会在整个 AP 掉电期间持续耗电。代价是 SD-NAND 一起断电，唤醒后如需访问 SD 卡要重新上电并初始化。

`ap_fast_boot on` 的串口现象：

```text
pm:I:AP_TIME ap0_restore_scheduler_ready total_us=...
pm:I:AP_TIME ap_full_ready total_us=...
pm:D:pm_dbg mb_send cmd=0x11
ap0:ap_main:display reinit after AP wake
ap0:media_dev:panel opened: fmt=... decompress=1
ap0:ap_main:display reinit done
```

唤醒后 AP 的 SRAM / PSRAM 数据仍在，但 **UART / DSI / DPU / GPU / TP 等外设寄存器已复位**，因此 `ap_main.c` 注册了一个 `PM_AP_POWER_PRIORITY_SERVICE` 的电源回调：

- `prepare_power_off`：关闭 flush 门禁，保证掉电前不再有新帧写 DPU（必须常数时间返回，不能用 RTOS 接口）。
- `quiesce`：在 PM 任务里 `lv_vendor_stop()` 停掉 LVGL 任务，再 `media_lcd_backlight_off()` 关背光、`media_board_power_off()` 关面板 3.3V / camera 2.8V / 显示 1.8V LDO 并拉住面板 reset。**停 LVGL 必须放在掉电前**：如果留到唤醒后再停，掉电瞬间未完成的帧永远等不到完成回调，`lv_vendor_stop()` 会永久阻塞（现象是只有 `display reinit after AP wake`，之后无任何日志，并周期性打印 `GPU hang`）。
    - 回调里**不发面板 sleep-in DCS**：这套 `0x28 + 20 ms + 0x10 + 120 ms` 序列固定要 140 ms，超过 `PM_AP_QUIESCE_CALLBACK_TIMEOUT_MS`（100 ms），一旦超时整个关电流程会回滚（`AP_FAST_CB quiesce timeout` → `AP close abort completed` → `AP vote OFF ret=-1`，屏幕又亮起来）。所以改成断电：面板直接失去供电，sleep-in 序列留到唤醒路径的 `media_lcd_panel_close()` 里执行（那里没有时限）。DSI/DPU 寄存器本身位于 AP 电源域，随掉电复位，不需要在这里处理。
- `resume`：唤醒后投递信号量，由独立线程 `disp_resume` 重建显示栈——`media_lcd_panel_close()`（拆掉已被掉电复位的旧 DSI/DPU/panel 上下文）→ `media_board_power_on()` → `media_lcd_panel_open()` → `lv_gpu_deinit()/lv_gpu_init()` → `drv_tp_close()/drv_tp_open()` → 重进蓝牙音乐页 → `lv_vendor_start()`。这条路径没有 100 ms 限制，完整重建约 1 s。

唤醒后看到 `display reinit after AP wake` 和 `display reinit done` 即表示重建完成；若只有前者，说明重建过程中某一步阻塞，需要继续排查。

> AP 侧不投票、也不注册 `app_resume`：关 / 开完全由 CP 投票驱动。AP 侧的 `bk_pm_module_vote_boot_ap_ctrl()` 只是给 CP 发一条 mailbox 请求并等 100 ms ack，不等整个掉电流程完成，用它做主控容易在掉电过程中继续跑代码。
>
> AP 的任务栈 / 队列必须留在 SRAM（`CONFIG_TASK_STACK_IN_PSRAM` / `CONFIG_QUEUE_IN_PSRAM` 保持 `n`），否则 AP 唤醒时会 dump。

## 7. 安全开发注意事项

- 修改签名密钥、Flash AES 密钥、应用版本或安全计数器后，必须重新生成并烧录匹配的完整镜像。
- 安全计数器只能按产品升级策略递增，避免设备拒绝旧版本或拒绝不匹配的 OTA 包。
- 当前配置未启用 TF-M Persistent Storage、Firmware Update 和 Initial Attestation。
- 量产前必须完成开发密钥替换、Root of Trust 配置、OTP/eFuse 注入方案和回滚策略评审。

## 8. 代码结构

| 位置 | 内容 |
|---|---|
| `ap/ap_main.c` | AP 入口：显示 / LVGL 初始化、蓝牙与 A2DP/HFP 初始化、低功耗唤醒重建 |
| `ap/beken_generated/page_bt_music/` | 单页面 UI（`page_bt_music.c`）、频谱分析（`bt_rhythm.c`）、字体与 i18n |
| `ap/beken_generated/page_bt_music/page_hooks.h` | 页面钩子 stub（单页面，无返回手势） |
| `ap/beken_generated/page_bt_music/ui_nav_router.h` | 按键导航 stub（无多页面导航） |
| `ap/src/common/media_devices.c` | 板级电源、MIPI 屏 / DPU 开关、背光 |
| `ap/src/common/board_usb_switch.c` | Type-C UART/USB 切换（`usbsw` CLI）与 SD-NAND 辅助 |
| `ap/a2dp_sink/`、`ap/hfp_hf/`、`ap/audio_param/` | A2DP Sink / HFP HF Demo 与音频参数 |
| `cp/cp_main.c` | CP 启动 AP（投 ON）、`ap_fast_boot on\|off\|cycle` CLI、A2DP/HFP offload |
| `partitions/bk7259/` | 分区表、安全配置、pack 描述 |
| `config/key/` | 开发用签名密钥 |

显示相关的共享驱动（`app_display` 等）来自方案组件目录 `components/multimedia_device_service`，由 `ap/CMakeLists.txt` 的 `EXTRA_COMPONENTS_DIRS` 引入，不再编译机器人方案的其它组件。
