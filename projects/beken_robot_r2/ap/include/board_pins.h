/**
 * @file board_pins.h
 * @brief Robot R2 board-level GPIO assignments.
 *
 * Keep fixed board wiring here so feature modules do not copy pin mappings
 * from another Robot hardware revision.
 */
#pragma once

#include <driver/gpio.h>

/* Shared peripheral power rails. */
#define BOARD_PIN_PERIPH_3V3       GPIO_20
#define BOARD_PIN_PERIPH_2V8       GPIO_29

/* MIPI DSI panel control. Backlight uses a PNP high-side switch: LOW = on. */
#define BOARD_LCD_PIN_BACKLIGHT    GPIO_40
#define BOARD_LCD_PIN_RESET        GPIO_42

/* MIPI CSI camera sensor control. */
#define BOARD_CAMERA_PIN_SCL       GPIO_64
#define BOARD_CAMERA_PIN_SDA       GPIO_65
#define BOARD_CAMERA_I2C_ID        0
#define BOARD_CAMERA_PIN_RESET     GPIO_30
#define BOARD_CAMERA_PIN_PWDN      (-1)
#define BOARD_CAMERA_PIN_XCLK      GPIO_59
