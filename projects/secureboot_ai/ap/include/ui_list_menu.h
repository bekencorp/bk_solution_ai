/**
 * @file ui_list_menu.h
 * @brief Reusable, data-driven vertical "long-bar" list menu component.
 *
 * The component renders a full-screen menu made of full-width rounded bars
 * (one menu entry per bar). Three bars are visible per screen and the list
 * scrolls vertically when the entries overflow, which satisfies the design
 * requirement that overflowing menus must page up/down by touch.
 *
 * It is the single building block shared by the demo-center category screen,
 * every demo sub-menu and the device-settings screen, so adding or
 * re-grouping menu entries only means editing a data table (see demo_catalog).
 *
 * Two construction modes are provided:
 *   - ui_list_menu_attach(): render onto an existing screen object (used by
 *     generated pages that must keep a stable bk_lv_ui_t::page_N identity).
 *   - ui_list_menu_create(): allocate a brand new dynamic full-screen menu
 *     (used by sub-menus and device settings). The previously created dynamic
 *     menu is destroyed first, giving a simple single-instance screen stack.
 *
 * A small "return menu" indirection (ui_demo_set_return_menu /
 * ui_demo_return_to_menu) lets a demo page go back to whichever menu launched
 * it instead of hard-coding a parent page, so the same demo can be reached
 * from different sub-menus and still return to the right place.
 */
#ifndef __UI_LIST_MENU_H__
#define __UI_LIST_MENU_H__

#include "lvgl.h"
#include "beken_ui.h"
#include "ui_theme.h"
#include "ui_nav_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of entries a single list menu may hold. */
#define UI_LIST_MENU_MAX_ITEMS 12

/** Called when an entry is activated (touch click or SCREEN_NEXT key). */
typedef void (*ui_list_menu_select_fn_t)(int index, void *user_data);

/** Menu "enter" function: builds and shows a menu screen, returns 0 on ok. */
typedef int (*ui_menu_enter_fn_t)(void);

/**
 * Optional navigation interceptor for overlay demos (e.g. camera preview)
 * that keep a menu screen active underneath while taking over the panel.
 * Return true if the event was consumed (the list menu then ignores it).
 */
typedef bool (*ui_list_menu_nav_intercept_fn_t)(ui_nav_event_t ev, void *user_data);

typedef struct {
    const char *title;                 /**< Header title; NULL hides it. */
    const char *subtitle;              /**< Status line under title; may be NULL. */
    const char *const *items;          /**< Entry label array. */
    const char *const *descriptions;   /**< Optional right-side entry hints. */
    const ui_theme_icon_kind_t *icons; /**< Optional semantic entry icons. */
    int item_count;                    /**< Entry count (<= UI_LIST_MENU_MAX_ITEMS). */
    int initial_focus;                 /**< Entry that starts focused (blue bar);
                                        *   defaults to 0. Use it to point the
                                        *   highlight at the active choice. */
    const char *const *tabs;           /**< Optional category pills above the list. */
    int tab_count;                     /**< Optional category pill count. */
    int active_tab;                    /**< Active category pill index. */
    ui_list_menu_select_fn_t on_select;/**< Entry activation callback. */
    ui_menu_enter_fn_t on_back;        /**< Back/swipe-right handler; may be NULL. */
    ui_list_menu_nav_intercept_fn_t on_nav_intercept; /**< Optional; may be NULL. */
    void *user_data;                   /**< Opaque pointer forwarded to callbacks. */
} ui_list_menu_config_t;

/**
 * @brief Render a list menu onto an existing screen object.
 *
 * The caller owns @p screen (its size/background should already be set).
 * Registers nav ops on @p screen. Pair with ui_list_menu_detach() in the
 * owning page's destroy hook.
 */
void ui_list_menu_attach(lv_obj_t *screen, const ui_list_menu_config_t *cfg);

/**
 * @brief Release the per-screen list-menu context bound by
 *        ui_list_menu_attach() and unregister its nav ops.
 */
void ui_list_menu_detach(lv_obj_t *screen);

/**
 * @brief Create and load a brand new full-screen list menu.
 *
 * Any previously created dynamic list menu is destroyed first.
 * @return the created screen object, or NULL on failure.
 */
lv_obj_t *ui_list_menu_create(const ui_list_menu_config_t *cfg);

/**
 * @brief Destroy the current dynamic list-menu screen (if any).
 */
void ui_list_menu_destroy_current(void);

/* ------------------------------------------------------------------ */
/* Demo "return to launching menu" indirection.                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Remember which menu should be shown when the demo about to be
 *        launched is exited. Call right before starting a demo.
 */
void ui_demo_set_return_menu(ui_menu_enter_fn_t enter_fn);

/**
 * @brief Navigate back to the menu remembered by ui_demo_set_return_menu().
 *        Falls back to the demo center when none was set.
 *
 * A demo page's on_screen_prev should call this instead of hard-coding a
 * parent page.
 * @return 0 on success.
 */
int ui_demo_return_to_menu(void);

#ifdef __cplusplus
}
#endif

#endif /* __UI_LIST_MENU_H__ */
