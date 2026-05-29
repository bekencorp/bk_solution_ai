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
#include <stdio.h>
#include <string.h>

#ifdef ROBOT_TEST
#include "ui_nav_router.h"
#include "components/log.h"

#define TAG "page9"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define PAGE9_MENU_COUNT 3

static int s_page9_menu_idx;

static lv_obj_t *page_9_menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }

    switch (idx) {
    case 0: return ui->page_9_button_play;
    case 1: return ui->page_9_button_stop;
    case 2: return ui->page_9_button_next;
    default: return NULL;
    }
}

/* Focus via recoloring instead of LV_STATE_DISABLED; see page_2 for why
 * this matters for TP click delivery. */
static void page_9_apply_menu_focus(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    for (int i = 0; i < PAGE9_MENU_COUNT; i++) {
        lv_obj_t *btn = page_9_menu_btn(ui, i);
        if (btn == NULL) {
            continue;
        }
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
        uint32_t color = (i == s_page9_menu_idx) ? 0xc0c0c0 : 0x2d75b9;
        lv_obj_set_style_bg_color(btn, lv_color_hex(color),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void page_9_refresh_text(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    if (ui->page_9_label_state) {
        lv_label_set_text_fmt(ui->page_9_label_state, "状态: %s", ui_music_state_text());
    }
    if (ui->page_9_label_track) {
        lv_label_set_text_fmt(ui->page_9_label_track, "曲目: %s", ui_music_track_text());
    }
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_page9_menu_idx = (s_page9_menu_idx + PAGE9_MENU_COUNT - 1) % PAGE9_MENU_COUNT;
    page_9_apply_menu_focus(ui);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_page9_menu_idx = (s_page9_menu_idx + 1) % PAGE9_MENU_COUNT;
    page_9_apply_menu_focus(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    ui_music_stop();
    LOGI("page9 back -> page_3\r\n");
    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    LOGI("page9 short press idx=%d\r\n", s_page9_menu_idx);
    switch (s_page9_menu_idx) {
    case 0:
        ui_music_play();
        break;
    case 1:
        ui_music_stop();
        break;
    case 2:
        ui_music_next();
        break;
    default:
        break;
    }

    page_9_refresh_text(ui);
}

const ui_page_nav_ops_t page_9_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

/* TP click adapter: tap play/stop/next = "select + confirm". */
static void page_9_button_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= PAGE9_MENU_COUNT) {
        return;
    }
    s_page9_menu_idx = idx;
    page_9_apply_menu_focus(&bk_lv_tool_ui);
    on_screen_next(&bk_lv_tool_ui);
}

static void page_9_register_button_clicks(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE9_MENU_COUNT; i++) {
        lv_obj_t *b = page_9_menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_add_event_cb(b, page_9_button_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
}
#endif

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

#ifdef ROBOT_TEST
    s_page9_menu_idx = 0;
    page_9_apply_menu_focus(bk_ui);
    page_9_refresh_text(bk_ui);
    page_9_register_button_clicks(bk_ui);
    (void)ui_nav_register_screen(bk_ui->page_9, &page_9_nav_ops);
#endif

    lv_obj_update_layout(bk_ui->page_9);
}

void destroy_page_page_9(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

#ifdef ROBOT_TEST
    ui_nav_unregister_screen(bk_ui->page_9);
#endif

    if (bk_ui->page_9 != NULL) {
        lv_obj_del(bk_ui->page_9);
        bk_ui->page_9 = NULL;
    }
}
