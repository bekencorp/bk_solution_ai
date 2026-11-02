/**
 * @file udisk.h
 * @brief U-disk mode demo (thin wrapper; the actual Type-C USB mux
 *        switch lives in src/common/board_usb_switch.c).
 *
 * start() flips the USB mux to BK7259 USB (U-disk) via
 * board_usb_switch_to_usb(); stop() flips it back to the CH340 UART path
 * via board_usb_switch_to_uart(). Both directions are reversible from the
 * UI, so the settings page exposes a UART/USB mode sub-menu.
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

/** @return 1 if Type-C is currently routed to BK7259 USB (U-disk), else 0. */
int udisk_is_usb_mode(void);

extern const bk_demo_iface_t g_demo_udisk;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_UDISK_H__ */
