/**
 * @file muse_session.h
 * @brief Minimum Muse Noise control-session lifecycle.
 */
#ifndef __BK_MUSE_SESSION_H__
#define __BK_MUSE_SESSION_H__

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MUSE_SESSION_STOPPED = 0,
    MUSE_SESSION_CONNECTING,
    MUSE_SESSION_HANDSHAKING,
    MUSE_SESSION_CONNECTED,
    MUSE_SESSION_RECONNECT_WAIT,
} muse_session_state_t;

typedef enum {
    MUSE_SESSION_OK = 0,
    MUSE_SESSION_ERR_ARGUMENT = -1,
    MUSE_SESSION_ERR_NO_MEMORY = -2,
    MUSE_SESSION_ERR_TRANSPORT = -3,
    MUSE_SESSION_ERR_PROTOCOL = -4,
    MUSE_SESSION_ERR_QUEUE = -5,
    MUSE_SESSION_ERR_TLS_VERIFY = -6,
    /* The edge refused the upgrade with 401/403: the vm_auth_token is
     * rejected. Not a transport fault; credentials must be re-fetched
     * rather than retried (esp32 noise_upgrade.h). */
    MUSE_SESSION_ERR_AUTH = -7,
} muse_session_error_t;

typedef void (*muse_session_status_cb_t)(muse_session_state_t state,
                                         int error,
                                         void *user);

typedef struct {
    const char *node_id;
    const char *vm_token;
    /* The VM id returned by fetch_vms; appended as ?vm_id= on the Noise
     * upgrade. The edge proxy uses it for routing and token validation. */
    const char *vm_id;
    /* A hostname only, without scheme or path. NULL selects the default. */
    const char *noise_host;
    const char *ca_cert_pem;
    size_t ca_cert_len;
    muse_session_status_cb_t status_cb;
    void *status_user;
} muse_session_config_t;

/**
 * Starts one worker-owned session. Credentials are copied before this returns;
 * ca_cert_pem must remain valid until muse_session_stop().
 * A dropped connection is retried until muse_session_stop() is called.
 */
int muse_session_start(const muse_session_config_t *config);

/**
 * Stops the worker and waits until the WebSocket callback can no longer run.
 * It is also safe to call from status_cb; in that case shutdown is requested
 * and the worker returns after the callback.
 */
void muse_session_stop(void);

bool muse_session_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
