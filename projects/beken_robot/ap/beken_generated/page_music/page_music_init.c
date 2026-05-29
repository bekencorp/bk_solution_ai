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


void init_page_page_9(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_9 != NULL && lv_obj_is_valid(bk_ui->page_9)) {
        destroy_page_page_9(bk_ui);
    }

    bk_ui->page_9 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_9, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_9, 360, 390);
    lv_obj_set_style_bg_color(bk_ui->page_9, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_9, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_9_label_title = lv_label_create(bk_ui->page_9);
    lv_label_set_text(bk_ui->page_9_label_title, "音乐播放");
    lv_obj_set_style_text_color(bk_ui->page_9_label_title, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_9_label_title, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(bk_ui->page_9_label_title, 24, 24);

    bk_ui->page_9_label_state = lv_label_create(bk_ui->page_9);
    lv_label_set_text(bk_ui->page_9_label_state, "状态: 已停止");
    lv_obj_set_style_text_color(bk_ui->page_9_label_state, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_9_label_state, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(bk_ui->page_9_label_state, 24, 62);

    bk_ui->page_9_label_track = lv_label_create(bk_ui->page_9);
    lv_label_set_text(bk_ui->page_9_label_track, "曲目: Hi Armino Tone");
    lv_obj_set_style_text_color(bk_ui->page_9_label_track, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_9_label_track, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(bk_ui->page_9_label_track, 24, 92);

    bk_ui->page_9_button_play = lv_btn_create(bk_ui->page_9);
    bk_ui->page_9_button_play_label = lv_label_create(bk_ui->page_9_button_play);
    lv_label_set_text(bk_ui->page_9_button_play_label, "播放");
    lv_obj_align(bk_ui->page_9_button_play_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_pos(bk_ui->page_9_button_play, 24, 170);
    lv_obj_set_size(bk_ui->page_9_button_play, 92, 44);

    bk_ui->page_9_button_stop = lv_btn_create(bk_ui->page_9);
    bk_ui->page_9_button_stop_label = lv_label_create(bk_ui->page_9_button_stop);
    lv_label_set_text(bk_ui->page_9_button_stop_label, "停止");
    lv_obj_align(bk_ui->page_9_button_stop_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_pos(bk_ui->page_9_button_stop, 134, 170);
    lv_obj_set_size(bk_ui->page_9_button_stop, 92, 44);

    bk_ui->page_9_button_next = lv_btn_create(bk_ui->page_9);
    bk_ui->page_9_button_next_label = lv_label_create(bk_ui->page_9_button_next);
    lv_label_set_text(bk_ui->page_9_button_next_label, "下一首");
    lv_obj_align(bk_ui->page_9_button_next_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_pos(bk_ui->page_9_button_next, 244, 170);
    lv_obj_set_size(bk_ui->page_9_button_next, 92, 44);

    lv_obj_set_style_bg_color(bk_ui->page_9_button_play, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_9_button_stop, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_9_button_next, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_9_button_play, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_9_button_stop, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_9_button_next, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_9_button_play, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_9_button_stop, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_9_button_next, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);

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
