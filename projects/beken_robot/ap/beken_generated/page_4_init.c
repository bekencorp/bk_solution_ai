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
#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#define TAG "page4"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define PAGE4_MENU_COUNT 3

static int s_page4_menu_idx;

static lv_obj_t *page_4_menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_4_button_1;
    case 1: return ui->page_4_button_2;
    case 2: return ui->page_4_button_3;
    default: return NULL;
    }
}

/* Focus is shown by recoloring, not LV_STATE_DISABLED. See page_2 for
 * the rationale: disabled widgets do not receive PRESSED/CLICKED, which
 * would break the per-button TP click adapter below. */
static void page_4_apply_menu_focus(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE4_MENU_COUNT; i++) {
        lv_obj_t *b = page_4_menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_remove_state(b, LV_STATE_DISABLED);
        uint32_t color = (i == s_page4_menu_idx) ? 0xc0c0c0 : 0x2d75b9;
        lv_obj_set_style_bg_color(b, lv_color_hex(color),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_page4_menu_idx = (s_page4_menu_idx + PAGE4_MENU_COUNT - 1) % PAGE4_MENU_COUNT;
    page_4_apply_menu_focus(ui);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_page4_menu_idx = (s_page4_menu_idx + 1) % PAGE4_MENU_COUNT;
    page_4_apply_menu_focus(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    /* page_4 现在是从 page_2 的"配网"按钮进入，返回应回 page_2 */
    navigate_to_screen((lv_obj_t **)&ui->page_2,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_2);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page4 short press idx=%d\r\n", s_page4_menu_idx);
    switch (s_page4_menu_idx) {
    case 0:
        LOGI("Start provisioning (short press S4 to trigger)\r\n");
#if CONFIG_BK_SMART_CONFIG
        bk_sconf_prepare_for_smart_config();
#endif
        break;
    case 1:
        LOGI("Delete\r\n");
        break;
    case 2:
        LOGI("Factory reset\r\n");
        break;
    default:
        break;
    }
}

static void on_confirm_long(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page4 long press idx=%d\r\n", s_page4_menu_idx);
    if (s_page4_menu_idx == 0) {
        LOGI("Provisioning is triggered by short press S4\r\n");
    }
}

const ui_page_nav_ops_t page_4_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
    .on_confirm_long = on_confirm_long,
};

/* TP click adapter: tap a menu button = "select + confirm" (short press
 * semantics). Long-press behaviour stays bound to the physical key. */
static void page_4_button_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= PAGE4_MENU_COUNT) {
        return;
    }
    s_page4_menu_idx = idx;
    page_4_apply_menu_focus(&bk_lv_tool_ui);
    on_screen_next(&bk_lv_tool_ui);
}

static void page_4_register_button_clicks(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE4_MENU_COUNT; i++) {
        lv_obj_t *b = page_4_menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_add_event_cb(b, page_4_button_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
}

#endif

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
    lv_obj_set_size(bk_ui->page_4, 390, 360);
    lv_obj_set_style_bg_color(bk_ui->page_4, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_4, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_4, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_4_button_1 = lv_btn_create(bk_ui->page_4);
    bk_ui->page_4_button_1_label = lv_label_create(bk_ui->page_4_button_1);
    lv_label_set_text(bk_ui->page_4_button_1_label, "开始");
    lv_label_set_long_mode(bk_ui->page_4_button_1_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_4_button_1_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_4_button_1, 17);
    lv_obj_set_y(bk_ui->page_4_button_1, 189);
    lv_obj_set_width(bk_ui->page_4_button_1, 100);
    lv_obj_set_height(bk_ui->page_4_button_1, 40);
    lv_obj_set_style_bg_color(bk_ui->page_4_button_1, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_4_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_4_button_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_4_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_4_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_4_button_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_4_button_1, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_4_button_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_4_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_4_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_4_button_1, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_4_button_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_4_button_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_4_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_4_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_4_button_2 = lv_btn_create(bk_ui->page_4);
    bk_ui->page_4_button_2_label = lv_label_create(bk_ui->page_4_button_2);
    lv_label_set_text(bk_ui->page_4_button_2_label, "删除");
    lv_label_set_long_mode(bk_ui->page_4_button_2_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_4_button_2_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_4_button_2, 131);
    lv_obj_set_y(bk_ui->page_4_button_2, 190);
    lv_obj_set_width(bk_ui->page_4_button_2, 100);
    lv_obj_set_height(bk_ui->page_4_button_2, 40);
    lv_obj_set_style_bg_color(bk_ui->page_4_button_2, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_4_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_4_button_2, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_4_button_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_4_button_2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_4_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_4_button_2, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_4_button_2, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_4_button_2, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_4_button_2, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_4_button_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_4_button_2, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_4_button_2, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
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
    lv_obj_set_x(bk_ui->page_4_button_3, 249);
    lv_obj_set_y(bk_ui->page_4_button_3, 189);
    lv_obj_set_width(bk_ui->page_4_button_3, 100);
    lv_obj_set_height(bk_ui->page_4_button_3, 40);
    lv_obj_set_style_bg_color(bk_ui->page_4_button_3, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_4_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_4_button_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_4_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_4_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_4_button_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_4_button_3, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_4_button_3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_4_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_4_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_4_button_3, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_4_button_3, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_4_button_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_4_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_4_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);


    // custom code implementation
            #ifdef ROBOT_TEST
                s_page4_menu_idx = 0;
                page_4_apply_menu_focus(bk_ui);
                page_4_register_button_clicks(bk_ui);
                (void)ui_nav_register_screen(bk_ui->page_4, &page_4_nav_ops);
            #endif
    
    lv_obj_update_layout(bk_ui->page_4);
}

/*
 * @brief: destroy page page_4
 */
void destroy_page_page_4(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }
    
    if (bk_ui->page_4 != NULL) {
        lv_obj_del(bk_ui->page_4);
        bk_ui->page_4 = NULL;
    }
}