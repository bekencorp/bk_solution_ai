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
- **RAW mode**: a multi-layer compositor stacks up to 3 BAF animations back-to-front
  (base is opaque, upper layers are src-over blended so their transparent regions
  reveal the layers below). It runs in one of two mutually-exclusive modes,
  switched at runtime with `baf_display` (see §4):

  - **SCENE** *(default)* — show a compiled-in preset stack:

    | Scene | Layers (back → front) |
    | --- | --- |
    | 1 | avatar only |
    | 2 *(default)* | background + avatar |
    | 3 | background + avatar + curtain |

  - **CUSTOM** — show only `.baf` files the user stacks from the TF card, one per
    layer slot. Assigning a file switches to CUSTOM and drops all preset layers;
    clearing a slot removes just that layer; when every slot is empty only the grey
    background is shown. Selecting a scene switches back to SCENE and drops all
    custom layers.
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

RAW mode (layered compositor, see §1):

| Command | Description |
| --- | --- |
| `ap_cmd baf_display scene <1\|2\|3>` | SCENE mode: select a preset stack (1 = avatar, 2 = background + avatar (default), 3 = + curtain); clears any custom layers |
| `ap_cmd baf_display layer <idx> <sdpath>` | CUSTOM mode: stack a card `.baf` onto layer `idx` (0 = back), e.g. `1:/baf/hug.baf`; the first assignment drops all preset layers |
| `ap_cmd baf_display layer <idx> clear` | CUSTOM mode: clear layer `idx`; when all slots are empty only the background shows (invalid in SCENE mode) |
| `ap_cmd baf_display maxlayers` | Print the maximum number of stackable layers |
| `ap_cmd baf_display freerun <0\|1>` | Enable/disable max-speed playback on the top layer (ignore per-frame durations) |

LVGL mode:

| Command | Description |
| --- | --- |
| `ap_cmd baf_display rot <0\|90\|180\|270>` | Set the LVGL display rotation |

Examples:

```text
ap_cmd tf ls baf
ap_cmd baf_display maxlayers
ap_cmd baf_display scene 3               # preset 3-layer stack
ap_cmd baf_display layer 0 1:/baf/quiet.baf   # -> CUSTOM mode, only quiet.baf
ap_cmd baf_display layer 1 1:/baf/hug.baf     # stack hug.baf on top
ap_cmd baf_display layer 0 clear              # remove the bottom layer
ap_cmd baf_display scene 2               # back to SCENE mode (presets)
ap_cmd baf_display freerun 1
```

## 5. Directory layout

```text
baf_example/
├── CMakeLists.txt
├── Makefile
├── ap/                              # AP core
│   ├── ap_main.c                    # entry: power-on, TF mount, start RAW/LVGL, baf_display CLI
│   ├── baf_raw.c / .h               # RAW backend: multi-layer decode + GPU/CPU compose + DPU flush + scene/layer CLI
│   ├── baf_page.c / .h              # LVGL backend: lv_baf widget page
│   ├── baf_file.c / .h              # parse a .baf file into a bk_baf_source_t (SD-card layer overlay)
│   ├── tf_card.c / .h               # TF/FATFS mount + `tf` CLI
│   ├── assets/                      # animations compiled into the firmware
│   │   ├── hello_bk_baf_asset.c     #   foreground avatar (scenes 1/2/3)
│   │   ├── background_bk_baf_asset.c#   background (scenes 2/3)
│   │   └── curtain_baf_asset.c      #   curtain overlay (scene 3)
│   ├── lv_conf_custom.h             # project LVGL config (LVGL mode only)
│   ├── Kconfig.projbuild            # mode / backend selection
│   └── config/bk7259_ap/
│       ├── defconfig                # feature switches (SDCARD/FATFS/SDIO1, mode, ...)
│       └── usr_gpio_cfg.h           # GPIO table (incl. SDIO1 pins)
├── cp/                              # CP core
└── partitions/                      # Flash/RAM partitions
```
