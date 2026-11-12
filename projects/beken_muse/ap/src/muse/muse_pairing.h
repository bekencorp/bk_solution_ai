/**
 * @file muse_pairing.h
 * @brief Community pairing-v5 state ownership.
 */
#ifndef __BK_MUSE_PAIRING_H__
#define __BK_MUSE_PAIRING_H__

#include <stdbool.h>
#include <stdint.h>

#define MUSE_PAIRING_VERSION 5
#define MUSE_PAIRING_SERVICE_UUID "7fdd3d1c-38ea-46cf-8b46-314ecf5f240c"
#define MUSE_PAIRING_RX_UUID      "4d593029-28a2-4a6e-a1f0-3c2d5e8f9b01"
#define MUSE_PAIRING_TX_UUID      "d75dc4ca-7b2b-4e9c-8f0a-1d2e3f4a5b6c"

typedef enum {
    MUSE_PAIRING_IDLE = 0,
    MUSE_PAIRING_WAIT_CLIENT_FINISHED,
    MUSE_PAIRING_CONFIRM_REQUIRED,
    MUSE_PAIRING_READY,
    MUSE_PAIRING_PROVISIONING,
} muse_pairing_state_t;

void muse_pairing_init(void);
void muse_pairing_reset(void);
uint32_t muse_pairing_begin(void);
bool muse_pairing_client_finished(uint32_t generation);
bool muse_pairing_arm_confirmation(uint32_t generation);
bool muse_pairing_confirm(uint32_t generation);
bool muse_pairing_begin_provisioning(uint32_t generation);
bool muse_pairing_commit(uint32_t generation);
muse_pairing_state_t muse_pairing_state(void);

#endif
