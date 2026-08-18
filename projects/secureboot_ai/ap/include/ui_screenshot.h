/**
 * @file ui_screenshot.h
 * @brief LVGL screen snapshot helper for saving UI screenshots to SD-NAND.
 */
#pragma once

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Save the active LVGL screen as a RGB565 BMP file on SD-NAND.
 *
 * @param path Optional FatFS path. Pass NULL or an empty string to use
 *             "1:/screenshots/lvgl_XXXX.bmp".
 * @return BK_OK on success, BK_FAIL on failure.
 */
bk_err_t ui_screenshot_save_bmp(const char *path);

/**
 * @brief Register the `snap` debug CLI command.
 */
void ui_screenshot_cli_init(void);

#ifdef __cplusplus
}
#endif
