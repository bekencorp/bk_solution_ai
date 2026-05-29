/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 */

#include "page_hooks.h"
#include <stddef.h>
#include <stdbool.h>

#define BK_PAGE_SLOT_COUNT (BK_PAGE_ID_MAX + 1)

static bk_page_init_hook_t
    s_init_hooks[BK_PAGE_SLOT_COUNT][BK_PAGE_HOOK_MAX_PER_PAGE];
static bk_page_destroy_hook_t
    s_destroy_hooks[BK_PAGE_SLOT_COUNT][BK_PAGE_HOOK_MAX_PER_PAGE];

static inline bool is_valid_page_id(int page_id)
{
    return page_id >= BK_PAGE_ID_MIN && page_id <= BK_PAGE_ID_MAX;
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
