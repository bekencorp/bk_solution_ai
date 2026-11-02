# BK7259 Secure-Boot Low-Power Headset Example Project

- [中文](./README_CN.md)

## 1. Introduction

`secureboot_headset_lp` targets a Bluetooth headset / soundbar form factor. On top of the secure boot chain it keeps a single product line — **a one-page Bluetooth music UI** (A2DP Sink playback with a live spectrum visualizer) — and adapts the power path around it.

Differences from `secureboot_ai`:

- Only one UI page (`page_bt_music`). The robot multi-page menu, demo center, provisioning, cloud AI and camera modules are removed.
- AP power-down + CP sleep are enabled: PSRAM data retention, AP fast boot, and BT controller retention across AP power-off.
- Board peripherals are trimmed to the headset/soundbar set: MIPI LCD, touch, audio, SD-NAND and the USB mux.

Secure boot itself is unchanged from `secureboot_ai`: BL1, BL2/MCUboot, TF-M, image signing and Flash AES encryption.

## 2. Main Configuration

### Security

- **Trusted boot chain**: BootROM → BL1 → BL2/MCUboot → TF-M Secure → CP Non-Secure → AP Non-Secure.
- **Image protection**: EC-P256 image signing plus fixed-key Flash AES encryption.
- **Isolation**: TF-M uses `profile_medium`, isolation level 2 and the Crypto service.
- **Flash layout**: 8 MB flash, `OVERWRITE_ONLY` single-slot strategy; one `primary_all` slot plus `ota_control`, no OTA staging partition.
- **Keys and security parameters**: development signing keys under `config/key/`; secure boot, encryption and version-counter settings in `partitions/bk7259/security.csv`, `ota.csv` and `auto_partitions.csv`.

### Low power

CP side (`cp/config/bk7259/defconfig`):

- `CONFIG_PM_AP_POWERDOWN_WHEN_LV=y` — allow the whole AP core to power off when idle.
- `CONFIG_DEEP_LV=y`, `CONFIG_OPTIMIZE_AON_CURRENT=y` — deep sleep plus AON current optimization.
- `CONFIG_PSRAM_DATA_RETENTION_ENABLE=y` — PSRAM data retention (PSRAM is not used as system memory).
- `CONFIG_PM_AP_FAST_BOOT_ENABLE=y` — AP fast boot restore path.
- `CONFIG_BLUETOOTH_SUPPORT_AP_PWD_RETENTION=y` — BT controller is retained across AP power-off.
- `CONFIG_BLE_LV_SUPPORT=y`, `CONFIG_STA_PS=y` — BLE / Wi-Fi low power.

AP side (`ap/config/bk7259_ap/defconfig`):

- `CONFIG_PM_AP_SUBPOWER_DOMAIN_DEFAULT_DISABLE=y` — power off the VIDEO_POST / H26E / ISP / NPU sub-power domains during AP power-management hardware init.
- `CONFIG_PSRAM_DATA_RETENTION_ENABLE=y`, `CONFIG_PM_AP_FAST_BOOT_ENABLE=y`.

> **Three options that must stay disabled**: `CONFIG_PM_CURRENT_OPTIMIZE_XTAL_RX_TX_ANABUF`, `CONFIG_PM_CP_PERI_CLK_DEFAULT_OFF` and `CONFIG_PM_XTAL_RX_TX_ANABUF_DEFAULT_OFF` in the CP defconfig. Setting any of them to `y` breaks DSI timing: the UART floods with `DSI ERR dpi_pld_full` and the panel shows color shifts with vertical rolling.

### Display and input

- 320×385 MIPI DSI LCD (`jd9855`), rotated 90°, ARGB8888 with LVGL V9 partial rendering.
- Output goes through the VG-Lite GPU compression path (`CONFIG_VG_LITE_GPU=y`) with dual frame buffers.
- Touch: CST9217 over I2C (RST=GPIO29, INT=GPIO30, SDA=GPIO71, SCL=GPIO70).

### Board

- **Target chip**: BK7259, build target `bk7259`.
- **Board power**: peripheral 3.3 V gated by GPIO53, 2.8 V by GPIO32; backlight on GPIO7 (active low); panel reset on GPIO5.
- **SD-NAND**: onboard SD-NAND on **SDIO1** (GPIO14–GPIO19), mounted at `/sd0`.
- **Type-C**: GPIO54 switches between the CH340 UART and the BK7259 USB device path. UART logs are unavailable while USB mode is selected.

## 3. Build

`SDK_DIR` in the `Makefile` defaults to `../../../bk_avdk_smp_release_4.0.1`. Pass it explicitly if your SDK lives elsewhere:

```bash
cd projects/secureboot_headset_lp
make clean
make bk7259                    # or: make bk7259 SDK_DIR=/path/to/bk_avdk_smp
```

Docker build:

```bash
export SDK_DIR=/path/to/bk_avdk_smp
./dbuild.sh make clean
./dbuild.sh make bk7259
```

Outputs:

```text
build/bk7259/secureboot_headset_lp/package/
```

- `all-app.bin`: complete flash image.
- `bootloader.bin`: secure-boot bootloader package.
- `app.elf`: AP debug symbols.
- `otp_efuse_config.json`: generated OTP/eFuse configuration reference.

During development, flash `all-app.bin` with BKFIL. See the repository-level [English guide](../../README.md) for environment setup and flashing.

> **After editing a defconfig**: delete `build/bk7259/secureboot_headset_lp/bk7259/config/config` and `.../bk7259_ap/config/config` first, otherwise the stale `.config` overrides the new defconfig. Do not regenerate a defconfig through menuconfig — it drops the explicit override lines for SoC defaults.

> **Security warning**: OTP/eFuse programming is normally irreversible. Signing private keys and symmetric keys stored in this repository are for development only and must not be used in production.

## 4. Boot Verification

After flashing, the UART should show the secure boot chain followed by the AP application logs:

```text
secureboot_headset_lp: CP NS world reached (secure boot OK)
media_dev: panel opened: fmt=... decompress=1
ap_main: LVGL ready on 320x385 MIPI (first page pending)
```

The panel should light up and show the Bluetooth music page. Search for `soundbar_xxxxxx` from a phone, connect, and stream A2DP audio.

If boot stops at signature verification, manifest processing, Flash AES or the security counter, the flashed image does not match the device OTP/eFuse configuration. Secure-boot and application logs may use different UART paths and must be checked separately.

## 5. UI Controls

This is a single-page product: there is no multi-level menu and no physical-key navigation (the page navigation API is stubbed out in this project).

### Touch

- Bottom transport row: `vol-` / `prev` / `play-pause` / `next` / `vol+`.
- Top status area: link state dot and status text; the `PAIR` button at the top right puts the sink back into connectable/discoverable mode.
- Six neon bars in the middle: live SBC sub-band energy, low to high frequency.
- After about 5 seconds without touch during playback, the UI enters immersive mode (status and transport rows hidden, bars enlarged). Tap anywhere to leave it.

## 6. Low-Power Verification

AP power-off / power-on is driven entirely from the **CP side** with CLI commands; the AP only registers power callbacks (same split as the SDK `pm_secureboot_ap_fast_boot` example). At boot `user_app_main()` in `cp_main.c` votes ON once to start the AP. After that, from the CP console:

| Command | Action |
|---|---|
| `ap_fast_boot on` | Votes ON, starts the AP, blocks until AP full ready |
| `ap_fast_boot off` | Votes OFF, powers the AP down, blocks until `ap power off!!!` |
| `ap_fast_boot cycle` | OFF → 1 s delay → ON, one command for a full power cycle |

Equivalent low-level commands (module 9 = `PM_BOOT_AP_MODULE_NAME_APP`; state 0 = ON, 1 = OFF, easy to mix up):

```text
pm_boot_ap 9 0      # AP on,  same as ap_fast_boot on
pm_boot_ap 9 1      # AP off, same as ap_fast_boot off
```

Expected UART output for `ap_fast_boot off`:

```text
pm:I:ap_close: send recovery seq=1
ap0:AP:D:AP close request waiting prepare seq=1
ap0:ap_main:display quiesce: lvgl stopped + panel powered off in X ms
pm:I:ap power off!!!
```

The screen goes dark here (backlight off + panel unpowered) while the AP SRAM/PSRAM contents are retained. The panel 3.3V rail (GPIO53, which is also NAND_VDD), the camera 2.8V rail (GPIO32) and the display 1.8V aux LDO are all driven off before the power-down: they sit in the always-on domain and would otherwise keep drawing current for the whole AP-off window. The cost is that the SD-NAND loses power too, so a post-wake SD access needs a fresh power-on and init.

Expected UART output for `ap_fast_boot on`:

```text
pm:I:AP_TIME ap0_restore_scheduler_ready total_us=...
pm:I:AP_TIME ap_full_ready total_us=...
pm:D:pm_dbg mb_send cmd=0x11
ap0:ap_main:display reinit after AP wake
ap0:media_dev:panel opened: fmt=... decompress=1
ap0:ap_main:display reinit done
```

After wake-up the AP SRAM/PSRAM contents are retained, but **the UART / DSI / DPU / GPU / TP registers are reset**. `ap_main.c` therefore registers a `PM_AP_POWER_PRIORITY_SERVICE` power callback:

- `prepare_power_off`: closes the flush gate so no new frame can reach the DPU after the power-off probe (must return in constant time, no RTOS services).
- `quiesce`: stops the LVGL task with `lv_vendor_stop()`, then calls `media_lcd_backlight_off()` and `media_board_power_off()` (panel 3.3V, camera 2.8V, display 1.8V aux LDO off, panel held in reset). **Stopping LVGL has to happen before power-off**: stopping it from the wake path never gets the stop ack for the frame that was in flight when the AP went down, so `lv_vendor_stop()` blocks forever (symptom: only `display reinit after AP wake`, nothing after it, plus periodic `GPU hang`).
    - The panel sleep-in DCS sequence is **not** sent here: `0x28 + 20 ms + 0x10 + 120 ms` is a fixed 140 ms and would blow the `PM_AP_QUIESCE_CALLBACK_TIMEOUT_MS` (100 ms) budget — on timeout the whole power-off is rolled back (`AP_FAST_CB quiesce timeout` → `AP close abort completed` → `AP vote OFF ret=-1`) and the screen comes back on. Cutting the rails achieves the same thing: the panel simply loses power, and the sleep-in sequence runs in `media_lcd_panel_close()` on the wake path, which has no such limit. The DSI/DPU registers live in the AP power domain and are reset by the power-down anyway.
- `resume`: signals a dedicated `disp_resume` thread that rebuilds the display stack — `media_lcd_panel_close()` (tears down the stale DSI/DPU/panel context left by the power-down) → `media_board_power_on()` → `media_lcd_panel_open()` → `lv_gpu_deinit()/lv_gpu_init()` → `drv_tp_close()/drv_tp_open()` → re-enter the Bluetooth music page → `lv_vendor_start()`. This path has no 100 ms limit; a full rebuild takes about 1 s.

Seeing both `display reinit after AP wake` and `display reinit done` means the rebuild finished. If only the first line appears, something blocked partway through the rebuild and needs further investigation.

> The AP does not vote and does not register `app_resume`: power on/off is fully owned by the CP. The AP-side `bk_pm_module_vote_boot_ap_ctrl()` only sends a mailbox request and waits for a 100 ms ack, so it keeps executing while the CP is already powering it down.
>
> AP task stacks / queues must stay in SRAM (`CONFIG_TASK_STACK_IN_PSRAM` / `CONFIG_QUEUE_IN_PSRAM` kept at `n`), otherwise the AP dumps on wake.

## 7. Secure Development Notes

- After changing a signing key, Flash AES key, application version or security counter, regenerate and flash a matching complete image.
- Increase the security counter only according to the product update policy, so the device does not reject older or mismatched OTA packages.
- TF-M Persistent Storage, Firmware Update and Initial Attestation are disabled in the current configuration.
- Before production, replace all development keys and review the Root of Trust, OTP/eFuse injection process and rollback policy.

## 8. Source Layout

| Location | Contents |
|---|---|
| `ap/ap_main.c` | AP entry: display/LVGL bring-up, Bluetooth and A2DP/HFP init, low-power wake rebuild |
| `ap/beken_generated/page_bt_music/` | Single-page UI (`page_bt_music.c`), spectrum analysis (`bt_rhythm.c`), fonts and i18n |
| `ap/beken_generated/page_bt_music/page_hooks.h` | Page hook stub (single page, no back gesture) |
| `ap/beken_generated/page_bt_music/ui_nav_router.h` | Key-navigation stub (no multi-page navigation) |
| `ap/src/common/media_devices.c` | Board power, MIPI panel / DPU open-close, backlight |
| `ap/src/common/board_usb_switch.c` | Type-C UART/USB mux (`usbsw` CLI) and SD-NAND helpers |
| `ap/a2dp_sink/`, `ap/hfp_hf/`, `ap/audio_param/` | A2DP Sink / HFP HF demos and audio parameters |
| `cp/cp_main.c` | CP side: boots the AP (votes ON), `ap_fast_boot on\|off\|cycle` CLI, A2DP/HFP offload |
| `partitions/bk7259/` | Partition table, security configuration, pack description |
| `config/key/` | Development signing keys |

Display drivers (`app_display`, etc.) come from the solution component directory `components/multimedia_device_service`, pulled in through `EXTRA_COMPONENTS_DIRS` in `ap/CMakeLists.txt`; the other robot components are no longer built.
