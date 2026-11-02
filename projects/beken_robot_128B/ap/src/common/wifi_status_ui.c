/**
 * @file wifi_status_ui.c
 * @brief Toggle page_2 status icon (WiFi / 4G) based on provisioning status.
 *
 * The top-right status icon shows the active bearer: the WiFi icon
 * (page_2_image_2) when WiFi is the bearer, or the 4G mobile-signal icon
 * (page_2_image_3) when the 4G modem (NETIF_IF_MODEM) has an IP. Both are
 * hidden by default during page init when bk_sconf_is_network_provisioned()
 * returns false. This bridge subscribes to provisioning / reconnect
 * app_event messages and updates the icons under the LVGL display lock
 * whenever the link state changes.
 */
#include <common/sys_config.h>

#include "wifi_status_ui.h"

#include <components/log.h>

#define TAG "wifi_status_ui"

#if CONFIG_LVGL && CONFIG_APP_EVT

#include "lvgl.h"
#include "lv_vendor.h"

#include "beken_ui.h"
#include "page_hooks.h"
#include "app_event.h"

#include <components/netif_types.h>
#include <modules/wifi_types.h>

#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#include "demo/provisioning.h"

static void status_icon_set_locked(bool visible, bool lte)
{
    lv_obj_t *wifi = bk_lv_tool_ui.page_2_image_2;
    lv_obj_t *mobile = bk_lv_tool_ui.page_2_image_3;

    if (wifi != NULL && lv_obj_is_valid(wifi)) {
        if (visible && !lte) {
            lv_obj_remove_flag(wifi, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(wifi, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (mobile != NULL && lv_obj_is_valid(mobile)) {
        if (visible && lte) {
            lv_obj_remove_flag(mobile, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(mobile, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*
 * Decide whether the active bearer is the 4G modem. Mirrors the provisioning
 * page logic: the modem counts as up when NETIF_IF_MODEM has a valid IP
 * (provisioning_get_lte_info()). No LVGL access, safe to call anywhere.
 */
static bool network_is_lte(void)
{
    char lte[NETIF_IP4_STR_LEN];

    return provisioning_get_lte_info(lte, sizeof(lte)) == 0;
}

static bool network_is_wifi(void)
{
    char ssid[WIFI_SSID_STR_LEN];

    return provisioning_get_ssid(ssid, sizeof(ssid)) == 0;
}

/*
 * Set the WiFi-icon initial visibility right after page_2 has been
 * created, based on whether the network is already provisioned.
 * Equivalent to the historical
 *     if (!bk_sconf_is_network_provisioned()) { ... }
 * block in page_2_init.c; living here keeps the bk_smart_config
 * dependency out of the UI-only files.
 *
 * The hook fires from init_page_page_2() after lv_obj_update_layout()
 * runs; at that point the caller already holds lv_vendor_disp_lock so
 * we poke the icon directly without taking the lock again.
 */
static void wifi_status_page_2_init_hook(bk_lv_ui_t *ui)
{
    (void)ui;
    /* Drive the icon from the live link state instead of the provisioned
     * flag: page_2 is rebuilt on every navigation, and provisioning tears
     * both bearers down while the stored config is still marked
     * provisioned - so the provisioned flag would wrongly re-show the
     * stale WiFi icon after "Start provisioning". */
    bool lte = network_is_lte();
    bool connected = lte || network_is_wifi();
    status_icon_set_locked(connected, lte);
}

static void wifi_status_evt_cb(app_evt_msg_t *msg, void *user_data)
{
    (void)user_data;
    if (msg == NULL) {
        return;
    }

    bool visible = false;
    bool lte = false;
    switch (msg->event) {
    case APP_EVT_NETWORK_PROVISIONING_SUCCESS:
    case APP_EVT_RECONNECT_NETWORK_SUCCESS:
        /* Show only the bearer that is actually in the air: SUCCESS is posted
         * on GOT_IP4, but the link may already be gone or the bearer query may
         * lag, so confirm the live state instead of guessing. Mirrors the
         * page_2 init hook (lte || network_is_wifi()). */
        lte = network_is_lte();
        visible = lte || network_is_wifi();
        break;
    case APP_EVT_NETWORK_PROVISIONING:
    case APP_EVT_RECONNECT_NETWORK:
    case APP_EVT_NETWORK_PROVISIONING_FAIL:
    case APP_EVT_RECONNECT_NETWORK_FAIL:
        /* Provisioning / reconnect tears both bearers down first and a FAIL
         * leaves nothing up: hide the icon (visible stays false). */
        break;
    default:
        return;
    }

    BK_LOGI(TAG, "status icon -> %s (evt=%d, bearer=%s)\n",
            visible ? "show" : "hide", (int)msg->event, lte ? "4g" : "wifi");

    lv_vendor_disp_lock();
    status_icon_set_locked(visible, lte);
    lv_vendor_disp_unlock();
}

void wifi_status_ui_init(void)
{
    static bool inited;

    if (inited) {
        return;
    }

    (void)app_event_register_handler(APP_EVT_NETWORK_PROVISIONING,
                                     wifi_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_NETWORK_PROVISIONING_SUCCESS,
                                     wifi_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_NETWORK_PROVISIONING_FAIL,
                                     wifi_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_RECONNECT_NETWORK,
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
    /* Show an icon only when a bearer is actually reachable, matching the
     * event path (lte || network_is_wifi()); the caller already holds the
     * display lock. */
    bool lte = provisioned && network_is_lte();
    bool visible = lte || (provisioned && network_is_wifi());
    BK_LOGI(TAG, "status icon -> %s (forced, locked, bearer=%s)\n",
            visible ? "show" : "hide", lte ? "4g" : "wifi");
    status_icon_set_locked(visible, lte);
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
