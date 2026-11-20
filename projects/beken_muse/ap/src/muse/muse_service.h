/**
 * @file muse_service.h
 * @brief Platform-neutral lifecycle for the minimum Muse Gadget integration.
 */
#ifndef __BK_MUSE_SERVICE_H__
#define __BK_MUSE_SERVICE_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MUSE_SERVICE_STOPPED = 0,
    MUSE_SERVICE_WAITING_FOR_PAIRING,
    MUSE_SERVICE_WAITING_FOR_CONFIRMATION,
    MUSE_SERVICE_WAITING_FOR_NETWORK,
    MUSE_SERVICE_CONNECTING,
    MUSE_SERVICE_CONNECTED,
    MUSE_SERVICE_ERROR,
} muse_service_state_t;

typedef struct {
    muse_service_state_t state;
    bool paired;
    bool network_ready;
    bool noise_connected;
    int last_error;
} muse_service_status_t;

enum {
    MUSE_SERVICE_OK = 0,
    MUSE_SERVICE_ERR_ARGUMENT = -100,
    MUSE_SERVICE_ERR_NOT_INITIALIZED = -101,
    MUSE_SERVICE_ERR_NO_CA_CERT = -102,
    MUSE_SERVICE_ERR_IDENTITY = -103,
    MUSE_SERVICE_ERR_STORAGE = -104,
    MUSE_SERVICE_ERR_WORKER = -105,
    MUSE_SERVICE_ERR_BUSY = -106,
    MUSE_SERVICE_ERR_WIFI = -107,
};

int muse_service_init(void);
/**
 * Copies a PEM trust anchor into service-owned storage. Configure it before
 * start; changing it while the worker is running returns ERR_BUSY.
 * len may include the terminating NUL, or be zero to use strlen().
 */
int muse_service_set_ca_cert(const char *pem, size_t len);
/* Persist (flash + RAM) a runtime `mgst_...` SDK token. Independent of
 * pairing state; survives reboot and is not cleared by clear_pairing(). */
int muse_service_set_sdk_token(const char *token);
int muse_service_start(void);
int muse_service_stop(void);
int muse_service_clear_pairing(void);
/* Stops the service, persists a fresh identity salt and clears the pairing,
 * so the next start() advertises/pairs as a brand-new device. Returns the
 * 24-bit salt (= new "MuseGadget-XXXXXX" suffix) via out_salt. */
int muse_service_new_identity(uint32_t *out_salt);
int muse_service_confirm_pairing(void);
int muse_service_apply_wifi(const char *ssid, const char *password,
                            unsigned int channel, bool hidden);
void muse_service_get_status(muse_service_status_t *status);
const char *muse_service_state_name(muse_service_state_t state);

/* Node identity ("homelink-<tail>"), derived from the WiFi MAC (or the
 * persisted new-identity salt) at service start. Empty string before the
 * first start. The voice note heads send it as the note's device_id so
 * the server attributes the turn to this board (Muse support, 2026-10:
 * without it the note lands in the account main chat). */
const char *muse_service_node_id(void);

/* 由 chat 会话调用：连续短命会话说明浏览器验证流程替换了设备侧 Noise
 * 密钥，单靠重连永远被踢。请求受理后 service worker 会停掉两条 Noise
 * 会话、重新拉取设备令牌并重建（等价于重启设备的恢复路径）。任意任务
 * 可调用；返回 false 表示 service 未运行、请求未被受理。 */
bool muse_service_request_token_refresh(void);

/* Unit-test and port callbacks; all are safe to call from event callbacks. */
void muse_service_on_pairing_started(void);
void muse_service_on_pairing_complete(void);
void muse_service_on_network_changed(bool ready);

#endif
