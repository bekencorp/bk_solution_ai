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
/* ~2 mm vertical shift on 385x320 logical canvas (~9.2 px/mm) */
#define UI_SHIFT_Y_2MM  18

typedef struct
{
    /* Page: 0 objects */
    lv_obj_t *page_1;
    lv_obj_t *page_1_label_1;
    lv_obj_t *page_1_image_3;
    /* Page: 1 objects */
    lv_obj_t *page_2;
    lv_obj_t *page_2_button_1;
    lv_obj_t *page_2_button_1_label;
    lv_obj_t *page_2_button_3;
    lv_obj_t *page_2_button_3_label;
    lv_obj_t *page_2_image_1;
    lv_obj_t *page_2_image_2;
    lv_obj_t *page_2_bar_1;
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
    lv_obj_t *page_3_button_7;
    lv_obj_t *page_3_button_7_label;
    lv_obj_t *page_3_button_8;
    lv_obj_t *page_3_button_8_label;
    /* Camera preview button (manual, not from Designer); slot (145, 171). */
    lv_obj_t *page_3_button_9;
    lv_obj_t *page_3_button_9_label;
    /* Robot video playback button plus hidden bottom-row placeholders. */
    lv_obj_t *page_3_button_10;
    lv_obj_t *page_3_button_10_label;
    lv_obj_t *page_3_button_11;
    lv_obj_t *page_3_button_11_label;
    lv_obj_t *page_3_button_12;
    lv_obj_t *page_3_button_12_label;
    /* NOTE: page_3_button_4 is reused as the U-disk (USB MSC) entry
     * (label "U-disk"). The earlier dedicated page_3_button_udisk struct
     * member / centered 4th-row layout was dropped -- the original
     * "face tracking" slot now drives board_usb_switch_to_usb(). See
     * page_3_init.c for the menu/action mapping. */
    /* Page: 3 objects */
    lv_obj_t *page_4;
    lv_obj_t *page_4_label_title;
    lv_obj_t *page_4_label_desc;
    lv_obj_t *page_4_status_card;
    lv_obj_t *page_4_label_status;
    lv_obj_t *page_4_label_hint;
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
    /* Page: 7 objects */
    lv_obj_t *page_8;
    /* Page: 8 objects */
    lv_obj_t *page_9;
    lv_obj_t *page_9_label_title;
    lv_obj_t *page_9_label_state;
    lv_obj_t *page_9_label_track;
    lv_obj_t *page_9_button_play;
    lv_obj_t *page_9_button_play_label;
    lv_obj_t *page_9_button_stop;
    lv_obj_t *page_9_button_stop_label;
    lv_obj_t *page_9_button_next;
    lv_obj_t *page_9_button_next_label;
    /* Page: 9 objects */
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
    /* Page: 10 objects */
    lv_obj_t *page_11;
    lv_obj_t *page_11_label_title;
    lv_obj_t *page_11_label_state;
    lv_obj_t *page_11_spinner;
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
void init_page_page_8(bk_lv_ui_t *bk_ui);
void destroy_page_page_8(bk_lv_ui_t *bk_ui);
void init_page_page_9(bk_lv_ui_t *bk_ui);
void destroy_page_page_9(bk_lv_ui_t *bk_ui);
void init_page_page_10(bk_lv_ui_t *bk_ui);
void destroy_page_page_10(bk_lv_ui_t *bk_ui);
void init_page_page_11(bk_lv_ui_t *bk_ui);
void destroy_page_page_11(bk_lv_ui_t *bk_ui);
void page_11_set_video_connected(void);

/* declare image */
LV_IMAGE_DECLARE(beken_logo_blue_321x147_RGB565A8_NONE);
LV_IMAGE_DECLARE(beken_logo_blue_336x149_RGB565A8_NONE);
LV_IMAGE_DECLARE(wifi_1_23x24_RGB565A8_NONE);

/* declare fonts */
LV_FONT_DECLARE(lv_font_ali_25);
LV_FONT_DECLARE(lv_font_ali_16);
LV_FONT_DECLARE(lv_font_doa_menu_16);
LV_FONT_DECLARE(lv_font_ali_30);
LV_FONT_DECLARE(lv_font_zh_demo_16);
LV_FONT_DECLARE(lv_font_zh_demo_20);
LV_FONT_DECLARE(lv_font_zh_demo_24);
LV_FONT_DECLARE(lv_font_zh_demo_28);
LV_FONT_DECLARE(lv_font_zh_demo_32);
LV_FONT_DECLARE(lv_font_zh_demo_56);
LV_FONT_DECLARE(lv_font_zh_demo_112);

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
