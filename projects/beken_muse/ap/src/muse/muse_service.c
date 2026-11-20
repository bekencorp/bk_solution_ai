/**
 * @file muse_service.c
 * @brief Worker-owned Muse pairing, network, VM and Noise lifecycle.
 */
#include "muse_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <lwip/sockets.h>
#include <lwip/dns.h>
#include <netdb.h>
#include <driver/aon_rtc.h>

#include <components/log.h>
#include <components/system.h>
#include <os/os.h>

#include "bk_wifi.h"
#include "bk_wifi_types.h"
#if CONFIG_BK_BLE_PROVISIONING
#include "bk_network_provisioning.h"
#endif

#include "muse_ble_pairing.h"
#include "muse_ca_cert.h"
#if CONFIG_MUSE_GADGET_VOICE
#include "muse_chat.h"
#endif
#include "muse_pairing.h"
#include "muse_session.h"
#include "muse_storage.h"
#include "muse_vm_api.h"

#define TAG "muse_service"
#define SERVICE_POLL_MS             250U
#define SERVICE_STOP_WAIT_MS        10000U
#define SERVICE_BACKOFF_MIN_MS      1000U
#define SERVICE_BACKOFF_MAX_MS      30000U
/* This worker performs the HTTPS VM-API exchange and the Noise WebSocket TLS
 * handshake (mbedTLS/PSA x509 chain verification) on its own stack, on top of
 * the ~4.5 KB of credential buffers. 12 KB overflowed once the built-in CA let
 * the handshake actually run; keep this at 24 KB. The thread lives in PSRAM. */
#define SERVICE_WORKER_STACK        24576U
#define ACCESS_TOKEN_SIZE           2048U
#define REFRESH_TOKEN_SIZE          2048U
#define API_URL_SIZE                256U
#define NOISE_HOST_SIZE             128U
#define MUSE_DEFAULT_API_BASE       "https://api.muse.ai"

static beken_mutex_t s_lock;
static beken_thread_t s_worker;
static bool s_initialized;
static bool s_worker_running;
static bool s_stop_requested;
static bool s_clear_requested;
static muse_service_status_t s_status;
static char *s_ca_cert;
static size_t s_ca_cert_len;
/* Pairing retains these pointers, so identity storage must be static. */
static char s_node_id[MUSE_VM_API_DEVICE_ID_MAX];
static char s_device_id[MUSE_VM_API_DEVICE_ID_MAX];
static char s_mac[18];
/* muse_ble_pairing_start() shallow-copies cfg (sdk_token is a pointer), and
 * pairing VM calls also reference the token asynchronously, so it must live in
 * service-owned static storage. Runtime value wins over the Kconfig default. */
static char s_sdk_token[MUSE_VM_API_TOKEN_MAX];
static bool s_sdk_report_attempted;
/* chat 会话连续短命（被 UNAUTHENTICATED 踢）后请求刷新设备令牌。置位后
 * worker 停掉两条 Noise 会话并走 fetch_and_start_session 的强制刷新路径。 */
static bool s_token_refresh_requested;
static bool s_force_token_refresh;
static uint32_t s_auth_failed_ms;

/* Active TLS trust anchor. A CA explicitly installed from an SD card via
 * muse_service_set_ca_cert() wins; otherwise fall back to the public roots
 * compiled into flash (g_muse_builtin_ca_cert), so provisioning reaches the
 * cloud even when /sd0 is not mounted. */
static const char *active_ca_cert(void)
{
    return s_ca_cert != NULL ? s_ca_cert : g_muse_builtin_ca_cert;
}

static size_t active_ca_cert_len(void)
{
    if (s_ca_cert != NULL && s_ca_cert_len > 0U) {
        return s_ca_cert_len;
    }
    /* Includes the trailing NUL, matching the explicit-install convention. */
    return strlen(g_muse_builtin_ca_cert) + 1U;
}

/* ---- Diagnostic-only token provenance helpers (temporary) ------------- */
/* The "s0:" prefix is followed by a compact JWT: header.payload.signature.
 * Decoding the (public, non-secret) middle segment shows iss/aud/scope/exp,
 * which differ between an account access token and a VM-scoped Noise bearer.
 * The signature is never decoded or printed. */

static int diag_b64url_val(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '-') {
        return 62;
    }
    if (c == '_') {
        return 63;
    }
    return -1;
}

/* Decode one unpadded base64url segment into out; returns decoded length. */
static size_t diag_b64url_decode(const char *src, size_t src_len,
                                 unsigned char *out, size_t out_size)
{
    size_t i = 0U;
    size_t used = 0U;

    while (i + 4U <= src_len && used + 3U <= out_size) {
        int v0 = diag_b64url_val(src[i]);
        int v1 = diag_b64url_val(src[i + 1U]);
        int v2 = diag_b64url_val(src[i + 2U]);
        int v3 = diag_b64url_val(src[i + 3U]);
        unsigned int v;
        if (v0 < 0 || v1 < 0 || v2 < 0 || v3 < 0) {
            break;
        }
        v = ((unsigned)v0 << 18) | ((unsigned)v1 << 12) |
            ((unsigned)v2 << 6) | (unsigned)v3;
        out[used++] = (unsigned char)((v >> 16) & 0xFF);
        out[used++] = (unsigned char)((v >> 8) & 0xFF);
        out[used++] = (unsigned char)(v & 0xFF);
        i += 4U;
    }
    if (src_len - i >= 2U && src_len - i < 4U && used + 2U <= out_size) {
        int v0 = diag_b64url_val(src[i]);
        int v1 = diag_b64url_val(src[i + 1U]);
        if (v0 >= 0 && v1 >= 0) {
            out[used++] = (unsigned char)(((unsigned)v0 << 2) |
                                         ((unsigned)v1 >> 4));
            if (src_len - i == 3U) {
                int v2 = diag_b64url_val(src[i + 2U]);
                if (v2 >= 0) {
                    out[used++] = (unsigned char)((((unsigned)v1 & 0x0F) << 4) |
                                                  ((unsigned)v2 >> 2));
                }
            }
        }
    }
    return used;
}

static void diag_log_jwt_claims(const char *label, const char *token)
{
    /* Claims JSON is metadata (iss/aud/scope/exp), not the signing secret.
     * Stack buffer (not static): internal RAM is too tight to spend 1 KB of
     * .bss here; this runs on the 24 KB service worker stack. */
    unsigned char claims[1024];
    const char *p;
    const char *dot1;
    const char *dot2;
    size_t seg_len;
    size_t out_len;

    if (token == NULL || token[0] == '\0') {
        BK_LOGI(TAG, "diag %s: <null/empty>\r\n", label);
        return;
    }
    p = strncmp(token, "s0:", 3U) == 0 ? token + 3U : token;
    dot1 = strchr(p, '.');
    if (dot1 == NULL) {
        BK_LOGI(TAG, "diag %s: not a JWT (len=%u pfx='%.3s')\r\n",
                label, (unsigned)strlen(token), token);
        return;
    }
    dot2 = strchr(dot1 + 1U, '.');
    if (dot2 == NULL) {
        dot2 = p + strlen(p);
    }
    seg_len = (size_t)(dot2 - dot1 - 1U);
    if (seg_len == 0U || seg_len >= (sizeof(claims) * 4U) / 3U) {
        BK_LOGI(TAG, "diag %s: bad payload segment len=%u\r\n",
                label, (unsigned)seg_len);
        return;
    }
    out_len = diag_b64url_decode(dot1 + 1U, seg_len, claims,
                                 sizeof(claims) - 1U);
    claims[out_len] = '\0';
    /* Bound the line so a large blob cannot flood the log. */
    BK_LOGI(TAG, "diag %s claims(%uB): %.700s\r\n",
            label, (unsigned)out_len, (const char *)claims);
}

static void set_state_locked(muse_service_state_t state, int error)
{
    s_status.state = state;
    s_status.last_error = error;
}

static bool deadline_reached(uint32_t deadline)
{
    return deadline != 0U && (int32_t)(rtos_get_time() - deadline) >= 0;
}

static bool network_has_ip(void)
{
    wifi_link_status_t link;
    memset(&link, 0, sizeof(link));
    return bk_wifi_sta_get_link_status(&link) == BK_OK &&
           link.state == WIFI_LINKSTATE_STA_GOT_IP;
}

/* 24-bit identity salt from the libc RNG, seeded from wall clock + tick.
 * Never returns 0: 0 is the "no salt stored" sentinel in the KV slot. */
static uint32_t identity_salt_mint(void)
{
    struct timeval tv;
    uint32_t salt;

    (void)bk_rtc_gettimeofday(&tv, NULL);
    srand(rtos_get_time() ^ (uint32_t)tv.tv_usec ^ (uint32_t)tv.tv_sec);
    salt = ((uint32_t)(rand() & 0xff) << 16) |
           ((uint32_t)(rand() & 0xff) << 8) |
           (uint32_t)(rand() & 0xff);
    return (salt == 0U) ? 1U : salt;
}

static int build_identity(void)
{
    uint8_t mac[6] = {0};
    /* Match upstream identity_init(): identity is derived from the stable
     * WiFi STA MAC, not the randomised BLE address. node_id uses the last
     * three octets; device_id is "hatch-link:" + lower-case colon MAC. */
    if (bk_get_mac(mac, MAC_TYPE_STA) != BK_OK) {
        return MUSE_SERVICE_ERR_IDENTITY;
    }
    /* Identity override: Meta binds a VM to (account, node_id), and a dead
     * VM binding can only be escaped by presenting a different identity. A
     * persisted salt replaces the MAC tail in every derived identifier
     * (advertise name, node_id, device_id); the real WiFi MAC used for
     * association is untouched.
     *
     * The salt must exist from the very first boot: under NEW_MAC_POLICY +
     * RANDOM_MAC_ADDR the SDK's mac_init() randomises the STA MAC tail on
     * EVERY boot (the get_net_info() return value is dropped, and
     * save_net_info() is #if 0'd out), so a MAC-derived identity changes on
     * every reboot and the server sees a brand-new device each time
     * (2026-11-05: cross-reboot 401 deviceRegistrationNotFound). Mint and
     * persist a salt when none is stored yet; muse_service_new_identity()
     * (`muse newid`) stays the only way to change it afterwards. */
    {
        uint32_t salt = 0;
        if (!muse_storage_load_id_salt(&salt) || salt == 0U) {
            salt = identity_salt_mint();
            if (muse_storage_save_id_salt(salt)) {
                BK_LOGI(TAG, "first boot: minted identity salt %06lx\r\n",
                        (unsigned long)(salt & 0xffffffU));
            } else {
                BK_LOGW(TAG, "identity salt persist failed; identity "
                        "changes on every reboot\r\n");
                salt = 0;
            }
        }
        if (salt != 0U) {
            BK_LOGI(TAG, "identity salt %06lx overrides MAC tail "
                    "%02x%02x%02x\r\n", (unsigned long)(salt & 0xffffffU),
                    mac[3], mac[4], mac[5]);
            mac[3] = (uint8_t)(salt >> 16);
            mac[4] = (uint8_t)(salt >> 8);
            mac[5] = (uint8_t)salt;
        }
    }
    (void)snprintf(s_mac, sizeof(s_mac), "%02x:%02x:%02x:%02x:%02x:%02x",
                   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    (void)snprintf(s_node_id, sizeof(s_node_id),
                   "homelink-%02x%02x%02x", mac[3], mac[4], mac[5]);
    (void)snprintf(s_device_id, sizeof(s_device_id), "hatch-link:%s", s_mac);
    return MUSE_SERVICE_OK;
}

/* Runtime-provided SDK token takes precedence; fall back to the compile-time
 * Kconfig default; otherwise NULL (community devices send none). */
static const char *effective_sdk_token(void)
{
    if (s_sdk_token[0] != '\0') {
        return s_sdk_token;
    }
    if (CONFIG_MUSE_GADGET_SDK_TOKEN != NULL &&
        CONFIG_MUSE_GADGET_SDK_TOKEN[0] != '\0') {
        return CONFIG_MUSE_GADGET_SDK_TOKEN;
    }
    return NULL;
}

int muse_service_set_sdk_token(const char *token)
{
    size_t len;

    if (token == NULL || token[0] == '\0') {
        return MUSE_SERVICE_ERR_ARGUMENT;
    }
    if (!s_initialized && muse_service_init() != MUSE_SERVICE_OK) {
        return MUSE_SERVICE_ERR_NOT_INITIALIZED;
    }
    len = strlen(token);
    if (len >= sizeof(s_sdk_token)) {
        return MUSE_SERVICE_ERR_ARGUMENT;
    }
    /* Persist first so the value survives reboot, then update RAM. */
    if (!muse_storage_save_sdk_token(token)) {
        return MUSE_SERVICE_ERR_STORAGE;
    }
    rtos_lock_mutex(&s_lock);
    (void)snprintf(s_sdk_token, sizeof(s_sdk_token), "%s", token);
    rtos_unlock_mutex(&s_lock);
    return MUSE_SERVICE_OK;
}

static void session_status_cb(muse_session_state_t state, int error, void *user)
{
    bool connected = state == MUSE_SESSION_CONNECTED;
    (void)user;
    if (!s_initialized) {
        return;
    }
    rtos_lock_mutex(&s_lock);
    if (s_worker_running && !s_stop_requested) {
        bool was_connected = s_status.noise_connected;
        s_status.noise_connected = connected;
        if (connected) {
            s_auth_failed_ms = 0U;
            BK_LOGI(TAG, "MUSE ==> [6/6] Noise up + auth_ok (cloud ready)\r\n");
            set_state_locked(MUSE_SERVICE_CONNECTED, 0);
        } else if (!s_status.network_ready) {
            set_state_locked(MUSE_SERVICE_WAITING_FOR_NETWORK, 0);
        } else {
            if (error == MUSE_SESSION_ERR_AUTH) {
                s_auth_failed_ms = rtos_get_time();
            }
            if (was_connected) {
                BK_LOGW(TAG, "MUSE ==> [6/6] Noise dropped (error=%d)\r\n",
                        error);
            }
            /* 状态页的"出错了"只留给不会自愈的失败：凭据被拒、证书校验
             * 失败、内存不足。传输层失败、对端 502（VM 尚未唤醒）等都由
             * worker 按退避自动重连，期间显示"连接中"——否则开机等 VM
             * 就绪的那几分钟里，状态页会每隔几秒闪一次"出错了 / 请清除
             * 配对后重试"（field 2026-11-16）。 */
            const bool fatal = (error == MUSE_SESSION_ERR_AUTH ||
                                error == MUSE_SESSION_ERR_TLS_VERIFY ||
                                error == MUSE_SESSION_ERR_NO_MEMORY);
            set_state_locked(fatal ? MUSE_SERVICE_ERROR
                                   : MUSE_SERVICE_CONNECTING, error);
        }
    }
    rtos_unlock_mutex(&s_lock);
}

static void stop_transports(void)
{
#if CONFIG_MUSE_GADGET_VOICE
    muse_chat_stop();
#endif
    if (muse_session_is_running()) {
        muse_session_stop();
    }
    if (muse_ble_pairing_is_started()) {
        (void)muse_ble_pairing_stop();
    }
}

static int start_pairing_transport(void)
{
    muse_ble_pairing_config_t config;

#if CONFIG_BK_BLE_PROVISIONING
    /* Both implementations own singleton DM-GATT/GAP callbacks. */
    bk_ble_provisioning_deinit();
#endif
    memset(&config, 0, sizeof(config));
    config.node_id = s_node_id;
    config.device_id = s_device_id;
    config.mac = s_mac;
    config.firmware_version = "4.0.1";
    config.sdk_token = effective_sdk_token();
    config.model = "hatch_link";
    return muse_ble_pairing_start(&config);
}

static int load_credentials(char access[ACCESS_TOKEN_SIZE],
                            char refresh[REFRESH_TOKEN_SIZE],
                            char api_url[API_URL_SIZE],
                            char noise_host[NOISE_HOST_SIZE])
{
    if (!muse_storage_is_setup() ||
        !muse_storage_read_field(MUSE_STORAGE_ACCESS_TOKEN,
                                 access, ACCESS_TOKEN_SIZE) ||
        !muse_storage_read_field(MUSE_STORAGE_REFRESH_TOKEN,
                                 refresh, REFRESH_TOKEN_SIZE) ||
        !muse_storage_read_field(MUSE_STORAGE_API_URL_V2,
                                 api_url, API_URL_SIZE) ||
        !muse_storage_read_field(MUSE_STORAGE_NOISE_HOST,
                                 noise_host, NOISE_HOST_SIZE) ||
        access[0] == '\0' || refresh[0] == '\0') {
        return MUSE_SERVICE_ERR_STORAGE;
    }
    /* api_url_v2/noise_host are optional in provision_v2; fall back to the
     * upstream default API base, and let fetch_vms supply the VM host. */
    if (api_url[0] == '\0') {
        (void)snprintf(api_url, API_URL_SIZE, "%s", MUSE_DEFAULT_API_BASE);
    }
    return MUSE_SERVICE_OK;
}

static int restore_wifi_from_storage(void)
{
    char ssid[33];
    char password[65];
    unsigned int channel = 0;
    bool hidden = false;

    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    if (!muse_storage_read_field(MUSE_STORAGE_SSID, ssid, sizeof(ssid)) ||
        !muse_storage_read_field(MUSE_STORAGE_PASSWORD, password,
                                 sizeof(password)) ||
        ssid[0] == '\0') {
        return MUSE_SERVICE_OK;
    }
    (void)muse_storage_read_wifi(&channel, &hidden);
    return muse_service_apply_wifi(ssid, password, channel, hidden);
}

/* ---- SNTP wall-clock sync (stage 3/6) --------------------------------- */
/* mbedTLS X.509 validity checks compare cert notBefore/notAfter against
 * "now". The AON RTC free-runs from power-on (observed epoch values mapped
 * to year 29056), so without a network sync every certificate looks EXPIRED.
 * NTP over UDP/123 to domestic servers needs no special egress. */

#define NTP_PORT                123U
#define NTP_TIMEOUT_MS          800U
#define NTP_EPOCH_OFFSET        2208988800ULL  /* seconds 1900 -> 1970 */
/* Reject timestamps outside [2024-01-01, 2100-01-01] as bogus. */
#define NTP_MIN_VALID_EPOCH     1704067200ULL
#define NTP_MAX_VALID_EPOCH     4102444800ULL

/* Numeric endpoints only: getaddrinfo has no timeout on this lwIP, and a
 * flaky DNS path once blocked the whole provisioning pipeline inside this
 * stage. With UDP/123 and an 800 ms receive timeout the entire stage costs
 * at most ~2 s; clock sync is best-effort diagnostics, never worth stalling
 * provisioning for. */
static const char *const ntp_servers[] = {
    "203.107.6.88",   /* ntp.aliyun.com */
    "210.72.145.44",  /* ntp.ntsc.ac.cn */
};

static bool ntp_probe_server(const char *host, uint64_t *out_epoch)
{
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    int sock = -1;
    bool synced = false;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host, NULL, &hints, &result) != 0 || result == NULL) {
        return false;
    }
    for (rp = result; rp != NULL && !synced; rp = rp->ai_next) {
        uint8_t packet[48];
        struct timeval tv;
        ssize_t len;

        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sock < 0) {
            continue;
        }
        tv.tv_sec = (time_t)(NTP_TIMEOUT_MS / 1000U);
        tv.tv_usec = (suseconds_t)((NTP_TIMEOUT_MS % 1000U) * 1000U);
        (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        (void)setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        memset(packet, 0, sizeof(packet));
        packet[0] = 0x23U; /* LI=0, VN=4, Mode=3 (client) */
        ((struct sockaddr_in *)rp->ai_addr)->sin_port = htons(NTP_PORT);
        if (sendto(sock, packet, sizeof(packet), 0,
                   rp->ai_addr, rp->ai_addrlen) == (ssize_t)sizeof(packet)) {
            len = recv(sock, packet, sizeof(packet), 0);
            if (len >= (ssize_t)sizeof(packet)) {
                uint32_t secs = ((uint32_t)packet[40] << 24) |
                                ((uint32_t)packet[41] << 16) |
                                ((uint32_t)packet[42] << 8) |
                                (uint32_t)packet[43];
                uint64_t epoch = (uint64_t)secs;
                if (epoch >= NTP_EPOCH_OFFSET + NTP_MIN_VALID_EPOCH &&
                    epoch <= NTP_EPOCH_OFFSET + NTP_MAX_VALID_EPOCH) {
                    *out_epoch = epoch - NTP_EPOCH_OFFSET;
                    synced = true;
                }
            }
        }
        closesocket(sock);
        sock = -1;
    }
    freeaddrinfo(result);
    return synced;
}

/* Best effort: try each server once; on success apply settimeofday so the
 * RTC offset in iot_rtc.c makes gettimeofday()/time() return real time. */
static void sync_wall_clock_ntp(void)
{
    size_t i;

    /* Already sane? Skip (e.g. after a previous sync this boot). */
    time_t now = time(NULL);
    if ((uint64_t)now >= NTP_MIN_VALID_EPOCH &&
        (uint64_t)now <= NTP_MAX_VALID_EPOCH) {
        BK_LOGI(TAG, "MUSE ==> [3/6] NTP: clock already sane (epoch=%llu)\r\n",
                (unsigned long long)now);
        return;
    }
    BK_LOGI(TAG, "MUSE ==> [3/6] NTP: syncing wall clock (now=%llu)...\r\n",
            (unsigned long long)now);
    for (i = 0U; i < sizeof(ntp_servers) / sizeof(ntp_servers[0]); ++i) {
        uint64_t epoch = 0ULL;
        if (ntp_probe_server(ntp_servers[i], &epoch)) {
            struct timeval tv;
            struct tm *tm;
            time_t synced = (time_t)epoch;
            tv.tv_sec = synced;
            tv.tv_usec = 0;
            /* Platform time base is bk_rtc_settimeofday (s_boot_time_us);
             * libc settimeofday is not linked in this project. */
            if (bk_rtc_settimeofday(&tv, NULL) != BK_OK) {
                BK_LOGW(TAG, "MUSE ==> [3/6] NTP: RTC set failed\r\n");
                return;
            }
            tm = gmtime(&synced);
            BK_LOGI(TAG,
                    "MUSE ==> [3/6] NTP: synced via %s -> %04d-%02d-%02d %02d:%02d:%02d UTC\r\n",
                    ntp_servers[i],
                    tm != NULL ? tm->tm_year + 1900 : 0,
                    tm != NULL ? tm->tm_mon + 1 : 0,
                    tm != NULL ? tm->tm_mday : 0,
                    tm != NULL ? tm->tm_hour : 0,
                    tm != NULL ? tm->tm_min : 0,
                    tm != NULL ? tm->tm_sec : 0);
            return;
        }
        BK_LOGW(TAG, "MUSE ==> [3/6] NTP: %s no reply\r\n", ntp_servers[i]);
    }
    BK_LOGW(TAG, "MUSE ==> [3/6] NTP: all servers failed; TLS time checks "
            "will use free-running RTC (certs may look EXPIRED)\r\n");
}

static int fetch_and_start_session(void)
{
    char access[ACCESS_TOKEN_SIZE];
    char refresh[REFRESH_TOKEN_SIZE];
    char api_url[API_URL_SIZE];
    char noise_host[NOISE_HOST_SIZE];
    muse_vm_api_tls_t tls;
    muse_vm_info_t vm;
    muse_session_config_t session;
    muse_vm_device_tokens_t *tokens = NULL;
    muse_vm_api_err_t api_result;
    int result;

    memset(access, 0, sizeof(access));
    memset(refresh, 0, sizeof(refresh));
    memset(api_url, 0, sizeof(api_url));
    memset(noise_host, 0, sizeof(noise_host));
    memset(&vm, 0, sizeof(vm));

    result = load_credentials(access, refresh, api_url, noise_host);
    if (result != MUSE_SERVICE_OK) {
        goto done;
    }
    /* The CA cannot change while the worker runs, so this pointer is stable.
     * Use the built-in public roots when no SD-card CA was installed. */
    memset(&tls, 0, sizeof(tls));
    tls.ca_cert_pem = active_ca_cert();
    tls.ca_cert_len = active_ca_cert_len();
    tls.timeout_ms = 10000U;
    BK_LOGI(TAG, "using %s CA (%u bytes)\r\n",
            s_ca_cert != NULL ? "SD-card" : "built-in",
            (unsigned)active_ca_cert_len());
    /* Stage 3/6: the X.509 validity window is meaningless until the wall
     * clock is sane; sync before any TLS handshake. */
    sync_wall_clock_ntp();
    /* Diagnostic: which DNS servers will the REST lookups below go to? The
     * hotspot hands out either the gateway (queries land in the proxy,
     * fake-ip answers instantly) or an external resolver (queries bypass
     * the proxy and can black-hole); identical proxy configs have produced
     * both. Knowing which one this run got settles "environment vs stack". */
    for (uint8_t i = 0; i < DNS_MAX_SERVERS; ++i) {
        const ip_addr_t *dns = dns_getserver(i);
        BK_LOGI(TAG, "diag dns server[%u] = %s\r\n", (unsigned)i,
                dns != NULL ? ipaddr_ntoa(dns) : "(null)");
    }
    /* Upstream (esp32 app.c, linux service.py) reports the SDK token in a
     * refresh body once per boot BEFORE fetch_vms: "The SDK token reaches
     * Muse only in refresh bodies until apps forward it at mint". Without
     * this report the gadget is unknown to the Noise admission layer, which
     * is a plausible cause of the edge 401 even while fetch_vms succeeds.
     * Attempted once per boot (linux: once per process, never retried) —
     * every successful report rotates the token pair server-side, and
     * re-reporting in a persistent 401 loop churns the VM binding once a
     * minute. On failure keep the stored tokens and continue, exactly like
     * upstream ("keeping the pairing"). */
    if (effective_sdk_token() != NULL &&
        (!s_sdk_report_attempted || s_force_token_refresh)) {
        s_sdk_report_attempted = true;
        s_force_token_refresh = false;
        tokens = (muse_vm_device_tokens_t *)calloc(1U, sizeof(*tokens));
        if (tokens != NULL) {
            /* Upstream sends the node_id ("homelink-xxx") as device_id in
             * the refresh body (esp32 app.c, linux service.py); the refresh
             * token is bound to it. Sending s_device_id here earned
             * "Invalid or expired refresh token" 401s. */
            api_result = muse_vm_api_refresh_device_token(
                &tls, api_url, access, refresh, s_node_id,
                effective_sdk_token(), tokens);
            if (api_result == MUSE_VM_API_OK) {
                BK_LOGI(TAG, "MUSE ==> [4/6] SDK token reported; device "
                        "tokens rotated\r\n");
                if (!muse_storage_update_tokens(tokens->access_token,
                                                tokens->refresh_token)) {
                    result = MUSE_SERVICE_ERR_STORAGE;
                    goto done;
                }
                (void)snprintf(access, sizeof(access), "%s",
                               tokens->access_token);
                (void)snprintf(refresh, sizeof(refresh), "%s",
                               tokens->refresh_token);
            } else {
                BK_LOGW(TAG, "MUSE ==> [4/6] SDK-token report refresh failed "
                        "(err=%d); keeping stored tokens\r\n",
                        (int)api_result);
            }
            memset(tokens, 0, sizeof(*tokens));
            free(tokens);
            tokens = NULL;
        }
    }
    BK_LOGI(TAG, "MUSE ==> [4/6] REST: fetching VM assignment (TLS + GET)\r\n");
    api_result = muse_vm_api_fetch_vms(&tls, api_url, access, s_node_id, &vm);
    if (api_result == MUSE_VM_API_ERR_UNAUTHORIZED) {
        BK_LOGW(TAG, "MUSE ==> [4/6] VM fetch 401, refreshing device token\r\n");
        tokens = (muse_vm_device_tokens_t *)calloc(1U, sizeof(*tokens));
        if (tokens == NULL) {
            result = MUSE_SERVICE_ERR_WORKER;
            goto done;
        }
        api_result = muse_vm_api_refresh_device_token(
            &tls, api_url, access, refresh, s_node_id,
            effective_sdk_token(), tokens);
        if (api_result != MUSE_VM_API_OK) {
            BK_LOGE(TAG, "MUSE ==> [4/6] token refresh failed (err=%d)\r\n",
                    (int)api_result);
            result = (int)api_result;
            goto done;
        }
        if (!muse_storage_update_tokens(tokens->access_token,
                                        tokens->refresh_token)) {
            result = MUSE_SERVICE_ERR_STORAGE;
            goto done;
        }
        (void)snprintf(access, sizeof(access), "%s", tokens->access_token);
        (void)snprintf(refresh, sizeof(refresh), "%s", tokens->refresh_token);
        memset(tokens, 0, sizeof(*tokens));
        free(tokens);
        tokens = NULL;
        api_result = muse_vm_api_fetch_vms(&tls, api_url, access,
                                           s_node_id, &vm);
    }
    if (api_result != MUSE_VM_API_OK) {
        BK_LOGE(TAG, "MUSE ==> [4/6] VM fetch failed (err=%d)\r\n",
                (int)api_result);
        result = (int)api_result;
        goto done;
    }
    BK_LOGI(TAG, "MUSE ==> [4/6] VM assignment + token OK\r\n");

    memset(&session, 0, sizeof(session));
    session.node_id = s_node_id;
    session.vm_token = vm.vm_auth_token;
    session.vm_id = vm.vm_id[0] != '\0' ? vm.vm_id : NULL;
    /* Upstream (esp32 noise_control.cpp, linux link_client) always dials the
     * provision_v2 noise_host (default hatch.metaaivm.com) and routes on the
     * ?vm_id= query parameter; the vm_ws_url host is only ever used to derive
     * a missing vm_id, never as a dial target. An earlier local change that
     * preferred vm_host was wrong: those per-VM names are not publicly
     * resolvable and the edge 401 happens on the gateway path anyway. */
    session.noise_host = noise_host;
    session.ca_cert_pem = active_ca_cert();
    session.ca_cert_len = active_ca_cert_len();
    session.status_cb = session_status_cb;

    /* Diagnostic-only: is the Noise bearer the same credential as the REST
     * access token (wrong -> account token), or a distinct VM bearer? Also
     * compare the endpoint the REST API returned against what we actually
     * dial, then dump both JWTs' public claims. No secrets are printed. */
    {
        bool same_token = strcmp(access, vm.vm_auth_token) == 0;
        bool host_override = noise_host[0] != '\0' &&
                             strcmp(noise_host, vm.vm_host) != 0;
        BK_LOGI(TAG,
                "diag token: access(len=%u) vm_bearer(len=%u) identical=%d\r\n",
                (unsigned)strlen(access),
                (unsigned)strlen(vm.vm_auth_token),
                (int)same_token);
        BK_LOGI(TAG,
                "diag host: rest_returned='%s' effective='%s' overridden=%d\r\n",
                vm.vm_host[0] != '\0' ? vm.vm_host : "(none)",
                session.noise_host, (int)host_override);
        BK_LOGI(TAG, "diag vm_id='%s'\r\n",
                vm.vm_id[0] != '\0' ? vm.vm_id : "(none)");
        diag_log_jwt_claims("access_token", access);
        diag_log_jwt_claims("vm_bearer   ", vm.vm_auth_token);
        /* Debug-only: full bearer so the identical upgrade can be replayed
         * from a PC (curl) to isolate header-set vs server-side rejection.
         * Remove before production builds. */
        BK_LOGW(TAG, "diag vm_bearer_full=%s\r\n", vm.vm_auth_token);
    }

    BK_LOGI(TAG, "MUSE ==> [5/6] Noise: connecting wss://%s/v1/noise\r\n",
            session.noise_host);
    result = muse_session_start(&session);
    if (result != MUSE_SERVICE_OK) {
        BK_LOGE(TAG, "MUSE ==> [5/6] Noise session start failed (err=%d)\r\n",
                result);
    }
#if CONFIG_MUSE_GADGET_VOICE
    if (result == MUSE_SERVICE_OK) {
        /* Stagger the chat connect from the noise connect above.
         * muse_session_start returns as soon as its worker thread is
         * spawned (the dial happens inside the worker), so calling
         * muse_chat_start immediately fires two SYNs to the same host
         * in the same second. Field: on the Meta 31.13.87.x/157.240.x
         * range the GFW black-holes such bursts far more aggressively
         * than single dials (6/6 double-SYN attempts timed out while
         * every single-SYN REST dial succeeded in the same window).
         * 4 s puts the second SYN well outside the burst-detection
         * window; both workers' retry loops then stay phase-shifted
         * by the same amount. */
        rtos_delay_milliseconds(4000);

        /* The chat connection mirrors the control session's credentials and
         * runs its own reconnect loop; it carries only /chat/stream +
         * /chat/subscribe (no link.register), so the conversation is the
         * gadget's own and assistant replies fan out to its subscription. */
        muse_chat_config_t chat;
        memset(&chat, 0, sizeof(chat));
        chat.noise_host = session.noise_host;
        chat.vm_id = session.vm_id;
        chat.vm_token = session.vm_token;
        chat.ca_cert_pem = session.ca_cert_pem;
        chat.ca_cert_len = session.ca_cert_len;
        if (muse_chat_start(&chat) != 0) {
            BK_LOGW(TAG, "MUSE ==> [5/6] chat session start failed\r\n");
        }
    }
#endif

done:
    /* Credentials and VM tokens are intentionally never logged. */
    memset(access, 0, sizeof(access));
    memset(refresh, 0, sizeof(refresh));
    memset(&vm, 0, sizeof(vm));
    if (tokens != NULL) {
        memset(tokens, 0, sizeof(*tokens));
        free(tokens);
    }
    return result;
}

static void service_worker(beken_thread_arg_t arg)
{
    uint32_t retry_deadline = 0U;
    uint32_t backoff_ms = SERVICE_BACKOFF_MIN_MS;
    bool wifi_restore_attempted = false;
    (void)arg;

    for (;;) {
        bool stopping;
        bool clearing;
        bool paired;
        bool ready;
        bool refresh_tokens;
        int result;

        rtos_lock_mutex(&s_lock);
        stopping = s_stop_requested;
        clearing = s_clear_requested;
        paired = s_status.paired;
        refresh_tokens = s_token_refresh_requested;
        s_token_refresh_requested = false;
        rtos_unlock_mutex(&s_lock);

        if (stopping) {
            break;
        }
        if (clearing) {
            stop_transports();
            muse_pairing_reset();
            result = muse_storage_clear() ? MUSE_SERVICE_OK
                                          : MUSE_SERVICE_ERR_STORAGE;
            rtos_lock_mutex(&s_lock);
            s_status.paired = false;
            s_status.noise_connected = false;
            s_clear_requested = false;
            set_state_locked(MUSE_SERVICE_WAITING_FOR_PAIRING, result);
            rtos_unlock_mutex(&s_lock);
            retry_deadline = 0U;
            backoff_ms = SERVICE_BACKOFF_MIN_MS;
            wifi_restore_attempted = false;
            continue;
        }

        if (refresh_tokens && paired) {
            /* chat 连续短命：设备侧 Noise 密钥已被浏览器验证流程替换，
             * 重连永远被踢。停掉两条会话并强制重报 SDK token（服务端会
             * 轮换设备令牌），随后正常路径重新 fetch_vms 并建链。 */
            BK_LOGW(TAG, "chat requested a token refresh; rebuilding both "
                    "Noise sessions\r\n");
#if CONFIG_MUSE_GADGET_VOICE
            muse_chat_stop();
#endif
            if (muse_session_is_running()) {
                muse_session_stop();
            }
            s_force_token_refresh = true;
            s_auth_failed_ms = 0U;
            retry_deadline = 0U;
            backoff_ms = SERVICE_BACKOFF_MIN_MS;
            continue;
        }

        if (!paired) {
            if (!muse_ble_pairing_is_started() &&
                (retry_deadline == 0U || deadline_reached(retry_deadline))) {
                result = start_pairing_transport();
                if (result != MUSE_BLE_PAIRING_OK) {
                    rtos_lock_mutex(&s_lock);
                    set_state_locked(MUSE_SERVICE_ERROR, result);
                    rtos_unlock_mutex(&s_lock);
                    retry_deadline = rtos_get_time() + backoff_ms;
                    if (backoff_ms < SERVICE_BACKOFF_MAX_MS / 2U) {
                        backoff_ms *= 2U;
                    } else {
                        backoff_ms = SERVICE_BACKOFF_MAX_MS;
                    }
                }
            }
            rtos_delay_milliseconds(SERVICE_POLL_MS);
            continue;
        }

        /* During asynchronous provision_v2 keep BLE alive so the pairing
         * worker can push wifi_connected / auth_ok. Once it requests teardown
         * (after auth_ok), run the full GATT/GAP stop here. */
        if (muse_ble_pairing_is_started()) {
            if (muse_ble_pairing_teardown_requested()) {
                (void)muse_ble_pairing_stop();
            }
            /* else: provisioning in progress or waiting for it; leave up. */
        }
        ready = network_has_ip();
        if (!ready && !wifi_restore_attempted) {
            result = restore_wifi_from_storage();
            wifi_restore_attempted = true;
            if (result != MUSE_SERVICE_OK) {
                rtos_lock_mutex(&s_lock);
                set_state_locked(MUSE_SERVICE_ERROR, result);
                rtos_unlock_mutex(&s_lock);
            }
        }
        muse_service_on_network_changed(ready);
        if (!ready) {
#if CONFIG_MUSE_GADGET_VOICE
            muse_chat_stop();
#endif
            if (muse_session_is_running()) {
                muse_session_stop();
            }
            retry_deadline = 0U;
            backoff_ms = SERVICE_BACKOFF_MIN_MS;
            rtos_delay_milliseconds(SERVICE_POLL_MS);
            continue;
        }

        /* Upstream paces auth failures with a separate 60 s cap
         * (RECONNECT_AUTH_FAILED_MS in esp32 noise_control.cpp); hot-looping
         * credential fetches after a 401 churns server-side token state. */
        bool auth_backoff_done =
            s_auth_failed_ms == 0U ||
            (int32_t)(rtos_get_time() - s_auth_failed_ms) >= 60000;
        if (auth_backoff_done && !muse_session_is_running() &&
            (retry_deadline == 0U || deadline_reached(retry_deadline))) {
            result = fetch_and_start_session();
            if (result == MUSE_SESSION_OK) {
                retry_deadline = 0U;
                backoff_ms = SERVICE_BACKOFF_MIN_MS;
            } else {
                rtos_lock_mutex(&s_lock);
                s_status.noise_connected = false;
                set_state_locked(MUSE_SERVICE_ERROR, result);
                rtos_unlock_mutex(&s_lock);
                retry_deadline = rtos_get_time() + backoff_ms;
                if (backoff_ms < SERVICE_BACKOFF_MAX_MS / 2U) {
                    backoff_ms *= 2U;
                } else {
                    backoff_ms = SERVICE_BACKOFF_MAX_MS;
                }
            }
        }
        rtos_delay_milliseconds(SERVICE_POLL_MS);
    }

    stop_transports();
    muse_pairing_reset();
    rtos_lock_mutex(&s_lock);
    s_status.network_ready = false;
    s_status.noise_connected = false;
    set_state_locked(MUSE_SERVICE_STOPPED, 0);
    s_worker_running = false;
    s_worker = NULL;
    rtos_unlock_mutex(&s_lock);
    rtos_delete_thread(NULL);
}

int muse_service_init(void)
{
    if (s_initialized) {
        return MUSE_SERVICE_OK;
    }
    if (rtos_init_mutex(&s_lock) != BK_OK) {
        return MUSE_SERVICE_ERR_WORKER;
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = MUSE_SERVICE_STOPPED;
    s_status.paired = muse_storage_is_setup();
    /* Restore the runtime SDK token if one was saved via `muse token`. */
    (void)muse_storage_load_sdk_token(s_sdk_token, sizeof(s_sdk_token));
    muse_pairing_init();
    s_initialized = true;
    return MUSE_SERVICE_OK;
}

int muse_service_set_ca_cert(const char *pem, size_t len)
{
    size_t pem_len;
    char *copy;
    if (pem == NULL) {
        return MUSE_SERVICE_ERR_ARGUMENT;
    }
    if (!s_initialized && muse_service_init() != MUSE_SERVICE_OK) {
        return MUSE_SERVICE_ERR_NOT_INITIALIZED;
    }
    if (len == 0U) {
        for (pem_len = 0U;
             pem_len < MUSE_VM_API_CA_CERT_MAX && pem[pem_len] != '\0';
             ++pem_len) {
        }
        if (pem_len == MUSE_VM_API_CA_CERT_MAX) {
            return MUSE_SERVICE_ERR_ARGUMENT;
        }
    } else {
        if (len > MUSE_VM_API_CA_CERT_MAX) {
            return MUSE_SERVICE_ERR_ARGUMENT;
        }
        pem_len = len;
    }
    if (pem_len > 0U && pem[pem_len - 1U] == '\0') {
        pem_len--;
    }
    if (pem_len == 0U || pem_len + 1U > MUSE_VM_API_CA_CERT_MAX) {
        return MUSE_SERVICE_ERR_ARGUMENT;
    }
    copy = (char *)malloc(pem_len + 1U);
    if (copy == NULL) {
        return MUSE_SERVICE_ERR_WORKER;
    }
    memcpy(copy, pem, pem_len);
    copy[pem_len] = '\0';
    rtos_lock_mutex(&s_lock);
    if (s_worker_running) {
        rtos_unlock_mutex(&s_lock);
        free(copy);
        return MUSE_SERVICE_ERR_BUSY;
    }
    free(s_ca_cert);
    s_ca_cert = copy;
    s_ca_cert_len = pem_len + 1U;
    rtos_unlock_mutex(&s_lock);
    return MUSE_SERVICE_OK;
}

int muse_service_apply_wifi(const char *ssid, const char *password,
                            unsigned int channel, bool hidden)
{
    wifi_sta_config_t config;
    size_t ssid_len;
    size_t password_len;
    bk_err_t result;

    if (ssid == NULL || ssid[0] == '\0') {
        return MUSE_SERVICE_ERR_ARGUMENT;
    }
    if (password == NULL) {
        password = "";
    }
    ssid_len = strlen(ssid);
    password_len = strlen(password);
    if (ssid_len >= sizeof(config.ssid) ||
        password_len >= sizeof(config.password) || channel > 14U) {
        return MUSE_SERVICE_ERR_ARGUMENT;
    }
    memset(&config, 0, sizeof(config));
    memcpy(config.ssid, ssid, ssid_len + 1U);
    memcpy(config.password, password, password_len + 1U);
    config.channel = (uint8_t)channel;
    config.auto_reconnect_count = 0;
    config.disable_auto_reconnect_after_disconnect = false;
    (void)hidden; /* BK7259 scans hidden SSIDs by the explicit SSID value. */

    (void)bk_wifi_sta_stop();
    BK_LOGI(TAG, "MUSE ==> [1/6] Wi-Fi: joining SSID=%s ch=%u\r\n",
            ssid, (unsigned)channel);
    result = bk_wifi_sta_set_config(&config);
    if (result == BK_OK) {
        result = bk_wifi_sta_start();
    }
    return result == BK_OK ? MUSE_SERVICE_OK : MUSE_SERVICE_ERR_WIFI;
}

int muse_service_start(void)
{
    int identity_result;
    bool paired;
    if (!s_initialized && muse_service_init() != MUSE_SERVICE_OK) {
        return MUSE_SERVICE_ERR_NOT_INITIALIZED;
    }
    identity_result = build_identity();
    if (identity_result != MUSE_SERVICE_OK) {
        return identity_result;
    }
    paired = muse_storage_is_setup();
    rtos_lock_mutex(&s_lock);
    if (s_worker_running) {
        rtos_unlock_mutex(&s_lock);
        return MUSE_SERVICE_OK;
    }
    s_stop_requested = false;
    s_clear_requested = false;
    s_status.paired = paired;
    set_state_locked(s_status.paired ? MUSE_SERVICE_WAITING_FOR_NETWORK
                                    : MUSE_SERVICE_WAITING_FOR_PAIRING, 0);
    s_worker_running = true;
    rtos_unlock_mutex(&s_lock);
    if (rtos_create_thread(&s_worker, 5, "muse_service", service_worker,
                           SERVICE_WORKER_STACK, NULL) != BK_OK) {
        rtos_lock_mutex(&s_lock);
        s_worker_running = false;
        set_state_locked(MUSE_SERVICE_ERROR, MUSE_SERVICE_ERR_WORKER);
        rtos_unlock_mutex(&s_lock);
        return MUSE_SERVICE_ERR_WORKER;
    }
    BK_LOGI(TAG, "lifecycle worker started\r\n");
    return MUSE_SERVICE_OK;
}

int muse_service_stop(void)
{
    uint32_t waited = 0U;
    bool running;
    if (!s_initialized) {
        return MUSE_SERVICE_OK;
    }
    rtos_lock_mutex(&s_lock);
    s_stop_requested = true;
    rtos_unlock_mutex(&s_lock);
    do {
        rtos_lock_mutex(&s_lock);
        running = s_worker_running;
        rtos_unlock_mutex(&s_lock);
        if (!running) {
            return MUSE_SERVICE_OK;
        }
        rtos_delay_milliseconds(10U);
        waited += 10U;
    } while (waited < SERVICE_STOP_WAIT_MS);
    return MUSE_SERVICE_ERR_BUSY;
}

int muse_service_new_identity(uint32_t *out_salt)
{
    uint32_t salt;
    if (!s_initialized && muse_service_init() != MUSE_SERVICE_OK) {
        return MUSE_SERVICE_ERR_NOT_INITIALIZED;
    }
    /* Identity is rebuilt in muse_service_start(); stop so the next start
     * re-advertises under the new name. */
    (void)muse_service_stop();
    salt = identity_salt_mint();
    if (!muse_storage_save_id_salt(salt)) {
        return MUSE_SERVICE_ERR_STORAGE;
    }
    /* The old pairing is bound to the old identity; force re-pairing. */
    if (muse_service_clear_pairing() != MUSE_SERVICE_OK) {
        return MUSE_SERVICE_ERR_STORAGE;
    }
    if (out_salt != NULL) {
        *out_salt = salt;
    }
    return MUSE_SERVICE_OK;
}

int muse_service_clear_pairing(void)
{
    uint32_t waited = 0U;
    bool pending;
    bool running;
    int result;
    if (!s_initialized) {
        return MUSE_SERVICE_ERR_NOT_INITIALIZED;
    }
    rtos_lock_mutex(&s_lock);
    running = s_worker_running;
    if (running) {
        s_clear_requested = true;
    }
    rtos_unlock_mutex(&s_lock);
    if (!running) {
        stop_transports();
        muse_pairing_reset();
        if (!muse_storage_clear()) {
            return MUSE_SERVICE_ERR_STORAGE;
        }
        rtos_lock_mutex(&s_lock);
        s_status.paired = false;
        s_status.noise_connected = false;
        set_state_locked(MUSE_SERVICE_WAITING_FOR_PAIRING, 0);
        rtos_unlock_mutex(&s_lock);
        return MUSE_SERVICE_OK;
    }
    do {
        rtos_lock_mutex(&s_lock);
        pending = s_clear_requested;
        running = s_worker_running;
        result = s_status.last_error;
        rtos_unlock_mutex(&s_lock);
        if (!pending) {
            return result == MUSE_SERVICE_ERR_STORAGE
                       ? MUSE_SERVICE_ERR_STORAGE : MUSE_SERVICE_OK;
        }
        if (!running) {
            return MUSE_SERVICE_ERR_BUSY;
        }
        rtos_delay_milliseconds(10U);
        waited += 10U;
    } while (waited < SERVICE_STOP_WAIT_MS);
    return MUSE_SERVICE_ERR_BUSY;
}

int muse_service_confirm_pairing(void)
{
    if (muse_ble_pairing_confirm() != MUSE_BLE_PAIRING_OK) {
        return MUSE_SERVICE_ERR_BUSY;
    }
    rtos_lock_mutex(&s_lock);
    set_state_locked(MUSE_SERVICE_WAITING_FOR_NETWORK, 0);
    rtos_unlock_mutex(&s_lock);
    return MUSE_SERVICE_OK;
}

void muse_service_get_status(muse_service_status_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    if (!s_initialized) {
        status->state = MUSE_SERVICE_STOPPED;
        return;
    }
    rtos_lock_mutex(&s_lock);
    *status = s_status;
    rtos_unlock_mutex(&s_lock);
}

const char *muse_service_node_id(void)
{
    return s_node_id;
}

bool muse_service_request_token_refresh(void)
{
    bool accepted = false;

    if (!s_initialized) {
        return false;
    }
    rtos_lock_mutex(&s_lock);
    if (s_worker_running && !s_stop_requested) {
        s_token_refresh_requested = true;
        accepted = true;
    }
    rtos_unlock_mutex(&s_lock);
    return accepted;
}

const char *muse_service_state_name(muse_service_state_t state)
{
    switch (state) {
    case MUSE_SERVICE_STOPPED: return "stopped";
    case MUSE_SERVICE_WAITING_FOR_PAIRING: return "waiting_pairing";
    case MUSE_SERVICE_WAITING_FOR_CONFIRMATION: return "waiting_confirmation";
    case MUSE_SERVICE_WAITING_FOR_NETWORK: return "waiting_network";
    case MUSE_SERVICE_CONNECTING: return "connecting";
    case MUSE_SERVICE_CONNECTED: return "connected";
    case MUSE_SERVICE_ERROR: return "error";
    default: return "unknown";
    }
}

void muse_service_on_pairing_started(void)
{
    if (!s_initialized) {
        return;
    }
    rtos_lock_mutex(&s_lock);
    if (s_worker_running && !s_stop_requested) {
        set_state_locked(MUSE_SERVICE_WAITING_FOR_CONFIRMATION, 0);
    }
    rtos_unlock_mutex(&s_lock);
}

void muse_service_on_pairing_complete(void)
{
    if (!s_initialized) {
        return;
    }
    rtos_lock_mutex(&s_lock);
    if (s_worker_running && !s_stop_requested) {
        s_status.paired = true;
        set_state_locked(MUSE_SERVICE_WAITING_FOR_NETWORK, 0);
    }
    rtos_unlock_mutex(&s_lock);
}

void muse_service_on_network_changed(bool ready)
{
    bool was_ready;
    if (!s_initialized) {
        return;
    }
    rtos_lock_mutex(&s_lock);
    if (s_worker_running && !s_stop_requested) {
        was_ready = s_status.network_ready;
        s_status.network_ready = ready;
        if (s_status.paired && !s_status.noise_connected) {
            if (!ready) {
                set_state_locked(MUSE_SERVICE_WAITING_FOR_NETWORK, 0);
            } else if (!was_ready) {
                BK_LOGI(TAG, "MUSE ==> [2/6] Wi-Fi associated + DHCP got IP\r\n");
                set_state_locked(MUSE_SERVICE_CONNECTING, 0);
            }
        }
    }
    rtos_unlock_mutex(&s_lock);
}
