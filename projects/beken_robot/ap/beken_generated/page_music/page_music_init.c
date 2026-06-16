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
#include <stdio.h>
#include <string.h>

#define PAGE9_BAR_Y_BASE    170
#define PAGE9_BUTTON_Y      240

static void page9_style_btn(lv_obj_t *btn)
{
    if (btn == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x21283f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x2a3458), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x3a4f86), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xeaf4ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void init_page_page_9(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_9 != NULL && lv_obj_is_valid(bk_ui->page_9)) {
        destroy_page_page_9(bk_ui);
    }

    bk_ui->page_9 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_9, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_9, 360, 390);
    lv_obj_set_style_bg_color(bk_ui->page_9, lv_color_hex(0x05070f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(bk_ui->page_9, lv_color_hex(0x121a2e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_9, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_9_label_title = NULL;
    bk_ui->page_9_label_state = NULL;

    bk_ui->page_9_label_track = lv_label_create(bk_ui->page_9);
    lv_label_set_text(bk_ui->page_9_label_track, "");
    lv_obj_set_width(bk_ui->page_9_label_track, 320);
    lv_label_set_long_mode(bk_ui->page_9_label_track, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(bk_ui->page_9_label_track, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_9_label_track, lv_color_hex(0xf4f7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_9_label_track, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(bk_ui->page_9_label_track, 20, PAGE9_BAR_Y_BASE + 10);

    bk_ui->page_9_button_play = lv_btn_create(bk_ui->page_9);
    lv_obj_set_size(bk_ui->page_9_button_play, 92, 44);
    lv_obj_set_pos(bk_ui->page_9_button_play, 20, PAGE9_BUTTON_Y);
    bk_ui->page_9_button_play_label = lv_label_create(bk_ui->page_9_button_play);
    lv_label_set_text(bk_ui->page_9_button_play_label, "PREV");
    lv_obj_align(bk_ui->page_9_button_play_label, LV_ALIGN_CENTER, 0, 0);
    page9_style_btn(bk_ui->page_9_button_play);

    bk_ui->page_9_button_stop = lv_btn_create(bk_ui->page_9);
    lv_obj_set_size(bk_ui->page_9_button_stop, 92, 44);
    lv_obj_set_pos(bk_ui->page_9_button_stop, 134, PAGE9_BUTTON_Y);
    bk_ui->page_9_button_stop_label = lv_label_create(bk_ui->page_9_button_stop);
    lv_label_set_text(bk_ui->page_9_button_stop_label, "PLAY");
    lv_obj_align(bk_ui->page_9_button_stop_label, LV_ALIGN_CENTER, 0, 0);
    page9_style_btn(bk_ui->page_9_button_stop);

    bk_ui->page_9_button_next = lv_btn_create(bk_ui->page_9);
    lv_obj_set_size(bk_ui->page_9_button_next, 92, 44);
    lv_obj_set_pos(bk_ui->page_9_button_next, 248, PAGE9_BUTTON_Y);
    bk_ui->page_9_button_next_label = lv_label_create(bk_ui->page_9_button_next);
    lv_label_set_text(bk_ui->page_9_button_next_label, "NEXT");
    lv_obj_align(bk_ui->page_9_button_next_label, LV_ALIGN_CENTER, 0, 0);
    page9_style_btn(bk_ui->page_9_button_next);

    lv_obj_update_layout(bk_ui->page_9);

    bk_page_fire_init(9, bk_ui);
}

void destroy_page_page_9(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(9, bk_ui);

    if (bk_ui->page_9 != NULL) {
        lv_obj_del(bk_ui->page_9);
        bk_ui->page_9 = NULL;
    }
}
