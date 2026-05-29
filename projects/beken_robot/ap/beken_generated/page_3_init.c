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
#include "palm_detection.h"
#include "camera_preview.h"
#include "lv_vendor.h"
// custom page code
#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "components/log.h"
#include "audio_engine.h"
#include "board_usb_switch.h"

#define TAG "page3"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define PAGE3_MENU_COUNT 10

static int s_page3_menu_idx;

/* Focus / navigation order is the visual reading order (top -> bottom,
 * left -> right), NOT the underlying page_3_button_N declaration order.
 * The LVGL designer assigned button_N names by creation history, so the
 * indices below intentionally don't match N.
 *
 * Current layout on page_3 (button N inside each cell):
 *
 *     col1 (x=28)      col2 (x=145)     col3 (x=258)
 *  y=51   btn_1            btn_2           btn_3
 *         AI对话          视觉识别        命令词识别
 *  y=111  btn_7            btn_5           btn_6
 *         音乐播放         音量设置        声源定位
 *  y=171  btn_8            btn_9           btn_4
 *         手掌跟随        摄像头预览       U盘
 *  y=231  btn_10           reserved        reserved
 *         图传播放         (hidden)        (hidden)
 *
 * btn_4 used to be "人脸跟踪" (face tracking). The face-tracking pipeline
 * was never wired up, so the slot is now repurposed as the U-disk (USB
 * MSC) entry -- pressing it flips the Type-C mux to BK7259 USB and the
 * PC enumerates the on-board SD-NAND as a removable disk.
 *
 * btn_10 is the robot video playback entry ported from the older btn_9
 * slot. btn_11/12 remain hidden placeholders and are not included in
 * PAGE3_MENU_COUNT.
 *
 * If a button is moved on screen, update both this table AND the
 * matching case in on_screen_next() so the action stays in sync.
 */
static lv_obj_t *page_3_menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_3_button_1;
    case 1: return ui->page_3_button_2;
    case 2: return ui->page_3_button_3;
    case 3: return ui->page_3_button_7;
    case 4: return ui->page_3_button_5;
    case 5: return ui->page_3_button_6;
    case 6: return ui->page_3_button_8;
    case 7: return ui->page_3_button_9;  /* camera preview */
    case 8: return ui->page_3_button_4;  /* U-disk (USB MSC) */
    case 9: return ui->page_3_button_10; /* robot video playback */
    default: return NULL;
    }
}

/*
 * Highlight the currently focused button.
 *
 * We intentionally do NOT use LV_STATE_DISABLED for focus anymore: the
 * LVGL pointer indev silently drops PRESSED/CLICKED on disabled widgets,
 * which would make TP taps on the already-focused button a no-op. With
 * the per-button click callbacks below, we want every visible button to
 * remain enabled and respond to taps, so focus is shown by a background
 * recolor instead. 0xc0c0c0 matches the Designer LV_STATE_DISABLED grey
 * the previous build used, keeping the look identical.
 */
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
        lv_obj_remove_state(b, LV_STATE_DISABLED);
        uint32_t color = (i == s_page3_menu_idx) ? 0xc0c0c0 : 0x2d75b9;
        lv_obj_set_style_bg_color(b, lv_color_hex(color),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    /* In preview: FOCUS_PREV -> take photo (RUNNING only). */
    if (camera_preview_is_active()) {
        if (camera_preview_is_running()) {
            LOGI("Camera preview: take photo\r\n");
            (void)camera_preview_take_photo();
        }
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
    /* In preview: FOCUS_NEXT -> resume live (FROZEN only). */
    if (camera_preview_is_active()) {
        if (camera_preview_is_frozen()) {
            LOGI("Camera preview: resume live\r\n");
            (void)camera_preview_resume_live();
        }
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
    /* In preview: SCREEN_PREV -> exit preview (worker teardown, 16KB stack). */
    if (camera_preview_is_running() || camera_preview_is_frozen()) {
        LOGI("Exit camera preview\r\n");
        (void)camera_preview_stop();
        return;
    }
    /* STARTING/STOPPING: ignore back until stable. */
    if (camera_preview_is_active()) {
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
    /* Block menu enter while preview is active (avoid re-entry). */
    if (camera_preview_is_active()) {
        return;
    }
    LOGI("page3 enter idx=%d\r\n", s_page3_menu_idx);
    /* Case indices follow the visual top->bottom, left->right order from
     * page_3_menu_btn() above. Keep both tables in sync. */
    switch (s_page3_menu_idx) {
    case 0: /* btn_1: AI chat */
        LOGI("AI chat -> page_6\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_6,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_6);
        break;
    case 1: /* btn_2: vision */
        LOGI("Vision recognition -> page_7\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_7,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_7);
        break;
    case 2: /* btn_3: speech */
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
    case 3: /* btn_7: music */
        LOGI("Music -> page_9\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_9,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_9);
        break;
    case 4: /* btn_5: volume */
        LOGI("Volume settings -> page_10\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_10,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_10);
        break;
    case 5: /* btn_6: DOA */
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
    case 6: /* btn_8: palm tracking */
        if (!palm_detection_can_start()) {
            LOGI("Palm tracking busy (start/exit in progress), ignore\r\n");
            break;
        }
        LOGI("Palm tracking\r\n");
        lv_vendor_stop();
        palm_detection_start();
        break;
    case 7: /* btn_9: camera preview (display only, no NN) */
        LOGI("Camera preview\r\n");
        /* Trigger only; heavy work runs in camera_preview_start worker (16KB). */
        if (camera_preview_start() != 0) {
            LOGE("camera_preview_start trigger failed\r\n");
        }
        break;
    case 8: /* btn_4: U-disk (USB MSC).
             *
             * Switch the Type-C mux to BK7259 USB and bring up MSC so the
             * PC enumerates the on-board SD-NAND as a removable U-disk.
             *
             * NOTE: this also disconnects the CH340 UART log path from
             * the Type-C connector -- the user has to reset / power-cycle
             * the board to fall back to UART mode (see board_usb_switch
             * .c warnings). Since the call is best-effort and one-way,
             * we don't try to navigate to a new screen here. */
        LOGI("U-disk mode -> route Type-C to BK7259 USB + MSC up\r\n");
        if (board_usb_switch_to_usb() != BK_OK) {
            LOGE("board_usb_switch_to_usb failed\r\n");
        }
        break;
    case 9: /* btn_10: robot video playback */
        LOGI("Robot video playback -> page_11\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_11,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_11);
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

/*
 * TP click adapter: tapping a menu button = "select this idx + confirm".
 * Runs on the LVGL task with the vendor display mutex already held, so
 * we call the page-local helpers directly. We also honor the same
 * camera-preview guard on_screen_next() uses: while the preview is
 * active, menu re-entry is blocked, but focus tracking still updates so
 * the user sees the highlight follow their finger when they come back.
 *
 * Index 9 here is btn_10 (= "图传播放" / robot video playback). TP taps
 * and keypad ENTER share the same on_screen_next() action table.
 */
static void page_3_button_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= PAGE3_MENU_COUNT) {
        return;
    }
    if (camera_preview_is_active()) {
        return;
    }
    s_page3_menu_idx = idx;
    page_3_apply_menu_focus(&bk_lv_tool_ui);
    on_screen_next(&bk_lv_tool_ui);
}

static void page_3_register_button_clicks(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE3_MENU_COUNT; i++) {
        lv_obj_t *b = page_3_menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_add_event_cb(b, page_3_button_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
}

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
    lv_obj_set_x(bk_ui->page_3_button_1, 28);
    lv_obj_set_y(bk_ui->page_3_button_1, 51);
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
    lv_obj_set_x(bk_ui->page_3_button_2, 145);
    lv_obj_set_y(bk_ui->page_3_button_2, 51);
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
    lv_obj_set_x(bk_ui->page_3_button_3, 258);
    lv_obj_set_y(bk_ui->page_3_button_3, 51);
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

    /* btn_4 was originally "人脸跟踪" (face tracking) in the LVGL Designer
     * export, but the face-tracking pipeline is not implemented on this
     * board. The slot is now reused as the U-disk (USB MSC) entry. It
     * stays in the third row with the other functional buttons; the fourth
     * row below is used for robot video playback plus hidden placeholders.
     * See page_3_menu_btn() for index mapping. */
    bk_ui->page_3_button_4 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_4_label = lv_label_create(bk_ui->page_3_button_4);
    lv_label_set_text(bk_ui->page_3_button_4_label, "U盘");
    lv_label_set_long_mode(bk_ui->page_3_button_4_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_4_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_4, 258);
    lv_obj_set_y(bk_ui->page_3_button_4, 171);
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
    lv_obj_set_x(bk_ui->page_3_button_5, 147);
    lv_obj_set_y(bk_ui->page_3_button_5, 111);
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
    lv_obj_set_x(bk_ui->page_3_button_6, 262);
    lv_obj_set_y(bk_ui->page_3_button_6, 111);
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
    lv_obj_set_x(bk_ui->page_3_button_7, 28);
    lv_obj_set_y(bk_ui->page_3_button_7, 111);
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

    /* page_3_button_9: camera preview (manual slot 145,171; merge if Designer regen). */
    bk_ui->page_3_button_9 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_9_label = lv_label_create(bk_ui->page_3_button_9);
    lv_label_set_text(bk_ui->page_3_button_9_label, "摄像头预览");
    lv_label_set_long_mode(bk_ui->page_3_button_9_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_9_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_9, 145);
    lv_obj_set_y(bk_ui->page_3_button_9, 171);
    lv_obj_set_width(bk_ui->page_3_button_9, 100);
    lv_obj_set_height(bk_ui->page_3_button_9, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_9, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_9, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_9, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_9, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_9, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_9, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_9, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_9, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_9, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_9, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_9, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_9, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_3_button_8 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_8_label = lv_label_create(bk_ui->page_3_button_8);
    lv_label_set_text(bk_ui->page_3_button_8_label, "手掌跟随");
    lv_label_set_long_mode(bk_ui->page_3_button_8_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_8_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_8, 28);
    lv_obj_set_y(bk_ui->page_3_button_8, 171);
    lv_obj_set_width(bk_ui->page_3_button_8, 100);
    lv_obj_set_height(bk_ui->page_3_button_8, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_8, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_8, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_8, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_8, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_8, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_8, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_8, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_8, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_8, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_8, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_8, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    bk_ui->page_3_button_10 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_10_label = lv_label_create(bk_ui->page_3_button_10);
    lv_label_set_text(bk_ui->page_3_button_10_label, "图传播放");
    lv_label_set_long_mode(bk_ui->page_3_button_10_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_10_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_10, 28);
    lv_obj_set_y(bk_ui->page_3_button_10, 231);
    lv_obj_set_width(bk_ui->page_3_button_10, 100);
    lv_obj_set_height(bk_ui->page_3_button_10, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_10, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_10, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_10, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_10, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_10, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_10, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_10, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_10, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_10, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_10, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_10, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_10, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

#if 0
    bk_ui->page_3_button_11 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_11_label = lv_label_create(bk_ui->page_3_button_11);
    lv_label_set_text(bk_ui->page_3_button_11_label, "null");
    lv_label_set_long_mode(bk_ui->page_3_button_11_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_11_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_11, 145);
    lv_obj_set_y(bk_ui->page_3_button_11, 231);
    lv_obj_set_width(bk_ui->page_3_button_11, 100);
    lv_obj_set_height(bk_ui->page_3_button_11, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_11, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_11, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_11, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_11, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_11, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_11, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_11, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_11, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_11, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_11, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_11, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_11, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_11, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_11, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_11, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_3_button_12 = lv_btn_create(bk_ui->page_3);
    bk_ui->page_3_button_12_label = lv_label_create(bk_ui->page_3_button_12);
    lv_label_set_text(bk_ui->page_3_button_12_label, "null");
    lv_label_set_long_mode(bk_ui->page_3_button_12_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_3_button_12_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_3_button_12, 258);
    lv_obj_set_y(bk_ui->page_3_button_12, 231);
    lv_obj_set_width(bk_ui->page_3_button_12, 100);
    lv_obj_set_height(bk_ui->page_3_button_12, 40);
    lv_obj_set_style_bg_color(bk_ui->page_3_button_12, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_3_button_12, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_3_button_12, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_3_button_12, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_3_button_12, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_3_button_12, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_3_button_12, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_3_button_12, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_3_button_12, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_3_button_12, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_3_button_12, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_3_button_12, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_3_button_12, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_3_button_12, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_3_button_12, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_3_button_12, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_3_button_12, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_3_button_12, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_3_button_12, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_3_button_12, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
#else
    bk_ui->page_3_button_11 = NULL;
    bk_ui->page_3_button_11_label = NULL;
    bk_ui->page_3_button_12 = NULL;
    bk_ui->page_3_button_12_label = NULL;
#endif

    // custom code implementation
        #ifdef ROBOT_TEST
            s_page3_menu_idx = 0;
            page_3_apply_menu_focus(bk_ui);
            page_3_register_button_clicks(bk_ui);
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
