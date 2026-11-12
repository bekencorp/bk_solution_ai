# Muse 语音（Push-to-Talk）功能设计书

- 状态：草稿（未实施）
- 上游：`facebookincubator/muse-gadget-sdk`，pinned commit `812c46f9fe42b69d916613a9dc702cbac5ff7b54`
- 相关文档：[muse_protocol_contract.md](muse_protocol_contract.md)

## 1. 背景与目标

上游 ESP32 固件支持按键说话（Push-to-Talk，下称 PTT）：设备把一段语音发给
Muse 做转写/理解，Muse 的回复一般以**文本**返回；带屏板把回复显示在屏幕上，
音频播报（TTS）由开发者自行选用第三方 API，不是上游固件的内置行为。

本设计为 BK7259 的 muse 集成增加与上游等价的 PTT 能力：

- **范围内（一期）**：按键/命令触发录音 → 音频流上行 → 接收 Muse 文本回复 →
  LVGL 屏显示。包含与现有 Agora RTC 语音通路的互斥管理。
- **范围外（二期可选）**：Muse 回复的 TTS 播报（本地播放器或云端 TTS）、
  免提连续对话、语音唤醒词触发 PTT（可复用现有 KWS）。

## 2. 上游协议契约

### 2.1 已确认（本地 pinned 代码 + 公开资料）

- 传输与命令通道复用现有 Noise 会话：`wss://<noise_host>/v1/noise` 升级，
  服务帧走 `ServiceType::Daemon` 流（现有控制流 `kControlStreamId = 1`，
  见 muse_session.cpp:54）。
- 服务帧支持流式上行：`ApplicationRequest`（verb/path/headers/body）+
  多个 `BodyChunk` + `end_body`，`ClientSession` 已提供
  `StartOutboundApplicationRequest / StartOutboundBodyChunk / StartOutboundReset`
  （noise_core/.../ClientSession.h:90-109）。
- Linux SDK 侧存在 `/link-control` 之外的 `/chat/stream` 端点
  （linux/link_client.py，用于 send-user-msg 上行文本），说明上游协议本身
  支持"设备主动发起"的流式消息。
- PTT 语义：音频消息发给 Muse 转写，回复为文本（上游开发者可自行加 TTS）。

### 2.2 待确认项（实施前必须核对 pinned 上游源码）

GitHub 当前不可达，以下按"策略模块"隔离，确认后只允许改一处：

| 编号 | 待确认项 | 备选假设（按可能性排序） | 核对方式 |
|---|---|---|---|
| P1 | 语音服务端点 path | `/voice`、`/chat/stream`（复用）、`/audio` | pinned 上游 esp32/main 语音相关源文件 |
| P2 | 音频编码/容器 | 16 kHz mono PCM16 裸流；Opus（Ogg/裸帧）；G711A | 同上 + 上游板级配置 |
| P3 | 请求形态 | verb=POST，headers 带 Content-Type/sample-rate；body=音频帧序列 | 同上 |
| P4 | 回复形态 | 同一 stream 的 Response 帧（JSON，含转写+回复文本） | 同上 |
| P5 | 是否有 voice.* 控制命令（云端主动发起/停止） | 无（PTT 纯设备发起） | 上游 noise_control.cpp 的 add_command 列表 |

设计约束：P1–P4 全部收敛到 `muse_voice_proto.c` 一个文件内（见 4.3），
其余代码不感知具体 path/编码/封包。

## 3. 本地可复用资产（已核实）

| 资产 | 位置 | 用途 |
|---|---|---|
| Noise 流式会话 | muse/noise_core ClientSession | 音频流上行/回复下行 |
| 会话单 worker + 事件队列 | muse/muse_session.cpp（`kEventQueueDepth=8`） | 发送串行化模型直接沿用 |
| audio_engine 语音通路 | components/audio_engine/audio_engine.h | mic 采集（AEC/EQ）、speaker 播放、提示音 |
| mic 数据回调 | `audio_engine_cfg_t.read_cb`（audio_engine.h:100） | 录音帧出口 |
| speaker 写入 | `audio_engine_write_data()`（audio_engine.h:143） | 二期 TTS 播报 |
| 编码 | 配置支持 PCM/G711A/G711U/G722（audio_engine.h:84）；AVDK 另含 Opus 编码器（ap/properties/modules/audio_codec/opus） | 按 P2 结论选择 |
| LVGL overlay | muse/muse_display.c（draw_url 同款 overlay 模式） | 回复文本/状态显示 |
| KWS/ASR | audio_engine_asr_* | 二期语音唤醒触发 PTT |
| Agora RTC 语音通路 | page_ai_chat 等（audio_engine 现有用户） | 互斥对象（见 4.6） |

## 4. 设计

### 4.1 总体架构

```
按键/CLI/命令 ─┐
               ▼
        muse_voice.c 状态机 ──► audio_engine(16k mono, AEC) ──read_cb──► 帧队列(PSRAM)
               │                                                          │
               │ (2) 会话事件          (3) 音频块                          ▼
               ▼                                                   muse_session worker
        muse_session.cpp  ◄── 事件队列(queue_event 模式) ── (逐帧取出,组 BodyChunk)
               │
               ▼
        Noise/WS (Daemon service, kVoiceStreamId)
               │
               ▼  Response(JSON: 转写+回复)
        LVGL 文本页（复用 overlay 机制）
```

关键原则：

- **ClientSession 只被 session worker 触碰**（沿用现有"caller 串行化"约束，
  ClientSession.h:56）。音频回调任务永不直接调 ClientSession，只投递到队列。
- 语音流使用独立 stream id（建议 `kVoiceStreamId = 2`），与控制流
  `kControlStreamId = 1` 并存；session 的入站分发已按 stream_id 判断
  （muse_session.cpp:780），扩展一个分支即可。
- 不新增长驻任务：录音回调在 audio_engine 现有读任务上下文跑；
  编码（如需 Opus）也在该上下文做，避免再加线程栈。

### 4.2 状态机（muse_voice.c）

```
IDLE ──触发──► STARTING ──引擎就绪──► RECORDING
                  │失败                  │停止(松手/超时/云端结束)
                  ▼                      ▼
                ERROR ◄──────────── CLOSING(发 end_body)
                                         │
                                         ▼
                                    WAIT_REPLY ──Response──► SHOWING(显示文本)
                                         │超时/Reset          │
                                         ▼                    ▼
                                       ERROR ────► IDLE ◄────┘
```

- 录音上限：`MUSE_VOICE_MAX_RECORD_MS`（默认 30 s），到点自动 CLOSING。
- WAIT_REPLY 超时：`MUSE_VOICE_REPLY_TIMEOUT_MS`（默认 15 s）。
- ERROR 状态必须做完整资源回收（停引擎、发 Reset、清空帧队列）。

### 4.3 协议策略模块 muse_voice_proto.c（唯一感知 P1–P4 的文件）

```c
/* 组流上行首帧：verb/path/headers（P1/P3） */
int muse_voice_proto_make_request(char *buf, size_t cap,
                                  muse_voice_format_t fmt);
/* 音频帧 -> 线上格式（P2：直通PCM / G711 / Opus 封装） */
int muse_voice_proto_encode_frame(const int16_t *pcm, size_t samples,
                                  uint8_t *out, size_t cap);
/* 解析 Response JSON：转写 + 回复文本（P4） */
int muse_voice_proto_parse_reply(const char *json, char *asr_text,
                                 size_t asr_cap, char *reply, size_t reply_cap);
```

一期默认实现按"16 kHz mono PCM16 裸流 + JSON 回复"落地（P2 最可能项），
Opus 编码器作为编译期备选（AVDK 已带 libopus，见第 3 节）。

### 4.4 会话集成（muse_session.cpp 改动点）

1. 新增 `kVoiceStreamId = 2`。
2. 新增 session 内部事件 `EventKind::VoiceChunk`（或复用 Wake + 独立音频队列），
   由 muse_voice.c 投递；worker 在事件循环里：
   - 首帧前：`StartOutboundApplicationRequest(Daemon, kVoiceStreamId, ...)`；
   - 每帧：`StartOutboundBodyChunk(...)`，末帧 `end_body=true`；
   - 中止：`StartOutboundReset(...)`。
3. 入站分发：`decoded.stream_id == kVoiceStreamId` 的 Response/Reset 帧
   转交 `muse_voice_on_rx()`（在 worker 上下文，muse_voice 不加锁）。
4. WS 发送缓冲约束：现有出站路径按
   `ClientSession::kMaxOutboundWebSocketPayloadSize` 配缓冲（muse_session.cpp:59），
   因此**每个 BodyChunk 上限固定为该值减帧头**，音频切块按 1 KB 对齐切分
   （16 kHz PCM ≈ 32 ms，Opus 则多帧合并到 1 KB）。

### 4.5 音频通路配置

| 项 | 取值 | 说明 |
|---|---|---|
| mic_sample_rate | 16000 | 语音转写标准；资源里提示音也是 16k |
| 帧长 | 20 ms（640 B PCM） | read_cb 每次回调粒度 |
| AEC | 开 | 与屏显/二期播报共存需要 |
| enc_type | PCM（一期默认）/ Opus（P2 确认后） | audio_engine 配置 |
| 帧队列 | PSRAM，深 50 帧 ≈ 32 KB（1 s 抖动吸收） | 满则丢最旧帧并计丢帧数 |
| speaker | 一期不用 | 二期 TTS 用 audio_engine_play_* / write_data |

### 4.6 与现有音频用户的互斥

audio_engine 是单实例（`audio_engine_is_running()`），现有用户：
Agora RTC（page_ai_chat / page_vision）、KWS 唤醒、A2DP 音乐。

规则：

1. PTT 触发时若 `audio_engine_is_running()`：
   - 属 RTC 会话中 → 拒绝启动，UI 提示"通话中"；
   - 属 KWS 待机 → 先停 KWS（`audio_engine_asr_stop()`），PTT 结束后恢复。
2. PTT 期间屏蔽 RTC/KWS 的启动入口（置 `muse_voice_active` 标志，
   demo 层查询）。
3. 会话断开（Noise/WS 掉线）→ 立即 CLOSING→ERROR，释放引擎。

### 4.7 命令注册与触发

- **触发源（一期）**：物理按键（复用 pairing 确认的按键路由，PTT 按住录音、
  松开发送）+ CLI `muse voice`（调试）。
- **link.register**：若 P5 确认上游无 voice.* 命令，注册表不变；
  若有（如云端可主动 `voice.end`），按现有格式在
  muse_commands.c 的 `commands_v2` 里追加，invoke 分发新增一个分支。
  （顺手建议：注册 JSON 与分发改成命令表驱动，见"差距对齐"结论，非必须。）

### 4.8 UI

- 复用 muse_display.c 的 overlay 机制，新增文本页：状态行
  （录音中/发送中/思考中）+ 转写文本 + 回复文本；收到回复后停留
  `MUSE_VOICE_REPLY_SHOW_MS`（默认 10 s）自动恢复原 UI。
- 显示锁沿用 `lv_vendor_disp_lock()`；overlay 与 draw_url 的图片互斥
  （语音页出现时清掉图片 overlay，反之亦然）。

### 4.9 内存预算（增量，相对 draw_url 峰值 ~790 KB PSRAM）

| 项 | 大小 |
|---|---|
| 帧队列（PSRAM） | 32 KB |
| 编解码工作区（Opus 时，PSRAM） | ~50 KB（libopus 16k mono 实测约 30–45 KB） |
| 回复/状态文本缓冲（.psram.bss） | 2 KB |
| 任务栈 | 0（不新增任务） |
| **合计** | **≤ 84 KB** |

draw_url 的 512 KB body 缓冲是用完即释放的，语音期间不并发下载大图，
峰值可控；若 Opus + 大图并发成为需求，再按 PSRAM heap（~1.4 MB）评估。

### 4.10 错误处理

| 场景 | 行为 |
|---|---|
| 引擎启动失败 | ERROR → 提示，不发任何流帧 |
| WS/Noise 掉线（录音中） | 停引擎、清队列、回 IDLE（不回放本地录音） |
| 对端 Reset 语音流 | 停录音，提示原因（ResetView.reason） |
| 回复超时 | ERROR 提示"无回复"，恢复 IDLE |
| 队列溢出 | 丢最旧帧 + 统计，不阻塞音频回调 |
| session 重连期间 | 禁止触发 PTT（状态机入口检查 `muse_session_is_connected()`） |

### 4.11 配置项（Kconfig.projbuild 追加）

```
CONFIG_MUSE_GADGET_VOICE            总开关，默认 n（随 CONFIG_MUSE_GADGET）
CONFIG_MUSE_VOICE_OPUS              P2 确认后启用 Opus 上行
CONFIG_MUSE_VOICE_MAX_RECORD_MS     默认 30000
CONFIG_MUSE_VOICE_REPLY_TIMEOUT_MS  默认 15000
```

## 5. 实施计划

| 阶段 | 内容 | 验收 |
|---|---|---|
| P0 | 核对 pinned 上游源码，确认 P1–P5；更新本文档第 2.2 节 | 契约表格填实 |
| P1 | muse_voice_proto（PCM 假设）+ 状态机 + 会话集成 + 按键触发 + 文本页 | 真机与 Muse App 完成一次"按键说话→屏显回复" |
| P2 | （可选）回复 TTS 播报（audio_engine_play 或云 TTS）；Opus 上行 | 播报清晰，带宽下降 |
| P3 | KWS 唤醒触发 PTT；命令表重构；协议契约文档更新 | 免提闭环 |

## 6. 验证计划

1. **单元（宿主机）**：muse_voice_proto 的请求组帧/回复解析用例；
   帧队列溢出策略用例。
2. **CLI 联调**：`muse voice start/stop` 触发，串口日志核对 stream 帧序列
   （Request → N×BodyChunk → end_body → Response）。
3. **真机互操作**：Muse App 侧确认转写正确、回复落屏；断网/弱网/超时注入。
4. **回归**：draw_url 图片显示、Agora RTC 通话、KWS 唤醒三条通路互斥正确。

## 7. 风险

| 风险 | 等级 | 缓解 |
|---|---|---|
| P1–P4 与上游不符导致互操作失败 | 高 | P0 阶段先核对 pinned 源码；协议细节隔离在 muse_voice_proto.c |
| 与 Agora RTC 抢音频引擎 | 中 | 单实例互斥规则（4.6），UI 明确提示 |
| PSRAM 峰值（语音 + 大图下载并发） | 中 | 语音期间禁并发 draw_url；预算留 ≥84 KB |
| 长录音上行占用控制流时延 | 低 | 语音流独立 stream id；控制流帧优先发送 |
