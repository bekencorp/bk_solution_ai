# Beken Robot：TFLite 模型文件使用说明

本文说明如何将 `beken_robot` 端侧 AI Demo 使用的 **TFLite / Vela 模型** 部署到设备存储，供 `components/avdk_nn_module` 在运行时加载。

> 适用工程：`ai_solution/projects/beken_robot`
> 适用组件：`ai_solution/components/avdk_nn_module`
> 存储介质：板载 SD-NAND（FatFS 盘符一般为 `1:`，VFS 挂载点为 `/sd0`）
> 说明范围：本文只覆盖端侧视觉 AI 模型；KWS 模型与提示音请参考 `kws_model_and_prompt_tone_user_mannual.md`

---

## 1. 目录内容说明

交付给客户后的推荐结构如下（请保持文件名与层级不变）：

```text
resources/
├── tflite/
│   ├── palm_detection_builtin_256_integer_quant_vela.tflite
│   ├── hand_gesture_detection_vela.tflite
│   ├── yoloface_int8_vela.tflite
│   ├── face_detection_int8_vela.tflite
│   └── face_verify_int8_vela.tflite
├── kws_model/
│   ├── bk_kws_wakeup.tflite
│   └── bk_kws_commands.tflite
├── ...（提示音文件）
├── kws_model_and_prompt_tone_user_mannual.md
└── tflite_model_user_mannual.md   # this guide
```

端侧视觉 AI 模型必须放在设备存储根目录下的 `tflite/` 子目录中：

| 模型文件                                                 | 设备上目标路径（FatFS）                                            | 设备上等价路径（VFS）                                                | 典型业务                      |
| -------------------------------------------------------- | ------------------------------------------------------------------ | -------------------------------------------------------------------- | ----------------------------- |
| `palm_detection_builtin_256_integer_quant_vela.tflite` | `1:/tflite/palm_detection_builtin_256_integer_quant_vela.tflite` | `/sd0/tflite/palm_detection_builtin_256_integer_quant_vela.tflite` | 掌心检测 / 云台跟踪           |
| `hand_gesture_detection_vela.tflite`                   | `1:/tflite/hand_gesture_detection_vela.tflite`                   | `/sd0/tflite/hand_gesture_detection_vela.tflite`                   | 手势识别 / 机械手 / 小车跟踪  |
| `yoloface_int8_vela.tflite`                            | `1:/tflite/yoloface_int8_vela.tflite`                            | `/sd0/tflite/yoloface_int8_vela.tflite`                            | 人脸检测 / 人脸跟踪           |
| `face_detection_int8_vela.tflite`                      | `1:/tflite/face_detection_int8_vela.tflite`                      | `/sd0/tflite/face_detection_int8_vela.tflite`                      | 人脸识别：检测模型            |
| `face_verify_int8_vela.tflite`                         | `1:/tflite/face_verify_int8_vela.tflite`                         | `/sd0/tflite/face_verify_int8_vela.tflite`                         | 人脸识别：特征提取 / 比对模型 |

---

## 2. 固件配置要求（出厂/编译侧）

`beken_robot` AP 默认配置已启用从 SD-NAND / FatFS 加载端侧 AI 模型。客户使用预编译固件时通常无需修改。

| 配置项                                    | 期望值 | 作用                                             |
| ----------------------------------------- | ------ | ------------------------------------------------ |
| `CONFIG_TFLITE_MICRO`                   | `y`  | 启用 TensorFlow Lite Micro                       |
| `CONFIG_TFLM_PALM_DETECTION_V1`         | `y`  | 编译掌心检测模型封装                             |
| `CONFIG_TFLM_YOLOFACE_V1`               | `y`  | 编译 YOLOFace 检测模型封装                       |
| `CONFIG_TFLM_HAND_GESTURE_DETECTION_V1` | `y`  | 编译手势检测模型封装                             |
| `CONFIG_TFLM_FACE_RECOG_V1`             | `y`  | 编译人脸识别模型封装                             |
| `CONFIG_SDCARD`                         | `y`  | `avdk_nn_module` 从 FatFS 文件加载 `.tflite` |
| `CONFIG_FATFS`                          | `y`  | 启用 FatFS 文件系统                              |
| `CONFIG_FATFS_SDCARD`                   | `y`  | 让 SD-NAND / SDIO 存储以 FatFS 方式访问          |
| `CONFIG_BOARD_SD_NAND_ENABLE`           | `y`  | 启用板载 SD-NAND                                 |
| `CONFIG_BOARD_USB_SWITCH_ENABLE`        | `y`  | Type-C 可切换到 USB MSC，方便 PC 拷贝文件        |

当前 `avdk_nn_module` 的编译逻辑是：

- `CONFIG_SDCARD=y` 时，不把模型数组编进固件，运行时通过 `f_open()` 从 `1:/tflite/` 读取模型文件；
- `CONFIG_SDCARD=n` 时，才会编译组件内的 `*_model_data.cc` 模型数组，外部 `tflite/` 目录不会被使用；
- `beken_robot` 默认是 `CONFIG_SDCARD=y`，所以必须在 SD-NAND 根目录部署本文列出的模型文件。

---

## 3. 部署到设备（推荐：U 盘模式）

目标：把本 `resources` 目录中的 `tflite` 文件夹拷贝到 SD-NAND **根目录**，使设备上呈现：

```text
/sd0/
├── tflite/
│   ├── palm_detection_builtin_256_integer_quant_vela.tflite
│   ├── hand_gesture_detection_vela.tflite
│   ├── yoloface_int8_vela.tflite
│   ├── face_detection_int8_vela.tflite
│   └── face_verify_int8_vela.tflite
├── kws_model/
│   ├── bk_kws_wakeup.tflite
│   └── bk_kws_commands.tflite
└── ...（提示音文件）
```

### 3.1 操作步骤

1. 设备上电，进入 Robot UI 的 **U 盘 / U-disk** 功能（将 Type-C 切换为 BK7259 USB，枚举板载 SD-NAND 为可移动磁盘）。
2. 在 PC 上打开该可移动磁盘。
3. 将 `resources/tflite` 整个文件夹复制到磁盘**根目录**。
4. 如果同时部署 KWS 模型与提示音，请按 `kws_model_and_prompt_tone_user_mannual.md` 将 `kws_model/` 和 `*.mp3` 也复制到根目录。
5. **安全弹出** USB 磁盘后，退出 U 盘模式（切回 UART/正常运行），必要时重启设备。
6. 进入端侧 AI 页面后，对应 Demo 会从 `1:/tflite/` 读取模型。

### 3.2 正确示例 vs 错误示例

**正确（PC 磁盘根目录）：**

```text
E:\
├── tflite\palm_detection_builtin_256_integer_quant_vela.tflite
├── tflite\hand_gesture_detection_vela.tflite
├── tflite\yoloface_int8_vela.tflite
├── tflite\face_detection_int8_vela.tflite
└── tflite\face_verify_int8_vela.tflite
```

**错误（多套了一层目录，固件找不到）：**

```text
E:\resources\tflite\...
```

也不要把端侧视觉 AI 模型放到 `kws_model/` 下。`kws_model/` 只用于语音唤醒 / 命令词模型。

---

## 4. 运行时加载路径

当前工程代码对模型路径有明确要求，文件名和目录必须完全一致。

| 业务入口                              | 模型类 / Runtime              | 代码加载路径                                                       | 备注                                                      |
| ------------------------------------- | ----------------------------- | ------------------------------------------------------------------ | --------------------------------------------------------- |
| `palm_tracking`                     | `PalmDetectionModel`        | `1:/tflite/palm_detection_builtin_256_integer_quant_vela.tflite` | 输入尺寸固定为 256x256 RGB                                |
| `hand_gesture`                      | `HandGestureDetectionModel` | `1:/tflite/hand_gesture_detection_vela.tflite`                   | 输入尺寸固定为 320x320 RGB                                |
| `car_tracking`                      | `HandGestureDetectionModel` | `1:/tflite/hand_gesture_detection_vela.tflite`                   | 与`hand_gesture` 共用同一个手势模型                     |
| `yoloface_tracking`（人脸跟踪模式） | `YolofaceDetectionModel`    | `1:/tflite/yoloface_int8_vela.tflite`                            | 输入尺寸固定为 56x56 RGB                                  |
| `yoloface_tracking`（人脸识别模式） | `FaceDetectionRuntime`      | `1:/tflite/face_detection_int8_vela.tflite`                      | 人脸检测，输入尺寸固定为 320x320 RGB                      |
| `yoloface_tracking`（人脸识别模式） | `FaceVerifyRuntime`         | `1:/tflite/face_verify_int8_vela.tflite`                         | 人脸特征提取，输入尺寸固定为 112x112 RGB，输出 512 维特征 |

加载流程由 `components/avdk_nn_module` 统一处理：

1. Demo 创建对应模型对象，并调用 `setModelFilePath()` 设置上表路径。
2. `AvdkDetectionModel::LoadModel()` 或 `AvdkMultiModel::LoadModel()` 使用 FatFS `f_open()` 打开 `.tflite`。
3. 文件内容被读入 PSRAM slab / uncoded slab。
4. TFLM 校验 schema version，并创建 `MicroInterpreter`。
5. NPU 模型初始化 Ethos-U runtime 后开始推理。

---

## 5. 模型替换要求

客户如需替换模型，请同时满足以下要求：

1. **文件名和路径固定**：除非同步修改工程代码中的 `*_MODEL_SD_PATH` 宏，否则必须使用本文列出的文件名。
2. **模型必须为当前固件配套的 Vela / Ethos-U 版本**：当前模型类通过 `AddEthosU()` 加载 NPU delegate 相关算子，不能直接替换为普通 PC 侧 `.tflite`。
3. **输入输出契约必须保持一致**：输入尺寸、量化类型、输出 tensor 数量、输出 shape、类别顺序和后处理语义都由代码写死或强依赖。
4. **模型大小必须能放入当前运行内存**：`CONFIG_SDCARD=y` 时模型会整体读入内存，再创建 TFLM arena；过大的模型会导致 `Failed to allocate SD model data` 或 arena 分配失败。
5. **建议整体替换配套模型**：人脸识别由 detection 与 verify 两个模型组合工作，替换时需要同时确认检测输出 keypoint、对齐流程和 512 维特征比对逻辑兼容。

不建议只替换文件而不验证模型结构。若结构变化，需要同步修改 `components/avdk_nn_module/src/tflm_*` 下对应的前处理、后处理和 tensor 校验代码。

---

## 6. 快速自检清单

部署完成后建议按下列项检查：

- [ ] PC 磁盘根目录可见 `tflite` 文件夹，而不是 `resources/tflite`
- [ ] `tflite` 文件夹内包含本文列出的 5 个 `.tflite` 文件
- [ ] 文件名大小写、下划线、后缀完全一致
- [ ] 退出 U 盘模式 / 安全弹出后已重启或重新进入对应 Demo
- [ ] 进入掌心、手势、人脸跟踪、人脸识别页面时串口没有出现 `f_open`、`Failed to load model` 或 `Model schema version mismatch`
- [ ] 如果 KWS 或提示音也需要使用，已按另一份手册部署 `kws_model/` 与根目录 `*.mp3`

若端侧 AI Demo 启动失败：优先检查 `1:/tflite/<model>.tflite` 是否存在。
若只有某一个 Demo 失败：优先检查该 Demo 对应的单个模型文件是否缺失、文件名是否拼错、模型是否与固件版本匹配。

---

## 7. 常见问题

**Q1：为什么代码路径写的是 `1:/tflite/...`，文档里又写 `/sd0/tflite/...`？**
这是同一块 SD-NAND 的两种访问方式。`avdk_nn_module` 当前用 FatFS `f_open()`，所以代码里的真实加载路径是 `1:/tflite/...`；`/sd0` 是 VFS 挂载路径，便于理解设备侧目录结构。

**Q2：能否把模型放在 SD-NAND 根目录，不建 `tflite` 文件夹？**
不能。当前代码查找的是 `1:/tflite/<文件名>.tflite`，必须有 `tflite/` 子目录。

**Q3：能否只拷贝正在使用的模型？**
可以。如果只使用掌心跟踪，只需要对应的 palm 模型；但交付整机功能时建议一次性放齐 5 个模型，避免客户切换到其他端侧 AI 页面时加载失败。

**Q4：能否重命名模型文件？**
默认不能。文件名已写在 `projects/beken_robot/ap/src/demo/edge_ai/*.cc` 和 `components/avdk_nn_module/src/tflm_*` 的默认路径宏中；如需改名，必须同步改代码并重新编译固件。

**Q5：关闭 `CONFIG_SDCARD` 后还需要拷贝模型吗？**
不需要。关闭 `CONFIG_SDCARD` 后组件会尝试使用编译进固件的模型数组。但当前 `beken_robot` 默认开启 `CONFIG_SDCARD`，出厂/预编译固件按本文方式部署外置模型。

**Q6：加载失败时串口可能看到什么日志？**
常见日志包括 `f_open 1:/tflite/... failed`、`read SD model failed`、`Failed to allocate SD model data`、`Failed to load model`、`Model schema version mismatch`、`FaceDetectionRuntime model init failed` 或 `FaceVerifyRuntime model init failed`。请按路径、文件完整性、模型版本和可用内存顺序排查。
