/**
 * @file wifi_status_ui.c
 * @brief Toggle page_2 wifi icon visibility based on the WiFi link state.
 *
 * The wifi icon (page_2_image_2) is hidden by default during page init when
 * no STA link is up. This bridge subscribes to provisioning / reconnect
 * app_event messages and updates the icon under the LVGL display lock
 * whenever the link state changes. (beken_muse: no bk_smart_config -- the
 * initial state reads the raw WiFi link status directly.)
 */
#include <common/sys_config.h>

#include "wifi_status_ui.h"

#include <components/log.h>
#include <string.h>

#define TAG "wifi_status_ui"

#if CONFIG_LVGL && CONFIG_APP_EVT

#include "lvgl.h"
#include "lv_vendor.h"

#include "beken_ui.h"
#include "page_hooks.h"
#include "app_event.h"
#include "bk_wifi.h"
#include "bk_wifi_types.h"

static void wifi_icon_set_visible_locked(bool visible)
{
    lv_obj_t *icon = bk_lv_tool_ui.page_2_image_2;
    if (icon == NULL || !lv_obj_is_valid(icon)) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }
}

/*
 * Set the WiFi-icon initial visibility right after page_2 has been
 * created, based on whether the STA link is already up.
 *
 * The hook fires from init_page_page_2() after lv_obj_update_layout()
 * runs; at that point the caller already holds lv_vendor_disp_lock so
 * we poke the icon directly without taking the lock again.
 */
static void wifi_status_page_2_init_hook(bk_lv_ui_t *ui)
{
    (void)ui;
    bool link_up = false;
    wifi_link_status_t link_status;

    memset(&link_status, 0, sizeof(link_status));
    if (bk_wifi_sta_get_link_status(&link_status) == BK_OK &&
        (link_status.state == WIFI_LINKSTATE_STA_CONNECTED ||
         link_status.state == WIFI_LINKSTATE_STA_GOT_IP)) {
        link_up = true;
    }
    wifi_icon_set_visible_locked(link_up);
}

static void wifi_status_evt_cb(app_evt_msg_t *msg, void *user_data)
{
    (void)user_data;
    if (msg == NULL) {
        return;
    }

    bool visible;
    switch (msg->event) {
    case APP_EVT_NETWORK_PROVISIONING_SUCCESS:
    case APP_EVT_RECONNECT_NETWORK_SUCCESS:
        visible = true;
        break;
    case APP_EVT_NETWORK_PROVISIONING_FAIL:
    case APP_EVT_RECONNECT_NETWORK_FAIL:
        visible = false;
        break;
    default:
        return;
    }

    BK_LOGI(TAG, "wifi icon -> %s (evt=%d)\n", visible ? "show" : "hide", (int)msg->event);

    lv_vendor_disp_lock();
    wifi_icon_set_visible_locked(visible);
    lv_vendor_disp_unlock();
}

void wifi_status_ui_init(void)
{
    static bool inited;

    if (inited) {
        return;
    }

    (void)app_event_register_handler(APP_EVT_NETWORK_PROVISIONING_SUCCESS,
                                     wifi_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_NETWORK_PROVISIONING_FAIL,
                                     wifi_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_RECONNECT_NETWORK_SUCCESS,
                                     wifi_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_RECONNECT_NETWORK_FAIL,
                                     wifi_status_evt_cb, NULL);

    bk_page_set_init_hook(2, wifi_status_page_2_init_hook);
    inited = true;
}

void wifi_status_ui_set_provisioned_locked(bool provisioned)
{
    BK_LOGI(TAG, "wifi icon -> %s (forced, locked)\n",
            provisioned ? "show" : "hide");
    wifi_icon_set_visible_locked(provisioned);
}

#else /* !(CONFIG_LVGL && CONFIG_APP_EVT) */

void wifi_status_ui_init(void)
{
    BK_LOGI(TAG, "wifi_status_ui skipped (LVGL or APP_EVT disabled)\n");
}

void wifi_status_ui_set_provisioned_locked(bool provisioned)
{
    (void)provisioned;
}

#endif /* CONFIG_LVGL && CONFIG_APP_EVT */
