/**
 * Copyright 2026 Beken Corporation
 *
 * Pairing-v5 cryptographic and framing behavior is adapted from Meta
 * Platforms, Inc.'s muse-gadget-sdk commit
 * 812c46f9fe42b69d916613a9dc702cbac5ff7b54.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "muse_ble_pairing.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <components/log.h>
#include <os/os.h>

#include "cJSON.h"
#include "components/bluetooth/bk_dm_bluetooth.h"
#include "components/bluetooth/bk_dm_gap_ble.h"
#include "components/bluetooth/bk_dm_gatt_types.h"
#include "components/bluetooth/bk_dm_gatts.h"
#include "mbedtls/base64.h"
#include "mbedtls/ecp.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "components/bk_platform.h"
#include "bk_wifi.h"
#include "muse_pairing.h"
#include "muse_service.h"
#include "muse_storage.h"

#define TAG "muse_ble"

#define PAIRING_LABEL              "hatch-link ble setup v1"
#define PAIRING_SUITE              "p256-hkdf-sha256-aes-gcm-v1"
#define COMMUNITY_AUTH             "none"
#define PAIRING_POLICY             "confirm_press"
#define DEFAULT_MODEL              "hatch_link"
#define ADV_NAME_PREFIX            "MuseGadget-"
#define ADV_INSTANCE               0
#define GATTS_APP_ID               0x4D55
#define SYNC_TIMEOUT_MS            4000
#define SESSION_TIMEOUT_MS         120000U
#define CONFIRM_TIMEOUT_MS         60000U
#define PROVISION_TIMEOUT_MS       90000U
/* When a result notify (wifi_connected/auth_ok) fails because the GATT link
 * is momentarily down, re-attempt at this cadence rather than every 50 ms poll,
 * so we neither flood the log nor starve the shared Wi-Fi/BLE radio. */
#define STATUS_NOTIFY_RETRY_MS     1000U
/* After auth_ok the upstream Muse build keeps the BLE client connected
 * (ble_server_stop_advertising(false)) and lets the phone hang up once its
 * post-auth_ok cloud steps finish. We mirror that and only fall back to our
 * own disconnect after this cap, so the radio eventually frees for noise. */
#define POST_AUTH_OK_GRACE_MS      60000U
/* Connection interval is in 1.25 ms units. During STA Auth/Assoc/EAPOL we
 * relax the BLE link to a sparse 500 ms interval (iOS accepts max 500 ms with
 * the corresponding 6 s supervision timeout) so the single 2.4 GHz radio is
 * effectively free for the short join window while the GATT session stays up.
 * Afterwards we restore a responsive 15-30 ms interval. */
#define BLE_CONN_INT_RELAX_MIN     400U   /* 500 ms */
#define BLE_CONN_INT_RELAX_MAX     400U   /* 500 ms */
#define BLE_CONN_INT_NORMAL_MIN    12U    /* 15 ms */
#define BLE_CONN_INT_NORMAL_MAX    24U    /* 30 ms */
#define BLE_CONN_LATENCY           0U
#define BLE_CONN_TIMEOUT_RELAX     600U   /* 6 s (10 ms units) */
#define BLE_CONN_TIMEOUT_NORMAL    400U   /* 4 s (10 ms units) */
#define BLE_CONN_PARAM_SETTLE_MS   300U
#define MAX_RX_TOTAL_BYTES         8192U
/* MTU 517 -> 514 byte ATT payload; one device_info/hello fits unframed. */
#define MAX_NOTIFY_BYTES           512U
#define CHUNK_HEADER_BYTES         3U
#define CHUNK_MAGIC                0xFE
#define P256_POINT_BYTES           65U
#define NONCE_BYTES                16U
#define HASH_BYTES                 32U
#define KEY_BYTES                  32U
#define SESSION_ID_BYTES           16U
#define GCM_NONCE_BYTES            12U
#define GCM_TAG_BYTES              16U

#define BK_GATT_ATTR_TYPE_16(u) \
    { .len = BK_UUID_LEN_16, .uuid = { .uuid16 = (u) } }
#define BK_GATT_ATTR_CONTENT_16(u) \
    { .len = BK_UUID_LEN_16, .uuid = { .uuid16 = (u) } }
#define BK_GATT_VALUE(n, p) \
    { .attr_max_len = (n), .attr_len = 0, .attr_value = (p) }

/* Bluetooth byte order, matching the UUIDs in muse_pairing.h. */
static const uint8_t s_service_uuid[16] = {
    0x0c, 0x24, 0x5f, 0xcf, 0x4e, 0x31, 0x46, 0x8b,
    0xcf, 0x46, 0xea, 0x38, 0x1c, 0x3d, 0xdd, 0x7f
};
enum {
    ATTR_SERVICE,
    ATTR_RX,
    ATTR_TX,
    ATTR_TX_CCC,
    ATTR_COUNT
};

static uint16_t s_ccc_value;
/* Back RX/TX values with static buffers instead of the shared EtherMind
 * dynamic-value pool, which is too small for 512-byte payloads. */
static uint8_t s_rx_value_store[MAX_NOTIFY_BYTES];
static uint8_t s_tx_value_store[MAX_NOTIFY_BYTES];
static const bk_gatts_attr_db_t s_attr_db[] = {
    {
        .att_desc = {
            .attr_type = BK_GATT_ATTR_TYPE_16(BK_GATT_UUID_PRI_SERVICE),
            .attr_content = {
                .len = BK_UUID_LEN_128,
                .uuid = { .uuid128 = {
                    0x0c, 0x24, 0x5f, 0xcf, 0x4e, 0x31, 0x46, 0x8b,
                    0xcf, 0x46, 0xea, 0x38, 0x1c, 0x3d, 0xdd, 0x7f
                } },
            },
        },
    },
    {
        .att_desc = {
            .attr_type = BK_GATT_ATTR_TYPE_16(BK_GATT_UUID_CHAR_DECLARE),
            .attr_content = {
                .len = BK_UUID_LEN_128,
                .uuid = { .uuid128 = {
                    0x01, 0x9b, 0x8f, 0x5e, 0x2d, 0x3c, 0xf0, 0xa1,
                    0x6e, 0x4a, 0xa2, 0x28, 0x29, 0x30, 0x59, 0x4d
                } },
            },
            .value = BK_GATT_VALUE(MAX_NOTIFY_BYTES, s_rx_value_store),
            .prop = BK_GATT_CHAR_PROP_BIT_WRITE |
                    BK_GATT_CHAR_PROP_BIT_WRITE_NR,
            .perm = BK_GATT_PERM_WRITE,
        },
        .attr_control = { .auto_rsp = BK_GATT_RSP_BY_APP },
    },
    {
        .att_desc = {
            .attr_type = BK_GATT_ATTR_TYPE_16(BK_GATT_UUID_CHAR_DECLARE),
            .attr_content = {
                .len = BK_UUID_LEN_128,
                .uuid = { .uuid128 = {
                    0x6c, 0x5b, 0x4a, 0x3f, 0x2e, 0x1d, 0x0a, 0x8f,
                    0x9c, 0x4e, 0x2b, 0x7b, 0xca, 0xc4, 0x5d, 0xd7
                } },
            },
            .value = BK_GATT_VALUE(MAX_NOTIFY_BYTES, s_tx_value_store),
            .prop = BK_GATT_CHAR_PROP_BIT_NOTIFY,
            .perm = BK_GATT_PERM_READ,
        },
        .attr_control = { .auto_rsp = BK_GATT_RSP_BY_APP },
    },
    {
        .att_desc = {
            .attr_type = BK_GATT_ATTR_TYPE_16(BK_GATT_UUID_CHAR_CLIENT_CONFIG),
            .value = {
                .attr_max_len = sizeof(s_ccc_value),
                .attr_len = sizeof(s_ccc_value),
                .attr_value = (uint8_t *)&s_ccc_value,
            },
            .perm = BK_GATT_PERM_READ | BK_GATT_PERM_WRITE,
        },
        .attr_control = { .auto_rsp = BK_GATT_RSP_BY_APP },
    },
};

typedef struct {
    bool started;
    bool stopping;
    bool advertising;
    bool subscribed;
    bool confirmed_sent;
    /* Asynchronous provision_v2 progress, mirroring upstream on_provision:
     * wifi_connecting -> wifi_connected -> auth_ok -> BLE shutdown. */
    bool provisioning;
    bool wifi_connected_sent;
    bool auth_ok_sent;
    bool stop_requested;
    /* BK7259 has a single 2.4 GHz radio shared by BLE and Wi-Fi. During the
     * STA authentication/association dense BLE connection events starve the
     * 802.11 Auth frames ("fallback to 1Mbps" + "ble pll failed", cause=18).
     *
     * We do NOT drop the link: the phone only learns the provisioning result
     * (wifi_connected / auth_ok) over this very GATT session, and the app does
     * not reliably reconnect once torn down. Instead, while rf_relaxed we keep
     * the link up but negotiate a much larger connection interval (matching
     * upstream's provisioning-time balanced coexistence), so almost all of the
     * short Auth/Assoc/EAPOL window is free for Wi-Fi. The interval is restored
     * once the STA has an IP. */
    bool rf_relaxed;
    uint32_t provision_deadline_ms;
    /* Earliest time a deferred wifi_connected/auth_ok notify may be retried
     * after the GATT link was observed down. 0 = no backoff pending. */
    uint32_t status_retry_deadline_ms;
    /* Post-auth_ok fallback teardown time while waiting for the phone to
     * disconnect itself (POST_AUTH_OK_GRACE_MS). 0 = grace not armed. */
    uint32_t teardown_deadline_ms;
    bk_gatt_if_t gatts_if;
    uint16_t handles[ATTR_COUNT];
    uint16_t conn_id;
    uint16_t mtu;
    uint8_t peer_addr[6];
    char adv_name[30];
    char mac[18];
    muse_ble_pairing_config_t cfg;
    beken_mutex_t lock;
    beken_mutex_t tx_lock;
    beken_semaphore_t sync;
    beken_queue_t rx_queue;
    beken_thread_t worker;

    uint8_t *rx_buf;
    size_t rx_len;
    uint8_t rx_total;
    uint8_t rx_next;

    mbedtls_ecp_group group;
    mbedtls_mpi private_key;
    mbedtls_ecp_point public_key;
    uint8_t mobile_pub[P256_POINT_BYTES];
    uint8_t device_pub[P256_POINT_BYTES];
    uint8_t mobile_nonce[NONCE_BYTES];
    uint8_t device_nonce[NONCE_BYTES];
    uint8_t transcript_hash[HASH_BYTES];
    uint8_t session_id[SESSION_ID_BYTES];
    uint8_t rx_key[KEY_BYTES];
    uint8_t tx_key[KEY_BYTES];
    char session_id_b64[32];
    uint64_t rx_counter;
    uint64_t tx_counter;
    uint32_t generation;
    uint32_t deadline_ms;
} muse_ble_context_t;

static muse_ble_context_t s_ctx;

static bool time_expired(uint32_t deadline)
{
    return deadline != 0 && (int32_t)(rtos_get_time() - deadline) >= 0;
}

static void secure_free(void *p, size_t len)
{
    if (p != NULL) {
        mbedtls_platform_zeroize(p, len);
        free(p);
    }
}

static int random_bytes(void *ctx, unsigned char *out, size_t len)
{
    (void)ctx;
    /*
     * Use the platform hardware RNG (TrustEngine/OTP via bk_fill_rand),
     * mirroring upstream esp_fill_random(). Do NOT instantiate a mbedtls
     * entropy + ctr_drbg context on this worker stack: mbedtls_entropy_context
     * holds a multi-KB accumulator and ctr_drbg_seed pushes another SHA/HMAC
     * frame, which deterministically overflowed the pairing task stack while
     * handling pairing_client_hello.
     */
    return (bk_fill_rand(out, len) == 0) ? 0 : -1;
}

static const mbedtls_md_info_t *sha256_info(void)
{
    return mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
}

static bool sha256(const void *data, size_t len, uint8_t out[HASH_BYTES])
{
    const mbedtls_md_info_t *md = sha256_info();
    return md != NULL && mbedtls_md(md, data, len, out) == 0;
}

static bool hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[HASH_BYTES])
{
    const mbedtls_md_info_t *md = sha256_info();
    return md != NULL &&
           mbedtls_md_hmac(md, key, key_len, data, data_len, out) == 0;
}

static bool hkdf_extract_expand(const uint8_t *ikm, size_t ikm_len,
                                const uint8_t *salt, size_t salt_len,
                                const char *info, uint8_t out[KEY_BYTES])
{
    uint8_t prk[HASH_BYTES];
    uint8_t block[128 + 1];
    size_t info_len = strlen(info);
    bool ok = false;

    if (info_len <= sizeof(block) - 1 &&
        hmac_sha256(salt, salt_len, ikm, ikm_len, prk)) {
        memcpy(block, info, info_len);
        block[info_len] = 1;
        ok = hmac_sha256(prk, sizeof(prk), block, info_len + 1, out);
    }
    mbedtls_platform_zeroize(prk, sizeof(prk));
    mbedtls_platform_zeroize(block, sizeof(block));
    return ok;
}

static bool hkdf_expand(const uint8_t prk[KEY_BYTES], const char *info,
                        uint8_t out[KEY_BYTES])
{
    uint8_t block[128 + 1];
    size_t info_len = strlen(info);
    bool ok = false;

    if (info_len <= sizeof(block) - 1) {
        memcpy(block, info, info_len);
        block[info_len] = 1;
        ok = hmac_sha256(prk, KEY_BYTES, block, info_len + 1, out);
    }
    mbedtls_platform_zeroize(block, sizeof(block));
    return ok;
}

static bool base64url_encode(const uint8_t *data, size_t len,
                             char *out, size_t out_size)
{
    size_t encoded_len = 0;
    size_t i;

    if (mbedtls_base64_encode((uint8_t *)out, out_size, &encoded_len,
                              data, len) != 0) {
        return false;
    }
    while (encoded_len != 0 && out[encoded_len - 1] == '=') {
        encoded_len--;
    }
    if (encoded_len >= out_size) {
        return false;
    }
    out[encoded_len] = '\0';
    for (i = 0; i < encoded_len; i++) {
        if (out[i] == '+') out[i] = '-';
        else if (out[i] == '/') out[i] = '_';
    }
    return true;
}

static bool base64url_decode(const char *text, uint8_t *out,
                             size_t out_size, size_t *out_len)
{
    size_t n, padded, i;
    char *tmp;
    int rc;

    if (text == NULL || out == NULL || out_len == NULL) return false;
    n = strlen(text);
    if (n == 0 || n > 4096 || n % 4 == 1) return false;
    padded = n + ((4 - n % 4) % 4);
    tmp = malloc(padded + 1);
    if (tmp == NULL) return false;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
        else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                   (c >= '0' && c <= '9'))) {
            free(tmp);
            return false;
        }
        tmp[i] = (char)c;
    }
    while (i < padded) tmp[i++] = '=';
    tmp[padded] = '\0';
    rc = mbedtls_base64_decode(out, out_size, out_len,
                               (const uint8_t *)tmp, padded);
    free(tmp);
    return rc == 0;
}

static const char *json_string(cJSON *root, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : NULL;
}

static unsigned int json_uint(cJSON *root, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) && item->valuedouble >= 0.0 &&
           item->valuedouble <= 14.0 ? (unsigned int)item->valuedouble : 0U;
}

static bool json_bool(cJSON *root, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsTrue(item) ||
           (cJSON_IsNumber(item) && item->valueint != 0);
}

static bool parse_counter(const char *text, uint64_t *counter)
{
    uint64_t value = 0;
    const char *p;
    if (text == NULL || *text == '\0') return false;
    for (p = text; *p; p++) {
        uint64_t digit;
        if (*p < '0' || *p > '9') return false;
        digit = (uint64_t)(*p - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    *counter = value;
    return true;
}

static void clear_crypto_locked(void)
{
    mbedtls_mpi_free(&s_ctx.private_key);
    mbedtls_ecp_point_free(&s_ctx.public_key);
    mbedtls_mpi_init(&s_ctx.private_key);
    mbedtls_ecp_point_init(&s_ctx.public_key);
    mbedtls_platform_zeroize(s_ctx.mobile_pub, sizeof(s_ctx.mobile_pub));
    mbedtls_platform_zeroize(s_ctx.device_pub, sizeof(s_ctx.device_pub));
    mbedtls_platform_zeroize(s_ctx.mobile_nonce, sizeof(s_ctx.mobile_nonce));
    mbedtls_platform_zeroize(s_ctx.device_nonce, sizeof(s_ctx.device_nonce));
    mbedtls_platform_zeroize(s_ctx.transcript_hash,
                             sizeof(s_ctx.transcript_hash));
    mbedtls_platform_zeroize(s_ctx.session_id, sizeof(s_ctx.session_id));
    mbedtls_platform_zeroize(s_ctx.rx_key, sizeof(s_ctx.rx_key));
    mbedtls_platform_zeroize(s_ctx.tx_key, sizeof(s_ctx.tx_key));
    memset(s_ctx.session_id_b64, 0, sizeof(s_ctx.session_id_b64));
    s_ctx.rx_counter = 0;
    s_ctx.tx_counter = 0;
    s_ctx.generation = 0;
    s_ctx.deadline_ms = 0;
    s_ctx.confirmed_sent = false;
}

static void reset_rx_locked(void)
{
    free(s_ctx.rx_buf);
    s_ctx.rx_buf = NULL;
    s_ctx.rx_len = 0;
    s_ctx.rx_total = 0;
    s_ctx.rx_next = 0;
}

static void reset_session(void)
{
    rtos_lock_mutex(&s_ctx.lock);
    reset_rx_locked();
    clear_crypto_locked();
    rtos_unlock_mutex(&s_ctx.lock);
    muse_pairing_reset();
}

static bool derive_keys_locked(void)
{
    mbedtls_ecp_point peer;
    mbedtls_ecp_point shared;
    uint8_t ecdh[KEY_BYTES] = {0};
    uint8_t salt_input[NONCE_BYTES * 2 + HASH_BYTES];
    uint8_t salt[HASH_BYTES];
    uint8_t secret[KEY_BYTES];
    uint8_t sid_input[sizeof("hatch-link session id v1") - 1 +
                      HASH_BYTES + KEY_BYTES];
    uint8_t sid_hash[HASH_BYTES];
    int rc;
    bool ok = false;

    mbedtls_ecp_point_init(&peer);
    mbedtls_ecp_point_init(&shared);
    rc = mbedtls_ecp_point_read_binary(&s_ctx.group, &peer,
                                       s_ctx.mobile_pub,
                                       sizeof(s_ctx.mobile_pub));
    if (rc == 0) rc = mbedtls_ecp_check_pubkey(&s_ctx.group, &peer);
    if (rc == 0) {
        rc = mbedtls_ecp_mul(&s_ctx.group, &shared, &s_ctx.private_key,
                             &peer, random_bytes, NULL);
    }
    if (rc == 0) {
        rc = mbedtls_mpi_write_binary(&shared.MBEDTLS_PRIVATE(X),
                                      ecdh, sizeof(ecdh));
    }
    mbedtls_ecp_point_free(&peer);
    mbedtls_ecp_point_free(&shared);
    if (rc != 0) goto done;

    memcpy(salt_input, s_ctx.mobile_nonce, NONCE_BYTES);
    memcpy(salt_input + NONCE_BYTES, s_ctx.device_nonce, NONCE_BYTES);
    memcpy(salt_input + NONCE_BYTES * 2, s_ctx.transcript_hash, HASH_BYTES);
    if (!sha256(salt_input, sizeof(salt_input), salt) ||
        !hkdf_extract_expand(ecdh, sizeof(ecdh), salt, sizeof(salt),
                             PAIRING_LABEL, secret) ||
        !hkdf_expand(secret, "mobile->device", s_ctx.rx_key) ||
        !hkdf_expand(secret, "device->mobile", s_ctx.tx_key)) {
        goto done;
    }

    memcpy(sid_input, "hatch-link session id v1",
           sizeof("hatch-link session id v1") - 1);
    memcpy(sid_input + sizeof("hatch-link session id v1") - 1,
           s_ctx.transcript_hash, HASH_BYTES);
    memcpy(sid_input + sizeof("hatch-link session id v1") - 1 + HASH_BYTES,
           ecdh, KEY_BYTES);
    if (!sha256(sid_input, sizeof(sid_input), sid_hash)) goto done;
    memcpy(s_ctx.session_id, sid_hash, SESSION_ID_BYTES);
    ok = base64url_encode(s_ctx.session_id, SESSION_ID_BYTES,
                          s_ctx.session_id_b64,
                          sizeof(s_ctx.session_id_b64));

done:
    mbedtls_platform_zeroize(ecdh, sizeof(ecdh));
    mbedtls_platform_zeroize(salt_input, sizeof(salt_input));
    mbedtls_platform_zeroize(salt, sizeof(salt));
    mbedtls_platform_zeroize(secret, sizeof(secret));
    mbedtls_platform_zeroize(sid_input, sizeof(sid_input));
    mbedtls_platform_zeroize(sid_hash, sizeof(sid_hash));
    return ok;
}

static void build_nonce(uint8_t direction, uint64_t counter,
                        uint8_t nonce[GCM_NONCE_BYTES])
{
    int i;
    memset(nonce, 0, GCM_NONCE_BYTES);
    nonce[0] = direction;
    for (i = 0; i < 8; i++) {
        nonce[11 - i] = (uint8_t)counter;
        counter >>= 8;
    }
}

static bool build_aad(uint8_t direction, uint64_t counter,
                      char *aad, size_t aad_size)
{
    int n = snprintf(aad, aad_size, "%s|%s|%s|%" PRIu64,
                     PAIRING_LABEL, s_ctx.session_id_b64,
                     direction == 0 ? "m2d" : "d2m", counter);
    return n > 0 && (size_t)n < aad_size;
}

static bool gcm_encrypt_locked(const uint8_t *plain, size_t plain_len,
                               uint8_t *cipher, uint8_t tag[GCM_TAG_BYTES])
{
    mbedtls_gcm_context gcm;
    uint8_t nonce[GCM_NONCE_BYTES];
    char aad[128];
    int rc;

    build_nonce(1, s_ctx.tx_counter, nonce);
    if (!build_aad(1, s_ctx.tx_counter, aad, sizeof(aad))) return false;
    mbedtls_gcm_init(&gcm);
    rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                            s_ctx.tx_key, KEY_BYTES * 8);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(
            &gcm, MBEDTLS_GCM_ENCRYPT, plain_len, nonce, sizeof(nonce),
            (const uint8_t *)aad, strlen(aad), plain, cipher,
            GCM_TAG_BYTES, tag);
    }
    mbedtls_gcm_free(&gcm);
    return rc == 0;
}

static bool gcm_decrypt_locked(const uint8_t *cipher, size_t cipher_len,
                               const uint8_t tag[GCM_TAG_BYTES],
                               uint8_t *plain)
{
    mbedtls_gcm_context gcm;
    uint8_t nonce[GCM_NONCE_BYTES];
    char aad[128];
    int rc;

    build_nonce(0, s_ctx.rx_counter, nonce);
    if (!build_aad(0, s_ctx.rx_counter, aad, sizeof(aad))) return false;
    mbedtls_gcm_init(&gcm);
    rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES,
                            s_ctx.rx_key, KEY_BYTES * 8);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(
            &gcm, cipher_len, nonce, sizeof(nonce),
            (const uint8_t *)aad, strlen(aad), tag, GCM_TAG_BYTES,
            cipher, plain);
    }
    mbedtls_gcm_free(&gcm);
    return rc == 0;
}

static bool notify_chunked(const char *json)
{
    size_t len, mtu_payload, chunk_data, total, i;
    bool ok = true;

    if (json == NULL || !s_ctx.subscribed || s_ctx.conn_id == UINT16_MAX) {
        return false;
    }
    len = strlen(json);
    mtu_payload = s_ctx.mtu > 3 ? s_ctx.mtu - 3 : 0;
    if (mtu_payload > MAX_NOTIFY_BYTES) mtu_payload = MAX_NOTIFY_BYTES;

    /* Fast path: the whole record fits the negotiated MTU, so send the raw
     * JSON with no 0xFE framing header (first byte is '{'). The app expects
     * exactly this for the single-packet device_info / pairing_ready flow. */
    if (len <= mtu_payload) {
        return bk_ble_gatts_send_indicate(
                   s_ctx.gatts_if, s_ctx.conn_id, s_ctx.handles[ATTR_TX],
                   (uint16_t)len, (uint8_t *)json, false) == BK_OK;
    }

    if (mtu_payload <= CHUNK_HEADER_BYTES) return false;
    chunk_data = mtu_payload - CHUNK_HEADER_BYTES;
    total = (len + chunk_data - 1) / chunk_data;
    if (total == 0) total = 1;
    if (total > UINT8_MAX) return false;

    for (i = 0; i < total; i++) {
        size_t offset = i * chunk_data;
        size_t part = len - offset;
        uint8_t *packet;
        if (s_ctx.stopping) return false;
        if (part > chunk_data) part = chunk_data;
        /* Heap: the muse_ble stack cannot host a 512-byte frame buffer. */
        packet = malloc(part + CHUNK_HEADER_BYTES);
        if (packet == NULL) { ok = false; break; }
        packet[0] = CHUNK_MAGIC;
        packet[1] = (uint8_t)i;
        packet[2] = (uint8_t)total;
        memcpy(packet + CHUNK_HEADER_BYTES, json + offset, part);
        if (bk_ble_gatts_send_indicate(
                s_ctx.gatts_if, s_ctx.conn_id, s_ctx.handles[ATTR_TX],
                (uint16_t)(part + CHUNK_HEADER_BYTES), packet, false) != BK_OK) {
            free(packet);
            ok = false;
            break;
        }
        free(packet);
        rtos_delay_milliseconds(35);
    }
    return ok;
}

static char *make_envelope_locked(const char *plain)
{
    size_t plain_len = strlen(plain);
    size_t cipher_b64_size = ((plain_len + 2) / 3) * 4 + 1;
    uint8_t *cipher = malloc(plain_len ? plain_len : 1);
    uint8_t tag[GCM_TAG_BYTES];
    char *cipher_b64 = malloc(cipher_b64_size);
    char tag_b64[32];
    char counter[21];
    cJSON *root = NULL;
    char *json = NULL;

    if (cipher == NULL || cipher_b64 == NULL ||
        !gcm_encrypt_locked((const uint8_t *)plain, plain_len, cipher, tag) ||
        !base64url_encode(cipher, plain_len, cipher_b64, cipher_b64_size) ||
        !base64url_encode(tag, sizeof(tag), tag_b64, sizeof(tag_b64))) {
        goto done;
    }
    root = cJSON_CreateObject();
    if (root == NULL) goto done;
    snprintf(counter, sizeof(counter), "%" PRIu64, s_ctx.tx_counter);
    cJSON_AddStringToObject(root, "type", "pairing_encrypted");
    cJSON_AddStringToObject(root, "session_id", s_ctx.session_id_b64);
    cJSON_AddStringToObject(root, "counter", counter);
    cJSON_AddStringToObject(root, "ciphertext", cipher_b64);
    cJSON_AddStringToObject(root, "tag", tag_b64);
    json = cJSON_PrintUnformatted(root);
    if (json != NULL) s_ctx.tx_counter++;

done:
    cJSON_Delete(root);
    secure_free(cipher, plain_len);
    secure_free(cipher_b64, cipher_b64_size);
    mbedtls_platform_zeroize(tag, sizeof(tag));
    return json;
}

static bool send_encrypted_json(const char *plain)
{
    char *envelope;
    bool sent;
    rtos_lock_mutex(&s_ctx.tx_lock);
    rtos_lock_mutex(&s_ctx.lock);
    envelope = s_ctx.generation != 0 ? make_envelope_locked(plain) : NULL;
    rtos_unlock_mutex(&s_ctx.lock);
    sent = notify_chunked(envelope);
    free(envelope);
    rtos_unlock_mutex(&s_ctx.tx_lock);
    return sent;
}

static bool send_status(const char *status)
{
    cJSON *root = cJSON_CreateObject();
    char *plain;
    bool sent;
    if (root == NULL) return false;
    cJSON_AddStringToObject(root, "type", "status");
    cJSON_AddStringToObject(root, "status", status);
    if (strcmp(status, "pairing_confirmed") == 0 &&
        s_ctx.cfg.sdk_token != NULL && *s_ctx.cfg.sdk_token != '\0') {
        cJSON_AddStringToObject(root, "sdk_token", s_ctx.cfg.sdk_token);
    }
    plain = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    sent = plain != NULL && send_encrypted_json(plain);
    if (plain != NULL) secure_free(plain, strlen(plain));
    return sent;
}

static void send_plain_error(const char *error)
{
    if (error != NULL && s_ctx.generation == 0) {
        (void)notify_chunked(error);
    } else if (error != NULL) {
        (void)send_status(error);
    }
}

static char *make_transcript(const char *mobile_pub, const char *device_pub,
                             const char *mobile_nonce, const char *device_nonce)
{
    /* Byte-for-byte match of pairing_transcript_build() upstream (community /
     * confirm_press). The transcript hash is part of session-key derivation,
     * so formatting and field order are load-bearing. */
    static const char fmt[] =
        "hatch-link-pairing-v%d\n"
        "version=%d\n"
        "initiator_role=mobile\n"
        "responder_role=link\n"
        "device_id=%s\n"
        "node_id=%s\n"
        "mac=%s\n"
        "model=%s\n"
        "firmware_version=%s\n"
        "selected_cipher_suite=%s\n"
        "pairing_auth=%s\n"
        "pairing_auth_epoch=%d\n"
        "pairing_policy=%s\n"
        "confirm_timeout_seconds=%d\n"
        "mobile_pub=%s\n"
        "device_pub=%s\n"
        "mobile_nonce=%s\n"
        "device_nonce=%s";
    int n;
    char *out;

    n = snprintf(NULL, 0, fmt,
                 MUSE_PAIRING_VERSION, MUSE_PAIRING_VERSION,
                 s_ctx.cfg.device_id, s_ctx.cfg.node_id, s_ctx.mac,
                 s_ctx.cfg.model, s_ctx.cfg.firmware_version,
                 PAIRING_SUITE, COMMUNITY_AUTH, 0, PAIRING_POLICY,
                 (int)(CONFIRM_TIMEOUT_MS / 1000U),
                 mobile_pub, device_pub, mobile_nonce, device_nonce);
    if (n <= 0) return NULL;
    out = malloc((size_t)n + 1);
    if (out == NULL) return NULL;
    snprintf(out, (size_t)n + 1, fmt,
             MUSE_PAIRING_VERSION, MUSE_PAIRING_VERSION,
             s_ctx.cfg.device_id, s_ctx.cfg.node_id, s_ctx.mac,
             s_ctx.cfg.model, s_ctx.cfg.firmware_version,
             PAIRING_SUITE, COMMUNITY_AUTH, 0, PAIRING_POLICY,
             (int)(CONFIRM_TIMEOUT_MS / 1000U),
             mobile_pub, device_pub, mobile_nonce, device_nonce);
    return out;
}

static bool handle_hello(cJSON *root)
{
    const char *mobile_pub = json_string(root, "mobile_pub");
    const char *mobile_nonce = json_string(root, "mobile_nonce");
    const char *auth = json_string(root, "pairing_auth");
    const char *policy = json_string(root, "pairing_policy");
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    mbedtls_ecp_point peer;
    char canonical_mobile_pub[96], canonical_device_pub[96];
    char canonical_mobile_nonce[32], canonical_device_nonce[32];
    char transcript_hash_b64[48];
    char *transcript = NULL;
    cJSON *reply = NULL;
    char *json = NULL;
    size_t decoded = 0, public_len = 0;
    bool ok = false;
    int rc;

    if (!cJSON_IsNumber(version) || version->valuedouble != 5 ||
        auth == NULL || strcmp(auth, COMMUNITY_AUTH) != 0 ||
        policy == NULL || strcmp(policy, PAIRING_POLICY) != 0 ||
        mobile_pub == NULL || mobile_nonce == NULL) {
        return false;
    }

    rtos_lock_mutex(&s_ctx.lock);
    clear_crypto_locked();
    if (!base64url_decode(mobile_pub, s_ctx.mobile_pub,
                          sizeof(s_ctx.mobile_pub), &decoded) ||
        decoded != P256_POINT_BYTES || s_ctx.mobile_pub[0] != 0x04 ||
        !base64url_decode(mobile_nonce, s_ctx.mobile_nonce,
                          sizeof(s_ctx.mobile_nonce), &decoded) ||
        decoded != NONCE_BYTES) {
        goto done_locked;
    }
    mbedtls_ecp_point_init(&peer);
    rc = mbedtls_ecp_point_read_binary(&s_ctx.group, &peer,
                                       s_ctx.mobile_pub,
                                       sizeof(s_ctx.mobile_pub));
    if (rc == 0) rc = mbedtls_ecp_check_pubkey(&s_ctx.group, &peer);
    mbedtls_ecp_point_free(&peer);
    if (rc != 0 ||
        mbedtls_ecp_gen_keypair(&s_ctx.group, &s_ctx.private_key,
                                &s_ctx.public_key,
                                random_bytes, NULL) != 0 ||
        random_bytes(NULL, s_ctx.device_nonce,
                     sizeof(s_ctx.device_nonce)) != 0 ||
        mbedtls_ecp_point_write_binary(
            &s_ctx.group, &s_ctx.public_key, MBEDTLS_ECP_PF_UNCOMPRESSED,
            &public_len, s_ctx.device_pub, sizeof(s_ctx.device_pub)) != 0 ||
        public_len != P256_POINT_BYTES ||
        !base64url_encode(s_ctx.mobile_pub, sizeof(s_ctx.mobile_pub),
                          canonical_mobile_pub,
                          sizeof(canonical_mobile_pub)) ||
        !base64url_encode(s_ctx.device_pub, sizeof(s_ctx.device_pub),
                          canonical_device_pub,
                          sizeof(canonical_device_pub)) ||
        !base64url_encode(s_ctx.mobile_nonce, sizeof(s_ctx.mobile_nonce),
                          canonical_mobile_nonce,
                          sizeof(canonical_mobile_nonce)) ||
        !base64url_encode(s_ctx.device_nonce, sizeof(s_ctx.device_nonce),
                          canonical_device_nonce,
                          sizeof(canonical_device_nonce))) {
        goto done_locked;
    }
    transcript = make_transcript(canonical_mobile_pub, canonical_device_pub,
                                 canonical_mobile_nonce,
                                 canonical_device_nonce);
    if (transcript == NULL ||
        !sha256(transcript, strlen(transcript), s_ctx.transcript_hash) ||
        !base64url_encode(s_ctx.transcript_hash,
                          sizeof(s_ctx.transcript_hash),
                          transcript_hash_b64,
                          sizeof(transcript_hash_b64)) ||
        !derive_keys_locked()) {
        goto done_locked;
    }
    s_ctx.generation = muse_pairing_begin();
    if (s_ctx.generation == 0) goto done_locked;
    s_ctx.deadline_ms = rtos_get_time() + CONFIRM_TIMEOUT_MS;

    reply = cJSON_CreateObject();
    if (reply == NULL) goto done_locked;
    cJSON_AddStringToObject(reply, "type", "pairing_ready");
    cJSON_AddNumberToObject(reply, "version", MUSE_PAIRING_VERSION);
    cJSON_AddStringToObject(reply, "device_id", s_ctx.cfg.device_id);
    cJSON_AddStringToObject(reply, "node_id", s_ctx.cfg.node_id);
    cJSON_AddStringToObject(reply, "mac", s_ctx.mac);
    cJSON_AddStringToObject(reply, "model", s_ctx.cfg.model);
    cJSON_AddStringToObject(reply, "firmware_version",
                           s_ctx.cfg.firmware_version);
    cJSON_AddStringToObject(reply, "pairing_auth", COMMUNITY_AUTH);
    cJSON_AddNumberToObject(reply, "pairing_auth_epoch", 0);
    cJSON_AddStringToObject(reply, "pairing_policy", PAIRING_POLICY);
    cJSON_AddStringToObject(reply, "device_pub", canonical_device_pub);
    cJSON_AddStringToObject(reply, "device_nonce", canonical_device_nonce);
    cJSON_AddStringToObject(reply, "transcript_hash", transcript_hash_b64);
    cJSON_AddStringToObject(reply, "session_id", s_ctx.session_id_b64);
    json = cJSON_PrintUnformatted(reply);
    ok = json != NULL;

done_locked:
    if (!ok) clear_crypto_locked();
    rtos_unlock_mutex(&s_ctx.lock);
    cJSON_Delete(reply);
    free(transcript);
    if (ok) {
        muse_service_on_pairing_started();
        ok = notify_chunked(json);
    }
    free(json);
    return ok;
}

static char *decrypt_envelope(cJSON *root)
{
    const char *sid = json_string(root, "session_id");
    const char *counter_text = json_string(root, "counter");
    const char *cipher_text = json_string(root, "ciphertext");
    const char *tag_text = json_string(root, "tag");
    uint64_t counter;
    size_t cipher_cap, cipher_len = 0, tag_len = 0;
    uint8_t *cipher = NULL;
    uint8_t *plain = NULL;
    uint8_t tag[GCM_TAG_BYTES];
    bool ok = false;

    rtos_lock_mutex(&s_ctx.lock);
    if (s_ctx.generation == 0 || time_expired(s_ctx.deadline_ms) ||
        sid == NULL || strcmp(sid, s_ctx.session_id_b64) != 0 ||
        !parse_counter(counter_text, &counter) ||
        counter != s_ctx.rx_counter || cipher_text == NULL ||
        tag_text == NULL) {
        goto done;
    }
    cipher_cap = strlen(cipher_text) / 4 * 3 + 3;
    if (cipher_cap > MAX_RX_TOTAL_BYTES) goto done;
    cipher = malloc(cipher_cap ? cipher_cap : 1);
    plain = malloc(cipher_cap + 1);
    if (cipher == NULL || plain == NULL ||
        !base64url_decode(cipher_text, cipher, cipher_cap, &cipher_len) ||
        !base64url_decode(tag_text, tag, sizeof(tag), &tag_len) ||
        tag_len != GCM_TAG_BYTES ||
        !gcm_decrypt_locked(cipher, cipher_len, tag, plain)) {
        goto done;
    }
    plain[cipher_len] = '\0';
    s_ctx.rx_counter++;
    s_ctx.deadline_ms = rtos_get_time() + SESSION_TIMEOUT_MS;
    ok = true;

done:
    if (!ok) {
        free(plain);
        plain = NULL;
    }
    secure_free(cipher, cipher_cap);
    mbedtls_platform_zeroize(tag, sizeof(tag));
    rtos_unlock_mutex(&s_ctx.lock);
    return (char *)plain;
}

static bool exact_client_finished(cJSON *root)
{
    cJSON *only = cJSON_IsObject(root) ? root->child : NULL;
    return only != NULL && only->next == NULL && only->string != NULL &&
           strcmp(only->string, "action") == 0 && cJSON_IsString(only) &&
           only->valuestring != NULL &&
           strcmp(only->valuestring, "pairing_client_finished") == 0;
}

/* Serve the phone's Wi-Fi picker. Mirrors upstream on_wifi_scan /
 * send_cached_scan_to_client(): a single encrypted "wifi_scan_result" record
 * with a networks[] array of {ssid,rssi,secure}. Runs on the 16 KB pairing
 * worker; the scan is bounded so the JSON fits one ATT_MTU-sized payload. */
#define WIFI_SCAN_MAX_RESULTS 12

static void handle_wifi_scan(void)
{
    wifi_scan_result_t result;
    wifi_scan_config_t scan_cfg;
    cJSON *root = NULL;
    cJSON *arr = NULL;
    char *json = NULL;
    bool have_result = false;
    bk_err_t err;
    int count = 0;
    int i;

    memset(&scan_cfg, 0, sizeof(scan_cfg)); /* NULL-like: scan all channels. */
    memset(&result, 0, sizeof(result));
    err = bk_wifi_scan_start(&scan_cfg);
    if (err == BK_OK) {
        err = bk_wifi_scan_get_result(&result);
        have_result = (err == BK_OK);
    }
    if (!have_result) {
        /* STA not initialised yet / scan busy: answer with an empty list so
         * the phone falls back to manual SSID entry instead of stalling. */
        BK_LOGW(TAG, "wifi_scan unavailable err=%d; returning empty list\r\n",
                (int)err);
        memset(&result, 0, sizeof(result));
    }

    root = cJSON_CreateObject();
    arr = cJSON_CreateArray();
    if (root == NULL || arr == NULL) {
        if (have_result) bk_wifi_scan_free_result(&result);
        if (root != NULL) cJSON_Delete(root);
        if (arr != NULL) cJSON_Delete(arr);
        send_status("error_operation_in_progress");
        return;
    }
    cJSON_AddStringToObject(root, "type", "wifi_scan_result");

    if (have_result) {
        count = result.ap_num < WIFI_SCAN_MAX_RESULTS
                    ? result.ap_num : WIFI_SCAN_MAX_RESULTS;
        for (i = 0; i < count; i++) {
            const wifi_scan_ap_info_t *ap = &result.aps[i];
            cJSON *entry;
            if (ap->ssid[0] == '\0') {
                continue; /* hide hidden/empty SSIDs, like the phone picker */
            }
            entry = cJSON_CreateObject();
            if (entry == NULL) {
                continue;
            }
            cJSON_AddStringToObject(entry, "ssid", ap->ssid);
            cJSON_AddNumberToObject(entry, "rssi", ap->rssi);
            cJSON_AddBoolToObject(entry, "secure",
                                  ap->security != WIFI_SECURITY_NONE);
            cJSON_AddItemToArray(arr, entry);
        }
        bk_wifi_scan_free_result(&result);
    }
    cJSON_AddItemToObject(root, "networks", arr);

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL || !send_encrypted_json(json)) {
        BK_LOGW(TAG, "wifi_scan_result send failed\r\n");
    } else {
        BK_LOGI(TAG, "wifi_scan_result sent (%d APs)\r\n", count);
    }
    if (json != NULL) {
        secure_free(json, strlen(json));
    }
}

static void handle_decrypted(char *plain)
{
    cJSON *root = cJSON_ParseWithLength(plain, strlen(plain));
    const char *action;
    if (!cJSON_IsObject(root)) {
        send_status("error_invalid_command");
        goto done;
    }
    action = json_string(root, "action");
    if (action == NULL) {
        send_status("error_invalid_command");
    } else {
        BK_LOGI(TAG, "decrypted action=%s state=%d\r\n", action,
                (int)muse_pairing_state());
    }
    if (action == NULL) {
        /* status already sent above */
    } else if (strcmp(action, "pairing_client_finished") == 0) {
        if (!exact_client_finished(root) ||
            !muse_pairing_client_finished(s_ctx.generation)) {
            send_status("error_pairing_decrypt");
            reset_session();
        } else {
            /* Upstream emits "confirm_required" BEFORE arming the button, so a
             * prompt can never overtake the record the mobile expects first. */
            if (!send_status("confirm_required")) {
                BK_LOGW(TAG, "failed to notify confirm_required\r\n");
                send_status("error_pairing_decrypt");
                reset_session();
            } else if (!muse_pairing_arm_confirmation(s_ctx.generation)) {
                reset_session();
            } else {
                s_ctx.deadline_ms = rtos_get_time() + CONFIRM_TIMEOUT_MS;
                BK_LOGI(TAG, "confirm_required; awaiting muse confirm\r\n");
            }
        }
    } else if (strcmp(action, "wifi_scan") == 0) {
        if (muse_pairing_state() != MUSE_PAIRING_READY) {
            send_status("error_encryption_required");
        } else {
            handle_wifi_scan();
        }
    } else if (strcmp(action, "provision_v2") == 0) {
        muse_storage_credentials_t credentials;
        const char *token_type = json_string(root, "token_type");
        if (muse_pairing_state() != MUSE_PAIRING_READY ||
            token_type == NULL || strcmp(token_type, "device") != 0) {
            send_status("error_pairing_confirm_required");
            goto done;
        }
        memset(&credentials, 0, sizeof(credentials));
        credentials.ssid = json_string(root, "ssid");
        credentials.password = json_string(root, "password");
        credentials.wifi_channel = json_uint(root, "wifi_channel");
        credentials.wifi_hidden = json_bool(root, "wifi_hidden");
        credentials.access_token = json_string(root, "access_token");
        credentials.refresh_token = json_string(root, "refresh_token");
        /* Optional fields: stored when present, never reject provisioning if
         * absent (mirrors upstream ble_server provision_v2 validation). */
        credentials.vm_url = json_string(root, "vm_url");
        credentials.api_url_v2 = json_string(root, "api_url_v2");
        if (credentials.api_url_v2 == NULL) {
            credentials.api_url_v2 = json_string(root, "api_url");
        }
        credentials.noise_host = json_string(root, "noise_host");
        /* Upstream requires only non-empty ssid/password/access_token/
         * refresh_token (plus token_type == "device", checked above). */
        if (credentials.ssid == NULL || credentials.ssid[0] == '\0' ||
            credentials.password == NULL || credentials.password[0] == '\0' ||
            credentials.access_token == NULL ||
            credentials.access_token[0] == '\0' ||
            credentials.refresh_token == NULL ||
            credentials.refresh_token[0] == '\0') {
            BK_LOGW(TAG, "provision_v2 missing credentials\r\n");
            send_status("error_missing_credentials");
        } else if (!muse_storage_commit(&credentials)) {
            BK_LOGW(TAG, "provision_v2 storage commit failed\r\n");
            send_status("error_storage");
        } else if (!muse_pairing_begin_provisioning(s_ctx.generation)) {
            BK_LOGW(TAG, "provision_v2 session not ready\r\n");
            send_status("error_pairing_confirm_required");
        } else {
            /* Upstream (on_provision) is asynchronous: immediately acknowledge
             * with wifi_connecting, then join Wi-Fi and emit wifi_connected /
             * auth_ok as those stages succeed; BLE shuts down after auth_ok. */
            s_ctx.provisioning = true;
            s_ctx.wifi_connected_sent = false;
            s_ctx.auth_ok_sent = false;
            s_ctx.status_retry_deadline_ms = 0U;
            s_ctx.provision_deadline_ms =
                rtos_get_time() + PROVISION_TIMEOUT_MS;
            send_status("wifi_connecting");
            BK_LOGI(TAG, "wifi_connecting ssid=%s ch=%u\r\n",
                    credentials.ssid, credentials.wifi_channel);
            /* Free the single 2.4 GHz radio for the join without dropping the
             * GATT session: relax the BLE connection interval (yields inside
             * until the L2CAP update settles). The phone stays connected and
             * subscribed, so it receives wifi_connected / auth_ok directly. */
            muse_ble_pairing_yield_rf();
            /* Kick the service lifecycle so it joins Wi-Fi and brings up the
             * cloud session; the pairing worker reports progress. */
            muse_service_on_pairing_complete();
        }
    } else {
        send_status("error_unknown_action");
    }

done:
    cJSON_Delete(root);
    secure_free(plain, strlen(plain));
}

/* Plaintext get_device_info response. Field names/order/types mirror
 * on_get_device_info() + link_pairing_add_device_info() upstream. The SDK
 * token is intentionally NOT sent in plaintext; it rides inside the
 * authenticated, encrypted pairing_confirmed status record. */
static void handle_get_device_info(void)
{
    cJSON *root = cJSON_CreateObject();
    char *json;
    bool sent;
    int len;

    if (root == NULL) return;
    cJSON_AddStringToObject(root, "type", "device_info");
    cJSON_AddStringToObject(root, "node_id", s_ctx.cfg.node_id);
    /* Upstream "version" is the firmware version string. */
    cJSON_AddStringToObject(root, "version", s_ctx.cfg.firmware_version);
    cJSON_AddStringToObject(root, "device_id", s_ctx.cfg.device_id);
    cJSON_AddStringToObject(root, "mac", s_ctx.mac);
    cJSON_AddStringToObject(root, "model", s_ctx.cfg.model);
    cJSON_AddNumberToObject(root, "pairing_protocol", MUSE_PAIRING_VERSION);
    cJSON_AddStringToObject(root, "pairing_auth", COMMUNITY_AUTH);
    cJSON_AddNumberToObject(root, "pairing_auth_epoch", 0);
    cJSON_AddStringToObject(root, "pairing_policy", PAIRING_POLICY);
    /* No firmware-ELF SHA descriptor on BK; keep the key present as "". */
    cJSON_AddStringToObject(root, "build_sha", "");
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) return;
    len = (int)strlen(json);
    sent = notify_chunked(json);
    BK_LOGI(TAG, "device_info tx %dB model=%s fw=%s mac=%s dev=%s node=%s\r\n",
            len, s_ctx.cfg.model, s_ctx.cfg.firmware_version, s_ctx.mac,
            s_ctx.cfg.device_id, s_ctx.cfg.node_id);
    (void)sent;
    free(json);
}

static void dispatch_message(const uint8_t *data, size_t len)
{
    cJSON *root;
    const char *action;
    char *plain;

    if (len == 0 || len > MAX_RX_TOTAL_BYTES || memchr(data, '\0', len)) {
        send_plain_error("error_invalid_command");
        return;
    }
    root = cJSON_ParseWithLength((const char *)data, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        send_plain_error("error_invalid_command");
        return;
    }
    action = json_string(root, "action");
    BK_LOGI(TAG, "dispatch action=%s generation=%lu\r\n",
            action ? action : "(none)", (unsigned long)s_ctx.generation);
    if (action != NULL && strcmp(action, "get_device_info") == 0) {
        handle_get_device_info();
    } else if (s_ctx.generation == 0 && action != NULL &&
        strcmp(action, "pairing_client_hello") == 0) {
        if (!handle_hello(root)) send_plain_error("error_pairing_invalid_hello");
    } else if (action != NULL && strcmp(action, "pairing_encrypted") == 0) {
        plain = decrypt_envelope(root);
        if (plain == NULL) {
            send_plain_error("error_pairing_decrypt");
            reset_session();
        } else {
            handle_decrypted(plain);
        }
    } else {
        send_plain_error(s_ctx.generation == 0 ?
                         "error_encryption_required" :
                         "error_pairing_decrypt");
    }
    cJSON_Delete(root);
}

/* Runs on the 3 KB EtherMind write task: copy the payload into a heap message
 * and hand it to the muse_ble worker. No cJSON or mbedtls work here. */
#define RX_QUEUE_DEPTH 4U
typedef struct {
    size_t len;
    uint8_t data[MAX_NOTIFY_BYTES];
} rx_msg_t;

static void enqueue_rx(const uint8_t *data, size_t len)
{
    rx_msg_t *msg;

    if (len == 0 || len > MAX_NOTIFY_BYTES) return;
    msg = malloc(sizeof(rx_msg_t));
    if (msg == NULL) {
        BK_LOGW(TAG, "rx queue alloc failed, drop %uB\r\n", (unsigned)len);
        return;
    }
    msg->len = len;
    memcpy(msg->data, data, len);
    if (rtos_push_to_queue(&s_ctx.rx_queue, &msg, 0) != BK_OK) {
        BK_LOGW(TAG, "rx queue full, drop %uB\r\n", (unsigned)len);
        free(msg);
    }
}

static void handle_rx(const uint8_t *data, size_t len)
{
    uint8_t *complete = NULL;
    size_t complete_len = 0;

    if (len < CHUNK_HEADER_BYTES || data[0] != CHUNK_MAGIC) {
        dispatch_message(data, len);
        return;
    }
    rtos_lock_mutex(&s_ctx.lock);
    if (data[2] == 0 || data[1] >= data[2]) {
        reset_rx_locked();
        goto done;
    }
    if (data[1] == 0) {
        reset_rx_locked();
        s_ctx.rx_buf = malloc(MAX_RX_TOTAL_BYTES);
        if (s_ctx.rx_buf == NULL) goto done;
        s_ctx.rx_total = data[2];
    }
    if (s_ctx.rx_buf == NULL || data[2] != s_ctx.rx_total ||
        data[1] != s_ctx.rx_next ||
        s_ctx.rx_len + len - CHUNK_HEADER_BYTES > MAX_RX_TOTAL_BYTES) {
        reset_rx_locked();
        goto done;
    }
    memcpy(s_ctx.rx_buf + s_ctx.rx_len, data + CHUNK_HEADER_BYTES,
           len - CHUNK_HEADER_BYTES);
    s_ctx.rx_len += len - CHUNK_HEADER_BYTES;
    s_ctx.rx_next++;
    if (s_ctx.rx_next == s_ctx.rx_total) {
        complete = s_ctx.rx_buf;
        complete_len = s_ctx.rx_len;
        s_ctx.rx_buf = NULL;
        s_ctx.rx_len = 0;
        s_ctx.rx_total = s_ctx.rx_next = 0;
    }
done:
    rtos_unlock_mutex(&s_ctx.lock);
    if (complete != NULL) {
        dispatch_message(complete, complete_len);
        secure_free(complete, complete_len);
    }
}

static void write_response(const struct gatts_write_evt_param *param,
                           bk_gatt_status_t status)
{
    bk_gatt_rsp_t rsp;
    if (!param->need_rsp) return;
    memset(&rsp, 0, sizeof(rsp));
    rsp.attr_value.handle = param->handle;
    rsp.attr_value.offset = param->offset;
    rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
    (void)bk_ble_gatts_send_response(s_ctx.gatts_if, param->conn_id,
                                     param->trans_id, status, &rsp);
}

static int32_t gatts_callback(bk_gatts_cb_event_t event,
                              bk_gatt_if_t gatts_if,
                              bk_ble_gatts_cb_param_t *param)
{
    switch (event) {
    case BK_GATTS_REG_EVT:
        s_ctx.gatts_if = param->reg.gatt_if;
        if (s_ctx.sync) rtos_set_semaphore(&s_ctx.sync);
        break;
    case BK_GATTS_CREAT_ATTR_TAB_EVT:
        if (param->add_attr_tab.status == BK_GATT_OK &&
            param->add_attr_tab.num_handle == ATTR_COUNT) {
            memcpy(s_ctx.handles, param->add_attr_tab.handles,
                   sizeof(s_ctx.handles));
        }
        if (s_ctx.sync) rtos_set_semaphore(&s_ctx.sync);
        break;
    case BK_GATTS_START_EVT:
    case BK_GATTS_UNREG_EVT:
        if (s_ctx.sync) rtos_set_semaphore(&s_ctx.sync);
        break;
    case BK_GATTS_CONNECT_EVT:
        s_ctx.conn_id = param->connect.conn_id;
        memcpy(s_ctx.peer_addr, param->connect.remote_bda,
               sizeof(s_ctx.peer_addr));
        s_ctx.advertising = false;
        /* A fresh link can carry any deferred provisioning result at once. */
        s_ctx.status_retry_deadline_ms = 0U;
        break;
    case BK_GATTS_DISCONNECT_EVT:
        s_ctx.conn_id = UINT16_MAX;
        s_ctx.subscribed = false;
        s_ctx.mtu = 23;
        s_ctx.rf_relaxed = false;
        /* While a provisioning session is active, an unexpected link drop must
         * not tear down the Noise pairing session/provisioning state; only the
         * pairing worker resets that on timeout. Outside provisioning a drop
         * means the user walked away, so reset and re-advertise. */
        if (!s_ctx.provisioning) {
            reset_session();
        }
        if (!s_ctx.stopping && s_ctx.teardown_deadline_ms == 0U) {
            const bk_ble_gap_ext_adv_t adv = {
                .instance = ADV_INSTANCE, .duration = 0, .max_events = 0
            };
            if (bk_ble_gap_adv_start(1, &adv) == BK_OK) {
                s_ctx.advertising = true;
            }
        }
        break;
    case BK_GATTS_MTU_EVT:
        s_ctx.mtu = param->mtu.mtu;
        break;
    case BK_GATTS_WRITE_EVT:
        if (param->write.is_prep || param->write.offset != 0) {
            write_response(&param->write, BK_GATT_REQ_NOT_SUPPORTED);
        } else if (param->write.handle == s_ctx.handles[ATTR_TX_CCC]) {
            if (param->write.len != 2) {
                write_response(&param->write, BK_GATT_INVALID_ATTR_LEN);
            } else {
                s_ccc_value = (uint16_t)param->write.value[0] |
                              (uint16_t)param->write.value[1] << 8;
                s_ctx.subscribed = (s_ccc_value & 1U) != 0;
                if (s_ctx.subscribed) {
                    /* Re-subscribed after a transient drop: flush any pending
                     * provisioning result on the next worker poll. */
                    s_ctx.status_retry_deadline_ms = 0U;
                }
                write_response(&param->write, BK_GATT_OK);
            }
        } else if (param->write.handle == s_ctx.handles[ATTR_RX]) {
            if (param->write.len == 0 ||
                param->write.len > MAX_NOTIFY_BYTES) {
                write_response(&param->write, BK_GATT_INVALID_ATTR_LEN);
            } else {
                write_response(&param->write, BK_GATT_OK);
                /* Defer cJSON/ECDH to the large-stack muse_ble worker; the
                 * EtherMind write task has only ~3 KB of stack. */
                enqueue_rx(param->write.value, param->write.len);
            }
        } else {
            write_response(&param->write, BK_GATT_WRITE_NOT_PERMIT);
        }
        break;
    case BK_GATTS_READ_EVT:
        if (param->read.need_rsp) {
            bk_gatt_rsp_t rsp;
            memset(&rsp, 0, sizeof(rsp));
            rsp.attr_value.handle = param->read.handle;
            rsp.attr_value.auth_req = BK_GATT_AUTH_REQ_NONE;
            (void)bk_ble_gatts_send_response(
                gatts_if, param->read.conn_id, param->read.trans_id,
                BK_GATT_READ_NOT_PERMIT, &rsp);
        }
        break;
    default:
        break;
    }
    return 0;
}

static void gap_callback(bk_ble_gap_cb_event_t event,
                         bk_ble_gap_cb_param_t *param)
{
    bool signal = false;
    switch (event) {
    case BK_BLE_GAP_EXT_ADV_PARAMS_SET_COMPLETE_EVT:
        signal = true;
        break;
    case BK_BLE_GAP_EXT_ADV_DATA_RAW_SET_COMPLETE_EVT:
        signal = true;
        break;
    case BK_BLE_GAP_EXT_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
        signal = true;
        break;
    case BK_BLE_GAP_EXT_ADV_START_COMPLETE_EVT:
        s_ctx.advertising = true;
        signal = true;
        break;
    case BK_BLE_GAP_EXT_ADV_STOP_COMPLETE_EVT:
    case BK_BLE_GAP_EXT_ADV_SET_REMOVE_COMPLETE_EVT:
        s_ctx.advertising = false;
        signal = true;
        break;
    default:
        (void)param;
        break;
    }
    if (signal && s_ctx.sync) rtos_set_semaphore(&s_ctx.sync);
}

static int wait_sync(void)
{
    return rtos_get_semaphore(&s_ctx.sync, SYNC_TIMEOUT_MS) == BK_OK ?
           MUSE_BLE_PAIRING_OK : MUSE_BLE_PAIRING_ERR_TIMEOUT;
}

static int setup_advertising(void)
{
    bk_ble_gap_ext_adv_params_t params;
    bk_ble_gap_ext_adv_t adv;
    uint8_t adv_data[31];
    uint8_t scan_rsp[31];
    size_t p = 0, name_len;

    memset(&params, 0, sizeof(params));
    params.type = BK_BLE_GAP_SET_EXT_ADV_PROP_LEGACY_IND;
    params.interval_min = 120;
    params.interval_max = 160;
    params.channel_map = BK_ADV_CHNL_ALL;
    params.filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;
    params.primary_phy = BK_BLE_GAP_PRI_PHY_1M;
    params.secondary_phy = BK_BLE_GAP_PHY_1M;
    params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    params.tx_power = EXT_ADV_TX_PWR_NO_PREFERENCE;

    if (bk_ble_gap_set_device_name(s_ctx.adv_name) != BK_OK ||
        bk_ble_gap_set_adv_params(ADV_INSTANCE, &params) != BK_OK ||
        wait_sync() != 0) {
        return MUSE_BLE_PAIRING_ERR_BLE;
    }

    adv_data[p++] = 2;
    adv_data[p++] = BK_BLE_AD_TYPE_FLAG;
    adv_data[p++] = 0x06;
    adv_data[p++] = 17;
    adv_data[p++] = BK_BLE_AD_TYPE_128SRV_CMPL;
    memcpy(adv_data + p, s_service_uuid, sizeof(s_service_uuid));
    p += sizeof(s_service_uuid);
    if (bk_ble_gap_set_adv_data_raw(ADV_INSTANCE, (uint16_t)p,
                                    adv_data) != BK_OK ||
        wait_sync() != 0) {
        return MUSE_BLE_PAIRING_ERR_BLE;
    }

    name_len = strlen(s_ctx.adv_name);
    if (name_len > sizeof(scan_rsp) - 2) name_len = sizeof(scan_rsp) - 2;
    scan_rsp[0] = (uint8_t)(name_len + 1);
    scan_rsp[1] = BK_BLE_AD_TYPE_NAME_CMPL;
    memcpy(scan_rsp + 2, s_ctx.adv_name, name_len);
    if (bk_ble_gap_set_scan_rsp_data_raw(
            ADV_INSTANCE, (uint16_t)(name_len + 2), scan_rsp) != BK_OK ||
        wait_sync() != 0) {
        return MUSE_BLE_PAIRING_ERR_BLE;
    }

    memset(&adv, 0, sizeof(adv));
    adv.instance = ADV_INSTANCE;
    if (bk_ble_gap_adv_start(1, &adv) != BK_OK || wait_sync() != 0) {
        return MUSE_BLE_PAIRING_ERR_BLE;
    }
    return MUSE_BLE_PAIRING_OK;
}

static void pairing_worker(beken_thread_arg_t arg)
{
    (void)arg;
    while (!s_ctx.stopping && !s_ctx.stop_requested) {
        rx_msg_t *msg = NULL;
        muse_pairing_state_t state;

        /* Wait up to 50 ms for an RX frame; cJSON and P-256 ECDH run on this
         * large-stack worker, never on the 3 KB EtherMind write task. */
        if (rtos_pop_from_queue(&s_ctx.rx_queue, &msg, 50) == BK_OK &&
            msg != NULL) {
            handle_rx(msg->data, msg->len);
            free(msg);
            continue;
        }

        state = muse_pairing_state();
        /* Post-auth_ok grace: the link stays up and RX keeps being handled so
         * the phone can finish its post-auth_ok steps and hang up on its own
         * (upstream Muse never disconnects the client after auth_ok). The
         * deadline is only a fallback to free the radio for the noise link. */
        if (s_ctx.teardown_deadline_ms != 0U &&
            (s_ctx.conn_id == UINT16_MAX ||
             time_expired(s_ctx.teardown_deadline_ms))) {
            if (s_ctx.conn_id != UINT16_MAX) {
                (void)bk_ble_gap_disconnect(s_ctx.peer_addr);
            }
            s_ctx.stop_requested = true;
            break;
        }
        if (s_ctx.provisioning) {
            muse_service_status_t svc;
            muse_service_get_status(&svc);
            /* A provisioning result can only be pushed over a live, subscribed
             * GATT session. During dense Wi-Fi/TLS traffic the single 2.4 GHz
             * radio may briefly drop the BLE link (HCI reason 0x08); the phone
             * then reconnects and re-subscribes. Do NOT spin every 50 ms
             * re-sending while the link is down: that floods the log and keeps
             * waking BLE, which itself starves Wi-Fi. Gate all result notifies
             * on a live link and throttle any genuine TX failure to ~1 Hz. */
            bool link_up = s_ctx.conn_id != UINT16_MAX && s_ctx.subscribed;
            uint32_t now_ms = rtos_get_time();

            /* status_retry_deadline_ms == 0 means "send at the next poll"
             * (set on provision_v2 / reconnect / resubscribe / after a
             * successful notify); time_expired() treats 0 as "no deadline",
             * so it must be handled explicitly here. */
            if (!s_ctx.wifi_connected_sent && svc.network_ready && link_up &&
                (s_ctx.status_retry_deadline_ms == 0U ||
                 time_expired(s_ctx.status_retry_deadline_ms))) {
                s_ctx.wifi_connected_sent = true;
                /* Keep the relaxed 500 ms / 6 s parameters through the whole
                 * cloud window instead of restoring 15-30 ms here: the heavy
                 * TLS/Noise downlink is exactly when the phone (often the
                 * hotspot itself) has the least BT scheduling room, and the
                 * aggressive restore was seen to wedge the link (conn update
                 * unanswered for 11 s, then supervision timeout 0x08). The
                 * tiny status JSONs still reach the phone within one 500 ms
                 * anchor; upstream never touches the interval at all. */
                if (!send_status("wifi_connected")) {
                    /* Link vanished between the gate and TX. Back off and
                     * re-arm for a later attempt (incl. after reconnect). */
                    s_ctx.wifi_connected_sent = false;
                    s_ctx.status_retry_deadline_ms =
                        now_ms + STATUS_NOTIFY_RETRY_MS;
                    BK_LOGW(TAG, "wifi_connected notify deferred (BLE link down); will retry on reconnect\r\n");
                } else {
                    s_ctx.status_retry_deadline_ms = 0U;
                    BK_LOGI(TAG, "wifi_connected\r\n");
                }
            }
            if (s_ctx.wifi_connected_sent && !s_ctx.auth_ok_sent &&
                svc.noise_connected && link_up &&
                (s_ctx.status_retry_deadline_ms == 0U ||
                 time_expired(s_ctx.status_retry_deadline_ms))) {
                s_ctx.auth_ok_sent = true;
                if (send_status("auth_ok") &&
                    muse_pairing_commit(s_ctx.generation)) {
                    BK_LOGI(TAG, "auth_ok; pairing complete\r\n");
                    s_ctx.provisioning = false;
                    /* Upstream Muse keeps the BLE client connected after
                     * auth_ok (ble_server_stop_advertising(false)) and lets
                     * the phone hang up once its post-auth_ok cloud steps are
                     * done; kicking the phone off earlier was seen to silently
                     * abort the app's finalize (no error, device never added).
                     * Keep serving this link until the phone disconnects; the
                     * deadline is just a fallback. stop_requested stays clear
                     * until then: the service worker tears down all of BLE the
                     * moment it sees it set. */
                    s_ctx.teardown_deadline_ms =
                        now_ms + POST_AUTH_OK_GRACE_MS;
                } else {
                    s_ctx.auth_ok_sent = false;
                    s_ctx.status_retry_deadline_ms =
                        now_ms + STATUS_NOTIFY_RETRY_MS;
                    BK_LOGW(TAG, "auth_ok notify deferred (BLE link down); will retry on reconnect\r\n");
                }
            }
            if (time_expired(s_ctx.provision_deadline_ms)) {
                BK_LOGW(TAG, "provisioning timed out\r\n");
                /* Report the failure on the live GATT session so the app can
                 * surface/retry it. Stay on the relaxed interval (see the
                 * wifi_connected path); the link is about to be reset anyway. */
                send_status("wifi_failed");
                s_ctx.provisioning = false;
                reset_session();
            }
        } else if (s_ctx.generation != 0 && time_expired(s_ctx.deadline_ms)) {
            BK_LOGW(TAG, "pairing session expired\r\n");
            reset_session();
        } else if (state == MUSE_PAIRING_READY && !s_ctx.confirmed_sent) {
            s_ctx.confirmed_sent = true;
            s_ctx.deadline_ms = rtos_get_time() + SESSION_TIMEOUT_MS;
            if (!send_status("pairing_confirmed")) {
                BK_LOGW(TAG, "failed to notify pairing confirmation\r\n");
            } else {
                BK_LOGI(TAG, "pairing_confirmed sent%s; awaiting provision_v2\r\n",
                        (s_ctx.cfg.sdk_token != NULL &&
                         *s_ctx.cfg.sdk_token != '\0') ? " (with sdk_token)" : "");
            }
        }
    }
    s_ctx.worker = NULL;
    rtos_delete_thread(NULL);
}

int muse_ble_pairing_start(const muse_ble_pairing_config_t *config)
{
    uint8_t mac[6] = {0};
    int rc;

    if (config == NULL || config->node_id == NULL ||
        config->device_id == NULL || config->firmware_version == NULL) {
        return MUSE_BLE_PAIRING_ERR_ARGUMENT;
    }
    if (s_ctx.started) return MUSE_BLE_PAIRING_ERR_BUSY;

    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.conn_id = UINT16_MAX;
    s_ctx.mtu = 23;
    s_ctx.cfg = *config;
    if (s_ctx.cfg.model == NULL) s_ctx.cfg.model = DEFAULT_MODEL;
    if (rtos_init_mutex(&s_ctx.lock) != BK_OK) {
        return MUSE_BLE_PAIRING_ERR_MEMORY;
    }
    if (rtos_init_mutex(&s_ctx.tx_lock) != BK_OK) {
        rtos_deinit_mutex(&s_ctx.lock);
        return MUSE_BLE_PAIRING_ERR_MEMORY;
    }
    if (rtos_init_semaphore(&s_ctx.sync, 1) != BK_OK) {
        rtos_deinit_mutex(&s_ctx.tx_lock);
        rtos_deinit_mutex(&s_ctx.lock);
        return MUSE_BLE_PAIRING_ERR_MEMORY;
    }
    if (rtos_init_queue(&s_ctx.rx_queue, "muse_rx", sizeof(rx_msg_t *),
                        RX_QUEUE_DEPTH) != BK_OK) {
        rtos_deinit_semaphore(&s_ctx.sync);
        rtos_deinit_mutex(&s_ctx.tx_lock);
        rtos_deinit_mutex(&s_ctx.lock);
        return MUSE_BLE_PAIRING_ERR_MEMORY;
    }

    mbedtls_ecp_group_init(&s_ctx.group);
    mbedtls_mpi_init(&s_ctx.private_key);
    mbedtls_ecp_point_init(&s_ctx.public_key);
    if (mbedtls_ecp_group_load(&s_ctx.group,
                               MBEDTLS_ECP_DP_SECP256R1) != 0) {
        rc = MUSE_BLE_PAIRING_ERR_BLE;
        goto fail;
    }
    /* Identity comes from the WiFi STA MAC (config.mac, lower-case colon
     * form). The BLE advertise suffix uses its last three octets, matching
     * upstream identity_init(); never the (randomised) BLE address. */
    if (config->mac != NULL &&
        sscanf(config->mac,
               "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
               &mac[0], &mac[1], &mac[2], &mac[3], &mac[4],
               &mac[5]) == 6) {
        snprintf(s_ctx.mac, sizeof(s_ctx.mac), "%s", config->mac);
    } else {
        snprintf(s_ctx.mac, sizeof(s_ctx.mac),
                 "%02x:%02x:%02x:%02x:%02x:%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    snprintf(s_ctx.adv_name, sizeof(s_ctx.adv_name),
             ADV_NAME_PREFIX "%02X%02X%02X", mac[3], mac[4], mac[5]);

    /*
     * These are singleton callbacks in this SDK. The caller contract requires
     * bk_ble_provisioning_deinit() before reaching this point.
     */
    if (bk_ble_gap_register_callback(gap_callback) != BK_OK ||
        bk_ble_gatts_register_callback(gatts_callback) != BK_OK ||
        bk_ble_gatts_app_register(GATTS_APP_ID) != BK_OK ||
        wait_sync() != 0) {
        rc = MUSE_BLE_PAIRING_ERR_BLE;
        goto fail;
    }
    if (bk_ble_gatts_create_attr_tab(s_attr_db, s_ctx.gatts_if,
                                     ATTR_COUNT, ATTR_COUNT) != BK_OK ||
        wait_sync() != 0 || s_ctx.handles[ATTR_SERVICE] == 0 ||
        bk_ble_gatts_start_service(s_ctx.handles[ATTR_SERVICE]) != BK_OK ||
        wait_sync() != 0) {
        rc = MUSE_BLE_PAIRING_ERR_BLE;
        goto fail;
    }
    rc = setup_advertising();
    if (rc != 0) goto fail;

    s_ctx.started = true;
    /* P-256 ecp_gen_keypair/ecp_mul have a deep modular-inverse chain. The
     * RNG callback uses the platform TRNG (bk_fill_rand), not a stack-resident
     * mbedtls entropy pool; 16 KB keeps comfortable headroom. */
    if (rtos_create_thread(&s_ctx.worker, 5, "muse_ble",
                           pairing_worker, 16384, NULL) != BK_OK) {
        rc = MUSE_BLE_PAIRING_ERR_MEMORY;
        goto fail;
    }
    BK_LOGI(TAG, "advertising as %s\r\n", s_ctx.adv_name);
    return MUSE_BLE_PAIRING_OK;

fail:
    (void)muse_ble_pairing_stop();
    return rc;
}

int muse_ble_pairing_stop(void)
{
    uint8_t instance = ADV_INSTANCE;
    uint32_t wait_count = 0;

    if (s_ctx.lock == NULL && s_ctx.sync == NULL) return MUSE_BLE_PAIRING_OK;
    s_ctx.provisioning = false;
    s_ctx.stopping = true;
    if (s_ctx.advertising &&
        bk_ble_gap_adv_stop(1, &instance) == BK_OK) {
        (void)wait_sync();
    }
    if (s_ctx.conn_id != UINT16_MAX) {
        (void)bk_ble_gap_disconnect(s_ctx.peer_addr);
        rtos_delay_milliseconds(100);
    }
    if (s_ctx.gatts_if != 0 &&
        bk_ble_gatts_app_unregister(s_ctx.gatts_if) == BK_OK) {
        (void)wait_sync();
    }
    (void)bk_ble_gap_adv_set_remove(ADV_INSTANCE);
    (void)bk_ble_gatts_register_callback(NULL);
    (void)bk_ble_gap_register_callback(NULL);

    while (s_ctx.worker != NULL && wait_count++ < 20) {
        rtos_delay_milliseconds(10);
    }
    /* Worker has exited; drain RX messages it never consumed. */
    if (s_ctx.rx_queue != NULL) {
        rx_msg_t *msg = NULL;
        while (rtos_pop_from_queue(&s_ctx.rx_queue, &msg, 0) == BK_OK &&
               msg != NULL) {
            free(msg);
        }
        rtos_deinit_queue(&s_ctx.rx_queue);
    }
    if (s_ctx.lock != NULL) {
        reset_session();
        mbedtls_ecp_group_free(&s_ctx.group);
        mbedtls_mpi_free(&s_ctx.private_key);
        mbedtls_ecp_point_free(&s_ctx.public_key);
        rtos_deinit_mutex(&s_ctx.lock);
    }
    if (s_ctx.tx_lock != NULL) rtos_deinit_mutex(&s_ctx.tx_lock);
    if (s_ctx.sync != NULL) rtos_deinit_semaphore(&s_ctx.sync);
    memset(&s_ctx, 0, sizeof(s_ctx));
    return MUSE_BLE_PAIRING_OK;
}

bool muse_ble_pairing_is_started(void)
{
    return s_ctx.started && !s_ctx.stopping;
}

bool muse_ble_pairing_provision_active(void)
{
    /* True while provision_v2 is in progress (wifi_connecting..auth_ok). The
     * service worker must keep BLE up until the pairing worker shuts it down. */
    return s_ctx.started && s_ctx.provisioning;
}

bool muse_ble_pairing_teardown_requested(void)
{
    /* Set by the pairing worker after auth_ok; service then calls stop(). */
    return s_ctx.started && s_ctx.stop_requested;
}

/* Ask the controller to move the live BLE link to the given connection
 * interval. The update is negotiated with the phone (L2CAP conn param update);
 * it is best-effort and asynchronous -- we give it a short settle time at the
 * call site. Staying connected (rather than dropping the link) keeps the GATT
 * session and CCC subscription valid so progress statuses keep flowing. */
static void request_conn_interval(uint16_t min_int, uint16_t max_int,
                                  uint16_t timeout)
{
    bk_ble_conn_update_params_t params;

    if (s_ctx.conn_id == UINT16_MAX) {
        return;
    }
    memset(&params, 0, sizeof(params));
    memcpy(params.bda, s_ctx.peer_addr, sizeof(params.bda));
    params.min_int = min_int;
    params.max_int = max_int;
    params.latency = BLE_CONN_LATENCY;
    params.timeout = timeout;
    if (bk_ble_gap_update_conn_params(&params) != BK_OK) {
        BK_LOGW(TAG, "conn param update (%u-%u) rejected\r\n",
                (unsigned)min_int, (unsigned)max_int);
    }
}

void muse_ble_pairing_yield_rf(void)
{
    /* Called right after "wifi_connecting" is sent and before the STA joins.
     * Keep the BLE GATT session up (the phone needs it to learn the result) but
     * relax the connection interval to 500 ms so the dense Auth/Assoc/EAPOL
     * exchanges have the single 2.4 GHz radio almost entirely to themselves.
     * If no phone is attached there is nothing to reschedule. */
    if (!s_ctx.started || s_ctx.rf_relaxed) {
        return;
    }
    if (s_ctx.conn_id == UINT16_MAX) {
        BK_LOGI(TAG, "no BLE link during Wi-Fi join; radio already free\r\n");
        return;
    }
    s_ctx.rf_relaxed = true;
    BK_LOGI(TAG, "relaxing BLE interval for Wi-Fi association (link kept up)\r\n");
    request_conn_interval(BLE_CONN_INT_RELAX_MIN, BLE_CONN_INT_RELAX_MAX,
                          BLE_CONN_TIMEOUT_RELAX);
    /* Let the L2CAP connection update take effect before the STA starts. */
    rtos_delay_milliseconds(BLE_CONN_PARAM_SETTLE_MS);
}

int muse_ble_pairing_confirm(void)
{
    if (!s_ctx.started || s_ctx.generation == 0 ||
        !muse_pairing_confirm(s_ctx.generation)) {
        return MUSE_BLE_PAIRING_ERR_BUSY;
    }
    return MUSE_BLE_PAIRING_OK;
}
