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
#include "lvgl.h"
#include "beken_ui.h"
#include "custom_func.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include <stdio.h>
#include <string.h>


/*
 * @brief: init page page_4
 */
void init_page_page_4(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_4 != NULL && lv_obj_is_valid(bk_ui->page_4)) {
        destroy_page_page_4(bk_ui);
    }
    

    bk_ui->page_4 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_4, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_4, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(bk_ui->page_4, lv_color_hex(0x07111f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_4_label_title = NULL;

    bk_ui->page_4_status_card = lv_obj_create(bk_ui->page_4);
    lv_obj_set_scrollbar_mode(bk_ui->page_4_status_card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_x(bk_ui->page_4_status_card, 20);
    lv_obj_set_y(bk_ui->page_4_status_card, 43);
    lv_obj_set_width(bk_ui->page_4_status_card, 316);
    lv_obj_set_height(bk_ui->page_4_status_card, 76);
    lv_obj_set_style_bg_color(bk_ui->page_4_status_card, lv_color_hex(0x101b2b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_4_status_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_4_status_card, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_4_status_card, lv_color_hex(0x225f9e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_4_status_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_4_status_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_4_status_card, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bk_ui->page_4_status_card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* SSID line (kept ASCII so it never depends on CJK glyph coverage). */
    bk_ui->page_4_label_status = lv_label_create(bk_ui->page_4_status_card);
    lv_label_set_text(bk_ui->page_4_label_status, "WiFi: --");
    lv_label_set_long_mode(bk_ui->page_4_label_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_x(bk_ui->page_4_label_status, 16);
    lv_obj_set_y(bk_ui->page_4_label_status, 14);
    lv_obj_set_width(bk_ui->page_4_label_status, 284);
    lv_obj_set_height(bk_ui->page_4_label_status, 24);
    lv_obj_set_style_text_color(bk_ui->page_4_label_status, lv_color_hex(0x66d9ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_4_label_status, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_4_label_status, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_4_label_status, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* State line (kept ASCII so it never depends on CJK glyph coverage). */
    bk_ui->page_4_label_hint = lv_label_create(bk_ui->page_4_status_card);
    lv_label_set_text(bk_ui->page_4_label_hint, "State: READY");
    lv_label_set_long_mode(bk_ui->page_4_label_hint, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_x(bk_ui->page_4_label_hint, 16);
    lv_obj_set_y(bk_ui->page_4_label_hint, 43);
    lv_obj_set_width(bk_ui->page_4_label_hint, 284);
    lv_obj_set_height(bk_ui->page_4_label_hint, 24);
    lv_obj_set_style_text_color(bk_ui->page_4_label_hint, lv_color_hex(0xd4d9e3), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_4_label_hint, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_4_label_hint, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_4_label_hint, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_4_button_1 = lv_btn_create(bk_ui->page_4);
    bk_ui->page_4_button_1_label = lv_label_create(bk_ui->page_4_button_1);
    lv_label_set_text(bk_ui->page_4_button_1_label, "开始配网");
    lv_label_set_long_mode(bk_ui->page_4_button_1_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_4_button_1_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_4_button_1, 20);
    lv_obj_set_y(bk_ui->page_4_button_1, 133);
    lv_obj_set_width(bk_ui->page_4_button_1, 316);
    lv_obj_set_height(bk_ui->page_4_button_1, 48);
    lv_obj_set_style_bg_color(bk_ui->page_4_button_1, lv_color_hex(0x1677ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_4_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_4_button_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_4_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_4_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_4_button_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_4_button_1, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_4_button_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_4_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_4_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_4_button_1, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_4_button_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_4_button_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_4_button_1, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_4_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_4_button_1, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_4_button_2 = lv_btn_create(bk_ui->page_4);
    bk_ui->page_4_button_2_label = lv_label_create(bk_ui->page_4_button_2);
    lv_label_set_text(bk_ui->page_4_button_2_label, "删除配网");
    lv_label_set_long_mode(bk_ui->page_4_button_2_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_4_button_2_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_4_button_2, 20);
    lv_obj_set_y(bk_ui->page_4_button_2, 192);
    lv_obj_set_width(bk_ui->page_4_button_2, 148);
    lv_obj_set_height(bk_ui->page_4_button_2, 48);
    lv_obj_set_style_bg_color(bk_ui->page_4_button_2, lv_color_hex(0x2a3a4f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_4_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_4_button_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_4_button_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_4_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_4_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_4_button_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_4_button_2, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_4_button_2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_4_button_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_4_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_4_button_2, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_4_button_2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_4_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_4_button_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_4_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_4_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_4_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_4_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_4_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_4_button_3 = lv_btn_create(bk_ui->page_4);
    bk_ui->page_4_button_3_label = lv_label_create(bk_ui->page_4_button_3);
    lv_label_set_text(bk_ui->page_4_button_3_label, "恢复出厂设置");
    lv_label_set_long_mode(bk_ui->page_4_button_3_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_4_button_3_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_4_button_3, 188);
    lv_obj_set_y(bk_ui->page_4_button_3, 192);
    lv_obj_set_width(bk_ui->page_4_button_3, 148);
    lv_obj_set_height(bk_ui->page_4_button_3, 48);
    lv_obj_set_style_bg_color(bk_ui->page_4_button_3, lv_color_hex(0xa83232), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_4_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_4_button_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_4_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_4_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_4_button_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_4_button_3, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_4_button_3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_4_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_4_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_4_button_3, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_4_button_3, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_4_button_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_4_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);


    lv_obj_update_layout(bk_ui->page_4);

    bk_page_fire_init(4, bk_ui);
}

/*
 * @brief: destroy page page_4
 */
void destroy_page_page_4(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(4, bk_ui);

    if (bk_ui->page_4 != NULL) {
        lv_obj_del(bk_ui->page_4);
        bk_ui->page_4 = NULL;
        bk_ui->page_4_label_title = NULL;
        bk_ui->page_4_label_desc = NULL;
        bk_ui->page_4_status_card = NULL;
        bk_ui->page_4_label_status = NULL;
        bk_ui->page_4_label_hint = NULL;
        bk_ui->page_4_button_1 = NULL;
        bk_ui->page_4_button_1_label = NULL;
        bk_ui->page_4_button_2 = NULL;
        bk_ui->page_4_button_2_label = NULL;
        bk_ui->page_4_button_3 = NULL;
        bk_ui->page_4_button_3_label = NULL;
    }
}
