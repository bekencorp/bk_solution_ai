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
#include "audio_engine.h"

#define TAG "page8"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

static lv_timer_t *s_page8_anim_timer;
static lv_obj_t *s_page8_label_state;
static lv_obj_t *s_page8_spinner;
static lv_obj_t *s_page8_label_hint;
static lv_obj_t *s_page8_label_action;
static uint32_t s_page8_recognized_start_ms;
static uint32_t s_page8_spinner_kick_ms;
static bool s_page8_recognized_active;
static char s_page8_action_text[32] = "前进";

static void page_8_apply_listening_view(void)
{
    if (s_page8_label_state) {
        lv_label_set_text(s_page8_label_state, "聆听中...");
    }
    if (s_page8_label_hint) {
        lv_label_set_text(s_page8_label_hint, "你可以说: 前进 后退 向左转 向右转");
        lv_obj_clear_flag(s_page8_label_hint, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_page8_spinner) {
        lv_obj_clear_flag(s_page8_spinner, LV_OBJ_FLAG_HIDDEN);
        /* Workaround for occasional spinner stall on long-running hide/show cycles. */
        lv_spinner_set_anim_params(s_page8_spinner, 1200, 90);
        s_page8_spinner_kick_ms = lv_tick_get();
    }
    if (s_page8_label_action) {
        lv_obj_add_flag(s_page8_label_action, LV_OBJ_FLAG_HIDDEN);
    }
}

static void page_8_apply_recognized_view(void)
{
    if (s_page8_label_state) {
        lv_label_set_text(s_page8_label_state, "已识别");
    }
    if (s_page8_label_hint) {
        lv_obj_add_flag(s_page8_label_hint, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_page8_spinner) {
        lv_obj_add_flag(s_page8_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_page8_label_action) {
        lv_label_set_text(s_page8_label_action, s_page8_action_text);
        lv_obj_clear_flag(s_page8_label_action, LV_OBJ_FLAG_HIDDEN);
    }
}

static void page_8_on_phrase_recognized(const char *phrase)
{
    if (phrase == NULL) {
        return;
    }

    if ((strcmp(phrase, "nihaobaotong") == 0) || (strcmp(phrase, "nihaobotong") == 0)) {
        snprintf(s_page8_action_text, sizeof(s_page8_action_text), "你好博通");
    } else if (strcmp(phrase, "zaijianbotong") == 0) {
        snprintf(s_page8_action_text, sizeof(s_page8_action_text), "再见博通");
    } else {
        return;
    }

    page_8_apply_recognized_view();
    s_page8_recognized_start_ms = lv_tick_get();
    s_page8_recognized_active = true;
}

static void page_8_refresh_text(void)
{
    /* Keep function for periodic refresh compatibility. */
}

static void page_8_anim_timer_cb(lv_timer_t *timer)
{
    char phrase[32] = {0};

    (void)timer;
    ui_asr_demo_anim_step();
    if (ui_asr_demo_consume_phrase_trigger(phrase, sizeof(phrase))) {
        page_8_on_phrase_recognized(phrase);
    }
    if (s_page8_recognized_active && lv_tick_elaps(s_page8_recognized_start_ms) >= 8000) {
        page_8_apply_listening_view();
        s_page8_recognized_active = false;
    }
    if (!s_page8_recognized_active && s_page8_spinner && lv_tick_elaps(s_page8_spinner_kick_ms) >= 3000) {
        lv_spinner_set_anim_params(s_page8_spinner, 1200, 90);
        s_page8_spinner_kick_ms = lv_tick_get();
    }
    page_8_refresh_text();
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    (void)ui;
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    (void)ui;
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    LOGI("page8 back -> page_3\r\n");
    #if (CONFIG_ASR_SERVICE)
    if (AUDIO_ENGINE_SUCCESS != audio_engine_asr_stop()) {
        LOGI("page8 stop asr failed\r\n");
    } else
    {
        LOGI("page8 stop asr\r\n");
    }
    #endif
    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
}

const ui_page_nav_ops_t page_8_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};
#endif

void init_page_page_8(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_8 != NULL && lv_obj_is_valid(bk_ui->page_8)) {
        destroy_page_page_8(bk_ui);
    }

    bk_ui->page_8 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_8, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_8, 360, 390);
    lv_obj_set_style_bg_color(bk_ui->page_8, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_page8_label_state = lv_label_create(bk_ui->page_8);
    lv_obj_set_style_text_color(s_page8_label_state, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_page8_label_state, &lv_font_zh_demo_32, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_page8_label_state, 122, 36);

    s_page8_spinner = lv_spinner_create(bk_ui->page_8);
    lv_spinner_set_anim_params(s_page8_spinner, 1200, 90);
    lv_obj_set_size(s_page8_spinner, 88, 88);
    lv_obj_set_pos(s_page8_spinner, 136, 154);
    lv_obj_set_style_arc_color(s_page8_spinner, lv_color_hex(0x33d6ff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(s_page8_spinner, 8, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_page8_spinner, lv_color_hex(0x1d4ed8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(s_page8_spinner, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_page8_label_hint = lv_label_create(bk_ui->page_8);
    lv_obj_set_width(s_page8_label_hint, 320);
    lv_label_set_long_mode(s_page8_label_hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(s_page8_label_hint, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_page8_label_hint, &lv_font_zh_demo_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_page8_label_hint, 48, 88);

    s_page8_label_action = lv_label_create(bk_ui->page_8);
    lv_obj_set_style_text_color(s_page8_label_action, lv_color_hex(0xff3b30), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_page8_label_action, &lv_font_zh_demo_56, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_page8_label_action, 92, 124);
    lv_obj_add_flag(s_page8_label_action, LV_OBJ_FLAG_HIDDEN);

#ifdef ROBOT_TEST
    ui_asr_demo_reset();
    ui_asr_demo_execute();
    s_page8_recognized_active = false;
    s_page8_recognized_start_ms = 0;
    s_page8_anim_timer = lv_timer_create(page_8_anim_timer_cb, 220, NULL);
    s_page8_spinner_kick_ms = lv_tick_get();
    page_8_apply_listening_view();
    (void)ui_nav_register_screen(bk_ui->page_8, &page_8_nav_ops);
#endif

    lv_obj_update_layout(bk_ui->page_8);
}

void destroy_page_page_8(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

#ifdef ROBOT_TEST
    ui_nav_unregister_screen(bk_ui->page_8);
    if (s_page8_anim_timer != NULL) {
        lv_timer_del(s_page8_anim_timer);
        s_page8_anim_timer = NULL;
    }
#endif

    if (bk_ui->page_8 != NULL) {
        lv_obj_del(bk_ui->page_8);
        bk_ui->page_8 = NULL;
    }

    s_page8_label_state = NULL;
    s_page8_spinner = NULL;
    s_page8_label_hint = NULL;
    s_page8_label_action = NULL;
}
