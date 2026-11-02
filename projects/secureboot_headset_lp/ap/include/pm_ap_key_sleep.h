/**
 * @file pm_ap_key_sleep.h
 * @brief AP-initiated low-voltage flow: GPIO28 (S4) is the only wake source.
 */
#pragma once

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create the worker thread and register the "apks" CLI command.
 *
 * The worker follows the SDK pm_ap_demo low-voltage entry sequence: release
 * the ADC sampler, register the GPIO ISR, arm GPIO28 as the CP wake source,
 * vote CP sleep and finally vote the AP off.
 *
 * @return BK_OK on success, otherwise an error code.
 */
int ap_key_sleep_init(void);

/** @brief Queue one low-voltage entry (same action as "apks enter"). */
bk_err_t ap_key_sleep_enter(void);

#ifdef __cplusplus
}
#endif
