/**
 * @file udisk.c
 * @brief U-disk mode demo: routes the Type-C connector to the BK7259
 *        USB peripheral so the host enumerates the on-board SD-NAND as
 *        a removable mass-storage device. This is one-way -- once the
 *        mux is flipped, the CH340 UART log path is gone -- so stop()
 *        is a no-op and the UI does not navigate away.
 */
#include "demo/udisk.h"
#include "demo/demo_iface.h"
#include "board_usb_switch.h"

#ifdef ROBOT_TEST

#include <components/log.h>

#define TAG "udisk_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

int udisk_init(void) { return 0; }

int udisk_start(void)
{
    LOGI("U-disk mode -> route Type-C to BK7259 USB + MSC up\r\n");
    if (board_usb_switch_to_usb() != BK_OK) {
        LOGE("board_usb_switch_to_usb failed\r\n");
        return -1;
    }
    return 0;
}

int udisk_stop(void) { return 0; }

#else  /* !ROBOT_TEST */

int udisk_init(void)  { return 0; }
int udisk_start(void) { return 0; }
int udisk_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_udisk = {
    .name  = "udisk",
    .init  = udisk_init,
    .start = udisk_start,
    .stop  = udisk_stop,
};
