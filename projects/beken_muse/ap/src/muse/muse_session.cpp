/**
 * @file muse_session.cpp
 * @brief BK WebSocket adapter and minimum Muse Noise control session.
 *
 * BK's WebSocket callback may split one frame into several DATA events.  This
 * adapter reassembles those events using payload_len before handing a complete
 * binary payload to ClientSession.  The API does not expose the WebSocket FIN
 * bit, so RFC 6455 continuation frames cannot be reconstructed safely; opcode
 * CONT is rejected and the connection is retried.  This behavior, TLS trust
 * setup, and the exact event offset semantics still require BK7259 on-target
 * validation.
 */

#include "muse_session.h"

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
}
#include "cJSON.h"
#include "muse_commands.h"
#include "muse_vm_api.h"

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

static char TAG[] = "muse_session";

namespace {

constexpr const char *kDefaultHost = "hatch.metaaivm.com";
constexpr int64_t kControlStreamId = 1;
constexpr size_t kNodeIdSize = 96;
constexpr size_t kHostSize = 128;
constexpr size_t kTokenSize = 2048;
constexpr size_t kWsPayloadSize =
    ClientSession::kMaxOutboundWebSocketPayloadSize;
/* Upstream runs SCRATCH = 64 KB for inbound service-frame reassembly and
 * expects NDJSON lines up to 16 KB. An 8 KB scratch gets a
 * RESOURCE_EXHAUSTED (req too large) from ClientSession the moment the
 * server's subscribe reply frame exceeds it — seen as a drop exactly one
 * RTT after end_body. 32 KB covers a 16 KB line plus frame overhead. */
constexpr size_t kServiceScratchSize = 32 * 1024;
constexpr size_t kOutboundScratchSize = 4096;
constexpr size_t kControlBodySize = 8192;
constexpr uint32_t kEventWaitMs = 250;
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kHandshakeTimeoutMs = 15000;
/* Upstream waits for the link.register reply before calling the session up;
 * bound that wait so a dropped/ignored registration retries cleanly. */
constexpr uint32_t kRegisterAckTimeoutMs = 8000;
constexpr uint32_t kSendTimeoutMs = 10000;
constexpr uint32_t kCloseTimeoutMs = 12000;
constexpr uint32_t kReconnectMinMs = 1000;
constexpr uint32_t kReconnectMaxMs = 15000;
constexpr uint8_t kWorkerPriority = 7;
/* Upstream esp32 runs the Noise control task on NOISE_CTRL_STACK=12288
 * (noise_control.cpp). 8192 overflowed on the first Noise XX handshake:
 * the worker corrupted its own stack and died in
 * bk_threading_mutex_free_freertos -> vQueueDelete (MemFault). */
constexpr uint32_t kWorkerStackSize = 12288;
constexpr int kEventQueueDepth = 8;

enum class EventKind : uint8_t {
    Connected,
    Frame,
    Disconnected,
    Closed,
    Error,
    Wake,
};

struct SessionEvent {
    EventKind kind;
    uint8_t *data;
    size_t size;
};

struct SessionBuffers {
    uint8_t *ws;
    uint8_t *transport;
    uint8_t *service;
    uint8_t *out_service;
    uint8_t *out_envelope;
    uint8_t *control_body;
    uint8_t *inbound_control;

    bool allocate()
    {
        ws = static_cast<uint8_t *>(malloc(kWsPayloadSize));
        transport = static_cast<uint8_t *>(malloc(kWsPayloadSize));
        /* 32 KB does not fit the SRAM heap floor; PSRAM is plentiful and
         * psram_zalloc'd blocks are released with the plain free() below. */
        service = static_cast<uint8_t *>(psram_zalloc(kServiceScratchSize));
        out_service = static_cast<uint8_t *>(malloc(kOutboundScratchSize));
        out_envelope = static_cast<uint8_t *>(malloc(kOutboundScratchSize));
        control_body = static_cast<uint8_t *>(malloc(kControlBodySize));
        inbound_control =
            static_cast<uint8_t *>(malloc(kControlBodySize + 1));
        return ws && transport && service && out_service && out_envelope &&
               control_body && inbound_control;
    }

    void release()
    {
        free(ws);
        free(transport);
        free(service);
        free(out_service);
        free(out_envelope);
        free(control_body);
        free(inbound_control);
        memset(this, 0, sizeof(*this));
    }
};

struct SessionContext {
    beken_mutex_t lock;
    beken_queue_t events;
    beken_thread_t worker;
    std::atomic<bool> initialized{false};
    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> callback_fault{false};
    std::atomic<bool> callback_closed{false};
    std::atomic<muse_session_state_t> state{MUSE_SESSION_STOPPED};

    char node_id[kNodeIdSize];
    char vm_id[kNodeIdSize];
    char host[kHostSize];
    char token[kTokenSize];
    const char *ca_cert;
    size_t ca_cert_len;
    muse_session_status_cb_t status_cb;
    void *status_user;

    /* link.register reply tracking (session worker thread only): CONNECTED
     * may be declared only after the server acks the registration. */
    char register_id[40];
    int register_state; /* 0=pending, 1=acked, 2=rejected */

    /* Accessed only by the serial BK WebSocket event callback. */
    uint8_t *partial;
    size_t partial_size;
    size_t partial_used;
};

SessionContext s_session{};

void release_event(SessionEvent &event)
{
    free(event.data);
    event.data = nullptr;
    event.size = 0;
}

void drain_events()
{
    SessionEvent event{};
    while (rtos_pop_from_queue(&s_session.events, &event, 0) == BK_OK) {
        release_event(event);
    }
}

bool queue_event(EventKind kind, uint8_t *data = nullptr, size_t size = 0)
{
    SessionEvent event{kind, data, size};
    if (rtos_push_to_queue(&s_session.events, &event, 0) == BK_OK) {
        return true;
    }
    free(data);
    s_session.callback_fault.store(true);
    return false;
}

void set_state(muse_session_state_t state, int error)
{
    s_session.state.store(state);

    muse_session_status_cb_t cb;
    void *user;
    rtos_lock_mutex(&s_session.lock);
    cb = s_session.status_cb;
    user = s_session.status_user;
    rtos_unlock_mutex(&s_session.lock);

    /* Never invoke application code while holding an internal lock. */
    if (cb) {
        cb(state, error, user);
    }
}

bool valid_host(const char *host)
{
    if (!host || !*host) {
        return false;
    }
    for (const unsigned char *p =
             reinterpret_cast<const unsigned char *>(host);
         *p; ++p) {
        if (*p <= 0x20 || *p == 0x7f || *p == '/' || *p == '\\') {
            return false;
        }
    }
    return true;
}

bool valid_node_id(const char *node_id)
{
    if (!node_id || !*node_id) {
        return false;
    }
    for (const unsigned char *p =
             reinterpret_cast<const unsigned char *>(node_id);
         *p; ++p) {
        if (*p <= 0x20 || *p == 0x7f || *p == '"' || *p == '\\') {
            return false;
        }
    }
    return true;
}

bool valid_bearer(const char *token)
{
    if (!token || !*token) {
        return false;
    }
    for (const unsigned char *p =
             reinterpret_cast<const unsigned char *>(token);
         *p; ++p) {
        if (*p <= 0x20 || *p == 0x7f) {
            return false;
        }
    }
    return true;
}

void websocket_event(void *, char *, int32_t event_id, void *event_data)
{
    auto *data = static_cast<bk_websocket_event_data_t *>(event_data);

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        queue_event(EventKind::Connected);
        return;
    case WEBSOCKET_EVENT_DISCONNECTED:
        queue_event(EventKind::Disconnected);
        return;
    case WEBSOCKET_EVENT_CLOSED:
        s_session.callback_closed.store(true);
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
    if (data->op_code != WS_TRANSPORT_OPCODES_BINARY) {
        /*
         * In particular, CONT cannot be accepted because BK does not expose
         * FIN.  Treat text/continuation as a transport error instead of
         * passing unauthenticated partial bytes to Noise.
         */
        s_session.callback_fault.store(true);
        queue_event(EventKind::Wake);
        return;
    }
    if (!data->data_ptr || data->data_len < 0 || data->payload_len <= 0 ||
        data->payload_len > static_cast<int>(kWsPayloadSize) ||
        data->data_len > data->payload_len) {
        s_session.callback_fault.store(true);
        queue_event(EventKind::Wake);
        return;
    }

    if (!s_session.partial) {
        s_session.partial_size = static_cast<size_t>(data->payload_len);
        s_session.partial_used = 0;
        s_session.partial =
            static_cast<uint8_t *>(malloc(s_session.partial_size));
        if (!s_session.partial) {
            s_session.callback_fault.store(true);
            queue_event(EventKind::Wake);
            return;
        }
    }

    const size_t chunk = static_cast<size_t>(data->data_len);
    if (static_cast<size_t>(data->payload_len) != s_session.partial_size ||
        chunk > s_session.partial_size - s_session.partial_used) {
        free(s_session.partial);
        s_session.partial = nullptr;
        s_session.partial_size = 0;
        s_session.partial_used = 0;
        s_session.callback_fault.store(true);
        queue_event(EventKind::Wake);
        return;
    }

    memcpy(s_session.partial + s_session.partial_used, data->data_ptr, chunk);
    s_session.partial_used += chunk;
    if (s_session.partial_used == s_session.partial_size) {
        uint8_t *complete = s_session.partial;
        const size_t complete_size = s_session.partial_size;
        s_session.partial = nullptr;
        s_session.partial_size = 0;
        s_session.partial_used = 0;
        queue_event(EventKind::Frame, complete, complete_size);
    }
}

bool pop_event(SessionEvent &event, uint32_t timeout_ms)
{
    memset(&event, 0, sizeof(event));
    return rtos_pop_from_queue(&s_session.events, &event, timeout_ms) == BK_OK;
}

bool wait_for(EventKind wanted, uint32_t timeout_ms, bool *closed = nullptr)
{
    uint32_t waited = 0;
    while (s_session.running.load() && waited < timeout_ms) {
        SessionEvent event{};
        uint32_t slice = timeout_ms - waited;
        if (slice > kEventWaitMs) {
            slice = kEventWaitMs;
        }
        if (!pop_event(event, slice)) {
            waited += slice;
            if (s_session.callback_closed.load()) {
                if (closed) {
                    *closed = true;
                }
                return wanted == EventKind::Closed;
            }
            if (s_session.callback_fault.load()) {
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
    while (s_session.running.load() && waited < timeout_ms) {
        SessionEvent event{};
        uint32_t slice = timeout_ms - waited;
        if (slice > kEventWaitMs) {
            slice = kEventWaitMs;
        }
        if (!pop_event(event, slice)) {
            waited += slice;
            if (s_session.callback_closed.load()) {
                closed = true;
                return false;
            }
            if (s_session.callback_fault.load()) {
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

bool send_binary(transport client, const uint8_t *data, size_t size)
{
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
        if (!result.ok() || !send_binary(client, ws, result.size())) {
            return false;
        }
    }
    return true;
}

bool handshake(transport client, ClientSession &session, SessionBuffers &buffers,
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

    /*
     * Authorization is carried by the WebSocket Bearer header.  The owner
     * NoiseHandshakeMessage3 protobuf is empty at the pinned protocol commit.
     */
    auto message3 = session.WriteHandshakeMessage3(
        ConstByteSpan(), ByteSpan(buffers.ws, kWsPayloadSize));
    return message3.ok() &&
           send_binary(client, buffers.ws, message3.size()) &&
           session.isEstablished();
}

bool open_control_stream(transport client, ClientSession &session,
                         SessionBuffers &buffers)
{
    ApplicationRequestView request;
    request.verb = StringView("POST");
    request.path = StringView("/link-control");
    request.end_body = false;
    auto result = session.StartOutboundApplicationRequest(
        ServiceType::Daemon, kControlStreamId, request,
        ByteSpan(buffers.out_service, kOutboundScratchSize),
        ByteSpan(buffers.out_envelope, kOutboundScratchSize));
    return result.ok() && flush_outbound(client, session, buffers.ws);
}

bool send_control_json(transport client, ClientSession &session,
                       SessionBuffers &buffers, const char *json, size_t size)
{
    if (!json || size > UINT32_MAX || size + 4 > kControlBodySize) {
        return false;
    }
    const uint32_t n = static_cast<uint32_t>(size);
    buffers.control_body[0] = static_cast<uint8_t>(n);
    buffers.control_body[1] = static_cast<uint8_t>(n >> 8);
    buffers.control_body[2] = static_cast<uint8_t>(n >> 16);
    buffers.control_body[3] = static_cast<uint8_t>(n >> 24);
    memcpy(buffers.control_body + 4, json, size);

    BodyChunkView body;
    body.data = ConstByteSpan(buffers.control_body, size + 4);
    body.end_body = false;
    auto result = session.StartOutboundBodyChunk(
        ServiceType::Daemon, kControlStreamId, body,
        ByteSpan(buffers.out_service, kOutboundScratchSize),
        ByteSpan(buffers.out_envelope, kOutboundScratchSize));
    return result.ok() && flush_outbound(client, session, buffers.ws);
}

int handle_invoke(const char *json, char *reply, size_t reply_size)
{
    int result = muse_commands_handle_invoke(json, reply, reply_size);
    if (result >= 0) {
        return result;
    }

    /*
     * The pinned daemon puts `command` beside `method`; the small C command
     * API accepts it inside `params`.  Normalize that one shape here so the
     * transport stays compatible without duplicating device.health logic.
     */
    cJSON *root = cJSON_Parse(json);
    cJSON *method = root
        ? cJSON_GetObjectItemCaseSensitive(root, "method") : nullptr;
    cJSON *command = root
        ? cJSON_GetObjectItemCaseSensitive(root, "command") : nullptr;
    cJSON *params = root
        ? cJSON_GetObjectItemCaseSensitive(root, "params") : nullptr;
    if (!cJSON_IsString(method) ||
        (strcmp(method->valuestring, "link.invoke") != 0 &&
         strcmp(method->valuestring, "device.invoke") != 0) ||
        !cJSON_IsString(command)) {
        cJSON_Delete(root);
        return -1;
    }
    if (!cJSON_IsObject(params)) {
        params = cJSON_CreateObject();
        if (!params || !cJSON_AddItemToObject(root, "params", params)) {
            cJSON_Delete(params);
            cJSON_Delete(root);
            return -1;
        }
    }
    cJSON_DeleteItemFromObjectCaseSensitive(params, "command");
    if (!cJSON_AddStringToObject(params, "command", command->valuestring)) {
        cJSON_Delete(root);
        return -1;
    }
    char *normalized = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!normalized) {
        return -1;
    }
    result = muse_commands_handle_invoke(normalized, reply, reply_size);
    free(normalized);
    return result;
}

int build_wire_result(const char *command_reply, const char *result_method,
                      char *out, size_t out_size)
{
    cJSON *source = cJSON_Parse(command_reply);
    cJSON *id = source
        ? cJSON_GetObjectItemCaseSensitive(source, "id") : nullptr;
    cJSON *result = source
        ? cJSON_GetObjectItemCaseSensitive(source, "result") : nullptr;
    if (!cJSON_IsString(id) || !cJSON_IsObject(result)) {
        cJSON_Delete(source);
        return -1;
    }

    cJSON *wire = cJSON_CreateObject();
    bool ok = wire &&
              cJSON_AddStringToObject(wire, "method", result_method) &&
              cJSON_AddStringToObject(wire, "id", id->valuestring);
    const char *fields[] = {"ok", "payload"};
    for (const char *field : fields) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(result, field);
        if (!item) {
            continue;
        }
        cJSON *copy = cJSON_Duplicate(item, true);
        if (!copy || !cJSON_AddItemToObject(wire, field, copy)) {
            cJSON_Delete(copy);
            ok = false;
            break;
        }
    }
    /* Upstream wire contract (noise_control.cpp build_result_json): the
     * error goes on the wire as the plain message STRING, not the
     * {code,message} object; code stays device-side for logs. Sending the
     * object made the daemon collapse every failure into one generic code. */
    if (ok) {
        cJSON *error = cJSON_GetObjectItemCaseSensitive(result, "error");
        cJSON *message = cJSON_IsObject(error)
            ? cJSON_GetObjectItemCaseSensitive(error, "message") : nullptr;
        if (cJSON_IsString(message) && message->valuestring) {
            ok = cJSON_AddStringToObject(wire, "error",
                                         message->valuestring);
        } else if (cJSON_IsString(error) && error->valuestring) {
            ok = cJSON_AddStringToObject(wire, "error", error->valuestring);
        }
    }
    if (ok) {
        ok = cJSON_PrintPreallocated(
            wire, out, static_cast<int>(out_size), false);
    }
    cJSON_Delete(wire);
    cJSON_Delete(source);
    return ok ? static_cast<int>(strlen(out)) : -1;
}

/* Per-session uuid for the link.register req id; the server matches its
 * reply by this id (upstream noise_control.cpp make_uuid). Randomness comes
 * from the TRNG-backed bk_rand() (psa_mbedtls platform.c). */
void make_register_id(char *out, size_t out_size)
{
    uint8_t rnd[16];
    if (out == nullptr || out_size < 37) {
        return;
    }
    for (size_t i = 0; i < sizeof(rnd); i += sizeof(int)) {
        int r = bk_rand();
        memcpy(rnd + i, &r, sizeof(r));
    }
    /* bk_rand() early after boot returns a fixed-seed sequence (the same
     * uuid was observed on two separate connections 19:50/20:24), which
     * defeats the per-connection correlation this id exists for. Mix in
     * the uptime and a per-boot connection counter. */
    static uint32_t s_register_seq;
    const uint32_t salt1 = rtos_get_time();
    const uint32_t salt2 = ++s_register_seq * 0x9E3779B1u;
    for (size_t i = 0; i < sizeof(rnd); i += sizeof(uint32_t)) {
        uint32_t v;
        memcpy(&v, rnd + i, sizeof(v));
        v ^= salt1 ^ (salt2 + (uint32_t)i);
        memcpy(rnd + i, &v, sizeof(v));
    }
    rnd[6] = (uint8_t)((rnd[6] & 0x0F) | 0x40); /* version 4 */
    rnd[8] = (uint8_t)((rnd[8] & 0x3F) | 0x80); /* variant 1 */
    snprintf(out, out_size,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
             "%02x%02x%02x%02x%02x%02x",
             rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5], rnd[6], rnd[7],
             rnd[8], rnd[9], rnd[10], rnd[11], rnd[12], rnd[13], rnd[14],
             rnd[15]);
}

/* Match an inbound control message against the pending link.register id and
 * record the server's verdict (upstream noise_control.cpp: "link.register
 * acked" / "link.register rejected: <message>"). Returns true when the
 * message was the register reply. */
bool detect_register_reply(const char *json)
{
    if (s_session.register_state != 0 || s_session.register_id[0] == '\0') {
        return false;
    }
    cJSON *msg = cJSON_Parse(json);
    if (msg == nullptr) {
        return false;
    }
    bool matched = false;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(msg, "id");
    if (cJSON_IsString(id) && id->valuestring != nullptr &&
        strcmp(id->valuestring, s_session.register_id) == 0) {
        matched = true;
        cJSON *failure = cJSON_GetObjectItemCaseSensitive(msg, "error");
        if (failure != nullptr && !cJSON_IsNull(failure) &&
            !cJSON_IsFalse(failure)) {
            cJSON *reason = cJSON_IsObject(failure)
                ? cJSON_GetObjectItemCaseSensitive(failure, "message")
                : failure;
            BK_LOGE(TAG, "link.register rejected: %s\r\n",
                    cJSON_IsString(reason) && reason->valuestring != nullptr
                        ? reason->valuestring : "no reason given");
            s_session.register_state = 2;
        } else {
            s_session.register_state = 1;
            BK_LOGI(TAG, "link.register acked\r\n");
        }
    }
    cJSON_Delete(msg);
    return matched;
}

bool consume_control_bytes(transport client, ClientSession &session,
                           SessionBuffers &buffers, uint8_t *pending,
                           size_t &pending_size, ConstByteSpan bytes)
{
    if (bytes.size() > kControlBodySize - pending_size) {
        return false;
    }
    memcpy(pending + pending_size, bytes.data(), bytes.size());
    pending_size += bytes.size();

    size_t consumed = 0;
    while (pending_size - consumed >= 4) {
        const uint8_t *p = pending + consumed;
        const uint32_t json_size = static_cast<uint32_t>(p[0]) |
                                   (static_cast<uint32_t>(p[1]) << 8) |
                                   (static_cast<uint32_t>(p[2]) << 16) |
                                   (static_cast<uint32_t>(p[3]) << 24);
        if (json_size >= kControlBodySize) {
            return false;
        }
        if (pending_size - consumed - 4 < json_size) {
            break;
        }

        /* Static for the same reason as registration[] above: 2 KB of reply
         * scratch in a nested frame pushed the 12 KB worker stack over its
         * limit during invoke handling. Only ever used on the session
         * worker thread. */
        static char command_reply[1024] __attribute__((section(".psram.bss")));
        static char wire_reply[1024] __attribute__((section(".psram.bss")));
        const uint8_t saved = p[4 + json_size];
        pending[consumed + 4 + json_size] = '\0';
        const char *msg = reinterpret_cast<const char *>(p + 4);
        const bool was_register_reply = detect_register_reply(msg);
        const int command_reply_size =
            handle_invoke(msg, command_reply, sizeof(command_reply));
        if (command_reply_size <= 0 && !was_register_reply) {
            /* Surface any other server chatter instead of swallowing it
             * (heartbeats, config pushes, unexpected errors). */
            BK_LOGW(TAG, "unhandled control message: %.160s\r\n", msg);
        } else if (command_reply_size > 0) {
            BK_LOGI(TAG, "link.invoke handled: %.120s\r\n", msg);
        }
        /* Reply method mirrors the request family: Muse's gateway spells
         * the invoke "device.invoke" and presumably waits for
         * "device.result"; SDK links use link.invoke/link.result. */
        const char *result_method =
            strstr(msg, "device.invoke") != nullptr ? "device.result"
                                                    : "link.result";
        pending[consumed + 4 + json_size] = saved;
        const int reply_size = command_reply_size > 0
            ? build_wire_result(command_reply, result_method, wire_reply,
                                sizeof(wire_reply))
            : -1;
        if (command_reply_size > 0 && reply_size < 0) {
            BK_LOGE(TAG, "link.result build failed: %.120s\r\n",
                    command_reply);
            return false;
        }
        if (reply_size > 0) {
            BK_LOGI(TAG, "link.result sending: %.160s\r\n", wire_reply);
            if (!send_control_json(client, session, buffers, wire_reply,
                                   static_cast<size_t>(reply_size))) {
                BK_LOGE(TAG, "link.result send failed\r\n");
                return false;
            }
        }
        consumed += 4 + json_size;
    }

    if (consumed) {
        memmove(pending, pending + consumed, pending_size - consumed);
        pending_size -= consumed;
    }
    return true;
}

/* One iteration of the inbound loop: waits up to wait_ms for a frame and
 * dispatches it. Returns 1 when a control message was handled, 0 on a
 * benign timeout/non-control frame, or a negative MUSE_SESSION_ERR_* on a
 * fatal fault (the caller returns it directly). */
int poll_inbound_once(transport client, ClientSession &session,
                      SessionBuffers &buffers, uint8_t *pending,
                      size_t &pending_size, HeaderView *headers,
                      size_t header_count, bool &closed, uint32_t wait_ms)
{
    size_t frame_size = 0;
    if (!receive_frame(buffers.ws, kWsPayloadSize, frame_size,
                       wait_ms, closed)) {
        if (s_session.running.load() &&
            (closed || s_session.callback_fault.load())) {
            return MUSE_SESSION_ERR_TRANSPORT;
        }
        return 0;
    }

    auto inbound = session.ProcessInboundWebSocketPayload(
        ConstByteSpan(buffers.ws, frame_size),
        ByteSpan(buffers.transport, kWsPayloadSize),
        ByteSpan(buffers.service, kServiceScratchSize),
        Span<HeaderView>(headers, header_count));
    if (!inbound.ok()) {
        BK_LOGE(TAG, "Noise receive failed: %s\r\n", inbound.status.str());
        return MUSE_SESSION_ERR_PROTOCOL;
    }
    if (inbound.frame_status != InboundFrameStatus::Complete) {
        return 0;
    }

    const auto &decoded = inbound.frame;
    if (decoded.kind == ServiceFrameKind::Request) {
        /* Server-initiated request (Muse's device.invoke is one): we don't
         * serve any yet, but never drop it quietly — a silent fall-through
         * here is exactly what made the display.draw_text timeouts
         * invisible on the device side. */
        const size_t preview = decoded.request.body.size() < 200
                                   ? decoded.request.body.size()
                                   : 200;
        BK_LOGW(TAG,
                "server request: stream=%lld %.*s %.*s body=%u %.*s\r\n",
                (long long)decoded.stream_id,
                (int)decoded.request.verb.size(), decoded.request.verb.data(),
                (int)decoded.request.path.size(), decoded.request.path.data(),
                (unsigned)decoded.request.body.size(),
                (int)preview, (const char *)decoded.request.body.data());
    }
    if (decoded.stream_id != kControlStreamId) {
        BK_LOGW(TAG, "frame on unknown stream %lld kind=%d\r\n",
                (long long)decoded.stream_id, (int)decoded.kind);
        return 0;
    }
    if (decoded.kind == ServiceFrameKind::Response) {
        if (decoded.response.status != 200 ||
            !consume_control_bytes(client, session, buffers, pending,
                                   pending_size, decoded.response.body)) {
            return MUSE_SESSION_ERR_PROTOCOL;
        }
        return 1;
    }
    if (decoded.kind == ServiceFrameKind::BodyChunk) {
        if (!consume_control_bytes(client, session, buffers, pending,
                                   pending_size, decoded.body_chunk.data)) {
            return MUSE_SESSION_ERR_PROTOCOL;
        }
        return 1;
    }
    if (decoded.kind == ServiceFrameKind::Reset) {
        return MUSE_SESSION_ERR_PROTOCOL;
    }
    return 0;
}

int run_noise_session(transport client, SessionBuffers &buffers, bool &closed)
{
    PsaCryptoBackend crypto;
    ClientSession session(crypto);
    set_state(MUSE_SESSION_HANDSHAKING, MUSE_SESSION_OK);
    if (!handshake(client, session, buffers, closed)) {
        return MUSE_SESSION_ERR_PROTOCOL;
    }
    if (!open_control_stream(client, session, buffers)) {
        return MUSE_SESSION_ERR_PROTOCOL;
    }

    uint8_t *pending = buffers.inbound_control;
    size_t pending_size = 0;
    HeaderView headers[8];
    constexpr size_t header_count = sizeof(headers) / sizeof(headers[0]);

    /* link.register follows the upstream esp32/linux contract (req envelope
     * with a per-session uuid), and CONNECTED is declared only after the
     * server acks the registration; previously a rejection was swallowed
     * silently while [6/6] claimed success. */
    /* Static: this buffer lives for the whole connection (the worker frame
     * below stays on the stack for the session's lifetime); on the stack it
     * contributed to a muse_noise stack overflow (PSPLIM UsageFault) when an
     * invoke arrived mid-session. Single worker => single user. */
    static char registration[4096] __attribute__((section(".psram.bss")));
    make_register_id(s_session.register_id, sizeof(s_session.register_id));
    s_session.register_state = 0;
    /* Log the per-connection register uuid so Muse can correlate which of
     * their server-side sockets this physical connection is; a link.invoke
     * written to a stale duplicate socket never reaches us (17:05 timeout
     * case: register acked + pings healthy, zero downstream frames). */
    BK_LOGI(TAG, "link.register id=%s node=%s\r\n", s_session.register_id,
            s_session.node_id);
    const int registration_size = muse_commands_build_register(
        s_session.node_id, s_session.register_id, registration,
        sizeof(registration));
    if (registration_size <= 0 ||
        !send_control_json(client, session, buffers, registration,
                           static_cast<size_t>(registration_size))) {
        return MUSE_SESSION_ERR_PROTOCOL;
    }

    const uint32_t ack_wait_start = rtos_get_time();
    while (s_session.running.load() && s_session.register_state == 0) {
        if ((int32_t)(rtos_get_time() - ack_wait_start) >=
            (int32_t)kRegisterAckTimeoutMs) {
            BK_LOGE(TAG, "link.register ack timeout\r\n");
            return MUSE_SESSION_ERR_PROTOCOL;
        }
        const int poll = poll_inbound_once(client, session, buffers, pending,
                                           pending_size, headers,
                                           header_count, closed,
                                           kEventWaitMs);
        if (poll < 0) {
            return poll;
        }
    }
    if (s_session.register_state != 1) {
        /* The rejection reason was logged by detect_register_reply. */
        return MUSE_SESSION_ERR_PROTOCOL;
    }

    s_session.connected.store(true);
    set_state(MUSE_SESSION_CONNECTED, MUSE_SESSION_OK);

    while (s_session.running.load()) {
        const int poll = poll_inbound_once(client, session, buffers, pending,
                                           pending_size, headers,
                                           header_count, closed,
                                           kEventWaitMs);
        if (poll < 0) {
            return poll;
        }
    }
    return MUSE_SESSION_OK;
}

void close_websocket(transport client, bool &closed)
{
    closed = closed || s_session.callback_closed.load();
    if (!client || closed) {
        return;
    }
    (void)websocket_client_destroy(client);

    uint32_t waited = 0;
    while (!closed && waited < kCloseTimeoutMs) {
        if (s_session.callback_closed.load()) {
            closed = true;
            break;
        }
        SessionEvent event{};
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
        /*
         * Do not reconnect while the old callback may still run.  The worker
         * remains alive and retries waiting; this intentionally favors memory
         * safety over forcing a second WebSocket client into the same context.
         */
        BK_LOGE(TAG, "WebSocket close callback timed out\r\n");
    }
}

int run_connection(SessionBuffers &buffers)
{
    char uri[kHostSize + kNodeIdSize + 48];
    char authorization[kTokenSize + 16];
    if (s_session.vm_id[0] != '\0') {
        snprintf(uri, sizeof(uri), "wss://%s/v1/noise?vm_id=%s",
                 s_session.host, s_session.vm_id);
    } else {
        /* Official contract requires vm_id for edge routing; without it the
         * upgrade is rejected. Fall back to the bare path only if absent. */
        snprintf(uri, sizeof(uri), "wss://%s/v1/noise", s_session.host);
    }
    snprintf(authorization, sizeof(authorization), "Bearer %s",
             s_session.token);

    websocket_client_input_t input{};
    input.uri = uri;
    input.enable_auto_reconnect = false;
    input.rx_retry = 10;
    input.user_context = &s_session;
    input.buffer_size = static_cast<int>(kWsPayloadSize);
    input.authorization = authorization;
    input.ws_event_handler = websocket_event;

    s_session.callback_fault.store(false);
    s_session.callback_closed.store(false);
    free(s_session.partial);
    s_session.partial = nullptr;
    s_session.partial_size = 0;
    s_session.partial_used = 0;
    drain_events();

    transport client = websocket_client_init(&input);
    if (!client) {
        return MUSE_SESSION_ERR_NO_MEMORY;
    }
    if (!client->bk_ssl) {
        (void)websocket_client_destroy(client);
        return MUSE_SESSION_ERR_TRANSPORT;
    }
    client->bk_ssl->cfg.cacert_buf =
        reinterpret_cast<const unsigned char *>(s_session.ca_cert);
    client->bk_ssl->cfg.cacert_bytes =
        static_cast<unsigned int>(s_session.ca_cert_len);
    client->bk_ssl->cfg.skip_common_name = false;
    bool closed = false;
    if (websocket_client_start(client) != BK_OK) {
        (void)websocket_client_destroy(client);
        return MUSE_SESSION_ERR_TRANSPORT;
    }
    if (!wait_for(EventKind::Connected, kConnectTimeoutMs, &closed)) {
        /* The stock websocket client does not expose the HTTP upgrade
         * status, so a 401/403 token rejection is indistinguishable from
         * a transport failure here; it is reported as TRANSPORT and the
         * reconnect loop keeps retrying. */
        close_websocket(client, closed);
        return MUSE_SESSION_ERR_TRANSPORT;
    }
    if (!client->bk_ssl->tls ||
        mbedtls_ssl_get_verify_result(&client->bk_ssl->tls->ssl) != 0U) {
        close_websocket(client, closed);
        return MUSE_SESSION_ERR_TLS_VERIFY;
    }

    int result = run_noise_session(client, buffers, closed);
    s_session.connected.store(false);
    close_websocket(client, closed);
    if (!closed) {
        while (s_session.running.load() && !closed) {
            (void)wait_for(EventKind::Closed, kEventWaitMs, &closed);
        }
    }
    free(s_session.partial);
    s_session.partial = nullptr;
    s_session.partial_size = 0;
    s_session.partial_used = 0;
    drain_events();
    return result;
}

void worker_entry(beken_thread_arg_t)
{
    BK_LOGI(TAG, "noise worker started\r\n");
    SessionBuffers buffers{};
    if (!buffers.allocate()) {
        /* Diagnose which stage failed: the whole set is ~150 KB of SRAM
         * plus 32 KB of PSRAM, so an exhausted SRAM heap lands here with
         * no other trace. */
        BK_LOGE(TAG, "buffer alloc failed: ws=%p transport=%p service=%p "
                "out_service=%p out_envelope=%p control=%p inbound=%p\r\n",
                buffers.ws, buffers.transport, buffers.service,
                buffers.out_service, buffers.out_envelope,
                buffers.control_body, buffers.inbound_control);
        buffers.release();
        set_state(MUSE_SESSION_STOPPED, MUSE_SESSION_ERR_NO_MEMORY);
        s_session.running.store(false);
        rtos_delete_thread(nullptr);
        return;
    }

    uint32_t reconnect_ms = kReconnectMinMs;
    while (s_session.running.load()) {
        set_state(MUSE_SESSION_CONNECTING, MUSE_SESSION_OK);
        const int result = run_connection(buffers);
        s_session.connected.store(false);
        if (!s_session.running.load()) {
            break;
        }
        if (result == MUSE_SESSION_ERR_AUTH) {
            /* Do not retry a bearer the edge has already refused. Stop so
             * the service re-runs fetch_vms and comes back with a fresh
             * token (upstream: ws_auth_failed -> app.c re-fetches). */
            set_state(MUSE_SESSION_STOPPED, MUSE_SESSION_ERR_AUTH);
            break;
        }

        set_state(MUSE_SESSION_RECONNECT_WAIT, result);
        uint32_t delayed = 0;
        while (s_session.running.load() && delayed < reconnect_ms) {
            uint32_t slice = reconnect_ms - delayed;
            if (slice > kEventWaitMs) {
                slice = kEventWaitMs;
            }
            rtos_delay_milliseconds(slice);
            delayed += slice;
        }
        reconnect_ms =
            reconnect_ms < kReconnectMaxMs / 2 ? reconnect_ms * 2
                                               : kReconnectMaxMs;
    }

    buffers.release();
    s_session.connected.store(false);
    set_state(MUSE_SESSION_STOPPED, MUSE_SESSION_OK);
    s_session.running.store(false);
    rtos_delete_thread(nullptr);
}

bool initialize_once()
{
    if (s_session.initialized.load()) {
        return true;
    }
    if (rtos_init_mutex(&s_session.lock) != BK_OK) {
        return false;
    }
    if (rtos_init_queue(&s_session.events, "muse_noise_events",
                        sizeof(SessionEvent), kEventQueueDepth) != BK_OK) {
        rtos_deinit_mutex(&s_session.lock);
        s_session.lock = nullptr;
        return false;
    }
    s_session.initialized.store(true);
    return true;
}

} // namespace

extern "C" int muse_session_start(const muse_session_config_t *config)
{
    if (!config || !valid_node_id(config->node_id) ||
        !valid_bearer(config->vm_token) ||
        !config->ca_cert_pem || config->ca_cert_len < 2 ||
        config->ca_cert_len > MUSE_VM_API_CA_CERT_MAX ||
        config->ca_cert_pem[config->ca_cert_len - 1] != '\0' ||
        (config->noise_host && !valid_host(config->noise_host)) ||
        strlen(config->node_id) >= sizeof(s_session.node_id) ||
        strlen(config->vm_token) >= sizeof(s_session.token) ||
        (config->vm_id && (strpbrk(config->vm_id, "\r\n?#& ") != NULL ||
                           strlen(config->vm_id) >= sizeof(s_session.vm_id)))) {
        return MUSE_SESSION_ERR_ARGUMENT;
    }
    if (!initialize_once()) {
        return MUSE_SESSION_ERR_NO_MEMORY;
    }

    if (s_session.worker && !s_session.running.load()) {
        (void)rtos_thread_join(&s_session.worker);
        s_session.worker = nullptr;
    }
    if (s_session.running.load() || s_session.worker) {
        return MUSE_SESSION_ERR_ARGUMENT;
    }

    const char *host =
        config->noise_host && *config->noise_host ? config->noise_host
                                                  : kDefaultHost;
    if (strlen(host) >= sizeof(s_session.host)) {
        return MUSE_SESSION_ERR_ARGUMENT;
    }

    rtos_lock_mutex(&s_session.lock);
    strcpy(s_session.node_id, config->node_id);
    s_session.vm_id[0] = '\0';
    if (config->vm_id && *config->vm_id) {
        strcpy(s_session.vm_id, config->vm_id);
    }
    strcpy(s_session.host, host);
    strcpy(s_session.token, config->vm_token);
    s_session.ca_cert = config->ca_cert_pem;
    s_session.ca_cert_len = config->ca_cert_len;
    s_session.status_cb = config->status_cb;
    s_session.status_user = config->status_user;
    rtos_unlock_mutex(&s_session.lock);

    s_session.running.store(true);
    if (rtos_create_thread(&s_session.worker, kWorkerPriority, "muse_noise",
                           worker_entry, kWorkerStackSize, 0) != BK_OK) {
        s_session.running.store(false);
        s_session.worker = nullptr;
        return MUSE_SESSION_ERR_NO_MEMORY;
    }
    return MUSE_SESSION_OK;
}

extern "C" void muse_session_stop(void)
{
    if (!s_session.initialized.load()) {
        return;
    }
    s_session.running.store(false);
    queue_event(EventKind::Wake);

    if (s_session.worker && !rtos_is_current_thread(&s_session.worker)) {
        (void)rtos_thread_join(&s_session.worker);
        s_session.worker = nullptr;
        drain_events();
    }
}

extern "C" bool muse_session_is_running(void)
{
    return s_session.running.load();
}
