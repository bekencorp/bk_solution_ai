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
#include "lv_vendor.h"
#include "components/log.h"
#if CONFIG_BK_ROBOT_CTRL_SERVICE
#include "robot_ctrl_service.h"
#endif

#define TAG "page11"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

static lv_timer_t *s_page11_start_timer;

static void page_11_start_service_cb(lv_timer_t *timer)
{
    (void)timer;
    s_page11_start_timer = NULL;
#if CONFIG_BK_ROBOT_CTRL_SERVICE
    LOGI("start robot video connection service\r\n");
    if (robot_ctrl_service_start() != BK_OK) {
        LOGI("robot ctrl service start failed\r\n");
    }
#else
    LOGI("robot ctrl service is disabled\r\n");
#endif
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

    LOGI("page11 back -> page_3\r\n");
#if CONFIG_BK_ROBOT_CTRL_SERVICE
    robot_ctrl_service_stop();
#endif
    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
}

const ui_page_nav_ops_t page_11_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

void page_11_set_video_connected(void)
{
    lv_vendor_disp_lock();
    if (bk_lv_tool_ui.page_11_label_state != NULL
        && lv_obj_is_valid(bk_lv_tool_ui.page_11_label_state)) {
        lv_label_set_text(bk_lv_tool_ui.page_11_label_state, "video connected");
    }
    lv_vendor_disp_unlock();
}
#endif

void init_page_page_11(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_11 != NULL && lv_obj_is_valid(bk_ui->page_11)) {
        destroy_page_page_11(bk_ui);
    }

    bk_ui->page_11 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_11, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_11, 360, 390);
    lv_obj_set_style_bg_color(bk_ui->page_11, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_11, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_11_label_title = NULL;
    bk_ui->page_11_spinner = NULL;

    bk_ui->page_11_label_state = lv_label_create(bk_ui->page_11);
#if defined(ROBOT_TEST) && CONFIG_BK_ROBOT_CTRL_SERVICE
    if (robot_ctrl_service_is_connected() || robot_ctrl_service_is_configured()) {
        lv_label_set_text(bk_ui->page_11_label_state, "video connected");
    } else
#endif
    lv_label_set_text(bk_ui->page_11_label_state, "video connecting...");
    lv_obj_set_style_text_color(bk_ui->page_11_label_state, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_11_label_state, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(bk_ui->page_11_label_state, LV_ALIGN_CENTER, 0, 0);

#ifdef ROBOT_TEST
    if (s_page11_start_timer != NULL) {
        lv_timer_del(s_page11_start_timer);
        s_page11_start_timer = NULL;
    }
    s_page11_start_timer = lv_timer_create(page_11_start_service_cb, 80, NULL);
    lv_timer_set_repeat_count(s_page11_start_timer, 1);
    (void)ui_nav_register_screen(bk_ui->page_11, &page_11_nav_ops);
#endif

    lv_obj_update_layout(bk_ui->page_11);
}

void destroy_page_page_11(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

#ifdef ROBOT_TEST
    ui_nav_unregister_screen(bk_ui->page_11);
    if (s_page11_start_timer != NULL) {
        lv_timer_del(s_page11_start_timer);
        s_page11_start_timer = NULL;
    }
#endif

    if (bk_ui->page_11 != NULL) {
        lv_obj_del(bk_ui->page_11);
        bk_ui->page_11 = NULL;
    }

    bk_ui->page_11_label_title = NULL;
    bk_ui->page_11_label_state = NULL;
    bk_ui->page_11_spinner = NULL;
}
