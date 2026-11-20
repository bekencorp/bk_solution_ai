/**
 * @file muse_chat.h
 * @brief Dedicated chat Noise connection (upstream esp32 muse_chat_session.cpp
 *        shape): a second WebSocket+Noise session that carries ONLY
 *        /chat/stream (voice notes) and /chat/subscribe — no /link-control
 *        stream and no link.register, so the server treats its conversation
 *        as the gadget's own side chat instead of the account's main chat
 *        (main-chat replies never reach the device).
 *
 *        The control session (muse_session.cpp) is untouched: pairing,
 *        link.register and device.invoke keep running on it. Voice glue
 *        (muse_voice_tx_next/tx_ack/on_rx_*) runs on the chat worker.
 */
#ifndef __BK_MUSE_CHAT_H__
#define __BK_MUSE_CHAT_H__

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *noise_host;   /* NULL/empty -> default host */
    const char *vm_id;        /* NULL/empty allowed (edge routes on it) */
    const char *vm_token;     /* required: VM bearer */
    const char *ca_cert_pem;  /* required: PEM trust anchor, NUL-terminated */
    size_t ca_cert_len;
} muse_chat_config_t;

/* Starts the chat worker (owns its reconnect loop). Idempotent: 0 when
 * already running. Copies the config. */
int  muse_chat_start(const muse_chat_config_t *config);
void muse_chat_stop(void);

/* True while the chat Noise connection is handshaken and serving voice
 * turns. Only the chat connection carries /chat/stream, so voice is
 * unusable whenever this is false even if the link session is up. */
bool muse_chat_is_connected(void);

void muse_chat_request_reconnect(const char *reason);

#ifdef __cplusplus
}
#endif

#endif
