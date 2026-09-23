# beken_robot and secureboot_ai source sharing

For daily development. Related: [BK7259SW-3502](http://192.168.0.61:8080/browse/BK7259SW-3502).

- [中文](./CODE_SHARE_GUIDE_CN.md)

## One sentence

**Keep application code in one place: `beken_robot`.**  
`secureboot_ai` compiles that tree and only keeps secure-boot differences. Do not copy Demo / UI sources into the secure project.

Most people keep working in `beken_robot`. Changes to existing `.c` files go into the secure image automatically. **Do not copy or sync them into `secureboot_ai/ap/src`.**

---

## How the two projects relate

```text
                    ┌─────────────────────────────────┐
                    │  beken_robot (application base) │
                    │  daily development home         │
                    │  ap/src  Demo / UI / peripherals│
                    │  cp/     keepalive / vnd_cal    │
                    └──────────────┬──────────────────┘
                                   │ CMake uses the same sources
                                   │ overlay replaces only files that must differ
                    ┌──────────────▼──────────────────┐
                    │  secureboot_ai (secure flavor)  │
                    │  keeps:                         │
                    │  partitions / keys / TF-M / sign│
                    │  cp_main (NS entry)             │
                    │  USB overlay (see below)        │
                    └─────────────────────────────────┘
```

| | `beken_robot` | `secureboot_ai` |
|---|---|---|
| Role | Application base | Same application + trusted boot chain |
| Build output | Non-secure image | BL1 → BL2/MCUboot → TF-M → CP NS → AP NS |
| Daily Demo / UI changes | **Edit here** | Do not keep a second copy |
| Partitions, keys, `security.csv` | Non-secure layout | **Do not merge with the base**; this is the flavor identity |

There is no third `projects/common/` tree. Both projects already passed test-group acceptance. This change removes duplicated application sources; it does not force-merge differences that would change secure-image behavior.

---

## How sources are selected at build time

The shared AP list is:

`beken_robot/ap/ap_sources.cmake`

Rules:

1. Building `beken_robot`: no overlay; every file comes from `beken_robot/ap/`.
2. Building `secureboot_ai`: `APP_ROOT` points at `../../beken_robot/ap`. If the same relative path exists under `secureboot_ai/ap/`, that overlay file is compiled; otherwise the base file is used.

Current overlay: **one file only**:

```text
secureboot_ai/ap/src/common/board_usb_switch.c
```

Reason: under NS + secure boot a full `usbd_deinitialize()` power cycle hangs on the second bring-up, so the secure flavor uses SOFTCONN. The base path avoids touching the USB PHY so the MIPI PLL does not glitch. This is not a forgotten sync. **Do not merge the two files.**

CP:

- `robot_keepalive.c` and `vnd_cal.c` come from `beken_robot/cp/` when building the secure project.
- `secureboot_ai/cp/cp_main.c` is the secure NS entry (AP boot vote + `CP NS world reached`). Do not replace it with the base `cp_main.c`.
- `secureboot_ai/cp/vnd_cal.h` is a jump header to the base `vnd_cal.h`. SDK `bk_init` includes this header from **this project's** `cp/` directory, so the file must stay. Do not put a second calibration implementation here.

---

## Daily workflow

### Change existing features (UI / Demo / peripherals)

Edit the corresponding `.c` / `.h` / `.cc` under `beken_robot/ap/`.

**Do not**:

- Place a same-named file under `secureboot_ai/ap/src/` (that becomes an accidental overlay).
- Copy the file to the secure project after editing the non-secure one.
- Duplicate directories "so the secure project has it too".

Headers also come from the base: `secureboot_ai/ap/include` must not exist. Edit `beken_robot/ap/include/`.

### Add / remove / rename a compilation unit

Then edit `_robot_app_rel_srcs` in `beken_robot/ap/ap_sources.cmake`.

Changing the contents of an existing `.c` does **not** require opening that cmake file.

Put new files under `beken_robot/ap/` and add the relative path to the list (for example `src/demo/foo.c`). Do not add new application units only under `secureboot_ai/`.

### Change CP application code

Edit `beken_robot/cp/` (keepalive, vnd_cal).  
`secureboot_ai/cp/cp_main.c` only does the boot vote and NS log.

### Flavor-owned files (not application code)

These belong to `secureboot_ai`. **Do not** align them with `beken_robot` for reuse:

- `partitions/` (`security.csv`, `ota.csv`, `auto_partitions.csv`)
- `config/key/`, `config/bk7259/config` (defconfig)
- `ap/config/**/usr_gpio_cfg.h`, `cp/config/**/usr_gpio_cfg.h`
- `cp/cp_main.c`
- this project's `Makefile` / `Kconfig.projbuild`

`resources/` (prompt tones, KWS models) is still per-project. Update both copies when you change resource files; they are not selected by CMake overlay.

`ap/lv_conf_custom.h` currently matches on both sides but is still a per-project copy. Change both, or confirm that only one flavor should change.

---

## When a new overlay is allowed

Add a file under `secureboot_ai/ap/` as overlay only when both are true:

1. NS / secure-boot **must** behave differently from the base (for example USB second-init hang).
2. The difference is commented and reviewed. "Forgot to sync the non-secure fix" is not a valid overlay.

Place the overlay at the **same relative path** as the base file, for example:

```text
beken_robot/ap/src/foo.c          ← base
secureboot_ai/ap/src/foo.c        ← overlay, compiled only for the secure image
```

The relative path must already be in `_robot_app_rel_srcs`. Overlay matching uses that list; do not add a second SRCS table.

**Do not** overlay comment-only diffs or forgotten feature patches. Merge those back into `beken_robot` and delete the secure copy.

Current whitelist: `src/common/board_usb_switch.c` only.

---

## Suggested checks

After an application change:

1. Build and test `beken_robot` first (daily path).
2. Build `secureboot_ai` at least once before merge (missing source / duplicate symbol).
3. If you touched USB switching, flash, or boot, re-test on a secure board (the NS path differs).

Secure-image boot smoke (does not replace Demo testing):

```text
[FCE] verify OK
secureboot_ai: CP NS world reached (secure boot OK)
LVGL started, page_1 loaded
AP main running...
```

---

## Common mistakes

| Mistake | Correct approach |
|---|---|
| There is still `ap/src/demo/*.c` in the secure project; edit that for the secure package | Those copies are gone. Edit `beken_robot` |
| Both project trees must be identical | Only the shared application files; partitions/keys must differ |
| A new Demo needs one line in each `CMakeLists.txt` | Edit `ap_sources.cmake` once |
| `secureboot_ai/cp/vnd_cal.h` looks empty, so delete it | Keep it; SDK includes it from this project's `cp/` |
| USB implementations differ, so merge them | Do not; known secure-path difference |
| Extracting `projects/common/` would be cleaner | Do not. Daily work stays in `beken_robot` |

---

## File map

| File | Role |
|---|---|
| `beken_robot/ap/ap_sources.cmake` | Shared AP source list + overlay selection |
| `beken_robot/ap/CMakeLists.txt` | Base: `APP_ROOT` = this directory |
| `secureboot_ai/ap/CMakeLists.txt` | Flavor: `APP_ROOT` = base, overlay on |
| `secureboot_ai/ap/src/common/board_usb_switch.c` | Only AP overlay |
| `secureboot_ai/cp/CMakeLists.txt` | Local `cp_main` + base keepalive/vnd_cal |
| `secureboot_ai/cp/vnd_cal.h` | Jump header |

Start reading application sources from `beken_robot/ap/src/` and `beken_robot/ap/include/`.
