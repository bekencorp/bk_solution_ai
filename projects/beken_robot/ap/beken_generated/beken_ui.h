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

/* Display configuration */
#define SCREEN_WIDTH    360
#define SCREEN_HEIGHT   390

typedef struct
{
    /* Page: 0 objects */
    lv_obj_t *page_1;
    lv_obj_t *page_1_label_1;
    lv_obj_t *page_1_image_2;
    lv_obj_t *page_1_bar_1;
    lv_obj_t *page_1_image_3;
    /* Page: 1 objects */
    lv_obj_t *page_2;
    lv_obj_t *page_2_button_1;
    lv_obj_t *page_2_button_1_label;
    lv_obj_t *page_2_button_3;
    lv_obj_t *page_2_button_3_label;
    lv_obj_t *page_2_image_1;
    /* Page: 2 objects */
    lv_obj_t *page_3;
    lv_obj_t *page_3_button_1;
    lv_obj_t *page_3_button_1_label;
    lv_obj_t *page_3_button_2;
    lv_obj_t *page_3_button_2_label;
    lv_obj_t *page_3_button_3;
    lv_obj_t *page_3_button_3_label;
    lv_obj_t *page_3_button_4;
    lv_obj_t *page_3_button_4_label;
    lv_obj_t *page_3_button_5;
    lv_obj_t *page_3_button_5_label;
    lv_obj_t *page_3_button_6;
    lv_obj_t *page_3_button_6_label;
    /* Page: 3 objects */
    lv_obj_t *page_4;
    lv_obj_t *page_4_button_1;
    lv_obj_t *page_4_button_1_label;
    lv_obj_t *page_4_button_2;
    lv_obj_t *page_4_button_2_label;
    lv_obj_t *page_4_button_3;
    lv_obj_t *page_4_button_3_label;
    /* Page: 4 objects */
    lv_obj_t *page_5;
    lv_obj_t *page_5_arc_1;
    /* Page: 5 objects */
    lv_obj_t *page_6;
    lv_obj_t *page_6_label_1;
    /* Page: 6 objects */
    lv_obj_t *page_7;
    lv_obj_t *page_7_label_1;
} bk_lv_ui_t;

void init_page_page_1(bk_lv_ui_t *bk_ui);
void destroy_page_page_1(bk_lv_ui_t *bk_ui);
void init_page_page_2(bk_lv_ui_t *bk_ui);
void destroy_page_page_2(bk_lv_ui_t *bk_ui);
void init_page_page_3(bk_lv_ui_t *bk_ui);
void destroy_page_page_3(bk_lv_ui_t *bk_ui);
void init_page_page_4(bk_lv_ui_t *bk_ui);
void destroy_page_page_4(bk_lv_ui_t *bk_ui);
void init_page_page_5(bk_lv_ui_t *bk_ui);
void destroy_page_page_5(bk_lv_ui_t *bk_ui);
void init_page_page_6(bk_lv_ui_t *bk_ui);
void destroy_page_page_6(bk_lv_ui_t *bk_ui);
void init_page_page_7(bk_lv_ui_t *bk_ui);
void destroy_page_page_7(bk_lv_ui_t *bk_ui);

/* declare image */
LV_IMAGE_DECLARE(beken_logo_blue_321x147_RGB565A8_NONE);
LV_IMAGE_DECLARE(beken_logo_blue_336x149_RGB565A8_NONE);
LV_IMAGE_DECLARE(wifi_1_23x24_RGB565A8_NONE);

/* declare fonts */
LV_FONT_DECLARE(lv_font_ali_25);
LV_FONT_DECLARE(lv_font_ali_16);

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

/* Digital clock functions */
void lv_digital_clock_timer(lv_timer_t *timer);
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second);
void lv_digital_clock_unregister(lv_obj_t *label);
void lv_digital_clock_register(lv_obj_t *label, int show_second, int use_ampm, int hour, int minute, int second);
void lv_digital_clock_unregister(lv_obj_t *label);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* __BEKEN_UI_H__ */
