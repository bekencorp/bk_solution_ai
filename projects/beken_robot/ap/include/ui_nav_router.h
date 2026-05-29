/**
 * @file ui_nav_router.h
 * @brief Dispatch ui_nav_event_t to the nav ops registered for the
 *        currently active LVGL screen (multi-page capable).
 */
#pragma once

#include "ui_nav_events.h"
#include <common/bk_err.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the router registry. Called once per process
 *        (e.g. from beken_ui_init).
 */
void ui_nav_router_init(void);

/**
 * @brief Bind a screen root object to its nav ops. Call after the
 *        page has been created.
 *
 * Re-registering the same screen updates the ops in place. Thread
 * safe (internal mutex).
 *
 * @param screen Typically a `bk_lv_ui_t::page_N` (compared against
 *               lv_screen_active()).
 * @param ops    Non-NULL; individual callbacks may be NULL.
 * @return BK_OK on success / BK_ERR_NO_MEM if the table is full.
 */
bk_err_t ui_nav_register_screen(lv_obj_t *screen, const ui_page_nav_ops_t *ops);

/**
 * @brief Unbind a screen (call from destroy_page_N to avoid dangling
 *        pointers).
 */
void ui_nav_unregister_screen(lv_obj_t *screen);

/**
 * @brief Post a navigation event. Looks up the active screen and
 *        invokes its registered ops while holding
 *        lv_vendor_disp_lock.
 *
 * Safe to call directly from the key thread; LVGL access is
 * serialized through the vendor mutex.
 */
void ui_nav_dispatch_event(ui_nav_event_t ev);

/**
 * @brief Register the `nav` CLI command (test helper) that simulates
 *        key-driven navigation events and supports direct page jumps
 *        for headless verification. Safe to call multiple times.
 */
void ui_nav_router_cli_init(void);

#ifdef __cplusplus
}
#endif
