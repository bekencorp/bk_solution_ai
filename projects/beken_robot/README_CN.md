# Beken Genie AI 解决方案DEMO开发指南

* [English](./README.md)

## 1 项目概述

本项目是一个基于BK7258芯片的通用AI设备解决方案框架，提供了完整的端到云、云到大模型的AI交互能力。项目支持声网RTC方案，集成了音频处理引擎、网络传输模块、事件管理系统和丰富的外设支持，适用于智能AI设备、语音助手、智能音箱等应用场景的开发。

## 2 功能特性

### 2.1 实时音视频通信
- 支持双向音视频通信
- 支持多路音视频流
- 支持自适应码率控制（BWE）
- 支持关键帧请求机制

### 2.2 音频处理
- 支持多种音频编码格式：
  - OPUS（推荐）
  - PCM
- 支持AEC（回声消除）
- 支持NS（噪声抑制）
- 支持KWS（关键词唤醒）
- 支持音频采集和播放
- 支持提示音播放

### 2.3 视频处理（可选）
- 支持H264编码
- 支持JPEG编码
- 支持视频采集和传输
- 支持图像识别

### 2.4 网络功能
- 支持WiFi STA模式连接
- 支持WiFi AP模式热点
- 支持蓝牙配网
- 支持TCP/UDP协议
- 支持HTTP/HTTPS请求

### 2.5 AI Agent集成
- 支持与多种AI Agent服务集成
- 支持语音对话和图像识别
- 支持Agent启动、停止和更新
- 支持从BK服务器或自定义服务器启动Agent
- 支持多种大语言模型（OpenAI、豆包、DeepSeek等）

### 2.6 房间管理
- 支持加入/离开RTC房间
- 支持用户上线/下线通知
- 支持Token权限管理
- 支持Token过期警告和自动刷新

### 2.7 外设支持
- **显示**: 支持双SPI LCD屏幕（GC9D01 160x160）
- **输入**: 麦克风、按键、陀螺仪、NFC
- **输出**: 扬声器、LED灯效、震动马达
- **存储**: SD NAND 128MB
- **电源**: 锂电池、充电管理（ETA3422）
- **摄像头**: DVP摄像头（gc2145）

## 3 快速开始

### 3.1 编译和烧录

编译流程参考 `AI 解决方案 <../../README_CN.md>`_

烧录流程参考 具体 `烧录流程 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/get-started/index.html>`_ 请参考 `SMP <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>`_

编译生成的烧录bin文件路径：``projects/beken_genie/build/bk7258/beken_genie/package/all-app.bin``


**编译命令示例：**

```bash
cd ~/armino/bk_solution_ai/projects/beken_genie
export SDK_DIR=~/armino/bk_avdk_smp
make clean
make bk7258
```

## 4 API参考

本章节提供项目中核心功能的API接口说明。

### 4.1 声网RTC API

如果启用声网RTC（`CONFIG_AGORA_RTC_EN=y`），可以使用以下API：

#### 4.1.1 bk_agora_start
```c
/**
 * @brief 启动完整的声网RTC和Agent服务
 * 
 * @param device_id 设备ID字符串
 * 
 * @return int 操作结果
 *         - BK_OK: 启动成功
 *         - BK_FAIL: 启动失败
 * 
 * @see bk_agora_stop()
 */
int bk_agora_start(void *device_id);
```

#### 6.1.2 bk_agora_stop
```c
/**
 * @brief 停止完整的声网RTC和Agent服务
 * 
 * @param device_id 设备ID字符串
 * 
 * @return int 操作结果
 *         - BK_OK: 停止成功
 *         - BK_FAIL: 停止失败
 * 
 * @see bk_agora_start()
 */
int bk_agora_stop(void *device_id);
```

### 4.2 通用API

#### 4.2.1 音频引擎API
```c
/**
 * @brief 初始化音频引擎
 * 
 * @return bk_err_t 操作结果
 */
bk_err_t audio_engine_init(void);
```

#### 4.2.2 网络传输API
```c
/**
 * @brief 初始化网络传输模块
 * 
 * @return bk_err_t 操作结果
 */
bk_err_t ntwk_trans_init(void);
```

#### 4.2.3 应用事件API
```c
/**
 * @brief 初始化应用事件系统
 * 
 * @return bk_err_t 操作结果
 */
bk_err_t app_event_init(void);
```


## 5 关于工程详细介绍以及指南请跳转如下链接

- `Armino SMP SDK 文档 <https://docs.bekencorp.com/arminodoc/bk_avdk_smp/smp_doc/bk7258/zh_CN/v3.1.1/index.html>`_
- `声网 RTC 文档 <https://docs.agora.io/>`_
- `声网DEMO工程具体详情请参考文档链接：<https://docs.bekencorp.com/arminodoc/bk_ai_smp/bk7258/zh_CN/v3.1.1/projects/beken_genie/index.html>`_
