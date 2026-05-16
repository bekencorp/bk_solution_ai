/**
 * @file wifi_status_ui.c
 * @brief Toggle page_1 wifi icon visibility based on provisioning status.
 *
 * The wifi icon (page_1_image_2) is hidden by default during page init when
 * bk_sconf_is_network_provisioned() returns false. This bridge subscribes to
 * provisioning / reconnect app_event messages and updates the icon under the
 * LVGL display lock whenever the link state changes.
 */
#include <common/sys_config.h>

#include "wifi_status_ui.h"

#include <components/log.h>

#define TAG "wifi_status_ui"

#if CONFIG_LVGL && CONFIG_APP_EVT

#include "lvgl.h"
#include "lv_vendor.h"

#include "beken_ui.h"
#include "app_event.h"

static void wifi_icon_set_visible_locked(bool visible)
{
    lv_obj_t *icon = bk_lv_tool_ui.page_1_image_2;
    if (icon == NULL || !lv_obj_is_valid(icon)) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }
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
    inited = true;
}

#else /* !(CONFIG_LVGL && CONFIG_APP_EVT) */

void wifi_status_ui_init(void)
{
    BK_LOGI(TAG, "wifi_status_ui skipped (LVGL or APP_EVT disabled)\n");
}

#endif /* CONFIG_LVGL && CONFIG_APP_EVT */
