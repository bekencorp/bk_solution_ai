/**
 * @file provisioning.c
 * @brief Backend for the provisioning demo (page_4): a thin wrapper around
 *        bk_sconf_prepare_for_smart_config(). All LVGL handling lives in
 *        beken_generated/page_provisioning/page_provisioning_hooks.c.
 */
#include "demo/provisioning.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#ifdef ROBOT_TEST

#include "audio_engine.h"
#include <components/log.h>
#include <components/system.h>
#include "bk_wifi.h"
#include "bk_wifi_types.h"

#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#if CONFIG_BK_BLE_PROVISIONING
#include "bk_network_provisioning.h"
#endif

#if CONFIG_BT
#include "demo/a2dp_sink.h"
#endif

#define TAG "prov_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

/* Firmware version advertised in the BLE provisioning core header. */
#define ROBOT_FW_MAJOR 1
#define ROBOT_FW_MINOR 0
#define ROBOT_FW_PATCH 0

/* Compose the provisioning device name (what the phone app scans for) using the
 * rule "bk_robot_XXXXXX" derived from the Bluetooth MAC. This is the single
 * source of truth: provisioning_init() pushes it into the BLE provisioning
 * component via bk_ble_provisioning_set_adv_name(), and the UI reads the same
 * string through provisioning_get_ble_name(), so both stay in sync. */
static int provisioning_build_device_name(char *buf, int len)
{
    uint8_t mac[6] = {0};

    if (buf == NULL || len <= 0) {
        return -1;
    }
    buf[0] = '\0';

    if (bk_get_mac(mac, MAC_TYPE_BLUETOOTH) != BK_OK) {
        return -1;
    }

    (void)snprintf(buf, (size_t)len, "BK_ROBOT_%02X%02X%02X", mac[3], mac[4], mac[5]);
    buf[len - 1] = '\0';
    return buf[0] != '\0' ? 0 : -1;
}

void provisioning_trigger_smart_config(void)
{
#if CONFIG_BK_SMART_CONFIG
#if CONFIG_BT
    /* Classic BT (A2DP/AVRCP) keeps the 2.4G radio busy, which starves the
     * Wi-Fi fast-connect single-channel scan -> the first provisioning attempt
     * reports "fail" before the slower full scan recovers. Drop the classic
     * link first so provisioning is reliable on the first try; the user can
     * reconnect BT after the network is provisioned. Safe no-op when nothing
     * is connected. */
    LOGI("freeing radio: disconnecting classic BT before provisioning\r\n");
    (void)a2dp_sink_demo_try_disconnect_current();
#endif

    bk_sconf_prepare_for_smart_config();
#else
    LOGI("BK_SMART_CONFIG disabled, provisioning trigger ignored\r\n");
#endif
}

void provisioning_delete_smart_config(void)
{
#if CONFIG_BK_SMART_CONFIG
    bk_sconf_erase_smart_config();
#else
    LOGI("BK_SMART_CONFIG disabled, provisioning delete ignored\r\n");
#endif
}

void provisioning_factory_reset(void)
{
#if CONFIG_BK_SMART_CONFIG
    bk_sconf_factory_reset();
#else
    LOGI("BK_SMART_CONFIG disabled, factory reset ignored\r\n");
#endif
}

int provisioning_get_ssid(char *buf, int len)
{
    wifi_link_status_t link_status;

    if (buf == NULL || len <= 0) {
        return -1;
    }
    buf[0] = '\0';

    memset(&link_status, 0, sizeof(link_status));
    if (bk_wifi_sta_get_link_status(&link_status) != BK_OK) {
        return -1;
    }
    if (link_status.state != WIFI_LINKSTATE_STA_CONNECTED &&
        link_status.state != WIFI_LINKSTATE_STA_GOT_IP) {
        return -1;
    }
    if (link_status.ssid[0] == '\0') {
        return -1;
    }

    strncpy(buf, link_status.ssid, (size_t)len - 1);
    buf[len - 1] = '\0';
    return 0;
}

int provisioning_get_ble_name(char *buf, int len)
{
    return provisioning_build_device_name(buf, len);
}

int provisioning_init(void)
{
    /* Push our device name into the BLE provisioning component so it advertises
     * exactly the name the UI shows. Done once at boot, before provisioning is
     * ever started, so the name is correct on the very first attempt. */
#if CONFIG_BK_BLE_PROVISIONING
    char name[32];

    if (provisioning_build_device_name(name, sizeof(name)) == 0) {
        bk_ble_provisioning_set_adv_name(name);
        LOGI("configured provisioning device name: %s\r\n", name);
    }

    /* Core header (proto_ver + device_type=ROBOT + fw) advertised in the ADV
     * packet per the BLE provisioning adv spec. */
    bk_ble_provisioning_set_dev_info(BK_BLE_PROV_DEV_TYPE_ROBOT,
                                     ROBOT_FW_MAJOR, ROBOT_FW_MINOR, ROBOT_FW_PATCH);
#endif
    return 0;
}

extern int page_provisioning_enter(void);

int provisioning_start(void)
{
    return page_provisioning_enter();
}

int provisioning_stop(void)
{
    if (audio_engine_is_running()) {
        (void)audio_engine_stop();
    }
    return 0;
}

#else  /* !ROBOT_TEST */

void provisioning_trigger_smart_config(void) {}
void provisioning_delete_smart_config(void) {}
void provisioning_factory_reset(void) {}
int  provisioning_get_ssid(char *buf, int len)
{
    if (buf != NULL && len > 0) {
        buf[0] = '\0';
    }
    return -1;
}
int  provisioning_get_ble_name(char *buf, int len)
{
    if (buf != NULL && len > 0) {
        buf[0] = '\0';
    }
    return -1;
}
int  provisioning_init(void)  { return 0; }
int  provisioning_start(void) { return 0; }
int  provisioning_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_provisioning = {
    "provisioning",
    provisioning_init,
    provisioning_start,
    provisioning_stop,
};
