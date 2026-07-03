/**
 * @file provisioning.c
 * @brief Backend for the provisioning demo (page_4): a thin wrapper around
 *        bk_sconf_prepare_for_smart_config(). All LVGL handling lives in
 *        beken_generated/page_provisioning/page_provisioning_hooks.c.
 */
#include "demo/provisioning.h"

#include <string.h>

#ifdef ROBOT_TEST

#include "audio_engine.h"
#include <components/log.h>
#include "bk_wifi.h"
#include "bk_wifi_types.h"

#if CONFIG_BLUETOOTH
#include <components/bluetooth/bk_dm_gap_ble.h>
#endif

#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#if CONFIG_BT
#include "demo/a2dp_sink.h"
#endif

#define TAG "prov_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

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
    if (buf == NULL || len <= 0) {
        return -1;
    }
    buf[0] = '\0';

#if CONFIG_BLUETOOTH
    uint32_t size = (uint32_t)len;
    if (bk_ble_gap_get_device_name(buf, &size) != BK_ERR_BLE_SUCCESS) {
        buf[0] = '\0';
        return -1;
    }
    buf[len - 1] = '\0';
    return buf[0] != '\0' ? 0 : -1;
#else
    return -1;
#endif
}

int provisioning_init(void) { return 0; }

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
