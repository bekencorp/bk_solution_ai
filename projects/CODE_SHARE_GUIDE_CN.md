# beken_robot 与 secureboot_ai 代码复用说明

面向日常开发同事。关联需求：[BK7259SW-3502](http://192.168.0.61:8080/browse/BK7259SW-3502)。

- [English](./CODE_SHARE_GUIDE.md)

## 一句话

**应用代码只维护一份，写在 `beken_robot`。**  
`secureboot_ai` 编译时直接引用这份代码，只保留安全启动相关差异，不要再复制一份 Demo / UI 源码。

多数人继续在非安全工程 `beken_robot` 里改功能即可；改完的 `.c` 会自动进安全镜像，**不必再往 `secureboot_ai/ap/src` 拷贝或同步**。

---

## 两个工程是什么关系

```text
                    ┌─────────────────────────────────┐
                    │  beken_robot（应用底座）          │
                    │  日常开发主战场                    │
                    │  ap/src  Demo / UI / 外设         │
                    │  cp/     keepalive / vnd_cal      │
                    └──────────────┬──────────────────┘
                                   │ CMake 引用同一份源码
                                   │ overlay 仅覆盖必须不同的文件
                    ┌──────────────▼──────────────────┐
                    │  secureboot_ai（安全 flavor）     │
                    │  自己只保留：                      │
                    │  分区 / 密钥 / TF-M / 签名         │
                    │  cp_main（NS 入口）               │
                    │  USB overlay（见下文）            │
                    └─────────────────────────────────┘
```

| | `beken_robot` | `secureboot_ai` |
|---|---|---|
| 角色 | 应用底座 | 同一套应用 + 安全启动链 |
| 编译目标 | 非安全镜像 | BL1 → BL2/MCUboot → TF-M → CP NS → AP NS |
| 日常改 Demo / UI | **在这里改** | 不要再改一份 |
| 分区、密钥、security.csv | 非安全布局 | **禁止与底座合并**，这是安全工程身份 |

没有抽出第三份 `projects/common/`。两边都已经过测试组验收，本方案只去双写，不强行合并会改变安全镜像行为的差异。

---

## 编译时源码怎么选

AP 共享清单在：

`beken_robot/ap/ap_sources.cmake`

规则：

1. 编 `beken_robot`：不设 overlay，全部用 `beken_robot/ap/` 下的文件。
2. 编 `secureboot_ai`：`APP_ROOT` 指向 `../../beken_robot/ap`；若 `secureboot_ai/ap/` 下存在**同名相对路径**，则编 overlay 这份，否则仍编底座。

当前 overlay **只有 1 个文件**：

```text
secureboot_ai/ap/src/common/board_usb_switch.c
```

原因：NS + secure boot 下不能走完整 `usbd_deinitialize()` 二次上电（会 hang），安全侧用 SOFTCONN 软断开；底座为避免 USB PLL 带花 MIPI，少动 PHY。这不是漏同步，**不要合并成一份**。

CP：

- `robot_keepalive.c`、`vnd_cal.c`：编安全工程时引用 `beken_robot/cp/`。
- `secureboot_ai/cp/cp_main.c`：安全工程自己的 NS 入口（AP boot vote + `CP NS world reached`），不要换成底座的 `cp_main.c`。
- `secureboot_ai/cp/vnd_cal.h`：跳转头，指向底座 `vnd_cal.h`。SDK `bk_init` 从**本工程** `cp/` 找这个头文件，所以安全工程必须留这个文件，但不要在这里再写一份校准实现。

---

## 日常怎么改（最重要）

### 改已有功能（UI / Demo / 外设逻辑）

直接改 `beken_robot/ap/` 里对应的 `.c` / `.h` / `.cc`。

**不要**：

- 在 `secureboot_ai/ap/src/` 再放一份同名文件（会变成意外 overlay，两边行为再次分叉）。
- 改完非安全工程后再手工拷到安全工程。
- 为了“安全工程也要有”去复制目录。

头文件同样走底座：`secureboot_ai/ap/include` 不应存在。改 `beken_robot/ap/include/` 即可。

### 新增 / 删除 / 重命名一个编译单元

这时才需要动 `beken_robot/ap/ap_sources.cmake` 里的 `_robot_app_rel_srcs` 列表。

只改已有 `.c` 的内容时，**不必**打开这个 cmake。

新增文件请放在 `beken_robot/ap/` 下，并在清单里加上相对路径（例如 `src/demo/foo.c`）。不要把新文件只放到 `secureboot_ai/`。

### 改 CP 应用代码

改 `beken_robot/cp/`（keepalive、vnd_cal）。  
安全工程的 `cp_main.c` 只有启动投票和 NS 日志，与 Demo 无关。

### 改安全工程“身份”文件（不要当应用代码改）

下列文件是 `secureboot_ai` 自己的，**不要**为了复用去和 `beken_robot` 对齐：

- `partitions/`（含 `security.csv`、`ota.csv`、`auto_partitions.csv`）
- `config/key/`、`config/bk7259/config`（defconfig）
- `ap/config/**/usr_gpio_cfg.h`、`cp/config/**/usr_gpio_cfg.h`
- `cp/cp_main.c`
- 工程自己的 `Makefile` / `Kconfig.projbuild`

提示音、KWS 模型等 `resources/` 仍是各工程一份，改资源文件时两边都要放（它们不走 CMake overlay）。

`ap/lv_conf_custom.h` 目前两边内容相同，但是各工程各一份；改 LVGL 工程级开关时请两边一起改，或先确认是否只影响其中一个 flavor。

---

## 什么时候才允许新增 overlay

只有同时满足下面两条，才把文件放到 `secureboot_ai/ap/` 做 overlay：

1. 安全启动 / NS 世界下**必须**和底座行为不同（例如 USB 二次初始化 hang）。
2. 差异已经用注释写清楚，并且评审过，不能靠“先改非安全、安全那边忘了合”来解释。

做法：在 `secureboot_ai/ap/` 下建立与底座**相同的相对路径**，例如：

```text
beken_robot/ap/src/foo.c          ← 底座
secureboot_ai/ap/src/foo.c        ← overlay，仅安全镜像编译这份
```

并在 `ap_sources.cmake` 的 `_robot_app_rel_srcs` 里已有该相对路径（overlay 机制按相对路径匹配，不靠另写一份 SRCS）。

**禁止**把“注释不同”“漏同步的功能补丁”做成 overlay。那种差异应合回 `beken_robot`，然后删掉安全工程里的副本。

当前白名单：仅 `src/common/board_usb_switch.c`。

---

## 建议自检

改应用后：

1. 先编、先测 `beken_robot`（日常主路径）。
2. 合入前至少编一次 `secureboot_ai`，确认没有找不到源文件 / 重复定义。
3. 若改了 USB 切换、Flash、启动路径，必须在安全板上再验一遍（NS 路径与非安全不同）。

安全镜像开机可通过的关键 log（与功能 Demo 无关，只证明引用底座后启动链仍在）：

```text
[FCE] verify OK
secureboot_ai: CP NS world reached (secure boot OK)
LVGL started, page_1 loaded
AP main running...
```

---

## 常见误区

| 误区 | 正确做法 |
|---|---|
| 安全工程里还有一份 `ap/src/demo/*.c`，改那边才算改安全包 | 那些副本已删除。改 `beken_robot` 即可 |
| 两个工程要保持目录结构完全一样 | 只要共享清单里的应用文件一样；分区/密钥本来就该不同 |
| 新增 Demo 要在两个 `CMakeLists.txt` 各加一行 | 只改 `ap_sources.cmake` 一处 |
| `secureboot_ai/cp/vnd_cal.h` 看起来像空壳，可以删 | 不能删，SDK 从本工程 `cp/` include |
| USB 两边实现不一样，应该合并 | 不要合；这是已知的安全路径差异 |
| 把应用抽到 `projects/common/` 更干净 | 不做。多数人工作目录仍是 `beken_robot` |

---

## 文件入口（给需要看实现的人）

| 文件 | 作用 |
|---|---|
| `beken_robot/ap/ap_sources.cmake` | 共享 AP 源码清单 + overlay 选择 |
| `beken_robot/ap/CMakeLists.txt` | 底座：`APP_ROOT` = 本目录 |
| `secureboot_ai/ap/CMakeLists.txt` | flavor：`APP_ROOT` 指底座，打开 overlay |
| `secureboot_ai/ap/src/common/board_usb_switch.c` | 唯一 AP overlay |
| `secureboot_ai/cp/CMakeLists.txt` | 本地 `cp_main` + 引用底座 keepalive/vnd_cal |
| `secureboot_ai/cp/vnd_cal.h` | 跳转头 |

应用源码仍从 `beken_robot/ap/src/`、`beken_robot/ap/include/` 读起。
