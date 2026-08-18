# baf_example — BAF Animation Playback Demo (BK7259)

* [中文](./README_CN.md)

`baf_example` plays **BAF (Beken Animation Format)** animations on BK7259
(Robot V1, jd9855 320×385 MIPI-DSI panel): an H.264-encoded RGB stream plus an
A8 alpha stream, hardware-decoded and composited into an ARGB8888 framebuffer
that is scanned out by the DPU. Animations can be compiled into the firmware or
played from a `.baf` file on the TF card.

## 1. Two playback modes (chosen at build time)

Select via menuconfig "BAF example playback backend" (see `ap/Kconfig.projbuild`):

| Mode | Kconfig | Description |
| --- | --- | --- |
| **RAW (default)** | `CONFIG_BAF_EXAMPLE_MODE_RAW` | Drive the bk_baf decoder + GPU(VG-Lite)/CPU(Helium) compositor + DPU flush directly, no LVGL. The RAW compositor is further chosen by "BAF RAW render backend" (GPU/CPU) |
| **LVGL** | `CONFIG_BAF_EXAMPLE_MODE_LVGL` (which `select`s `CONFIG_LVGL`) | Play through the `lv_baf` widget (an `lv_image` subclass) inside LVGL |

Animation source:
- **RAW mode**: on boot it auto-cycles every `.baf` under `1:/baf/` on the TF
  card — sorted by filename, each looped **twice**, then advancing to the next and
  wrapping around forever. If `1:/baf/` has no usable `.baf` (or no card), it
  falls back to the compiled-in `ap/assets/sample_bk_baf_asset.c`. Use
  `baf_display play <path>` to interpose a specific file (see §4); it returns to
  the auto-cycle afterwards.
- **LVGL mode**: plays the compiled-in sample animation.

`.baf` and `*_baf_asset.c` are generated from GIF/APNG/MP4 by
`baf_tool/tools/to_baf.py`.

## 2. Hardware (Robot V1)

| Item | Value |
| --- | --- |
| SoC | BK7259 (AP + CP) |
| Panel | jd9855 MIPI-DSI 320×385, ARGB8888 |
| Panel reset / backlight | GPIO_5 / GPIO_7 (backlight active-low) |
| Peripheral 3.3V rail | GPIO_53 |
| TF card | SDIO1 (GPIO_14–19, 4-bit), FATFS drive `1:`, powered from GPIO_53 |

## 3. Build

This is a solution project, so the SDK path must be given:

```bash
cd bk_solution_ai_release_4.0.1/projects/baf_example
make bk7259 SDK_DIR=<path-to>/avdk -j
# CI form: ./dbuild.sh make bk7259
```

- Builds **RAW** mode by default; output at `build/bk7259/baf_example/package/all-app.bin`.
- Switch to **LVGL**: set `CONFIG_BAF_EXAMPLE_MODE_LVGL=y` in
  `ap/config/bk7259_ap/defconfig`, then `make clean` before rebuilding (switching
  modes requires a clean, otherwise the stale sdkconfig is reused).

## 4. Serial CLI commands

Commands are sent from the CP serial console; AP-side commands need the `ap_cmd` prefix.

### TF/SD card (FATFS, drive `1:`)

| Command | Description |
| --- | --- |
| `ap_cmd tf ls [path]` | List a directory (default `1:/`); directories show `<DIR>`, files show byte size |
| `ap_cmd tf mount` | Mount the card (auto-mounted at boot; re-mount after swapping cards) |
| `ap_cmd tf unmount` | Unmount the card |

### Playback control (`baf_display`)

RAW mode auto-cycles `1:/baf/*.baf` on boot (see §1); these commands intervene:

| Command | Description |
| --- | --- |
| `ap_cmd baf_display play <path>` | Interpose a `.baf` from the card (e.g. `1:/baf/dizzy.baf`), play it twice, then return to the auto-cycle; returns to the cycle immediately on failure |
| `ap_cmd baf_display freerun <0\|1>` | Enable/disable max-speed playback (ignore per-frame durations) |

LVGL mode:

| Command | Description |
| --- | --- |
| `ap_cmd baf_display rot <0\|90\|180\|270>` | Set the LVGL display rotation |

Examples:

```text
ap_cmd tf ls baf
ap_cmd baf_display play 1:/baf/fine.baf
ap_cmd baf_display freerun 1
```

## 5. Directory layout

```text
baf_example/
├── CMakeLists.txt
├── Makefile
├── ap/                              # AP core
│   ├── ap_main.c                    # entry: power-on, TF mount, start RAW/LVGL, baf_display CLI
│   ├── baf_raw.c / .h               # RAW backend: decode + GPU/CPU compose + DPU flush + /baf auto-cycle + interpose/freerun
│   ├── baf_page.c / .h              # LVGL backend: lv_baf widget page
│   ├── baf_file.c / .h              # parse a .baf file into a bk_baf_source_t (file playback)
│   ├── tf_card.c / .h               # TF/FATFS mount + `tf` CLI
│   ├── assets/sample_bk_baf_asset.c # animation compiled into the firmware
│   ├── lv_conf_custom.h             # project LVGL config (LVGL mode only)
│   ├── Kconfig.projbuild            # mode / backend selection
│   └── config/bk7259_ap/
│       ├── defconfig                # feature switches (SDCARD/FATFS/SDIO1, mode, ...)
│       └── usr_gpio_cfg.h           # GPIO table (incl. SDIO1 pins)
├── cp/                              # CP core
└── partitions/                      # Flash/RAM partitions
```
