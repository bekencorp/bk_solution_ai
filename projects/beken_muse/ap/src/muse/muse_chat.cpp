/**
 * @file muse_chat.cpp
 * @brief Dedicated chat-only Noise connection for voice turns, mirroring the
 *        pinned upstream esp32 muse_chat_session.cpp: TLS + WebSocket +
 *        Noise XX, then POST /chat/subscribe (long-lived) and one
 *        POST /chat/stream per voice note. Unlike the control session there
 *        is NO /link-control stream and NO link.register here — the server
 *        scopes the conversation to this connection, so assistant replies
 *        fan out to this connection's subscription instead of the account's
 *        main chat.
 *
 *        Transport/event handling is transcribed from muse_session.cpp
 *        (same BK WebSocket adapter caveats apply: frames are reassembled
 *        from DATA events by payload_len; CONT is a transport fault).
 */

#include "muse_chat.h"

#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <components/log.h>
#include <components/bk_platform.h>
#include <os/os.h>
#include <os/mem.h>

extern "C" {
#include "bk_websocket_client.h"
#include "muse_service.h"   /* muse_service_request_token_refresh() */
}
#include "muse_voice.h"
#include "muse_voice_proto.h"

#include <xplat/noise/core/ClientSession.h>
#include <xplat/noise/core/PsaCryptoBackend.h>

using musegadgets::noise::core::ApplicationRequestView;
using musegadgets::noise::core::BodyChunkView;
using musegadgets::noise::core::ByteSpan;
using musegadgets::noise::core::ClientSession;
using musegadgets::noise::core::ConstByteSpan;
using musegadgets::noise::core::HeaderView;
using musegadgets::noise::core::InboundFrameStatus;
using musegadgets::noise::core::PsaCryptoBackend;
using musegadgets::noise::core::ServiceFrameKind;
using musegadgets::noise::core::ServiceType;
using musegadgets::noise::core::ResetCode;
using musegadgets::noise::core::ResetView;
using musegadgets::noise::core::Span;
using musegadgets::noise::core::StringView;

static char TAG[] = "muse_chat";

namespace {

constexpr const char *kDefaultHost = "hatch.metaaivm.com";
constexpr size_t kNodeIdSize = 96;
constexpr size_t kHostSize = 128;
constexpr size_t kTokenSize = 2048;
/* Buffer sizing: the Noise ordered framer's theoretical max frame
 * (kMaxOutboundWebSocketPayloadSize = 65535) is far above anything this
 * deployment actually sends or receives, yet it was allocated FOUR times
 * (ws, transport, plus bk_websocket's internal rx/tx) -- ~256 KB of PSRAM
 * that starved the chat reconnect path on a fragmented heap (2026-11-10
 * field: "alloc tx_buffer fail", psram min_free 18 KB). Real traffic is
 * bounded much lower: outbound voice batches are capped by
 * kOutboundScratchSize (10240), inbound NDJSON lines top out at 16 KB per
 * the protocol notes, so 20 KB covers both with 25% margin. Safety nets
 * are unchanged: oversized inbound frames are rejected by the payload_len
 * check in the WS event handler (drop + reconnect, no corruption), and
 * the Noise layer answers ResourceExhausted without consuming framer
 * state if an outbound frame ever exceeds the buffer. */
constexpr size_t kWsPayloadSize = 20480;
/* Same sizing rationale as the control session: upstream expects NDJSON
 * lines up to 16 KB; 32 KB covers a line plus frame overhead. */
constexpr size_t kServiceScratchSize = 32 * 1024;
/* Sized for the batched voice frame: 4 chunks x 2056 B = 8224 B payload plus
 * Noise/proto (~50 B) and WS envelope overhead. */
constexpr size_t kOutboundScratchSize = 10240;
constexpr uint32_t kEventWaitMs = 250;

constexpr uint32_t kRxIdleReportMs = 30000;
/* Same voice pump cadence as the control session had: the mic produces a
 * 2 KB chunk every 48 ms while one send costs ~77 ms on this uplink. */
constexpr uint32_t kVoiceTxWaitMs = 15;
constexpr uint32_t kVoiceInboundWaitMs = 15;
/* The note body is a continuous base64 byte stream, so chunk grouping is a
 * purely local choice: batch 4 chunks (~8 KB, 192 ms of audio) per send to
 * amortise the ~77 ms per-send cost that otherwise backlogs the queue
 * (~0.9 s tail flush measured after a 2 s take, 2026-11-05). */
constexpr uint8_t kVoiceBatchChunks = 4;
/* Batches (not chunks) per pump turn: 8 x 4 chunks = 1.5 s of audio, bounds
 * the time inbound polling can starve while draining. */
constexpr uint8_t kVoiceBurstMax = 8;
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kHandshakeTimeoutMs = 15000;
constexpr uint32_t kSendTimeoutMs = 10000;
constexpr uint32_t kCloseTimeoutMs = 12000;
constexpr uint32_t kReconnectMinMs = 1000;
constexpr uint32_t kReconnectMaxMs = 15000;

constexpr uint32_t kStableSessionMs = 30000;

constexpr uint8_t kShortLivedLimit = 3;
constexpr uint8_t kWorkerPriority = 7;
constexpr uint32_t kWorkerStackSize = 12288;

constexpr int kEventQueueDepth = 32;

enum {
    CHAT_OK = 0,
    CHAT_ERR_TRANSPORT = -1,
    CHAT_ERR_PROTOCOL = -2,
    CHAT_ERR_AUTH = -3,
    CHAT_ERR_NO_MEMORY = -4,
    CHAT_ERR_ARGUMENT = -5,
};

enum class EventKind : uint8_t {
    Connected,
    Frame,
    Disconnected,
    Closed,
    Error,
    Wake,
};

struct ChatEvent {
    EventKind kind;
    uint8_t *data;
    size_t size;
};

struct ChatBuffers {
    uint8_t *ws;
    uint8_t *transport;
    uint8_t *service;
    uint8_t *out_service;
    uint8_t *out_envelope;

    bool allocate()
    {
        /* The control session already holds ~150 KB of SRAM buffers; this
         * second set goes to PSRAM (plentiful) except the small outbound
         * scratch. psram_zalloc'd blocks are released with plain free(). */
        ws = static_cast<uint8_t *>(psram_zalloc(kWsPayloadSize));
        transport = static_cast<uint8_t *>(psram_zalloc(kWsPayloadSize));
        service = static_cast<uint8_t *>(psram_zalloc(kServiceScratchSize));
        out_service = static_cast<uint8_t *>(malloc(kOutboundScratchSize));
        out_envelope = static_cast<uint8_t *>(malloc(kOutboundScratchSize));
        return ws && transport && service && out_service && out_envelope;
    }

    void release()
    {
        free(ws);
        free(transport);
        free(service);
        free(out_service);
        free(out_envelope);
        memset(this, 0, sizeof(*this));
    }
};

struct ChatContext {
    beken_queue_t events;
    beken_thread_t worker;
    std::atomic<bool> initialized{false};
    std::atomic<bool> running{false};
    std::atomic<bool> callback_fault{false};
    std::atomic<bool> callback_closed{false};
    /* DISCONNECTED has been dispatched: with auto_reconnect=false the ws
     * client task will exit and free_client itself (including mutex deinit).
     * An owner destroy then races with that self-cleanup (2026-11-11 dump:
     * after a failed connect, close_websocket's destroy->stop held the lock
     * while free_client destroyed it, and unlocking NULL triggered the
     * xQueueGiveMutexRecursive assert). Once set, close_websocket only waits
     * for CLOSED and never touches client again. */
    std::atomic<bool> callback_disconnected{false};
    std::atomic<bool> up{false};
    std::atomic<bool> refresh_sent{false};
    std::atomic<bool> reconnect_requested{false};

    const char *ca_cert;
    size_t ca_cert_len;

    /* Accessed only by the WebSocket event callback. */
    uint8_t *partial;
    size_t partial_size;
    size_t partial_used;

    /* Diagnostics only (worker thread): Noise transport frames successfully
     * decrypted on the current link (= recv nonce) and the link start time.
     * A decrypt failure can then be told apart: first frame (rx_frames=1) vs
     * a link that ran fine for N frames and then lost its keys. */
    uint32_t rx_frames;
    uint32_t conn_started_ms;
    std::atomic<uint32_t> last_rx_ms{0};
};

static ChatContext s_chat;
/* Config copies live in PSRAM: the 2 KB bearer alone would otherwise eat
 * link-time SRAM heap (the AP heap floor asserts at 90 KB — see
 * bk7259_ap_bsp.ld). */
static char s_host[kHostSize] __attribute__((section(".psram.bss")));
static char s_vm_id[kNodeIdSize] __attribute__((section(".psram.bss")));
static char s_token[kTokenSize] __attribute__((section(".psram.bss")));

/* ---- event queue ---------------------------------------------------------*/

void release_event(ChatEvent &event)
{
    free(event.data);
    event.data = nullptr;
    event.size = 0;
}

bool queue_event(EventKind kind, uint8_t *data = nullptr, size_t size = 0)
{
    ChatEvent event{kind, data, size};
    if (rtos_push_to_queue(&s_chat.events, &event, 0) == BK_OK) {
        return true;
    }
    free(data);
    /* The only push site that can fail: the queue is full, i.e. the worker
     * fell behind the inbound burst. */
    BK_LOGE(TAG, "chat ws fault: event queue full (kind=%d size=%u depth=%d)\r\n",
            (int)kind, (unsigned)size, kEventQueueDepth);
    s_chat.callback_fault.store(true);
    return false;
}

void drain_events()
{
    ChatEvent event{};
    while (rtos_pop_from_queue(&s_chat.events, &event, 0) == BK_OK) {
        release_event(event);
    }
}

/* BK WebSocket callback: reassemble one WS frame from DATA events using
 * payload_len (the component may split a frame), queue complete binary
 * payloads for the worker. Same contract as muse_session.cpp: only BINARY
 * is accepted; anything else is a transport fault (the API hides FIN, so
 * continuation frames cannot be reconstructed safely). */
void websocket_event(void *, char *, int32_t event_id, void *event_data)
{
    auto *data = static_cast<bk_websocket_event_data_t *>(event_data);

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        queue_event(EventKind::Connected);
        return;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_chat.callback_disconnected.store(true);
        queue_event(EventKind::Disconnected);
        return;
    case WEBSOCKET_EVENT_CLOSED:
        /* The client task dispatches CLOSED right before free_client()
         * releases the transport, so this flag is the only thing
         * close_websocket() can trust to skip a use-after-free destroy
         * (field 2026-11-11: write error -> task self-freed -> teardown
         * destroyed the stale client -> xQueueSemaphoreTake assert).
         * muse_session.cpp already does this; the chat path missed it. */
        s_chat.callback_closed.store(true);
        queue_event(EventKind::Closed);
        return;
    case WEBSOCKET_EVENT_ERROR:
        queue_event(EventKind::Error);
        return;
    case WEBSOCKET_EVENT_DATA:
        break;
    default:
        return;
    }

    if (!data || data->op_code == WS_TRANSPORT_OPCODES_PING ||
        data->op_code == WS_TRANSPORT_OPCODES_PONG) {
        return;
    }

    /* 只统计业务下行：SDK 连 PONG 也会当 DATA 事件派发，不过滤的话
     * idle 永远在 0~10s 之间打转（10s 是 SDK 自己的 ping 周期），
     * 测不出服务端真正的业务空闲。 */
    s_chat.last_rx_ms.store(rtos_get_time());
    if (data->op_code != WS_TRANSPORT_OPCODES_BINARY) {
        BK_LOGE(TAG, "chat ws fault: non-binary opcode=%d\r\n",
                (int)data->op_code);
        s_chat.callback_fault.store(true);
        queue_event(EventKind::Wake);
        return;
    }
    if (!data->data_ptr || data->data_len < 0 || data->payload_len <= 0 ||
        data->payload_len > static_cast<int>(kWsPayloadSize) ||
        data->data_len > data->payload_len) {
        BK_LOGE(TAG, "chat ws fault: bad message (ptr=%d len=%d payload=%d "
                "max=%u)\r\n",
                data->data_ptr != nullptr, (int)data->data_len,
                (int)data->payload_len, (unsigned)kWsPayloadSize);
        s_chat.callback_fault.store(true);
        queue_event(EventKind::Wake);
        return;
    }

    if (!s_chat.partial) {
        s_chat.partial_size = static_cast<size_t>(data->payload_len);
        s_chat.partial_used = 0;
        s_chat.partial =
            static_cast<uint8_t *>(malloc(s_chat.partial_size));
        if (!s_chat.partial) {
            BK_LOGE(TAG, "chat ws fault: no memory for %u byte message\r\n",
                    (unsigned)s_chat.partial_size);
            s_chat.callback_fault.store(true);
            queue_event(EventKind::Wake);
            return;
        }
    }

    const size_t chunk = static_cast<size_t>(data->data_len);
    if (static_cast<size_t>(data->payload_len) != s_chat.partial_size ||
        chunk > s_chat.partial_size - s_chat.partial_used) {
        BK_LOGE(TAG, "chat ws fault: chunk mismatch (payload=%d partial=%u "
                "used=%u chunk=%u)\r\n",
                (int)data->payload_len, (unsigned)s_chat.partial_size,
                (unsigned)s_chat.partial_used, (unsigned)chunk);
        free(s_chat.partial);
        s_chat.partial = nullptr;
        s_chat.partial_size = 0;
        s_chat.partial_used = 0;
        s_chat.callback_fault.store(true);
        queue_event(EventKind::Wake);
        return;
    }

    memcpy(s_chat.partial + s_chat.partial_used, data->data_ptr, chunk);
    s_chat.partial_used += chunk;
    if (s_chat.partial_used == s_chat.partial_size) {
        uint8_t *complete = s_chat.partial;
        const size_t complete_size = s_chat.partial_size;
        s_chat.partial = nullptr;
        s_chat.partial_size = 0;
        s_chat.partial_used = 0;
        queue_event(EventKind::Frame, complete, complete_size);
    }
}

bool pop_event(ChatEvent &event, uint32_t timeout_ms)
{
    memset(&event, 0, sizeof(event));
    return rtos_pop_from_queue(&s_chat.events, &event, timeout_ms) == BK_OK;
}

bool wait_for(EventKind wanted, uint32_t timeout_ms, bool *closed = nullptr)
{
    uint32_t waited = 0;
    while (s_chat.running.load() && waited < timeout_ms) {
        ChatEvent event{};
        uint32_t slice = timeout_ms - waited;
        if (slice > kEventWaitMs) {
            slice = kEventWaitMs;
        }
        if (!pop_event(event, slice)) {
            waited += slice;
            if (s_chat.callback_closed.load()) {
                if (closed) {
                    *closed = true;
                }
                return wanted == EventKind::Closed;
            }
            if (s_chat.callback_fault.load()) {
                return false;
            }
            continue;
        }
        const EventKind kind = event.kind;
        release_event(event);
        if (kind == wanted) {
            if (closed && kind == EventKind::Closed) {
                *closed = true;
            }
            return true;
        }
        if (kind == EventKind::Closed) {
            if (closed) {
                *closed = true;
            }
            return false;
        }
        if (kind == EventKind::Error || kind == EventKind::Disconnected) {
            return false;
        }
    }
    return false;
}

bool receive_frame(uint8_t *out, size_t capacity, size_t &written,
                   uint32_t timeout_ms, bool &closed)
{
    written = 0;
    uint32_t waited = 0;
    while (s_chat.running.load() && waited < timeout_ms) {
        /* 丢帧已经让 Noise 接收 nonce 错位，剩余队列里的帧必然解不开；先于
         * 解密退出，免得把本地丢帧误报成对端换钥的 UNAUTHENTICATED。 */
        if (s_chat.callback_fault.load()) {
            return false;
        }
        ChatEvent event{};
        uint32_t slice = timeout_ms - waited;
        if (slice > kEventWaitMs) {
            slice = kEventWaitMs;
        }
        if (!pop_event(event, slice)) {
            waited += slice;
            if (s_chat.callback_closed.load()) {
                closed = true;
                return false;
            }
            if (s_chat.callback_fault.load()) {
                return false;
            }
            continue;
        }
        if (event.kind == EventKind::Frame) {
            if (event.size <= capacity) {
                memcpy(out, event.data, event.size);
                written = event.size;
            }
            release_event(event);
            return written != 0;
        }
        if (event.kind == EventKind::Closed) {
            closed = true;
        }
        const bool fatal = event.kind == EventKind::Closed ||
                           event.kind == EventKind::Disconnected ||
                           event.kind == EventKind::Error;
        release_event(event);
        if (fatal) {
            return false;
        }
    }
    return false;
}

/* ---- Noise framing --------------------------------------------------------*/

bool send_binary(transport client, const uint8_t *data, size_t size)
{
    /* DISCONNECTED/CLOSED 已派发 = 客户端任务正在或已经收尸
     * (free_client 会释放 tx_buffer 并销毁 mutex，且不收发送锁保护)。
     * 标志在 ws_disconnect 里同步置位、早于收尸全程；这里再发就会撞上
     * 释放（2026-11-11 dump：RST 后任务收尸与语音泵 memcpy 并发，
     * tx_buffer==NULL -> MemFault）。失败走上层断线重连。 */
    if (s_chat.callback_disconnected.load() || s_chat.callback_closed.load()) {
        BK_LOGW(TAG, "chat ws tearing down, drop send (%u bytes)\r\n",
                (unsigned)size);
        return false;
    }
    return size <= static_cast<size_t>(INT_MAX) &&
           websocket_client_send_binary(
               client, reinterpret_cast<const char *>(data),
               static_cast<int>(size), kSendTimeoutMs) ==
               static_cast<int>(size);
}

bool flush_outbound(transport client, ClientSession &session, uint8_t *ws)
{
    while (session.HasOutboundWebSocketPayload()) {
        auto result =
            session.WriteNextOutboundWebSocketPayload(ByteSpan(ws, kWsPayloadSize));
        if (!result.ok()) {
            BK_LOGE(TAG, "chat seal outbound failed: %s\r\n",
                    result.status().str());
            return false;
        }
        if (!send_binary(client, ws, result.size())) {
            BK_LOGE(TAG, "chat send outbound failed (%u bytes)\r\n",
                    (unsigned)result.size());
            return false;
        }
    }
    return true;
}

bool handshake(transport client, ClientSession &session, ChatBuffers &buffers,
               bool &closed)
{
    auto message1 =
        session.WriteHandshakeMessage1(ByteSpan(buffers.ws, kWsPayloadSize));
    if (!message1.ok() ||
        !send_binary(client, buffers.ws, message1.size())) {
        return false;
    }

    size_t message2_size = 0;
    if (!receive_frame(buffers.ws, kWsPayloadSize, message2_size,
                       kHandshakeTimeoutMs, closed)) {
        return false;
    }
    uint8_t peer_extra[256];
    size_t peer_extra_written = 0;
    auto read2 = session.ReadHandshakeMessage2(
        ConstByteSpan(buffers.ws, message2_size),
        ByteSpan(peer_extra, sizeof(peer_extra)), peer_extra_written);
    if (!read2.ok()) {
        return false;
    }

    /* Authorization is the WebSocket Bearer header; message 3 is empty at
     * the pinned protocol commit. */
    auto message3 = session.WriteHandshakeMessage3(
        ConstByteSpan(), ByteSpan(buffers.ws, kWsPayloadSize));
    return message3.ok() &&
           send_binary(client, buffers.ws, message3.size()) &&
           session.isEstablished();
}

/* ---- voice streams --------------------------------------------------------*/

/* Per-request x-request-id (upstream request(): "muse-<rand>-<rand>"). */
void make_req_id(char *out, size_t out_size)
{
    snprintf(out, out_size, "muse-%08lx-%08lx",
             (unsigned long)bk_rand(), (unsigned long)bk_rand());
}

/* POST /chat/stream (voice note upload) on the note stream. */
bool open_note(transport client, ClientSession &session, ChatBuffers &buffers)
{
    char req_id[40];
    make_req_id(req_id, sizeof(req_id));
    const HeaderView headers[] = {
        {StringView("x-request-id"), StringView(req_id)},
        {StringView("x-app-id"), StringView(muse_voice_proto_app_id())},
        {StringView("Content-Type"),
         StringView(muse_voice_proto_content_type())},
        {StringView("Accept"), StringView(muse_voice_proto_note_accept())},
    };
    ApplicationRequestView request;
    request.verb = StringView("POST");
    request.path = StringView(muse_voice_proto_note_path());
    request.headers = Span<const HeaderView>(headers,
                                             sizeof(headers) / sizeof(headers[0]));
    request.end_body = false;
    auto result = session.StartOutboundApplicationRequest(
        ServiceType::Daemon, muse_voice_note_stream(), request,
        ByteSpan(buffers.out_service, kOutboundScratchSize),
        ByteSpan(buffers.out_envelope, kOutboundScratchSize));
    return result.ok() && flush_outbound(client, session, buffers.ws);
}

/* POST /chat/subscribe, body "{}", end_body (upstream open_subscription();
 * the chat connection's own conversation needs no session_id field). */
bool open_sub(transport client, ClientSession &session, ChatBuffers &buffers)
{
    char req_id[40];
    make_req_id(req_id, sizeof(req_id));
    const HeaderView headers[] = {
        {StringView("x-request-id"), StringView(req_id)},
        {StringView("x-app-id"), StringView(muse_voice_proto_app_id())},
        {StringView("Content-Type"),
         StringView(muse_voice_proto_content_type())},
        {StringView("Accept"), StringView(muse_voice_proto_sub_accept())},
    };
    ApplicationRequestView request;
    request.verb = StringView("POST");
    request.path = StringView(muse_voice_proto_sub_path());
    request.headers = Span<const HeaderView>(headers,
                                             sizeof(headers) / sizeof(headers[0]));
    request.body = ConstByteSpan(reinterpret_cast<const uint8_t *>("{}"), 2);
    request.end_body = true;
    auto result = session.StartOutboundApplicationRequest(
        ServiceType::Daemon, muse_voice_sub_stream(), request,
        ByteSpan(buffers.out_service, kOutboundScratchSize),
        ByteSpan(buffers.out_envelope, kOutboundScratchSize));
    return result.ok() && flush_outbound(client, session, buffers.ws);
}

/* Send one outbound voice unit. Runs on the chat worker only. */
bool voice_send_kind(transport client, ClientSession &session,
                     ChatBuffers &buffers, muse_voice_tx_kind_t kind,
                     const uint8_t *frame, size_t frame_len)
{
    switch (kind) {
    case MUSE_VOICE_TX_OPEN_NOTE: {
        const bool ok = open_note(client, session, buffers);
        BK_LOGI(TAG, "voice note open -> %s\r\n", ok ? "sent" : "failed");
        muse_voice_tx_ack(kind, ok);
        return ok;
    }
    case MUSE_VOICE_TX_OPEN_SUB: {
        const bool ok = open_sub(client, session, buffers);
        BK_LOGI(TAG, "voice subscribe open -> %s\r\n",
                ok ? "sent" : "failed");
        muse_voice_tx_ack(kind, ok);
        return ok;
    }
    case MUSE_VOICE_TX_DATA:
    case MUSE_VOICE_TX_END: {
        BodyChunkView body;
        body.data = ConstByteSpan(frame, frame_len);
        body.end_body = kind == MUSE_VOICE_TX_END;
        auto result = session.StartOutboundBodyChunk(
            ServiceType::Daemon, muse_voice_note_stream(), body,
            ByteSpan(buffers.out_service, kOutboundScratchSize),
            ByteSpan(buffers.out_envelope, kOutboundScratchSize));
        const bool ok =
            result.ok() && flush_outbound(client, session, buffers.ws);
        if (kind == MUSE_VOICE_TX_END) {
            BK_LOGI(TAG, "voice note end_body -> %s\r\n",
                    ok ? "sent" : "failed");
        }
        muse_voice_tx_ack(kind, ok);
        return ok;
    }
    case MUSE_VOICE_TX_RESET: {
        /* Reset only the note stream: the subscribe stream is
         * per-connection and must survive turn aborts/timeouts or late
         * assistant replies have nowhere to arrive. The target id comes
         * from voice_abort() (barge-in safe), falling back to the current
         * note stream. */
        const int64_t stream_id = muse_voice_reset_stream();
        bool ok = true;
        if (stream_id != 0) {
            ResetView reset;
            reset.code = ResetCode::Cancelled;
            reset.reason = StringView("cancelled");
            auto result = session.StartOutboundReset(
                ServiceType::Daemon, stream_id, reset,
                ByteSpan(buffers.out_service, kOutboundScratchSize),
                ByteSpan(buffers.out_envelope, kOutboundScratchSize));
            ok = result.ok();
        }
        ok = ok && flush_outbound(client, session, buffers.ws);
        BK_LOGI(TAG, "voice note reset -> %s\r\n", ok ? "sent" : "failed");
        /* The reset is best-effort cleanup; reach IDLE either way and never
         * tear down a healthy chat connection over it. */
        muse_voice_tx_ack(MUSE_VOICE_TX_RESET, true);
        return true;
    }
    default:
        return true;
    }
}

/* Wait for one voice outbound unit, then drain the queued burst without
 * further blocking (same as the control session's pump: the mic produces
 * 50 frames/s; one frame per loop turn would backlog the queue).
 * DATA chunks are batched kVoiceBatchChunks-to-a-send: the note body is one
 * continuous base64 stream, so concatenation is invisible to the server. */
bool voice_pump_once(transport client, ClientSession &session,
                     ChatBuffers &buffers)
{
    /* Static: 8 KB on the 12 KB worker stack is not flat-safe; reused across
     * burst pops since flush_outbound() consumes the envelope first. */
    static uint8_t frame[MUSE_VOICE_CHUNK_CAP * kVoiceBatchChunks]
        __attribute__((section(".psram.bss")));
    for (uint8_t sent = 0;; sent++) {
        size_t frame_len = 0;
        muse_voice_tx_kind_t kind =
            muse_voice_tx_next(frame, sizeof(frame), &frame_len,
                               sent == 0 ? kVoiceTxWaitMs : 0U);
        if (kind == MUSE_VOICE_TX_NONE) {
            return true;
        }
        if (kind == MUSE_VOICE_TX_DATA) {
            /* Opportunistically append more queued chunks to this send. An
             * END chunk closes the batch (end_body must ride the last send);
             * NONE just ends it; control kinds are flag-based (tx_next did
             * not consume anything) and pop again on the next loop turn. */
            while (frame_len + MUSE_VOICE_CHUNK_CAP <= sizeof(frame)) {
                size_t more = 0;
                const muse_voice_tx_kind_t next =
                    muse_voice_tx_next(frame + frame_len,
                                       sizeof(frame) - frame_len, &more, 0U);
                if (next == MUSE_VOICE_TX_DATA) {
                    frame_len += more;
                    continue;
                }
                if (next == MUSE_VOICE_TX_END) {
                    frame_len += more;
                    kind = MUSE_VOICE_TX_END;
                }
                break;
            }
        }
        if (!voice_send_kind(client, session, buffers, kind, frame,
                             frame_len)) {
            return false;
        }
        if (sent >= kVoiceBurstMax) {
            return true;
        }
    }
}

/* ---- inbound --------------------------------------------------------------*/

/* One iteration of the inbound loop. Returns >=0 when fine (1 = something
 * was dispatched), a negative CHAT_ERR_* on a fatal fault. */
int poll_inbound_once(transport client, ClientSession &session,
                      ChatBuffers &buffers, HeaderView *headers,
                      size_t header_count, bool &closed, uint32_t wait_ms)
{
    size_t frame_size = 0;
    if (!receive_frame(buffers.ws, kWsPayloadSize, frame_size,
                       wait_ms, closed)) {
        if (s_chat.running.load() &&
            (closed || s_chat.callback_fault.load())) {
            /* Which of the two fired decides the remedy: a peer close means
             * the server reclaimed the session (same root cause as a
             * UNAUTHENTICATED kick), while a callback fault means the local
             * receive path rejected the stream (oversized/non-binary frame,
             * event queue overflow). */
            BK_LOGE(TAG, "chat link fault: %s (rx_frames=%lu, alive=%lu ms)\r\n",
                    closed ? "peer closed the websocket"
                           : "local websocket callback fault",
                    (unsigned long)s_chat.rx_frames,
                    (unsigned long)(rtos_get_time() - s_chat.conn_started_ms));
            return CHAT_ERR_TRANSPORT;
        }
        return 0;
    }

    auto inbound = session.ProcessInboundWebSocketPayload(
        ConstByteSpan(buffers.ws, frame_size),
        ByteSpan(buffers.transport, kWsPayloadSize),
        ByteSpan(buffers.service, kServiceScratchSize),
        Span<HeaderView>(headers, 8));
    if (!inbound.ok()) {
        /* 同一个 UNAUTHENTICATED 有两种根因：
         *  - 本地接收路径先丢了一帧（队列满 / 异常帧），Noise 接收 nonce
         *    因此少走一步，之后每一帧都认证失败（field 2026-11-16）；
         *  - 对端换了会话密钥（例如浏览器打开同一 VM 的 Muse 页面）。
         * callback_fault 只在本地丢帧/拒绝帧时置位，用它区分两种情况。 */
        const char *cause = s_chat.callback_fault.load()
            ? "; local receive fault dropped a frame first (queue full or bad "
              "frame) -> recv nonce desync, local root cause"
            : (inbound.status.IsUnauthenticated()
                   ? "; recv keys no longer match (peer re-keyed?), link is"
                     " permanently poisoned, reconnect required"
                   : "");
        BK_LOGE(TAG, "Noise receive failed: %s (ws=%u B, rx_frames=%lu, "
                "alive=%lu ms)%s\r\n",
                inbound.status.str(), (unsigned)frame_size,
                (unsigned long)s_chat.rx_frames,
                (unsigned long)(rtos_get_time() - s_chat.conn_started_ms),
                cause);
        return CHAT_ERR_PROTOCOL;
    }
    ++s_chat.rx_frames;
    if (inbound.frame_status != InboundFrameStatus::Complete) {
        return 0;
    }

    const auto &decoded = inbound.frame;
    const int64_t note_id = muse_voice_note_stream();
    const int64_t sub_id = muse_voice_sub_stream();
    if (note_id != 0 && decoded.stream_id == note_id) {
        /* The note ACK arrives as the Response (status + body) possibly
         * split across BodyChunks; the voice state machine reassembles. */
        if (decoded.kind == ServiceFrameKind::Response) {
            muse_voice_on_rx_ack(decoded.response.status,
                                 decoded.response.body.data(),
                                 decoded.response.body.size(),
                                 decoded.response.end_body);
        } else if (decoded.kind == ServiceFrameKind::BodyChunk) {
            muse_voice_on_rx_ack(0, decoded.body_chunk.data.data(),
                                 decoded.body_chunk.data.size(),
                                 decoded.body_chunk.end_body);
        } else if (decoded.kind == ServiceFrameKind::Reset) {
            muse_voice_on_rx_note_reset();
        }
        return 1;
    }
    if (sub_id != 0 && decoded.stream_id == sub_id) {
        /* The NDJSON reply stream: Response body followed by BodyChunks. */
        if (decoded.kind == ServiceFrameKind::Response) {
            muse_voice_on_rx_sub_data(decoded.response.status,
                                      decoded.response.body.data(),
                                      decoded.response.body.size(),
                                      decoded.response.end_body);
        } else if (decoded.kind == ServiceFrameKind::BodyChunk) {
            muse_voice_on_rx_sub_data(0, decoded.body_chunk.data.data(),
                                      decoded.body_chunk.data.size(),
                                      decoded.body_chunk.end_body);
        } else if (decoded.kind == ServiceFrameKind::Reset) {
            muse_voice_on_rx_sub_reset();
        }
        return 1;
    }
    /* No control stream exists on this connection; anything else is
     * unexpected but never fatal. */
    BK_LOGW(TAG, "chat: frame on unexpected stream %lld kind=%d\r\n",
            (long long)decoded.stream_id, (int)decoded.kind);
    return 0;
}

/* ---- connection lifecycle --------------------------------------------------*/

/* Only consume events until CLOSED; never touch client.
 *
 * The SDK ws task calls free_client itself when it exits (which deinits the
 * recursive mutex). An owner websocket_client_destroy -> stop would then
 * unlock an already deinited lock and trip the
 * xQueueGiveMutexRecursive:797 assert (2026-11-18 dump: connect-timeout
 * teardown racing with task self-cleanup). Therefore the application never
 * destroys the client after a successful start. */
void wait_for_client_closed(bool &closed)
{
    uint32_t waited = 0;
    while (!closed && waited < kCloseTimeoutMs) {
        if (s_chat.callback_closed.load()) {
            closed = true;
            break;
        }
        ChatEvent event{};
        if (!pop_event(event, kEventWaitMs)) {
            waited += kEventWaitMs;
            continue;
        }
        if (event.kind == EventKind::Closed) {
            closed = true;
        }
        release_event(event);
    }
    if (!closed) {
        /* The task may still be stuck in ws_connect/ws_tcp_close; do not
         * destroy (that would contend on the same lock, which is exactly the
         * dump we are avoiding). Let it clean up and let the worker
         * reconnect. */
        BK_LOGE(TAG, "chat WebSocket close callback timed out\r\n");
    }
}

void close_websocket(transport client, bool &closed)
{
    closed = closed || s_chat.callback_closed.load();
    if (!client || closed) {
        if (client && closed) {
            /* The client task already freed the transport (see the CLOSED
             * handler); destroying here would touch freed memory. */
            BK_LOGI(TAG, "chat ws already closed by its task, skip destroy\r\n");
        }
        return;
    }

    if (s_chat.callback_disconnected.load()) {
        /* The task is already cleaning up after a connect failure / peer
         * kick: just wait for CLOSED and leave client alone. */
        BK_LOGI(TAG, "chat ws task self-closing, wait for CLOSED\r\n");
    } else if (websocket_client_is_connected(client)) {
        /* Connection is still healthy (local stop / business-layer rebuild):
         * send a WS close frame first, then set run=false so the task walks
         * CLOSED -> free_client itself. Do not use destroy: it would contend
         * with the task's free_client on the same recursive mutex. */
        (void)websocket_client_send_with_opcode(
            client, WS_TRANSPORT_OPCODES_CLOSE, (const uint8_t *)"", 0,
            kSendTimeoutMs);
        client->run = false;
        BK_LOGI(TAG, "chat ws stop requested, wait for CLOSED\r\n");
    } else {
        /* Not connected (INIT/CLOSING/WAIT_TIMEOUT): the task exits on its
         * own; do not touch it. */
        BK_LOGI(TAG, "chat ws not connected, wait for CLOSED\r\n");
    }
    wait_for_client_closed(closed);
}

int run_chat_session(transport client, ChatBuffers &buffers, bool &closed)
{
    PsaCryptoBackend crypto;
    ClientSession session(crypto);
    if (!handshake(client, session, buffers, closed)) {
        return CHAT_ERR_PROTOCOL;
    }

    muse_voice_on_session(true);
    s_chat.up.store(true);
    s_chat.last_rx_ms.store(rtos_get_time());
    BK_LOGI(TAG, "chat connected (no link.register; own conversation)\r\n");

    HeaderView headers[8];
    uint32_t idle_report_at = rtos_get_time() + kRxIdleReportMs;
    while (s_chat.running.load()) {
        if (s_chat.reconnect_requested.load()) {
            BK_LOGW(TAG, "chat rebuilding session on request (rx_frames=%lu, alive=%lu ms)\r\n",
                    (unsigned long)s_chat.rx_frames,
                    (unsigned long)(rtos_get_time() - s_chat.conn_started_ms));
            return CHAT_ERR_TRANSPORT;
        }
        if (!voice_pump_once(client, session, buffers)) {
            BK_LOGE(TAG, "chat outbound pump failed (rx_frames=%lu, alive=%lu ms)\r\n",
                    (unsigned long)s_chat.rx_frames,
                    (unsigned long)(rtos_get_time() - s_chat.conn_started_ms));
            return CHAT_ERR_TRANSPORT;
        }
        muse_voice_on_tick();
        const uint32_t inbound_wait = muse_voice_tx_active()
                                          ? kVoiceInboundWaitMs
                                          : kEventWaitMs;
        const int poll = poll_inbound_once(client, session, buffers, headers,
                                           8, closed, inbound_wait);
        if (poll < 0) {
            return poll;
        }
        const uint32_t now = rtos_get_time();
        if ((int32_t)(now - idle_report_at) >= 0) {
            const uint32_t rx = s_chat.last_rx_ms.load();
            BK_LOGD(TAG, "chat rx idle %lu ms (frames=%lu)\r\n",
                    (unsigned long)(rx ? now - rx : 0),
                    (unsigned long)s_chat.rx_frames);
            idle_report_at = now + kRxIdleReportMs;
        }
    }
    return CHAT_OK;
}

int run_connection(ChatBuffers &buffers, bool &came_up)
{
    char uri[kHostSize + kNodeIdSize + 48];
    char authorization[kTokenSize + 16];
    came_up = false;
    s_chat.rx_frames = 0;
    s_chat.conn_started_ms = rtos_get_time();
    if (s_vm_id[0] != '\0') {
        snprintf(uri, sizeof(uri), "wss://%s/v1/noise?vm_id=%s",
                 s_host, s_vm_id);
    } else {
        snprintf(uri, sizeof(uri), "wss://%s/v1/noise", s_host);
    }
    snprintf(authorization, sizeof(authorization), "Bearer %s", s_token);

    websocket_client_input_t input{};
    input.uri = uri;
    input.enable_auto_reconnect = false;
    input.rx_retry = 10;
    input.user_context = &s_chat;
    input.buffer_size = static_cast<int>(kWsPayloadSize);
    input.authorization = authorization;
    input.ws_event_handler = websocket_event;

    s_chat.callback_fault.store(false);
    s_chat.callback_closed.store(false);
    s_chat.callback_disconnected.store(false);
    s_chat.reconnect_requested.store(false);
    free(s_chat.partial);
    s_chat.partial = nullptr;
    s_chat.partial_size = 0;
    s_chat.partial_used = 0;
    drain_events();

    transport client = websocket_client_init(&input);
    if (!client) {
        return CHAT_ERR_NO_MEMORY;
    }
    if (!client->bk_ssl) {
        (void)websocket_client_destroy(client);
        return CHAT_ERR_TRANSPORT;
    }
    client->bk_ssl->cfg.cacert_buf =
        reinterpret_cast<const unsigned char *>(s_chat.ca_cert);
    client->bk_ssl->cfg.cacert_bytes =
        static_cast<unsigned int>(s_chat.ca_cert_len);
    client->bk_ssl->cfg.skip_common_name = false;
    bool closed = false;
    if (websocket_client_start(client) != BK_OK) {
        BK_LOGE(TAG, "chat connect failed: websocket_client_start error\r\n");
        (void)websocket_client_destroy(client);
        return CHAT_ERR_TRANSPORT;
    }
    if (!wait_for(EventKind::Connected, kConnectTimeoutMs, &closed)) {
        /* The stock websocket client does not expose the HTTP upgrade
         * status, so a 401/403 token rejection is indistinguishable
         * from a transport failure; reported as TRANSPORT. */
        BK_LOGE(TAG, "chat connect failed: no websocket upgrade within %lu ms "
                "(401/403 token rejection looks identical here)\r\n",
                (unsigned long)kConnectTimeoutMs);
        /* The connection never came up: after a failed ws_connect the SDK
         * task exits and free_client itself; only wait for CLOSED here and
         * never destroy. */
        wait_for_client_closed(closed);
        return CHAT_ERR_TRANSPORT;
    }
    if (!client->bk_ssl->tls ||
        mbedtls_ssl_get_verify_result(&client->bk_ssl->tls->ssl) != 0U) {
        BK_LOGE(TAG, "chat connect failed: TLS verify result=%lu\r\n",
                client->bk_ssl->tls
                    ? (unsigned long)mbedtls_ssl_get_verify_result(
                          &client->bk_ssl->tls->ssl)
                    : 0UL);
        close_websocket(client, closed);
        return CHAT_ERR_TRANSPORT;
    }

    const int result = run_chat_session(client, buffers, closed);
    came_up = s_chat.up.load();
    s_chat.up.store(false);
    muse_voice_on_session(false);
    /* A mid-session error is usually a peer kick (UNAUTHENTICATED) followed
     * by WS close: do not destroy immediately, let the SDK task clean up.
     * A business-layer rebuild request is a healthy connection instead, and
     * close_websocket sends a close frame proactively. */
    close_websocket(client, closed);
    if (!closed) {
        /* On some paths the SDK ws task frees the client without ever
         * dispatching CLOSED. Waiting unbounded here would hang the chat
         * worker forever and every later wakeup would be rejected with
         * "muse session not connected". Wait one bounded window, then force
         * a reconnect. */
        uint32_t waited = 0;
        while (s_chat.running.load() && !closed && waited < kCloseTimeoutMs) {
            (void)wait_for(EventKind::Closed, kEventWaitMs, &closed);
            waited += kEventWaitMs;
        }
        if (!closed) {
            BK_LOGW(TAG, "closed event never arrived after ws task exit; "
                    "force reconnect\r\n");
        }
    }
    free(s_chat.partial);
    s_chat.partial = nullptr;
    s_chat.partial_size = 0;
    s_chat.partial_used = 0;
    drain_events();
    return result;
}

void worker_entry(beken_thread_arg_t)
{
    BK_LOGI(TAG, "chat worker started\r\n");
    BK_LOGI(TAG, "chat worker psram baseline: free=%lu min=%lu\r\n",
            (unsigned long)rtos_get_psram_free_heap_size(),
            (unsigned long)rtos_get_psram_minimum_free_heap_size());
    ChatBuffers buffers{};
    if (!buffers.allocate()) {
        BK_LOGE(TAG, "chat buffer alloc failed: ws=%p transport=%p "
                "service=%p out_service=%p out_envelope=%p "
                "[psram free=%lu]\r\n",
                buffers.ws, buffers.transport, buffers.service,
                buffers.out_service, buffers.out_envelope,
                (unsigned long)rtos_get_psram_free_heap_size());
        buffers.release();
        s_chat.running.store(false);
        rtos_delete_thread(nullptr);
        return;
    }

    uint32_t reconnect_ms = kReconnectMinMs;
    uint8_t short_lived = 0;
    while (s_chat.running.load()) {
        bool came_up = false;
        const uint32_t started_ms = rtos_get_time();
        const int result = run_connection(buffers, came_up);
        const uint32_t alive_ms = rtos_get_time() - started_ms;
        if (!s_chat.running.load()) {
            break;
        }
        if (result == CHAT_ERR_AUTH) {
            /* 上游对认证失败单独用一个长得多的退避（60s）：热循环重试
             * 被拒的 bearer 只会反复搅动服务端令牌状态。 */
            BK_LOGW(TAG, "chat bearer refused; retry in 60s\r\n");
            reconnect_ms = 60000U;
        } else if (!came_up) {
            /* 根本没连上（DNS/TCP/TLS）：正常指数退避。 */
            reconnect_ms = reconnect_ms < kReconnectMaxMs / 2
                               ? reconnect_ms * 2
                               : kReconnectMaxMs;
        } else if (alive_ms >= kStableSessionMs) {
            /* 稳定会话：退避复位，下次掉线 1s 内恢复。 */
            short_lived = 0;
            s_chat.refresh_sent.store(false);
            reconnect_ms = kReconnectMinMs;
        } else {
            /* 连上很快就掉（浏览器验证替换了 Noise 密钥的典型表现）：
             * 先按 1s 快速重连，连续 kShortLivedLimit 次后请 service
             * 刷新设备令牌；刷新后仍被踢就退回指数退避，避免打爆服务端。 */
            if (short_lived < 255U) {
                short_lived++;
            }
            if (short_lived <= kShortLivedLimit) {
                reconnect_ms = kReconnectMinMs;
            } else {
                reconnect_ms = reconnect_ms < kReconnectMaxMs / 2
                                   ? reconnect_ms * 2
                                   : kReconnectMaxMs;
            }
            /* refresh_sent 跨 worker 重启保留：service 刷新令牌会重启本
             * worker，若还按局部计数发请求就会每隔十几秒轮换一次服务端
             * 令牌（上游明确警告过这种搅动）。只有重新出现稳定会话才
             * 允许下一次刷新。 */
            if (short_lived >= kShortLivedLimit &&
                !s_chat.refresh_sent.load()) {
                BK_LOGW(TAG, "chat: %u short-lived sessions in a row; "
                        "requesting device token refresh\r\n",
                        (unsigned)short_lived);
                if (muse_service_request_token_refresh()) {
                    s_chat.refresh_sent.store(true);
                } else {
                    BK_LOGW(TAG, "token refresh request refused; will retry "
                            "after the next short-lived session\r\n");
                }
            }
        }
        BK_LOGW(TAG, "chat disconnected (err=%d up=%d alive=%lu ms); "
                "reconnect in %lu ms [psram free=%lu min=%lu]\r\n",
                result, (int)came_up, (unsigned long)alive_ms,
                (unsigned long)reconnect_ms,
                (unsigned long)rtos_get_psram_free_heap_size(),
                (unsigned long)rtos_get_psram_minimum_free_heap_size());
        uint32_t delayed = 0;
        while (s_chat.running.load() && delayed < reconnect_ms) {
            const uint32_t slice = reconnect_ms - delayed < kEventWaitMs
                                       ? reconnect_ms - delayed
                                       : kEventWaitMs;
            rtos_delay_milliseconds(slice);
            delayed += slice;
        }
    }

    buffers.release();
    s_chat.running.store(false);
    rtos_delete_thread(nullptr);
}

bool initialize_once()
{
    if (s_chat.initialized.load()) {
        return true;
    }
    if (rtos_init_queue(&s_chat.events, "muse_chat_events",
                        sizeof(ChatEvent), kEventQueueDepth) != BK_OK) {
        return false;
    }
    /* The voice module's queues/mutex/chunk pool live behind the chat
     * connection now (PTT entry points refuse to run before this). */
    if (muse_voice_init() != 0) {
        rtos_deinit_queue(&s_chat.events);
        return false;
    }
    s_chat.initialized.store(true);
    return true;
}

} // namespace

extern "C" int muse_chat_start(const muse_chat_config_t *config)
{
    if (!config || !config->vm_token || !*config->vm_token ||
        !config->ca_cert_pem || config->ca_cert_len < 2 ||
        config->ca_cert_pem[config->ca_cert_len - 1] != '\0' ||
        strlen(config->vm_token) >= sizeof(s_token) ||
        (config->noise_host && strlen(config->noise_host) >= sizeof(s_host)) ||
        (config->vm_id && strlen(config->vm_id) >= sizeof(s_vm_id))) {
        return CHAT_ERR_ARGUMENT;
    }
    if (!initialize_once()) {
        return CHAT_ERR_NO_MEMORY;
    }
    if (s_chat.running.load()) {
        return CHAT_OK;   /* idempotent */
    }
    if (s_chat.worker) {
        (void)rtos_thread_join(&s_chat.worker);
        s_chat.worker = nullptr;
    }

    const char *host =
        config->noise_host && *config->noise_host ? config->noise_host
                                                  : kDefaultHost;
    snprintf(s_host, sizeof(s_host), "%s", host);
    if (config->vm_id) {
        snprintf(s_vm_id, sizeof(s_vm_id), "%s", config->vm_id);
    } else {
        s_vm_id[0] = '\0';
    }
    snprintf(s_token, sizeof(s_token), "%s", config->vm_token);
    s_chat.ca_cert = config->ca_cert_pem;
    s_chat.ca_cert_len = config->ca_cert_len;

    s_chat.running.store(true);
    if (rtos_create_thread(&s_chat.worker, kWorkerPriority, "muse_chat",
                           worker_entry, kWorkerStackSize, 0) != BK_OK) {
        s_chat.running.store(false);
        s_chat.worker = nullptr;
        return CHAT_ERR_NO_MEMORY;
    }
    return CHAT_OK;
}

extern "C" void muse_chat_stop(void)
{
    if (!s_chat.initialized.load()) {
        return;
    }
    s_chat.running.store(false);
    queue_event(EventKind::Wake);

    if (s_chat.worker && !rtos_is_current_thread(&s_chat.worker)) {
        (void)rtos_thread_join(&s_chat.worker);
        s_chat.worker = nullptr;
    }
}

extern "C" bool muse_chat_is_connected(void)
{
    return s_chat.up.load();
}

extern "C" void muse_chat_request_reconnect(const char *reason)
{
    if (!s_chat.running.load()) {
        return;
    }
    s_chat.reconnect_requested.store(true);
    BK_LOGW(TAG, "chat reconnect requested: %s\r\n", reason ? reason : "-");
}
