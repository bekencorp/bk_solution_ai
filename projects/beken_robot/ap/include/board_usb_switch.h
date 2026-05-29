/**
 * @file board_usb_switch.h
 * @brief Board-level helpers for the BK7259 Robot V1 AI kit.
 *
 *  - On-board SD-NAND (U14, MKDV4GCL-ABB) is wired to SDIO controller 1
 *    (P14~P19 = CLK/CMD/DAT0~DAT3). Its NAND_VDD power rail is gated by
 *    a GPIO (default P53). Call board_sd_nand_power_on() during boot to
 *    pull NAND_VDD high before the FATFS / SDIO driver tries to probe
 *    the card.
 *
 *  - The single Type-C connector is shared between:
 *      * CH340 USB-to-UART (used as log / firmware download port)
 *      * BK7259 full-speed USB device (e.g. USB MSC exposing SD-NAND
 *        to a PC for asset copy)
 *    The two paths are multiplexed by a pair of FSW3157A 1:2 switches
 *    steered by SW_SEL (default P54). Per the schematic truth table:
 *      SW_SEL = 0  -> Type-C routed to CH340 (UART)  (default at boot)
 *      SW_SEL = 1  -> Type-C routed to BK7259 USB
 *    Call board_usb_switch_init() at boot to drive SW_SEL low (UART
 *    mode for log capture and bootloader download). The same call also
 *    registers an `usbsw` CLI command so the developer can flip the
 *    mux at runtime when they want to plug into a PC for asset transfer.
 *
 * All GPIO indices and the "USB-on" level are configurable via Kconfig
 * (CONFIG_BOARD_SD_NAND_POWER_GPIO, CONFIG_BOARD_USB_SWITCH_GPIO,
 * CONFIG_BOARD_USB_SWITCH_USB_LEVEL).
 */
#ifndef _BOARD_USB_SWITCH_H_
#define _BOARD_USB_SWITCH_H_

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pull the SD-NAND power-gate GPIO high (provides NAND_VDD).
 *
 * Must be called BEFORE any SDIO / FATFS initialization that talks to
 * the on-board SD-NAND. Idempotent.
 *
 * @return BK_OK on success, BK_FAIL if the GPIO programming fails.
 */
bk_err_t board_sd_nand_power_on(void);

/**
 * @brief Power-gate the SD-NAND off (drive the gate GPIO low).
 *
 * Useful for power measurement / sleep tests.
 */
bk_err_t board_sd_nand_power_off(void);

/**
 * @brief Initialize the Type-C USB mux to UART (CH340) by default and
 *        register the `usbsw` CLI command.
 *
 * The CLI exposes:
 *      usbsw uart    -> route Type-C to CH340 (UART log + flash)
 *      usbsw usb     -> route Type-C to BK7259 USB device
 *      usbsw toggle  -> flip the current mux state
 *      usbsw status  -> print current mux state
 */
bk_err_t board_usb_switch_init(void);

/**
 * @brief Drive the USB mux to BK7259 USB (host PC sees BK7259 USB device).
 */
bk_err_t board_usb_switch_to_usb(void);

/**
 * @brief Drive the USB mux back to CH340 UART (default boot state).
 */
bk_err_t board_usb_switch_to_uart(void);

#ifdef __cplusplus
}
#endif

#endif /* _BOARD_USB_SWITCH_H_ */
