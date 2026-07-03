# Edge AI Demo

本目录存放 BK7259 beken_robot 方案中的边缘 AI 相关 demo，目前包含：

- `palm_tracking.cc` —— palm_detection NN pipeline + 全屏 camera preview 的手掌跟踪 demo。
  对外接口头文件为 `ap/include/demo/palm_tracking.h`。
- `yoloface_tracking.cc` —— yoloface NN pipeline + 全屏 camera preview 的人脸检测 demo（仅 OSD，无舵机）。
  对外接口头文件为 `ap/include/demo/yoloface_tracking.h`。
- `hand_gesture.cc` —— hand_gesture NN pipeline + Hiwonder 6-DOF 手掌舵机控制。
  对外接口头文件为 `ap/include/demo/hand_gesture.h`。
  舵机驱动位于 `components/bk_servo/bk_hiwonder_hand_servo.c`。

## 管脚复用情况

下表整理当前硬件接插件管脚（PIN）与 BK7259 GPIO（P）的复用映射。

| PIN  | 功能       | GPIO | 说明                     | Edge AI demo 使用情况 |
| ---- | ---------- | ---- | ------------------------ | --------------------- |
| PIN01 | 4V2       | -    | 电源 4.2V               |                       |
| PIN02 | 3V3       | -    | 电源 3.3V               |                       |
| PIN03 | GND       | -    | 地                       |                       |
| PIN04 | QSPI0_CLK | P22  | QSPI0 时钟              | `palm_tracking` V 轴舵机 / `hand_gesture` 舵机 1 |
| PIN05 | QSPI0_CS  | P23  | QSPI0 片选              | `palm_tracking` H 轴舵机 / `hand_gesture` 舵机 2 |
| PIN06 | QSPI0_D0  | P24  | QSPI0 数据 0            | `hand_gesture` 舵机 3 |
| PIN07 | QSPI0_D1  | P25  | QSPI0 数据 1            | `hand_gesture` 舵机 4 |
| PIN08 | QSPI0_D2  | P26  | QSPI0 数据 2            | `hand_gesture` 舵机 5 |
| PIN09 | QSPI0_D3  | P27  | QSPI0 数据 3            | `hand_gesture` 舵机 6 |
| PIN10 | TP_RST    | P29  | 触摸屏复位              |                       |
| PIN11 | TP_INT    | P30  | 触摸屏中断              |                       |
| PIN12 | I2C0_SCL  | P70  | I2C0 时钟               | MIPI camera SCL，触摸屏专用，禁止用于舵机 |
| PIN13 | I2C0_SDA  | P71  | I2C0 数据               | MIPI camera SDA，触摸屏专用，禁止用于舵机 |
| PIN14 | QSPI1_CLK | P02  | QSPI1 时钟              |                       |
| PIN15 | QSPI1_CS  | P03  | QSPI1 片选              |                       |
| PIN16 | QSPI1_D0  | P04  | QSPI1 数据 0            |                       |
| PIN17 | LCD_RST   | P05  | LCD 复位                |                       |
| PIN18 | LCD_TE    | P06  | LCD Tearing Effect 同步 |                       |
| PIN19 | LCD_BL    | P07  | LCD 背光                |                       |
| PIN20 | GND       | -    | 地                       |                       |
| PIN21 | GND       | -    | 地                       |                       |

### 按功能分组

- **电源 / 地**：PIN01(4V2)、PIN02(3V3)、PIN03/PIN20/PIN21(GND)
- **QSPI0（6 线，触摸屏 / 显示数据总线）**：P22~P27（CLK / CS / D0~D3）。当前 Edge AI 舵机 demo 会把 P22~P27 复用为 PWM GPIO，运行时不要再接 QSPI0 外设。
- **QSPI1（3 线）**：P02(CLK)、P03(CS)、P04(D0)
- **触摸屏（TP）**：P29(RST)、P30(INT)
- **I2C0**：P70(SCL)、P71(SDA) —— **已分配给触摸屏使用，不可复用于舵机（servo）**
- **LCD 控制**：P05(RST)、P06(TE)、P07(BL)

> 注：GPIO 复用配置以实际 SDK pinmux / board 配置为准，本表仅用于记录当前硬件接线对应关系。

> ⚠️ **复用约束**：I2C0（P70/P71）已被触摸屏占用，**不可分配给舵机（servo）或其他外设**。

### `palm_tracking.cc` GPIO 使用

`palm_tracking` 使用 MIPI camera + OSD 显示，并用 2 路 PWM 舵机做手掌跟踪云台：

| 功能 | GPIO | PWM 通道 | 代码配置 |
| ---- | ---- | -------- | -------- |
| 云台 H 轴舵机（pan） | P23 | PWM0 | `PALM_SERVO_GPIO_ID_H` / `PALM_SERVO_PWM_CHAN_H` |
| 云台 V 轴舵机（tilt） | P22 | PWM1 | `PALM_SERVO_GPIO_ID_V` / `PALM_SERVO_PWM_CHAN_V` |
| MIPI camera SCL | P70 | - | `camera_board.mipi.pin_scl` |
| MIPI camera SDA | P71 | - | `camera_board.mipi.pin_sda` |
| MIPI camera RESET | P31 | - | `camera_board.mipi.pin_reset` |
| MIPI camera XCLK | P59 | - | `camera_board.mipi.pin_xclk` |

> P22/P23 在接插件表中同时标为 QSPI0_CLK/QSPI0_CS；运行 `palm_tracking` 时它们被作为舵机 PWM GPIO 使用，不应再连接 QSPI0 外设。

### `hand_gesture.cc` GPIO 使用

| 舵机 ID | GPIO | PWM 通道 |
| ------- | ---- | -------- |
| 1       | P22  | PWM8     |
| 2       | P23  | PWM6     |
| 3       | P24  | PWM7     |
| 4       | P25  | PWM5     |
| 5       | P26  | PWM4     |
| 6       | P27  | PWM3     |

`hand_gesture` 与 `palm_tracking` 使用相同的 MIPI camera 管脚：P70(SCL)、P71(SDA)、P31(RESET)、P59(XCLK)。

> P22~P27 在接插件表中同时标为 QSPI0 6 线；运行 `hand_gesture` 时整组 P22~P27 被 6 路 Hiwonder 手掌舵机占用，不应再连接 QSPI0 外设。

SD 卡模型路径：`1:/tflite/hand_gesture_detection_vela.tflite`（与 `components/avdk_nn_module/src/tflm_hand_gesture_detection/` 中同名文件一致）。
