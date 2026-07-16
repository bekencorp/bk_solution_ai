/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 *
 * For permission requests, write to BekenCorp at armino_support@bekencorp.com.
 */

/**
 * @file page_hooks.h
 * @brief Decoupling point between UI pages and business logic.
 *
 * `beken_generated/page_<feature>/page_<feature>_init.c` files only
 * create and destroy LVGL widgets; the actual business logic
 * (nav_ops registration, button click callbacks, backend start/stop,
 * debug CLI binding, ...) is attached via the hook mechanism declared
 * here. The page UI sibling file `page_<feature>_hooks.c` is the
 * typical caller; backend demos in src/demo/ stay LVGL-free.
 *
 * Each page supports multiple init / destroy hooks (fired in
 * registration order), which lets a "primary UI hook + status icon
 * bridge" coexist on the same page (e.g. page_top + wifi_status_ui).
 *
 * Trigger order:
 *   init_page_page_N()    tail (widgets laid out)  -> bk_page_fire_init
 *   destroy_page_page_N() head (before lv_obj_del) -> bk_page_fire_destroy
 *
 * Calling thread: same as init/destroy_page_page_N() -- typically
 * holding lv_vendor_disp_lock.
 *
 * Page ids are 1-based to match `bk_lv_ui_t::page_N`, range
 * [BK_PAGE_ID_MIN, BK_PAGE_ID_MAX].
 */

#ifndef __BK_PAGE_HOOKS_H__
#define __BK_PAGE_HOOKS_H__

#include "beken_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BK_PAGE_ID_MIN  1
#define BK_PAGE_ID_MAX  11

/** Maximum number of hooks that can be registered per page id
 *  (independent slots for init and destroy). */
#define BK_PAGE_HOOK_MAX_PER_PAGE  4

typedef void (*bk_page_init_hook_t)(bk_lv_ui_t *ui);
typedef void (*bk_page_destroy_hook_t)(bk_lv_ui_t *ui);

/**
 * @brief Append a hook for the given page id.
 *
 * - Re-registering the same function pointer is idempotent.
 * - Returns false once BK_PAGE_HOOK_MAX_PER_PAGE slots are full.
 * - Returns true on success.
 */
bool bk_page_set_init_hook(int page_id, bk_page_init_hook_t hook);
bool bk_page_set_destroy_hook(int page_id, bk_page_destroy_hook_t hook);

/**
 * @brief Invoked from the end of init_page_page_N() / start of
 *        destroy_page_page_N() to fire every registered hook in the
 *        order they were registered.
 */
void bk_page_fire_init(int page_id, bk_lv_ui_t *ui);
void bk_page_fire_destroy(int page_id, bk_lv_ui_t *ui);

/**
 * @brief Attach a right-swipe -> UI_NAV_EVENT_SCREEN_PREV handler to
 *        any screen root (including dynamically created pages).
 */
void bk_page_attach_right_swipe_gesture(lv_obj_t *screen);

/**
 * @brief Release the page we navigated away from, to avoid accumulating
 *        resident pages as the user drills deeper into the UI.
 *
 * Destroys `prev` via its destroy_page_page_N() (full teardown: UI EXIT
 * hook + nav/service cleanup + lv_obj_del) when `prev` is a disposable
 * generated page. No-op when `prev` is NULL, equal to `next`, or a
 * non-generated (dynamic) screen that owns its own lifetime. MUST be called
 * AFTER `next` has been loaded as the active screen (deleting the active
 * screen is illegal in LVGL).
 */
void bk_page_release_prev_screen(lv_obj_t *prev, lv_obj_t *next);

/**
 * @brief Register all per-page UI hooks under beken_generated/page_*.
 *
 * Each page subdirectory provides a page_<feature>_init_hooks() function
 * that wires its on_page_init / on_page_destroy callbacks for the matching
 * page id. This function calls all of them in a fixed order and must run
 * exactly once during startup, AFTER wifi_status_ui_init() (so that the
 * status-icon hook lands first) and BEFORE the first navigation event
 * that materializes a page.
 */
void bk_pages_init_all_hooks(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK_PAGE_HOOKS_H__ */
