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
- `CONFIG_PM_POWER_VOTE_RECORD=y`, `CONFIG_PM_CLOCK_VOTE_RECORD=y` — record the caller of every vote, which is what you need when the CP refuses to sleep.

AP side (`ap/config/bk7259_ap/defconfig`):

- `CONFIG_PM_AP_SUBPOWER_DOMAIN_DEFAULT_DISABLE=y` — power off the VIDEO_POST / H26E / ISP / NPU sub-power domains during AP power-management hardware init.
- `CONFIG_PSRAM_DATA_RETENTION_ENABLE=y`, `CONFIG_PM_AP_FAST_BOOT_ENABLE=y`.
- `CONFIG_WIFI_ENABLE=y`, `CONFIG_WIFI_AT_ENABLE=y` — Wi-Fi enabled explicitly. The Kconfig default for `WIFI_ENABLE` is already `y`, but a stale generated config has silently overridden it to `n` in this project before.

> **The secure-world RAM must be 0x14000**: `CP_SPE_RAM` in `partitions/bk7259/ram_regions.csv` is `0x014000`. The TF-M linker script `bk7259_s.ld` reserves the last 4 KB of that region at a fixed address for the fastboot resume code (`TFM_FASTBOOT_CODE_ADDR = 0x28011000`); with `0x012000` the regular `.data/.bss` grows into that window and the link fails with `section .tfm_fastboot_code VMA overlaps section .TFM_BSS`.

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

This is a single-page product: there is no multi-level menu, and the page navigation API is stubbed out. The physical key is only used to power the AP off.

### Touch

- Bottom transport row: `vol-` / `prev` / `play-pause` / `next` / `vol+`.
- Top status area: link state dot and status text; the `PAIR` button at the top right puts the sink back into connectable/discoverable mode.
- Six neon bars in the middle: live SBC sub-band energy, low to high frequency.
- After about 5 seconds without touch during playback, the UI enters immersive mode (status and transport rows hidden, bars enlarged). Tap anywhere to leave it.

### Physical key

The board has four ADC keys (S2~S5); only S4 is used: **long press for 3 seconds powers the AP off**. The other keys are not bound. The key never wakes the AP — after power-off the AP is woken by the phone resuming its audio stream. See [6.1 Keys](#61-keys).

## 6. Low-Power Verification

The low-power paths are triggered by hardware, not by UART commands:

- **AP off**: long press the on-board key **S4** (3 s). The AP-side key module votes AP OFF to the CP, which runs the power-down sequence.
- **AP on**: just start streaming from the phone again. After the AP is down, the ACL data the phone sends reaches the CP controller and `bt_ipc_mailbox_send_msg()` votes AP ON automatically, so the AP resumes through the fast-boot path.
- **CP sleeps by default**: about 3 s after the AP is up, `cp_main.c` votes `PM_SLEEP_MODULE_NAME_APP` once. While the AP is running the `PM_SLEEP_MODULE_NAME_AP` vote (set to 0 on AP power-on) keeps the CP awake; once the AP is powered off that vote flips to 1 and the CP enters low-voltage sleep on its own.

### 6.1 Keys

The on-board ADC keys use the same hardware as the `beken_robot` project:

| Key | Channel | Function |
|---|---|---|
| S2 | GPIO13 / ADC ch15 (500~1500 mV) | not bound |
| S3 | GPIO13 / ADC ch15 (4500~6000 mV) | not bound |
| **S4** | GPIO28 / ADC ch4 (500~1500 mV) | **long press 3 s → AP off** |
| S5 | GPIO28 / ADC ch4 (4500~6000 mV) | not bound |

The long-press threshold comes from `CONFIG_ADC_KEY_LONG_PRESS_MS=3000`. Two things matter in `ap/src/common/ap_key.c`:

- The vote runs in its own thread; the key callback only posts a semaphore so the 20 ms ADC sampling timer is never blocked.
- **The saradc client must be released before the power-off** (`bk_adc_key_deinit_ex()` → `bk_saradc_driver_deinit()`). If it is released after the wake-up instead, the CP-side connection is already gone, `mb_ipc_close()` fails and the ipc port stays allocated forever, so every later `mb_ipc_socket()` reports `create socket failed`. On the wake path only the key registration has to be rebuilt (the `resume` callback).

Expected UART output for AP off:

```text
pm:I:ap_close: send recovery seq=1
ap0:AP:D:AP close request waiting prepare seq=1
ap0:ap_main:display quiesce: lvgl stopped + panel powered off in X ms
pm:I:ap power off!!!
```

The screen goes dark here (backlight off, display 1.8V LDO off, panel held in reset) while the AP SRAM/PSRAM contents are retained. Note that `media_board_power_off()` **does not drive GPIO53 low** (peripheral 3.3V, which also gates NAND_VDD): it is the always-on rail for the whole board, and cutting it lets the board run off its bulk capacitors for a few seconds and then cold-boot, which kills the fast-boot path entirely. The SD-NAND therefore stays powered while the AP is down, but the panel is unpowered, so a post-wake SD access still needs a fresh init.

Expected UART output when the AP wakes up (phone resumes streaming):

```text
D:boot_ap 9 0 0x0 [0]E_1
pm:I:AP_TIME ap0_restore_scheduler_ready total_us=...
pm:I:AP_TIME ap_full_ready total_us=...
pm:D:pm_dbg mb_send cmd=0x11
ap0:ap_main:display reinit after AP wake
ap0:media_dev:panel opened: fmt=... decompress=1
ap0:ap_key:ADC key re-init after AP wake
ap0:ap_main:display reinit done
```

After wake-up the AP SRAM/PSRAM contents are retained, but **the UART / DSI / DPU / GPU / TP registers are reset**. `ap_main.c` therefore registers a `PM_AP_POWER_PRIORITY_SERVICE` power callback:

- `prepare_power_off`: closes the flush gate so no new frame can reach the DPU after the power-off probe (must return in constant time, no RTOS services).
- `quiesce`: stops the LVGL task with `lv_vendor_stop()`, then calls `media_lcd_backlight_off()` and `media_board_power_off()` (display 1.8V aux LDO off, panel held in reset). **Stopping LVGL has to happen before power-off**: stopping it from the wake path never gets the stop ack for the frame that was in flight when the AP went down, so `lv_vendor_stop()` blocks forever (symptom: only `display reinit after AP wake`, nothing after it, plus periodic `GPU hang`).
    - The panel sleep-in DCS sequence is **not** sent here: `0x28 + 20 ms + 0x10 + 120 ms` is a fixed 140 ms and would blow the `PM_AP_QUIESCE_CALLBACK_TIMEOUT_MS` (100 ms) budget — on timeout the whole power-off is rolled back (`AP_FAST_CB quiesce timeout` → `AP close abort completed` → `AP vote OFF ret=-1`) and the screen comes back on. Cutting the rails achieves the same thing: the panel simply loses power, and the sleep-in sequence runs in `media_lcd_panel_close()` on the wake path, which has no such limit. The DSI/DPU registers live in the AP power domain and are reset by the power-down anyway.
- `resume`: signals a dedicated `disp_resume` thread that rebuilds the display stack — `media_lcd_panel_close()` (tears down the stale DSI/DPU/panel context left by the power-down) → `media_board_power_on()` → `media_lcd_panel_open()` → `lv_gpu_deinit()/lv_gpu_init()` → `drv_tp_close()/drv_tp_open()` → re-enter the Bluetooth music page → `lv_vendor_start()`. This path has no 100 ms limit; a full rebuild takes about 1 s.

Seeing both `display reinit after AP wake` and `display reinit done` means the rebuild finished. If only the first line appears, something blocked partway through the rebuild and needs further investigation.

> The AP does not vote and does not register `app_resume`: power on/off is fully owned by the CP. The AP-side key module calls `bk_pm_module_vote_boot_ap_ctrl()`, which only sends a mailbox request and waits for a 100 ms ack, not for the whole power-down.
>
> AP task stacks / queues must stay in SRAM (`CONFIG_TASK_STACK_IN_PSRAM` / `CONFIG_QUEUE_IN_PSRAM` kept at `n`), otherwise the AP dumps on wake.

### 6.2 Known issues

**Powering the AP off while audio is playing can fail and roll back.** Typical log:

```text
ap0:pwr_core:E:AP fast suspend: DMA still busy mask=0x70 after quiesce
pm:W:ap_close: suspend failed seq=N
pm:I:AP close abort completed; business mailbox reopened
ap0:ap_main:display reinit after AP wake      ← the screen blinks and comes back, the AP never went down
```

`bk_pm_client_thread.c` checks the GDMA channel state after quiesce and rejects the suspend if any channel is still enabled. The AP currently stops LVGL and the panel only — **the audio DMA is left running**, so an A2DP stream trips this protection. Pausing playback first makes the power-off succeed.

**Once the CP enters low-voltage sleep the serial console is mostly unusable**: the UART is gated, command bytes are dropped and usually only a single `wakeup` line comes back.

## 7. Secure Development Notes

- After changing a signing key, Flash AES key, application version or security counter, regenerate and flash a matching complete image.
- Increase the security counter only according to the product update policy, so the device does not reject older or mismatched OTA packages.
- TF-M Persistent Storage, Firmware Update and Initial Attestation are disabled in the current configuration.
- Before production, replace all development keys and review the Root of Trust, OTP/eFuse injection process and rollback policy.

## 8. Source Layout

| Location | Contents |
|---|---|
| `ap/ap_main.c` | AP entry: display/LVGL bring-up, Bluetooth and A2DP/HFP init, low-power wake rebuild |
| `ap/beken_generated/` | LVGL Designer generated single-page UI (`page_1_init.c`, fonts, `event_runtime`) |
| `ap/beken_generated/page_hooks.c` | Page hook registry; `bk_pages_init_all_hooks()` registers them all |
| `ap/beken_generated/page_1_hooks.c` | page_1 business logic: pair / play-pause / prev-next / volume / status dot, wired to the A2DP sink |
| `ap/src/common/media_devices.c` | Board power, MIPI panel / DPU open-close, backlight |
| `ap/src/common/ap_key.c` | ADC keys: S4 long press powers the AP off, releases the saradc client before power-off, re-registers on wake |
| `ap/src/common/board_usb_switch.c` | Type-C UART/USB mux (`usbsw` CLI) and SD-NAND helpers |
| `ap/a2dp_sink/`, `ap/hfp_hf/`, `ap/audio_param/` | A2DP Sink / HFP HF demos and audio parameters |
| `cp/cp_main.c` | CP side: boots the AP (votes ON), default CP sleep vote, A2DP/HFP offload |
| `partitions/bk7259/` | Partition table, security configuration, pack description |
| `config/key/` | Development signing keys |

Display drivers (`app_display`, etc.) come from the solution component directory `components/multimedia_device_service`, pulled in through `EXTRA_COMPONENTS_DIRS` in `ap/CMakeLists.txt`; the other robot components are no longer built.
