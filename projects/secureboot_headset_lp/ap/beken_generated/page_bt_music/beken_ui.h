/**
 * @file beken_ui.h
 * @brief Headset stub of the beken_robot UI globals, just enough for
 *        page_bt_music.c. The headset product hosts this page as its only
 *        screen, so there is no real bk_lv_ui_t object tree behind it.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Logical (pre-rotation) canvas: panel is 320x385 portrait, UI is authored
 * landscape and lv_vendor rotates by 90 deg (cfg.rotation = ROTATE_90). */
#define LOGICAL_SCREEN_WIDTH   385
#define LOGICAL_SCREEN_HEIGHT  320

typedef struct {
    int _unused;
} bk_lv_ui_t;

/* CJK font for status/toast text (lv_font_ali_16.c sits next to this file). */
LV_FONT_DECLARE(lv_font_ali_16);

#ifdef __cplusplus
}
#endif
