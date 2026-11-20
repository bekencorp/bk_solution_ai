/**
 * @file muse_storage.h
 * @brief Bounded, atomic Muse credential storage.
 */
#ifndef __BK_MUSE_STORAGE_H__
#define __BK_MUSE_STORAGE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *ssid;
    const char *password;
    unsigned int wifi_channel;
    bool wifi_hidden;
    const char *access_token;
    const char *refresh_token;
    const char *vm_url;
    const char *api_url_v2;
    const char *noise_host;
} muse_storage_credentials_t;

typedef enum {
    MUSE_STORAGE_SSID = 0,
    MUSE_STORAGE_PASSWORD,
    MUSE_STORAGE_ACCESS_TOKEN,
    MUSE_STORAGE_REFRESH_TOKEN,
    MUSE_STORAGE_VM_URL,
    MUSE_STORAGE_API_URL_V2,
    MUSE_STORAGE_NOISE_HOST,
} muse_storage_field_t;

bool muse_storage_is_setup(void);
/* Legacy raw-key API retained for compatibility. */
bool muse_storage_read(const char *key, char *out, size_t out_size);
bool muse_storage_read_field(muse_storage_field_t field,
                             char *out,
                             size_t out_size);
bool muse_storage_read_wifi(unsigned int *channel, bool *hidden);
bool muse_storage_commit(const muse_storage_credentials_t *credentials);
bool muse_storage_update_tokens(const char *access_token,
                                const char *refresh_token);
/* SDK token is independent of pairing state and is not erased by clear(). */
bool muse_storage_save_sdk_token(const char *token);
bool muse_storage_load_sdk_token(char *out, size_t out_size);
/* Volcano speech credentials (old-console: APP ID + per-service Access
 * Token). Runtime-only: set via CLI (`muse volcappid/asrkey/ttskey`),
 * never baked into source/defconfig. */
bool muse_storage_save_volc_asr_key(const char *key);
bool muse_storage_load_volc_asr_key(char *out, size_t out_size);
bool muse_storage_save_volc_tts_key(const char *key);
bool muse_storage_load_volc_tts_key(char *out, size_t out_size);
bool muse_storage_save_volc_appid(const char *appid);
bool muse_storage_load_volc_appid(char *out, size_t out_size);
bool muse_storage_save_volc_asr_res(const char *res);
bool muse_storage_load_volc_asr_res(char *out, size_t out_size);
bool muse_storage_save_volc_tts_voice(const char *voice);
bool muse_storage_load_volc_tts_voice(char *out, size_t out_size);
/* Identity salt (24-bit, overrides the MAC tail in build_identity()).
 * Independent of pairing state and not erased by clear(). */
bool muse_storage_save_id_salt(uint32_t salt);
bool muse_storage_load_id_salt(uint32_t *salt);
bool muse_storage_clear(void);

#endif
