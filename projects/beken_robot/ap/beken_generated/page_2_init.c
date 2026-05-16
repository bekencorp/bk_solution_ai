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
#include "bk_smart_config.h"
// custom page code
#if ROBOT_TEST

#include "ui_nav_router.h"
#include "components/log.h"

#define TAG "page2"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

/*
 * page_2 当前布局（设计器已重排）：
 *   - page_2_button_1  "配 网"     -> 跳转 page_4 (BLE 配网界面)
 *   - page_2_button_3  "示例模式"  -> 跳转 page_3 (子菜单)
 *
 * 注意：page_2_button_2 已被 designer 删除，不在 bk_lv_ui_t 里，
 * 所以菜单循环只走两个按钮，idx -> obj 的映射跳过 button_2。
 */
#define PAGE2_MENU_COUNT  2

static int s_page2_menu_idx;

static lv_obj_t *page_2_menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_2_button_1;   /* 配网 */
    case 1: return ui->page_2_button_3;   /* 示例模式 */
    default: return NULL;
    }
}

static void page_2_apply_menu_focus(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE2_MENU_COUNT; i++) {
        lv_obj_t *b = page_2_menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        if (i == s_page2_menu_idx) {
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
    s_page2_menu_idx = (s_page2_menu_idx + PAGE2_MENU_COUNT - 1) % PAGE2_MENU_COUNT;
    page_2_apply_menu_focus(ui);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_page2_menu_idx = (s_page2_menu_idx + 1) % PAGE2_MENU_COUNT;
    page_2_apply_menu_focus(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    navigate_to_screen((lv_obj_t **)&ui->page_1,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_1);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page2 enter idx=%d\r\n", s_page2_menu_idx);
    switch (s_page2_menu_idx) {
    case 0:  /* 配网 -> page_4 (BLE 配网界面) */
        LOGI("Provisioning -> page_4\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_4,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_4);
        break;
    case 1:  /* 示例模式 -> page_3 */
        LOGI("Demo mode -> page_3\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_3,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_3);
        break;
    default:
        break;
    }
}

const ui_page_nav_ops_t page_2_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

#endif

/*
 * @brief: init page page_2
 */
void init_page_page_2(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_2 != NULL && lv_obj_is_valid(bk_ui->page_2)) {
        destroy_page_page_2(bk_ui);
    }
    

    bk_ui->page_2 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_2, 390, 360);
    lv_obj_set_style_bg_color(bk_ui->page_2, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Status indicators (moved from page_1): wifi icon + battery bar. */
    bk_ui->page_2_image_2 = lv_image_create(bk_ui->page_2);
    lv_image_set_src(bk_ui->page_2_image_2, &wifi_1_23x24_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->page_2_image_2, 50, 50);
    lv_image_set_rotation(bk_ui->page_2_image_2, 0);
    lv_obj_set_x(bk_ui->page_2_image_2, 53);
    lv_obj_set_y(bk_ui->page_2_image_2, 10);
    lv_obj_set_width(bk_ui->page_2_image_2, 23);
    lv_obj_set_height(bk_ui->page_2_image_2, 24);
    lv_obj_set_style_bg_color(bk_ui->page_2_image_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_image_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_image_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_image_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_image_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_2_image_2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_image_2, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_image_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_2_image_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_2_image_2, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_2_image_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (!bk_sconf_is_network_provisioned()) {
        lv_obj_add_flag(bk_ui->page_2_image_2, LV_OBJ_FLAG_HIDDEN);
    }

    bk_ui->page_2_bar_1 = lv_bar_create(bk_ui->page_2);
    lv_bar_set_range(bk_ui->page_2_bar_1, 0, 90);
    lv_obj_set_style_anim_duration(bk_ui->page_2_bar_1, 1000, 0);
    lv_bar_set_start_value(bk_ui->page_2_bar_1, 0, LV_ANIM_ON);
    lv_bar_set_value(bk_ui->page_2_bar_1, 0, LV_ANIM_ON);
    lv_bar_set_mode(bk_ui->page_2_bar_1, LV_BAR_MODE_NORMAL);
    lv_obj_set_x(bk_ui->page_2_bar_1, 255);
    lv_obj_set_y(bk_ui->page_2_bar_1, 17);
    lv_obj_set_width(bk_ui->page_2_bar_1, 50);
    lv_obj_set_height(bk_ui->page_2_bar_1, 10);
    lv_obj_set_style_anim_duration(bk_ui->page_2_bar_1, 1000, 0);
    lv_obj_set_style_bg_color(bk_ui->page_2_bar_1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_bar_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_bar_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_bar_1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_bar_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_bar_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_2_bar_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_bar_1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_bar_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_2_button_1 = lv_btn_create(bk_ui->page_2);
    bk_ui->page_2_button_1_label = lv_label_create(bk_ui->page_2_button_1);
    lv_label_set_text(bk_ui->page_2_button_1_label, "配    网");
    lv_label_set_long_mode(bk_ui->page_2_button_1_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_2_button_1_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_2_button_1, 25);
    lv_obj_set_y(bk_ui->page_2_button_1, 245);
    lv_obj_set_width(bk_ui->page_2_button_1, 100);
    lv_obj_set_height(bk_ui->page_2_button_1, 40);
    lv_obj_set_style_bg_color(bk_ui->page_2_button_1, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_button_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_button_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_button_1, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_2_button_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_2_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_button_1, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_button_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_button_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_2_button_1, lv_color_hex(0xc0c0c0), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_button_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DISABLED);

    bk_ui->page_2_button_3 = lv_btn_create(bk_ui->page_2);
    bk_ui->page_2_button_3_label = lv_label_create(bk_ui->page_2_button_3);
    lv_label_set_text(bk_ui->page_2_button_3_label, "示例模式");
    lv_label_set_long_mode(bk_ui->page_2_button_3_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_2_button_3_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_2_button_3, 246);
    lv_obj_set_y(bk_ui->page_2_button_3, 246);
    lv_obj_set_width(bk_ui->page_2_button_3, 100);
    lv_obj_set_height(bk_ui->page_2_button_3, 40);
    lv_obj_add_state(bk_ui->page_2_button_3, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(bk_ui->page_2_button_3, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_button_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_button_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_button_3, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_2_button_3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_2_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_button_3, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_button_3, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_button_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_2_button_3, lv_color_hex(0xc0c0c0), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_button_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DISABLED);

    bk_ui->page_2_image_1 = lv_image_create(bk_ui->page_2);
    lv_image_set_src(bk_ui->page_2_image_1, &beken_logo_blue_321x147_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->page_2_image_1, 50, 50);
    lv_image_set_rotation(bk_ui->page_2_image_1, 0);
    lv_obj_set_x(bk_ui->page_2_image_1, 35);
    lv_obj_set_y(bk_ui->page_2_image_1, 80);
    lv_obj_set_width(bk_ui->page_2_image_1, 321);
    lv_obj_set_height(bk_ui->page_2_image_1, 147);
    lv_obj_set_style_bg_color(bk_ui->page_2_image_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_image_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_image_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_image_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_image_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_2_image_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_image_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_image_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_2_image_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_2_image_1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);


    // custom code implementation
        // custom code implementation
            #if ROBOT_TEST
                s_page2_menu_idx = 0;
                page_2_apply_menu_focus(bk_ui);
                (void)ui_nav_register_screen(bk_ui->page_2, &page_2_nav_ops);
            #endif
    
    lv_obj_update_layout(bk_ui->page_2);
}

/*
 * @brief: destroy page page_2
 */
void destroy_page_page_2(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->page_2 != NULL) {
        lv_obj_del(bk_ui->page_2);
        bk_ui->page_2 = NULL;
    }
}