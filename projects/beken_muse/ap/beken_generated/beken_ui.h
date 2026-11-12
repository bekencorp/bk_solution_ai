/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 * 
 * This software is proprietary and confidential. No part of this software may be
 * reproduced, distributed, or transmitted in any form or by any means, including
 * photocopying, recording, or other electronic or mechanical methods, without the
 * prior written permission of BekenCorp, except in the case of brief quotations
 * embodied in critical reviews and certain other noncommercial uses permitted
 * by copyright law.
 * 
 * For permission requests, write to BekenCorp at armino_support@bekencorp.com.

 * Author: Beken LVGL Designer Tool
*/
/**
 * @file beken_ui.c
 * @brief Beken UI implementation file
 * 
 * This file contains the implementation of the Beken UI system.
 * Customers can modify this file to customize their UI without
 * touching the main application code or build system.
 */

#ifndef __BEKEN_UI_H__
#define __BEKEN_UI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* Display configuration (physical panel scan order) */
#define SCREEN_WIDTH    320
#define SCREEN_HEIGHT   385
/* Logical canvas after ap_main ROTATE_90 */
#define LOGICAL_SCREEN_WIDTH   385
#define LOGICAL_SCREEN_HEIGHT  320
/* Scale Designer coords (390x360 logical) to current logical canvas */
#define UI_SCALE_X(v)  ((v) * LOGICAL_SCREEN_WIDTH  / 390)
#define UI_SCALE_Y(v)  ((v) * LOGICAL_SCREEN_HEIGHT / 360)

typedef struct
{
    /* Page: 0 objects */
    lv_obj_t *page_1;
    lv_obj_t *page_1_label_1;
    lv_obj_t *page_1_image_3;
    /* Page: 1 objects */
    lv_obj_t *page_2;
    lv_obj_t *page_2_image_2;
    lv_obj_t *page_2_bar_1;
    /* Page: 2 objects */
    lv_obj_t *page_3;
    /* Page: 4..9/11 objects dropped with their demo backends (edge-AI,
     * Agora chat/vision, music, robot video, ASR, DOA, BT music, Beken
     * provisioning) -- those generated pages are deleted from this tree. */
    /* Page: 10 objects */
    lv_obj_t *page_10;
    lv_obj_t *page_10_label_title;
    lv_obj_t *page_10_icon_speaker;
    lv_obj_t *page_10_icon_body;
    lv_obj_t *page_10_icon_cone;
    lv_obj_t *page_10_icon_wave_1;
    lv_obj_t *page_10_icon_wave_2;
    lv_obj_t *page_10_icon_wave_3;
    lv_obj_t *page_10_icon_slash;
    lv_obj_t *page_10_label_value;
    lv_obj_t *page_10_slider_volume;
    lv_obj_t *page_10_button_minus;
    lv_obj_t *page_10_button_minus_label;
    lv_obj_t *page_10_button_plus;
    lv_obj_t *page_10_button_plus_label;
} bk_lv_ui_t;

void init_page_page_1(bk_lv_ui_t *bk_ui);
void destroy_page_page_1(bk_lv_ui_t *bk_ui);
void init_page_page_2(bk_lv_ui_t *bk_ui);
void destroy_page_page_2(bk_lv_ui_t *bk_ui);
void init_page_page_3(bk_lv_ui_t *bk_ui);
void destroy_page_page_3(bk_lv_ui_t *bk_ui);
void init_page_page_10(bk_lv_ui_t *bk_ui);
void destroy_page_page_10(bk_lv_ui_t *bk_ui);

/* declare image */
LV_IMAGE_DECLARE(beken_logo_blue_321x147_RGB565A8_NONE);
LV_IMAGE_DECLARE(muse_mascot_140x140_RGB565A8_NONE);

/* declare fonts */
LV_FONT_DECLARE(lv_font_ali_25);
LV_FONT_DECLARE(lv_font_ali_16);
LV_FONT_DECLARE(lv_font_ali_30);
LV_FONT_DECLARE(muse_font_cjk_30);
LV_FONT_DECLARE(muse_font_noto_16);

/**
 * @brief Initialize the Beken UI system
 * 
 * This function initializes the UI components and creates the main interface.
 * Customers can modify this function to customize their UI layout.
 */
void beken_ui_init(void);

/**
 * @brief Get the configured screen width
 * @return Screen width in pixels
 */
int beken_get_screen_width(void);

/**
 * @brief Get the configured screen height
 * @return Screen height in pixels
 */
int beken_get_screen_height(void);

extern bk_lv_ui_t bk_lv_tool_ui;

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* __BEKEN_UI_H__ */
