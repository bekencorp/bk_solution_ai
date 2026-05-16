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
#include <stdio.h>
#include <string.h>
// custom page code
#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "components/log.h"
#include "audio_engine.h"

#define TAG "page3"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define PAGE3_MENU_COUNT 7

static int s_page3_menu_idx;

static lv_obj_t *page_3_menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_3_button_1;
    case 1: return ui->page_3_button_2;
    case 2: return ui->page_3_button_3;
    case 3: return ui->page_3_button_4;
    case 4: return ui->page_3_button_5;
    case 5: return ui->page_3_button_6;
    case 6: return ui->page_3_button_7;
    default: return NULL;
    }
}

static void page_3_apply_menu_focus(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE3_MENU_COUNT; i++) {
        lv_obj_t *b = page_3_menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        if (i == s_page3_menu_idx) {
            lv_obj_add_state(b, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(b, LV_STATE_DISABLED);
        }
    }
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_page3_menu_idx = (s_page3_menu_idx + PAGE3_MENU_COUNT - 1) % PAGE3_MENU_COUNT;
    page_3_apply_menu_focus(ui);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_page3_menu_idx = (s_page3_menu_idx + 1) % PAGE3_MENU_COUNT;
    page_3_apply_menu_focus(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    navigate_to_screen((lv_obj_t **)&ui->page_2,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_2);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page3 enter idx=%d\r\n", s_page3_menu_idx);
    switch (s_page3_menu_idx) {
    case 0:
        LOGI("AI chat -> page_6\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_6,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_6);
        break;
    case 1:
        LOGI("Vision recognition -> page_7\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_7,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_7);
        break;
    case 2:
        LOGI("Speech recognition -> page_8\r\n");
#if (CONFIG_ASR_SERVICE)
        if (AUDIO_ENGINE_SUCCESS == audio_engine_asr_start()) {
            LOGI("page8 start asr\r\n");
            navigate_to_screen((lv_obj_t **)&ui->page_8,
                               LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                               init_page_page_8);
            if (ui->page_8 == NULL || !lv_obj_is_valid(ui->page_8)) {
                LOGI("page8 init failed, rollback asr\r\n");
                (void)audio_engine_asr_stop();
            }
        } else {
            LOGI("page8 start asr failed\r\n");
        }
#else
        navigate_to_screen((lv_obj_t **)&ui->page_8,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_8);
#endif
        break;
    case 3:
        LOGI("Face tracking\r\n");
        break;
    case 4:
        LOGI("Volume settings -> page_10\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_10,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_10);
        break;
    case 5:
        LOGI("Sound source localization -> page_5\r\n");
#if (CONFIG_ASR_SERVICE)
        if (AUDIO_ENGINE_SUCCESS == audio_engine_asr_start()) {
            LOGI("page5 start asr\r\n");
            navigate_to_screen((lv_obj_t **)&ui->page_5,
                               LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                               init_page_page_5);
            if (ui->page_5 == NULL || !lv_obj_is_valid(ui->page_5)) {
                LOGI("page5 init failed, rollback asr\r\n");
                (void)audio_engine_asr_stop();
            }
        } else {
            LOGI("page5 start asr failed\r\n");
        }
#else
        navigate_to_screen((lv_obj_t **)&ui->page_5,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_5);
#endif
        break;
    case 6:
        LOGI("Music -> page_9\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_9,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_9);
        break;
    default:
        break;
    }
}

const ui_page_nav_ops_t page_3_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

#endif

/*
 * @brief: init page page_3
 */
void init_page_page_3(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_3 != NULL && lv_obj_is_valid(bk_ui->page_3)) {
        destroy_page_page_3(bk_ui);
    }
    

    bk_ui->page_3 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_3, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_3, 390, 360);
    lv_obj_set_style_bg_color(bk_ui->page_3, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_3_button_1 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_1_label = lv_label_create(bk_ui->page_3_button_1);
    lv_label_set_text(bk_ui->page_3_button_1_label, "AI对话");
    lv_label_set_long_mode(bk_ui->page_3_button_1_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_1_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_1, 14);
    lv_obj_set_y(bk_ui->page_3_button_1, 93);
    lv_obj_set_width(bk_ui->page_3_button_1, 100);
    lv_obj_set_height(bk_ui->page_3_button_1, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_1, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_1, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_1, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_3_button_2 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_2_label = lv_label_create(bk_ui->page_3_button_2);
    lv_label_set_text(bk_ui->page_3_button_2_label, "视觉识别");
    lv_label_set_long_mode(bk_ui->page_3_button_2_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_2_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_2, 131);
    lv_obj_set_y(bk_ui->page_3_button_2, 93);
    lv_obj_set_width(bk_ui->page_3_button_2, 100);
    lv_obj_set_height(bk_ui->page_3_button_2, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_2, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_2, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_2, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_2, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_3_button_3 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_3_label = lv_label_create(bk_ui->page_3_button_3);
    lv_label_set_text(bk_ui->page_3_button_3_label, "命令词识别");
    lv_label_set_long_mode(bk_ui->page_3_button_3_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_3_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_3, 244);
    lv_obj_set_y(bk_ui->page_3_button_3, 94);
    lv_obj_set_width(bk_ui->page_3_button_3, 100);
    lv_obj_set_height(bk_ui->page_3_button_3, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_3, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_3, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_3, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_3, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_3_button_4 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_4_label = lv_label_create(bk_ui->page_3_button_4);
    lv_label_set_text(bk_ui->page_3_button_4_label, "人脸跟踪");
    lv_label_set_long_mode(bk_ui->page_3_button_4_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_4_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_4, 16);
    lv_obj_set_y(bk_ui->page_3_button_4, 153);
    lv_obj_set_width(bk_ui->page_3_button_4, 100);
    lv_obj_set_height(bk_ui->page_3_button_4, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_4, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_4, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_4, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_4, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_4, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_4, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_4, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_4, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_3_button_5 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_5_label = lv_label_create(bk_ui->page_3_button_5);
    lv_label_set_text(bk_ui->page_3_button_5_label, "音量设置");
    lv_label_set_long_mode(bk_ui->page_3_button_5_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_5_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_5, 133);
    lv_obj_set_y(bk_ui->page_3_button_5, 152);
    lv_obj_set_width(bk_ui->page_3_button_5, 100);
    lv_obj_set_height(bk_ui->page_3_button_5, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_5, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_5, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_5, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_5, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_5, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_5, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_5, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_5, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_5, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_3_button_6 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_6_label = lv_label_create(bk_ui->page_3_button_6);
    lv_label_set_text(bk_ui->page_3_button_6_label, "声源定位");
    lv_label_set_long_mode(bk_ui->page_3_button_6_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_6_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_6, 248);
    lv_obj_set_y(bk_ui->page_3_button_6, 153);
    lv_obj_set_width(bk_ui->page_3_button_6, 100);
    lv_obj_set_height(bk_ui->page_3_button_6, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_6, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_6, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_6, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_6, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_6, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_6, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_6, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_6, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_6, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_6, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_6, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_3_button_7 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_7_label = lv_label_create(bk_ui->page_3_button_7);
    lv_label_set_text(bk_ui->page_3_button_7_label, "音乐播放");
    lv_label_set_long_mode(bk_ui->page_3_button_7_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_7_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_7, 131);
    lv_obj_set_y(bk_ui->page_3_button_7, 212);
    lv_obj_set_width(bk_ui->page_3_button_7, 100);
    lv_obj_set_height(bk_ui->page_3_button_7, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_7, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_7, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_7, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_7, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_7, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_7, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_7, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_7, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_7, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_7, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_7, 0, LV_PART_MAIN | LV_STATE_DEFAULT);


    // custom code implementation
        #ifdef ROBOT_TEST
            s_page3_menu_idx = 0;
            page_3_apply_menu_focus(bk_ui);
            (void)ui_nav_register_screen(bk_ui->page_3, &page_3_nav_ops);
        #endif
    
    lv_obj_update_layout(bk_ui->page_3);
}

/*
 * @brief: destroy page page_3
 */
void destroy_page_page_3(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->page_3 != NULL) {
        lv_obj_del(bk_ui->page_3);
        bk_ui->page_3 = NULL;
    }
}