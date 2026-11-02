#pragma once

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the on-board ADC keys (S2~S5, same hardware as beken_robot).
 *
 * Only S4 long press is used: it votes AP OFF to the CP, and the CP runs the
 * AP power-off sequence. AP wake-up does not use a key: when the phone starts
 * streaming again, the ACL data reaches the CP controller and the CP votes
 * AP ON automatically through the BT IPC path.
 *
 * @return BK_OK on success, otherwise an error code.
 */
int ap_key_init(void);

/* Re-register the ADC keys after a rejected AP power-off attempt. */
bk_err_t ap_key_restore(void);

#ifdef __cplusplus
}
#endif
