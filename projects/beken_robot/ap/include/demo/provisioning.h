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

int provisioning_init(void);
int provisioning_start(void);
int provisioning_stop(void);

extern const bk_demo_iface_t g_demo_provisioning;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_PROVISIONING_H__ */
