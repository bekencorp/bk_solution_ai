/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 */

#include "page_hooks.h"
#include <stddef.h>
#include <stdbool.h>

#include "lvgl.h"
#include "ui_touch_gesture.h"
#include <components/log.h>

#define BK_PAGE_HOOK_TAG "ui_page"

#define BK_PAGE_SLOT_COUNT (BK_PAGE_ID_MAX + 1)

/* Human-readable page names for entry/exit tracing. Index == page_id. */
static const char *bk_page_name(int page_id)
{
    switch (page_id) {
    case 1:  return "splash";
    case 2:  return "top";
    case 3:  return "demo_menu";
    case 4:  return "provisioning";
    case 5:  return "doa";
    case 6:  return "ai_chat";
    case 7:  return "vision";
    case 8:  return "asr";
    case 9:  return "music";
    case 10: return "volume";
    case 11: return "robot_video";
    default: return "unknown";
    }
}

static bk_page_init_hook_t
    s_init_hooks[BK_PAGE_SLOT_COUNT][BK_PAGE_HOOK_MAX_PER_PAGE];
static bk_page_destroy_hook_t
    s_destroy_hooks[BK_PAGE_SLOT_COUNT][BK_PAGE_HOOK_MAX_PER_PAGE];

static inline bool is_valid_page_id(int page_id)
{
    return page_id >= BK_PAGE_ID_MIN && page_id <= BK_PAGE_ID_MAX;
}

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

/* Reverse of bk_page_screen_by_id: return the generated page id that owns
 * `screen`, or -1 if `screen` is not a generated page (e.g. a dynamically
 * created list-menu screen, which manages its own lifetime). */
static int bk_page_id_of_screen(lv_obj_t *screen, bk_lv_ui_t *ui)
{
    if (screen == NULL || ui == NULL) {
        return -1;
    }
    for (int id = BK_PAGE_ID_MIN; id <= BK_PAGE_ID_MAX; id++) {
        if (bk_page_screen_by_id(id, ui) == screen) {
            return id;
        }
    }
    return -1;
}

/* Every generated page is released when navigated away from, so drilling
 * deep into the UI does not accumulate resident pages / leak PSRAM. No page
 * is kept resident: per-page focus/scroll state lives in static variables
 * that survive a page rebuild, and all pages are re-created via their
 * init_page_page_N() on the next visit. */
static bool bk_page_is_keep_alive(int page_id)
{
    (void)page_id;
    return false;
}

static void bk_page_destroy_by_id(int page_id, bk_lv_ui_t *ui)
{
    switch (page_id) {
    case 1:  destroy_page_page_1(ui);  break;
    case 2:  destroy_page_page_2(ui);  break;
    case 3:  destroy_page_page_3(ui);  break;
    case 4:  destroy_page_page_4(ui);  break;
    case 5:  destroy_page_page_5(ui);  break;
    case 6:  destroy_page_page_6(ui);  break;
    case 7:  destroy_page_page_7(ui);  break;
    case 8:  destroy_page_page_8(ui);  break;
    case 9:  destroy_page_page_9(ui);  break;
    case 10: destroy_page_page_10(ui); break;
    case 11: destroy_page_page_11(ui); break;
    default: break;
    }
}

void bk_page_release_prev_screen(lv_obj_t *prev, lv_obj_t *next)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    if (prev == NULL || prev == next) {
        return;
    }

    int id = bk_page_id_of_screen(prev, ui);
    if (id < 0 || bk_page_is_keep_alive(id)) {
        return;  /* dynamic screen or a resident hub: leave it alone */
    }

    /* `next` is already the active screen at this point, so `prev` is no
     * longer active and is safe to delete. destroy_page_page_N() fires
     * bk_page_fire_destroy (UI EXIT + nav/service teardown) then lv_obj_del
     * and NULLs the slot; it is NULL-guarded so a page that already tore
     * itself down on back is just a no-op here. */
    BK_LOGI(BK_PAGE_HOOK_TAG, "UI RELEASE page_id=%d (%s) on switch\r\n",
            id, bk_page_name(id));
    bk_page_destroy_by_id(id, ui);
}

void bk_page_attach_right_swipe_gesture(lv_obj_t *screen)
{
    ui_touch_attach_nav_back_edge_swipe(screen);
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
    BK_LOGI(BK_PAGE_HOOK_TAG, "UI ENTER page_id=%d (%s)\r\n",
            page_id, bk_page_name(page_id));
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
    BK_LOGI(BK_PAGE_HOOK_TAG, "UI EXIT  page_id=%d (%s)\r\n",
            page_id, bk_page_name(page_id));
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
