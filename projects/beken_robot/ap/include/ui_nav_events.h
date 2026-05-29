/**
 * @file ui_nav_events.h
 * @brief Navigation-semantics events passed from physical keys to LVGL
 *        pages.
 *
 * The key layer only emits ui_nav_event_t; each page implements the
 * actual behavior via ui_page_nav_ops_t.
 */
#pragma once

#include <stdint.h>

#include "beken_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Navigation-semantics events. The physical-key mapping is
 *        configured in ui_key_bridge.c and may be customized.
 *
 * | Enum value              | Suggested meaning            | Typical page-side use                          |
 * |-------------------------|------------------------------|------------------------------------------------|
 * | UI_NAV_EVENT_FOCUS_PREV | Previous focus / left        | Move highlight left in lists / grids           |
 * | UI_NAV_EVENT_FOCUS_NEXT | Next focus / right           | Move highlight right in lists / grids          |
 * | UI_NAV_EVENT_SCREEN_PREV| Previous screen / back       | lv_screen_load previous page, or sub-page back |
 * | UI_NAV_EVENT_SCREEN_NEXT| Next screen / forward        | lv_screen_load next page, or sub-page forward  |
 */
typedef enum {
    UI_NAV_EVENT_FOCUS_PREV = 0,
    UI_NAV_EVENT_FOCUS_NEXT,
    UI_NAV_EVENT_SCREEN_PREV,
    UI_NAV_EVENT_SCREEN_NEXT,
    UI_NAV_EVENT_CONFIRM_LONG,
    UI_NAV_EVENT_COUNT
} ui_nav_event_t;

/**
 * @brief Per-page navigation handler set. Each callback may be NULL
 *        to indicate "ignore this event on this page".
 */
typedef struct ui_page_nav_ops {
    void (*on_focus_prev)(bk_lv_ui_t *ui);
    void (*on_focus_next)(bk_lv_ui_t *ui);
    void (*on_screen_prev)(bk_lv_ui_t *ui);
    void (*on_screen_next)(bk_lv_ui_t *ui);
    void (*on_confirm_long)(bk_lv_ui_t *ui);
} ui_page_nav_ops_t;

#ifdef __cplusplus
}
#endif
