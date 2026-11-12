/**
 * Copyright 2026 Beken Corporation
 *
 * Portions of the pairing protocol are adapted from Meta Platforms, Inc.'s
 * muse-gadget-sdk commit 812c46f9fe42b69d916613a9dc702cbac5ff7b54.
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file muse_ble_pairing.h
 * @brief BK DM-GATT transport and pairing-v5 cryptographic record layer.
 */
#ifndef __BK_MUSE_BLE_PAIRING_H__
#define __BK_MUSE_BLE_PAIRING_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MUSE_BLE_PAIRING_OK = 0,
    MUSE_BLE_PAIRING_ERR_ARGUMENT = -1,
    MUSE_BLE_PAIRING_ERR_BUSY = -2,
    MUSE_BLE_PAIRING_ERR_BLE = -3,
    MUSE_BLE_PAIRING_ERR_TIMEOUT = -4,
    MUSE_BLE_PAIRING_ERR_MEMORY = -5,
};

/**
 * All pointed-to strings must remain valid until muse_ble_pairing_stop().
 * sdk_token is deliberately emitted only inside the authenticated,
 * encrypted pairing_confirmed status record.
 */
typedef struct {
    const char *node_id;
    const char *device_id;
    const char *mac; /* lower-case colon MAC from the WiFi STA identity. */
    const char *firmware_version;
    const char *sdk_token;
    const char *model; /* NULL selects the upstream "hatch_link". */
} muse_ble_pairing_config_t;

/**
 * Start the exclusive Muse BLE GATT server.
 *
 * BK's provisioning implementation and this module both own the singleton
 * DM-GATT/GAP callbacks and advertising instance 0. The caller MUST stop and
 * deinitialise an active bk_smart_config/BLE provisioning session (normally
 * bk_ble_provisioning_deinit()) before calling this function. This module does
 * not deinitialise provisioning behind its owner's back.
 */
int muse_ble_pairing_start(const muse_ble_pairing_config_t *config);

/** Stop advertising, disconnect the peer, unregister GATT, and wipe secrets. */
int muse_ble_pairing_stop(void);

bool muse_ble_pairing_is_started(void);

/* True while an accepted provision_v2 is joining Wi-Fi / reaching the cloud.
 * The service keeps the BLE link up so progress statuses can be pushed. */
bool muse_ble_pairing_provision_active(void);

/* True once the pairing worker has pushed auth_ok and exited; the service
 * should then run muse_ble_pairing_stop() for full GATT/GAP teardown. */
bool muse_ble_pairing_teardown_requested(void);

/* BK7259 shares one 2.4 GHz radio between BLE and Wi-Fi. Call yield right
 * after "wifi_connecting" and before bk_wifi_sta_start(): it KEEPS the GATT
 * session (and CCC subscription) up but relaxes the BLE connection interval to
 * ~500 ms, so dense BLE connection events cannot starve the 802.11
 * authentication/association frames. */
void muse_ble_pairing_yield_rf(void);

/**
 * Optional direct S4 hook. Existing code may continue to call
 * muse_service_confirm_pairing(); the BLE worker also observes the transition
 * to MUSE_PAIRING_READY and sends pairing_confirmed exactly once.
 */
int muse_ble_pairing_confirm(void);

#ifdef __cplusplus
}
#endif

#endif
