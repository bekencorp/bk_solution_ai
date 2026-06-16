/**
 * @file provisioning.h
 * @brief Backend API for the provisioning demo (page_4).
 *
 * All UI / LVGL handling lives in
 *   beken_generated/page_provisioning/page_provisioning_hooks.c.
 */
#ifndef __BK_DEMO_PROVISIONING_H__
#define __BK_DEMO_PROVISIONING_H__

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Kick the smart-config provisioning workflow. */
void provisioning_trigger_smart_config(void);
/** Delete saved Wi-Fi provisioning and Agent channel state. */
void provisioning_delete_smart_config(void);
/** Restore factory defaults, clear provisioning state, and reboot. */
void provisioning_factory_reset(void);

/**
 * @brief Get the SSID of the currently connected Wi-Fi AP.
 *
 * @param buf  output buffer for the SSID string (always NUL-terminated).
 * @param len  size of @p buf in bytes.
 * @return 0 when STA is connected and @p buf holds a non-empty SSID;
 *         -1 otherwise (buf is set to an empty string).
 */
int provisioning_get_ssid(char *buf, int len);

/**
 * @brief Get the current BLE provisioning device name.
 *
 * @param buf  output buffer for the device name (always NUL-terminated).
 * @param len  size of @p buf in bytes.
 * @return 0 when a non-empty BLE name is available; -1 otherwise.
 */
int provisioning_get_ble_name(char *buf, int len);

int provisioning_init(void);
int provisioning_start(void);
int provisioning_stop(void);

extern const bk_demo_iface_t g_demo_provisioning;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_PROVISIONING_H__ */
