#ifndef __WIFI_STATUS_UI_H__
#define __WIFI_STATUS_UI_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bridge network provisioning status to page_2 wifi icon visibility.
 *
 * Subscribes to app_event APP_EVT_NETWORK_PROVISIONING_SUCCESS /
 * APP_EVT_NETWORK_PROVISIONING_FAIL / APP_EVT_RECONNECT_NETWORK_SUCCESS /
 * APP_EVT_RECONNECT_NETWORK_FAIL and updates bk_lv_tool_ui.page_2_image_2.
 *
 * Must be called once after app_event_init() and before bk_sconf_init().
 */
void wifi_status_ui_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_STATUS_UI_H__ */
