/**
 * @file muse_gadget.h
 * @brief Headless entry points for the experimental Muse Gadget service.
 */
#ifndef __BK_DEMO_MUSE_GADGET_H__
#define __BK_DEMO_MUSE_GADGET_H__

#include "ui/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int muse_gadget_init(void);
int muse_gadget_start(void);
int muse_gadget_stop(void);

extern const bk_demo_iface_t g_demo_muse_gadget;

#ifdef __cplusplus
}
#endif

#endif
