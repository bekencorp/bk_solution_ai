/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 */

#include "page_hooks.h"
#include <stddef.h>
#include <stdbool.h>

#include "lvgl.h"
#include "ui_nav_router.h"

#define BK_PAGE_SLOT_COUNT (BK_PAGE_ID_MAX + 1)

static bk_page_init_hook_t
    s_init_hooks[BK_PAGE_SLOT_COUNT][BK_PAGE_HOOK_MAX_PER_PAGE];
static bk_page_destroy_hook_t
    s_destroy_hooks[BK_PAGE_SLOT_COUNT][BK_PAGE_HOOK_MAX_PER_PAGE];

static inline bool is_valid_page_id(int page_id)
{
    return page_id >= BK_PAGE_ID_MIN && page_id <= BK_PAGE_ID_MAX;
}

#define BK_PAGE_RIGHT_SWIPE_DEBOUNCE_MS 500U

static uint32_t s_last_right_swipe_ms;

static lv_obj_t *bk_page_screen_by_id(int page_id, bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return NULL;
    }

    switch (page_id) {
    case 1:  return ui->page_1;
    case 2:  return ui->page_2;
    case 3:  return ui->page_3;
    case 4:  return ui->page_4;
    case 5:  return ui->page_5;
    case 6:  return ui->page_6;
    case 7:  return ui->page_7;
    case 8:  return ui->page_8;
    case 9:  return ui->page_9;
    case 10: return ui->page_10;
    case 11: return ui->page_11;
    default: return NULL;
    }
}

static void bk_page_global_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        indev = lv_indev_active();
    }

    if (indev != NULL && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
        uint32_t now = lv_tick_get();
        if (s_last_right_swipe_ms != 0 &&
            now - s_last_right_swipe_ms < BK_PAGE_RIGHT_SWIPE_DEBOUNCE_MS) {
            return;
        }
        s_last_right_swipe_ms = now;
        ui_nav_dispatch_event_from_lvgl(UI_NAV_EVENT_SCREEN_PREV);
    }
}

void bk_page_attach_right_swipe_gesture(lv_obj_t *screen)
{
    if (screen == NULL) {
        return;
    }

    lv_obj_add_event_cb(screen, bk_page_global_gesture_cb, LV_EVENT_GESTURE, NULL);
}

static void bk_page_attach_global_gesture(int page_id, bk_lv_ui_t *ui)
{
    if (page_id == 1) {
        return;
    }

    lv_obj_t *screen = bk_page_screen_by_id(page_id, ui);
    bk_page_attach_right_swipe_gesture(screen);
}

bool bk_page_set_init_hook(int page_id, bk_page_init_hook_t hook)
{
    if (!is_valid_page_id(page_id) || hook == NULL) {
        return false;
    }
    bk_page_init_hook_t *row = s_init_hooks[page_id];
    int empty = -1;
    for (int i = 0; i < BK_PAGE_HOOK_MAX_PER_PAGE; i++) {
        if (row[i] == hook) {
            return true;  /* idempotent re-registration */
        }
        if (empty < 0 && row[i] == NULL) {
            empty = i;
        }
    }
    if (empty < 0) {
        return false;
    }
    row[empty] = hook;
    return true;
}

bool bk_page_set_destroy_hook(int page_id, bk_page_destroy_hook_t hook)
{
    if (!is_valid_page_id(page_id) || hook == NULL) {
        return false;
    }
    bk_page_destroy_hook_t *row = s_destroy_hooks[page_id];
    int empty = -1;
    for (int i = 0; i < BK_PAGE_HOOK_MAX_PER_PAGE; i++) {
        if (row[i] == hook) {
            return true;
        }
        if (empty < 0 && row[i] == NULL) {
            empty = i;
        }
    }
    if (empty < 0) {
        return false;
    }
    row[empty] = hook;
    return true;
}

void bk_page_fire_init(int page_id, bk_lv_ui_t *ui)
{
    if (!is_valid_page_id(page_id) || ui == NULL) {
        return;
    }
    bk_page_init_hook_t *row = s_init_hooks[page_id];
    for (int i = 0; i < BK_PAGE_HOOK_MAX_PER_PAGE; i++) {
        if (row[i] != NULL) {
            row[i](ui);
        }
    }
    bk_page_attach_global_gesture(page_id, ui);
}

void bk_page_fire_destroy(int page_id, bk_lv_ui_t *ui)
{
    if (!is_valid_page_id(page_id) || ui == NULL) {
        return;
    }
    bk_page_destroy_hook_t *row = s_destroy_hooks[page_id];
    for (int i = 0; i < BK_PAGE_HOOK_MAX_PER_PAGE; i++) {
        if (row[i] != NULL) {
            row[i](ui);
        }
    }
}

/* Forward declarations -- each beken_generated/page_<feature>/page_<feature>_hooks.c
 * defines exactly one of these. */
void page_splash_init_hooks(void);
void page_top_init_hooks(void);
void page_demo_menu_init_hooks(void);
void page_provisioning_init_hooks(void);
void page_doa_init_hooks(void);
void page_ai_chat_init_hooks(void);
void page_vision_init_hooks(void);
void page_asr_init_hooks(void);
void page_music_init_hooks(void);
void page_volume_init_hooks(void);
void page_robot_video_init_hooks(void);

void bk_pages_init_all_hooks(void)
{
    page_splash_init_hooks();
    page_top_init_hooks();
    page_demo_menu_init_hooks();
    page_provisioning_init_hooks();
    page_doa_init_hooks();
    page_ai_chat_init_hooks();
    page_vision_init_hooks();
    page_asr_init_hooks();
    page_music_init_hooks();
    page_volume_init_hooks();
    page_robot_video_init_hooks();
}
