/**
 * @file provisioning.c
 * @brief Backend for the provisioning demo (page_4): a thin wrapper around
 *        bk_sconf_prepare_for_smart_config(). All LVGL handling lives in
 *        beken_generated/page_provisioning/page_provisioning_hooks.c.
 */
#include "demo/provisioning.h"

#include <string.h>

#ifdef ROBOT_TEST

#include <components/log.h>
#include "bk_wifi.h"
#include "bk_wifi_types.h"

#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#define TAG "prov_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

void provisioning_trigger_smart_config(void)
{
#if CONFIG_BK_SMART_CONFIG
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

int provisioning_init(void) { return 0; }

extern int page_provisioning_enter(void);

int provisioning_start(void)
{
    return page_provisioning_enter();
}

int provisioning_stop(void) { return 0; }

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
