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
#include "page_chat_anim.h"
#include <stdio.h>
#include <string.h>

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "components/log.h"
#include "bk_smart_config.h"

#define TAG "page7"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }

    LOGI("Vision recognition back -> page_3\r\n");
    /* Run the heavy teardown (Agora destroy + video_engine_deinit +
     * MIPI camera close) on a worker thread so the LVGL display lock
     * held by ui_nav_dispatch_event is released within ~50ms instead
     * of the ~250ms-1s the synchronous chain would take. */
    if (bk_sconf_exit_ai_mode_async(1) != BK_OK) {
        LOGW("Vision recognition exit dispatch failed (worker busy?)\r\n");
    }

    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
    destroy_page_page_7(ui);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    LOGI("Vision recognition page active\r\n");
}

const ui_page_nav_ops_t page_7_nav_ops = {
    .on_focus_prev = NULL,
    .on_focus_next = NULL,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

#endif

void init_page_page_7(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_7 != NULL && lv_obj_is_valid(bk_ui->page_7)) {
        destroy_page_page_7(bk_ui);
    }

    bk_ui->page_7 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_7, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_7, 390, 360);
    lv_obj_set_style_bg_color(bk_ui->page_7, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_7, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Vision-mode animation (core + ripples + EQ + viewfinder + scan line + REC).
     * Initial state is CONNECTING; subsequent transitions are driven by
     * app_event AGENT_* / RTC_* / AI_* messages. */
    bk_ui->page_7_label_1 = NULL;
    page_chat_anim_attach(bk_ui->page_7, PAGE_CHAT_ANIM_MODE_VISION);

#ifdef ROBOT_TEST
    (void)ui_nav_register_screen(bk_ui->page_7, &page_7_nav_ops);
    if (bk_sconf_enter_vision_mode() != BK_OK) {
        LOGW("Vision mode start failed\r\n");
    } else {
        LOGI("Vision mode started\r\n");
    }
#endif

    lv_obj_update_layout(bk_ui->page_7);
}

void destroy_page_page_7(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    if (bk_ui->page_7 != NULL) {
#ifdef ROBOT_TEST
        ui_nav_unregister_screen(bk_ui->page_7);
#endif
        page_chat_anim_detach();
        lv_obj_del(bk_ui->page_7);
        bk_ui->page_7 = NULL;
        bk_ui->page_7_label_1 = NULL;
    }
}
