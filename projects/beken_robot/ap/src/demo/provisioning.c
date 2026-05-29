/**
 * @file provisioning.c
 * @brief Backend for the provisioning demo (page_4): a thin wrapper around
 *        bk_sconf_prepare_for_smart_config(). All LVGL handling lives in
 *        beken_generated/page_provisioning/page_provisioning_hooks.c.
 */
#include "demo/provisioning.h"

#ifdef ROBOT_TEST

#include <components/log.h>

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

int provisioning_init(void) { return 0; }

extern int page_provisioning_enter(void);

int provisioning_start(void)
{
    return page_provisioning_enter();
}

int provisioning_stop(void) { return 0; }

#else  /* !ROBOT_TEST */

void provisioning_trigger_smart_config(void) {}
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
