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
 *
 * Author: Beken LVGL Designer Tool
*/
#include "lvgl.h"
#include "beken_ui.h"
#include "custom_func.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "ui_i18n.h"
#include <stdio.h>
#include <string.h>


static void page_10_style_text(lv_obj_t *obj, const lv_font_t *font)
{
    lv_obj_set_style_text_color(obj, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void page_10_style_button(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x555555), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(obj, 180, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_radius(obj, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void init_page_page_10(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_10 != NULL && lv_obj_is_valid(bk_ui->page_10)) {
        destroy_page_page_10(bk_ui);
    }

    bk_ui->page_10 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_10, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_10, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(bk_ui->page_10, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_10, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_label_title = lv_label_create(bk_ui->page_10);
    lv_label_set_text(bk_ui->page_10_label_title, ui_tr(STR_VOLUME_TITLE));
    page_10_style_text(bk_ui->page_10_label_title, &lv_font_ali_25);
    lv_obj_set_pos(bk_ui->page_10_label_title, UI_LX(24), UI_LY(24));

    bk_ui->page_10_icon_speaker = lv_obj_create(bk_ui->page_10);
    lv_obj_remove_style_all(bk_ui->page_10_icon_speaker);
    lv_obj_set_pos(bk_ui->page_10_icon_speaker, UI_LX(132), UI_LY(64));
    lv_obj_set_size(bk_ui->page_10_icon_speaker, UI_LX(96), UI_LY(64));

    bk_ui->page_10_icon_body = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_body);
    lv_obj_set_pos(bk_ui->page_10_icon_body, UI_LX(6), UI_LY(24));
    lv_obj_set_size(bk_ui->page_10_icon_body, UI_LX(18), UI_LY(20));
    lv_obj_set_style_bg_color(bk_ui->page_10_icon_body, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_icon_body, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_10_icon_body, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_cone = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_cone);
    lv_obj_set_pos(bk_ui->page_10_icon_cone, UI_LX(23), UI_LY(18));
    lv_obj_set_size(bk_ui->page_10_icon_cone, UI_LX(18), UI_LY(32));
    lv_obj_set_style_bg_color(bk_ui->page_10_icon_cone, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_icon_cone, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_10_icon_cone, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_wave_1 = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_wave_1);
    lv_obj_set_pos(bk_ui->page_10_icon_wave_1, UI_LX(51), UI_LY(30));
    lv_obj_set_size(bk_ui->page_10_icon_wave_1, UI_LX(6), UI_LY(16));
    lv_obj_set_style_radius(bk_ui->page_10_icon_wave_1, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_wave_2 = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_wave_2);
    lv_obj_set_pos(bk_ui->page_10_icon_wave_2, UI_LX(63), UI_LY(24));
    lv_obj_set_size(bk_ui->page_10_icon_wave_2, UI_LX(6), UI_LY(28));
    lv_obj_set_style_radius(bk_ui->page_10_icon_wave_2, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_wave_3 = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_wave_3);
    lv_obj_set_pos(bk_ui->page_10_icon_wave_3, UI_LX(75), UI_LY(18));
    lv_obj_set_size(bk_ui->page_10_icon_wave_3, UI_LX(6), UI_LY(40));
    lv_obj_set_style_radius(bk_ui->page_10_icon_wave_3, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_slash = lv_label_create(bk_ui->page_10_icon_speaker);
    lv_label_set_text(bk_ui->page_10_icon_slash, "X");
    page_10_style_text(bk_ui->page_10_icon_slash, &lv_font_ali_30);
    lv_obj_set_style_text_color(bk_ui->page_10_icon_slash, lv_color_hex(0xff4d4f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(bk_ui->page_10_icon_slash, UI_LX(57), UI_LY(15));

    bk_ui->page_10_label_value = lv_label_create(bk_ui->page_10);
    lv_label_set_text(bk_ui->page_10_label_value, "0 / 10");
    page_10_style_text(bk_ui->page_10_label_value, &lv_font_ali_30);
    lv_obj_set_width(bk_ui->page_10_label_value, UI_LX(160));
    lv_obj_set_style_text_align(bk_ui->page_10_label_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(bk_ui->page_10_label_value, UI_LX(100), UI_LY(140));

    bk_ui->page_10_slider_volume = lv_slider_create(bk_ui->page_10);
    lv_obj_set_size(bk_ui->page_10_slider_volume, UI_LX(250), UI_LY(18));
    lv_obj_set_pos(bk_ui->page_10_slider_volume, UI_LX(55), UI_LY(185));
    lv_obj_set_style_bg_color(bk_ui->page_10_slider_volume, lv_color_hex(0x3a3a3a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_slider_volume, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_10_slider_volume, lv_color_hex(0x49a9ff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_slider_volume, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_10_slider_volume, lv_color_hex(0xffffff), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_slider_volume, 255, LV_PART_KNOB | LV_STATE_DEFAULT);

    bk_ui->page_10_button_minus = lv_btn_create(bk_ui->page_10);
    page_10_style_button(bk_ui->page_10_button_minus);
    lv_obj_set_pos(bk_ui->page_10_button_minus, UI_LX(76), UI_LY(245));
    lv_obj_set_size(bk_ui->page_10_button_minus, UI_LX(80), UI_LY(46));
    bk_ui->page_10_button_minus_label = lv_label_create(bk_ui->page_10_button_minus);
    lv_label_set_text(bk_ui->page_10_button_minus_label, "-");
    page_10_style_text(bk_ui->page_10_button_minus_label, &lv_font_ali_30);
    lv_obj_align(bk_ui->page_10_button_minus_label, LV_ALIGN_CENTER, 0, 0);

    bk_ui->page_10_button_plus = lv_btn_create(bk_ui->page_10);
    page_10_style_button(bk_ui->page_10_button_plus);
    lv_obj_set_pos(bk_ui->page_10_button_plus, UI_LX(204), UI_LY(245));
    lv_obj_set_size(bk_ui->page_10_button_plus, UI_LX(80), UI_LY(46));
    bk_ui->page_10_button_plus_label = lv_label_create(bk_ui->page_10_button_plus);
    lv_label_set_text(bk_ui->page_10_button_plus_label, "+");
    page_10_style_text(bk_ui->page_10_button_plus_label, &lv_font_ali_30);
    lv_obj_align(bk_ui->page_10_button_plus_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_update_layout(bk_ui->page_10);

    bk_page_fire_init(10, bk_ui);
}

void destroy_page_page_10(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(10, bk_ui);

    if (bk_ui->page_10 != NULL) {
        lv_obj_del(bk_ui->page_10);
        bk_ui->page_10 = NULL;
    }
}
