/**
 * @file udisk.h
 * @brief U-disk mode demo (thin wrapper; the actual Type-C USB mux
 *        switch lives in src/common/board_usb_switch.c).
 *
 * start() flips the USB mux via board_usb_switch_to_usb(). The switch
 * is a one-way lock that can only be released by rebooting the board,
 * so stop() just returns 0; the UI does not navigate away from page_3.
 */
#ifndef __BK_DEMO_UDISK_H__
#define __BK_DEMO_UDISK_H__

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int udisk_init(void);
int udisk_start(void);
int udisk_stop(void);

extern const bk_demo_iface_t g_demo_udisk;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_UDISK_H__ */
