/**
 * @file muse_storage.c
 * @brief EasyFlash-backed storage with a last-written setup marker.
 */
#include "muse_storage.h"

#include <stdint.h>
#include <string.h>

#include "bk_factory_config.h"

#define KEY_SSID    "muse_ssid"
#define KEY_PASSWORD "muse_pwd"
#define KEY_CHANNEL "muse_wifi_ch"
#define KEY_HIDDEN  "muse_wifi_hid"
#define KEY_ACCESS  "muse_access"
#define KEY_REFRESH "muse_refresh"
#define KEY_VM_URL  "muse_vm_url"
#define KEY_API_V2  "muse_api_v2"
#define KEY_NOISE   "muse_noise"
#define KEY_SETUP   "muse_setup"
#define KEY_SDK_TOKEN "muse_sdk_token"
#define KEY_ID_SALT "muse_id_salt"

static bool write_string(const char *key, const char *value, size_t max)
{
    size_t len;
    if (value == NULL) {
        value = "";
    }
    len = strlen(value) + 1;
    return len <= max && bk_config_write(key, value, (int)len) == 0;
}

bool muse_storage_is_setup(void)
{
    uint32_t setup = 0;
    return bk_config_read(KEY_SETUP, &setup, sizeof(setup)) == sizeof(setup)
        && setup == 1;
}

bool muse_storage_read(const char *key, char *out, size_t out_size)
{
    int n;
    if (key == NULL || out == NULL || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    n = bk_config_read(key, out, (int)out_size);
    if (n <= 0 || (size_t)n > out_size ||
        memchr(out, '\0', (size_t)n) == NULL) {
        out[0] = '\0';
        return false;
    }
    return true;
}

bool muse_storage_read_field(muse_storage_field_t field,
                             char *out,
                             size_t out_size)
{
    const char *key;

    switch (field) {
    case MUSE_STORAGE_SSID:          key = KEY_SSID; break;
    case MUSE_STORAGE_PASSWORD:      key = KEY_PASSWORD; break;
    case MUSE_STORAGE_ACCESS_TOKEN:  key = KEY_ACCESS; break;
    case MUSE_STORAGE_REFRESH_TOKEN: key = KEY_REFRESH; break;
    case MUSE_STORAGE_VM_URL:        key = KEY_VM_URL; break;
    case MUSE_STORAGE_API_URL_V2:    key = KEY_API_V2; break;
    case MUSE_STORAGE_NOISE_HOST:    key = KEY_NOISE; break;
    default: return false;
    }
    return muse_storage_read(key, out, out_size);
}

bool muse_storage_read_wifi(unsigned int *channel, bool *hidden)
{
    uint32_t stored_channel = 0;
    uint32_t stored_hidden = 0;
    if (channel == NULL || hidden == NULL ||
        bk_config_read(KEY_CHANNEL, &stored_channel,
                       sizeof(stored_channel)) != sizeof(stored_channel) ||
        bk_config_read(KEY_HIDDEN, &stored_hidden,
                       sizeof(stored_hidden)) != sizeof(stored_hidden)) {
        return false;
    }
    *channel = stored_channel;
    *hidden = stored_hidden != 0U;
    return true;
}

bool muse_storage_commit(const muse_storage_credentials_t *credentials)
{
    uint32_t setup = 1;
    uint32_t clear = 0;
    uint32_t channel;
    uint32_t hidden;
    if (credentials == NULL || credentials->access_token == NULL ||
        credentials->access_token[0] == '\0' ||
        credentials->refresh_token == NULL ||
        credentials->refresh_token[0] == '\0' ||
        credentials->ssid == NULL || credentials->ssid[0] == '\0' ||
        credentials->password == NULL) {
        return false;
    }
    channel = credentials->wifi_channel;
    hidden = credentials->wifi_hidden ? 1U : 0U;

    /* Clear the commit marker first; it is written last after every value. */
    if (bk_config_write(KEY_SETUP, &clear, sizeof(clear)) != 0 ||
        !write_string(KEY_SSID, credentials->ssid, 33) ||
        !write_string(KEY_PASSWORD, credentials->password, 65) ||
        bk_config_write(KEY_CHANNEL, &channel, sizeof(channel)) != 0 ||
        bk_config_write(KEY_HIDDEN, &hidden, sizeof(hidden)) != 0 ||
        !write_string(KEY_ACCESS, credentials->access_token, 2048) ||
        !write_string(KEY_REFRESH, credentials->refresh_token, 2048) ||
        !write_string(KEY_VM_URL, credentials->vm_url, 384) ||
        !write_string(KEY_API_V2, credentials->api_url_v2, 256) ||
        !write_string(KEY_NOISE, credentials->noise_host, 128) ||
        bk_config_write(KEY_SETUP, &setup, sizeof(setup)) != 0) {
        (void)bk_config_write(KEY_SETUP, &clear, sizeof(clear));
        (void)bk_config_sync_flash_safely();
        return false;
    }
    return bk_config_sync_flash_safely() == BK_OK;
}

bool muse_storage_update_tokens(const char *access_token,
                                const char *refresh_token)
{
    uint32_t setup = 1;
    uint32_t clear = 0;
    if (!muse_storage_is_setup() || access_token == NULL ||
        refresh_token == NULL || access_token[0] == '\0' ||
        refresh_token[0] == '\0') {
        return false;
    }
    if (bk_config_write(KEY_SETUP, &clear, sizeof(clear)) != 0 ||
        !write_string(KEY_ACCESS, access_token, 2048) ||
        !write_string(KEY_REFRESH, refresh_token, 2048) ||
        bk_config_write(KEY_SETUP, &setup, sizeof(setup)) != 0) {
        (void)bk_config_write(KEY_SETUP, &clear, sizeof(clear));
        (void)bk_config_sync_flash_safely();
        return false;
    }
    return bk_config_sync_flash_safely() == BK_OK;
}

bool muse_storage_save_sdk_token(const char *token)
{
    if (token == NULL || token[0] == '\0') {
        return false;
    }
    if (!write_string(KEY_SDK_TOKEN, token, 2048)) {
        return false;
    }
    return bk_config_sync_flash_safely() == BK_OK;
}

bool muse_storage_load_sdk_token(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }
    return muse_storage_read(KEY_SDK_TOKEN, out, out_size);
}

bool muse_storage_save_id_salt(uint32_t salt)
{
    if (bk_config_write(KEY_ID_SALT, &salt, sizeof(salt)) != 0) {
        return false;
    }
    return bk_config_sync_flash_safely() == BK_OK;
}

bool muse_storage_load_id_salt(uint32_t *salt)
{
    if (salt == NULL) {
        return false;
    }
    return bk_config_read(KEY_ID_SALT, salt, sizeof(*salt)) ==
           sizeof(*salt);
}

bool muse_storage_clear(void)
{
    static const char empty[] = "";
    uint32_t setup = 0;
    bool ok = bk_config_write(KEY_SETUP, &setup, sizeof(setup)) == 0;
    ok = bk_config_write(KEY_SSID, empty, sizeof(empty)) == 0 && ok;
    ok = bk_config_write(KEY_PASSWORD, empty, sizeof(empty)) == 0 && ok;
    ok = bk_config_write(KEY_CHANNEL, &setup, sizeof(setup)) == 0 && ok;
    ok = bk_config_write(KEY_HIDDEN, &setup, sizeof(setup)) == 0 && ok;
    ok = bk_config_write(KEY_ACCESS, empty, sizeof(empty)) == 0 && ok;
    ok = bk_config_write(KEY_REFRESH, empty, sizeof(empty)) == 0 && ok;
    ok = bk_config_write(KEY_VM_URL, empty, sizeof(empty)) == 0 && ok;
    ok = bk_config_write(KEY_API_V2, empty, sizeof(empty)) == 0 && ok;
    ok = bk_config_write(KEY_NOISE, empty, sizeof(empty)) == 0 && ok;
    return bk_config_sync_flash_safely() == BK_OK && ok;
}
