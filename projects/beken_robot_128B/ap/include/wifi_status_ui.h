#ifndef __WIFI_STATUS_UI_H__
#define __WIFI_STATUS_UI_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bridge network provisioning status to page_2 status icon.
 *
 * Subscribes to app_event APP_EVT_NETWORK_PROVISIONING /
 * APP_EVT_NETWORK_PROVISIONING_SUCCESS / APP_EVT_NETWORK_PROVISIONING_FAIL /
 * APP_EVT_RECONNECT_NETWORK / APP_EVT_RECONNECT_NETWORK_SUCCESS /
 * APP_EVT_RECONNECT_NETWORK_FAIL and updates bk_lv_tool_ui.page_2_image_2
 * (WiFi icon) vs page_2_image_3 (4G mobile-signal icon): the WiFi icon shows
 * when WiFi is the bearer, the 4G icon when the modem has an IP
 * (NETIF_IF_MODEM), and both are hidden while provisioning/reconnecting or
 * when not provisioned.
 *
 * Must be called once after app_event_init() and before bk_sconf_init().
 */
void wifi_status_ui_init(void);

/**
 * @brief Force the page_2 status icon visibility immediately, from a context
 *        that already holds the LVGL display lock (e.g. an LV_EVENT_CLICKED
 *        callback dispatched from lv_task_handler()).
 *
 * Used by actions that change the provisioning state without emitting an
 * app_event (e.g. "delete provisioning" on page_4, which calls
 * bk_sconf_erase_smart_config()). page_2 is cached across navigation, so its
 * init hook does not re-run on return; this updates the cached icon directly.
 * The bearer (WiFi / 4G) is re-evaluated internally, so no bearer parameter
 * is needed.
 *
 * The caller MUST already hold lv_vendor_disp_lock(). g_disp_mutex is a
 * non-recursive mutex, so this function deliberately does NOT take the lock
 * itself - doing so from an event-callback context would deadlock the LVGL
 * task and freeze the UI.
 *
 * @param provisioned true to show the icon, false to hide it.
 */
void wifi_status_ui_set_provisioned_locked(bool provisioned);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_STATUS_UI_H__ */
