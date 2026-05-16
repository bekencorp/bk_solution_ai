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
#include "audio_engine.h"
#include <stdio.h>
#include <string.h>

#ifdef ROBOT_TEST
#include "ui_nav_router.h"
#include "components/log.h"

#define TAG "page10"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

static bool s_page10_refreshing;

static void page_10_set_volume_level(uint8_t target)
{
    uint8_t max = audio_engine_volume_get_max_level();
    if (target > max) {
        target = max;
    }

    while (audio_engine_volume_get_level() < target) {
        uint8_t before = audio_engine_volume_get_level();
        audio_engine_volume_increase();
        if (audio_engine_volume_get_level() == before) {
            break;
        }
    }

    while (audio_engine_volume_get_level() > target) {
        uint8_t before = audio_engine_volume_get_level();
        audio_engine_volume_decrease();
        if (audio_engine_volume_get_level() == before) {
            break;
        }
    }
}

static void page_10_set_wave_state(lv_obj_t *wave, bool active)
{
    if (wave == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(wave, lv_color_hex(active ? 0x49a9ff : 0x4a4a4a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(wave, active ? 255 : 90, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void page_10_set_speaker_icon_level(bk_lv_ui_t *ui, uint8_t level)
{
    if (ui == NULL) {
        return;
    }

    page_10_set_wave_state(ui->page_10_icon_wave_1, level >= 1);
    page_10_set_wave_state(ui->page_10_icon_wave_2, level >= 4);
    page_10_set_wave_state(ui->page_10_icon_wave_3, level >= 8);

    if (ui->page_10_icon_slash) {
        if (level == 0) {
            lv_obj_clear_flag(ui->page_10_icon_slash, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui->page_10_icon_slash, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void page_10_refresh_volume(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    uint8_t level = audio_engine_volume_get_level();
    uint8_t max = audio_engine_volume_get_max_level();

    s_page10_refreshing = true;
    if (ui->page_10_label_value) {
        lv_label_set_text_fmt(ui->page_10_label_value, "%u / %u", level, max);
    }
    if (ui->page_10_slider_volume) {
        lv_slider_set_range(ui->page_10_slider_volume, 0, max);
        lv_slider_set_value(ui->page_10_slider_volume, level, LV_ANIM_OFF);
    }
    s_page10_refreshing = false;

    page_10_set_speaker_icon_level(ui, level);

    if (ui->page_10_button_minus) {
        if (level == 0) {
            lv_obj_add_state(ui->page_10_button_minus, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(ui->page_10_button_minus, LV_STATE_DISABLED);
        }
    }

    if (ui->page_10_button_plus) {
        if (level >= max) {
            lv_obj_add_state(ui->page_10_button_plus, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(ui->page_10_button_plus, LV_STATE_DISABLED);
        }
    }
}

static void page_10_volume_down(bk_lv_ui_t *ui)
{
    audio_engine_volume_decrease();
    page_10_refresh_volume(ui);
}

static void page_10_volume_up(bk_lv_ui_t *ui)
{
    audio_engine_volume_increase();
    page_10_refresh_volume(ui);
}

static void page_10_slider_event_cb(lv_event_t *e)
{
    if (s_page10_refreshing) {
        return;
    }

    bk_lv_ui_t *ui = (bk_lv_ui_t *)lv_event_get_user_data(e);
    if (ui == NULL || ui->page_10_slider_volume == NULL) {
        return;
    }

    page_10_set_volume_level((uint8_t)lv_slider_get_value(ui->page_10_slider_volume));
    page_10_refresh_volume(ui);
}

static void page_10_minus_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *ui = (bk_lv_ui_t *)lv_event_get_user_data(e);
    page_10_volume_down(ui);
}

static void page_10_plus_event_cb(lv_event_t *e)
{
    bk_lv_ui_t *ui = (bk_lv_ui_t *)lv_event_get_user_data(e);
    page_10_volume_up(ui);
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    page_10_volume_down(ui);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    page_10_volume_up(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    LOGI("page10 back -> page_3\r\n");
    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
}

const ui_page_nav_ops_t page_10_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};
#endif

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
    lv_obj_set_size(bk_ui->page_10, 360, 390);
    lv_obj_set_style_bg_color(bk_ui->page_10, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_10, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_label_title = lv_label_create(bk_ui->page_10);
    lv_label_set_text(bk_ui->page_10_label_title, "音量");
    page_10_style_text(bk_ui->page_10_label_title, &lv_font_ali_16);
    lv_obj_set_pos(bk_ui->page_10_label_title, 24, 24);

    bk_ui->page_10_icon_speaker = lv_obj_create(bk_ui->page_10);
    lv_obj_remove_style_all(bk_ui->page_10_icon_speaker);
    lv_obj_set_pos(bk_ui->page_10_icon_speaker, 132, 64);
    lv_obj_set_size(bk_ui->page_10_icon_speaker, 96, 64);

    bk_ui->page_10_icon_body = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_body);
    lv_obj_set_pos(bk_ui->page_10_icon_body, 6, 24);
    lv_obj_set_size(bk_ui->page_10_icon_body, 18, 20);
    lv_obj_set_style_bg_color(bk_ui->page_10_icon_body, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_icon_body, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_10_icon_body, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_cone = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_cone);
    lv_obj_set_pos(bk_ui->page_10_icon_cone, 23, 18);
    lv_obj_set_size(bk_ui->page_10_icon_cone, 18, 32);
    lv_obj_set_style_bg_color(bk_ui->page_10_icon_cone, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_icon_cone, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_10_icon_cone, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_wave_1 = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_wave_1);
    lv_obj_set_pos(bk_ui->page_10_icon_wave_1, 51, 30);
    lv_obj_set_size(bk_ui->page_10_icon_wave_1, 6, 16);
    lv_obj_set_style_radius(bk_ui->page_10_icon_wave_1, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_wave_2 = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_wave_2);
    lv_obj_set_pos(bk_ui->page_10_icon_wave_2, 63, 24);
    lv_obj_set_size(bk_ui->page_10_icon_wave_2, 6, 28);
    lv_obj_set_style_radius(bk_ui->page_10_icon_wave_2, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_wave_3 = lv_obj_create(bk_ui->page_10_icon_speaker);
    lv_obj_remove_style_all(bk_ui->page_10_icon_wave_3);
    lv_obj_set_pos(bk_ui->page_10_icon_wave_3, 75, 18);
    lv_obj_set_size(bk_ui->page_10_icon_wave_3, 6, 40);
    lv_obj_set_style_radius(bk_ui->page_10_icon_wave_3, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_10_icon_slash = lv_label_create(bk_ui->page_10_icon_speaker);
    lv_label_set_text(bk_ui->page_10_icon_slash, "X");
    page_10_style_text(bk_ui->page_10_icon_slash, &lv_font_ali_30);
    lv_obj_set_style_text_color(bk_ui->page_10_icon_slash, lv_color_hex(0xff4d4f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(bk_ui->page_10_icon_slash, 57, 15);

    bk_ui->page_10_label_value = lv_label_create(bk_ui->page_10);
    lv_label_set_text(bk_ui->page_10_label_value, "0 / 10");
    page_10_style_text(bk_ui->page_10_label_value, &lv_font_ali_25);
    lv_obj_set_width(bk_ui->page_10_label_value, 160);
    lv_obj_set_style_text_align(bk_ui->page_10_label_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(bk_ui->page_10_label_value, 100, 140);

    bk_ui->page_10_slider_volume = lv_slider_create(bk_ui->page_10);
    lv_obj_set_size(bk_ui->page_10_slider_volume, 250, 18);
    lv_obj_set_pos(bk_ui->page_10_slider_volume, 55, 185);
    lv_obj_set_style_bg_color(bk_ui->page_10_slider_volume, lv_color_hex(0x3a3a3a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_slider_volume, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_10_slider_volume, lv_color_hex(0x49a9ff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_slider_volume, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_10_slider_volume, lv_color_hex(0xffffff), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_10_slider_volume, 255, LV_PART_KNOB | LV_STATE_DEFAULT);

    bk_ui->page_10_button_minus = lv_btn_create(bk_ui->page_10);
    page_10_style_button(bk_ui->page_10_button_minus);
    lv_obj_set_pos(bk_ui->page_10_button_minus, 76, 245);
    lv_obj_set_size(bk_ui->page_10_button_minus, 80, 46);
    bk_ui->page_10_button_minus_label = lv_label_create(bk_ui->page_10_button_minus);
    lv_label_set_text(bk_ui->page_10_button_minus_label, "-");
    page_10_style_text(bk_ui->page_10_button_minus_label, &lv_font_ali_25);
    lv_obj_align(bk_ui->page_10_button_minus_label, LV_ALIGN_CENTER, 0, 0);

    bk_ui->page_10_button_plus = lv_btn_create(bk_ui->page_10);
    page_10_style_button(bk_ui->page_10_button_plus);
    lv_obj_set_pos(bk_ui->page_10_button_plus, 204, 245);
    lv_obj_set_size(bk_ui->page_10_button_plus, 80, 46);
    bk_ui->page_10_button_plus_label = lv_label_create(bk_ui->page_10_button_plus);
    lv_label_set_text(bk_ui->page_10_button_plus_label, "+");
    page_10_style_text(bk_ui->page_10_button_plus_label, &lv_font_ali_25);
    lv_obj_align(bk_ui->page_10_button_plus_label, LV_ALIGN_CENTER, 0, 0);

#ifdef ROBOT_TEST
    lv_obj_add_event_cb(bk_ui->page_10_slider_volume, page_10_slider_event_cb, LV_EVENT_VALUE_CHANGED, bk_ui);
    lv_obj_add_event_cb(bk_ui->page_10_button_minus, page_10_minus_event_cb, LV_EVENT_CLICKED, bk_ui);
    lv_obj_add_event_cb(bk_ui->page_10_button_plus, page_10_plus_event_cb, LV_EVENT_CLICKED, bk_ui);
    page_10_refresh_volume(bk_ui);
    (void)ui_nav_register_screen(bk_ui->page_10, &page_10_nav_ops);
#endif

    lv_obj_update_layout(bk_ui->page_10);
}

void destroy_page_page_10(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

#ifdef ROBOT_TEST
    ui_nav_unregister_screen(bk_ui->page_10);
#endif

    if (bk_ui->page_10 != NULL) {
        lv_obj_del(bk_ui->page_10);
        bk_ui->page_10 = NULL;
    }
}
