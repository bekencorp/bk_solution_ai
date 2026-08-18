/**
 * @file page_splash_hooks.c
 * @brief page_1 splash screen: BK7259 logo + title, tap anywhere to enter
 *        the top-level menu.
 *
 * Pure UI: registers nav ops + a click adapter on the page root. No
 * backend demo state is involved, so there is no matching src/demo file.
 */
#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include <stdint.h>

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "ui_touch_gesture.h"

static void on_screen_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    navigate_to_screen((lv_obj_t **)&ui->page_2,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_2);
}

static const ui_page_nav_ops_t page_1_nav_ops = {
    .on_focus_prev  = NULL,
    .on_focus_next  = NULL,
    .on_screen_prev = NULL,
    .on_screen_next = on_screen_next,
};

static ui_touch_tap_state_t s_tap_state;

/*
 * TP click adapter: page_1 is a welcome screen with only a logo + title.
 * Any tap is treated as "confirm" and enters the top-level menu. The
 * listener is installed on the page root and root clickable is enabled
 * explicitly so the indev does not forward the event past us.
 */
static void screen_press_cb(lv_event_t *e)
{
    ui_touch_tap_press(e, &s_tap_state);
}

static void screen_release_cb(lv_event_t *e)
{
    if (!ui_touch_tap_release(e, &s_tap_state,
                              UI_TOUCH_TAP_MOVE_LIMIT_DEFAULT)) {
        return;
    }
    on_screen_next(&bk_lv_tool_ui);
}

static void screen_press_lost_cb(lv_event_t *e)
{
    ui_touch_tap_cancel(e, &s_tap_state);
}

static void attach_tap_to_enter(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj, screen_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, screen_release_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(obj, screen_press_lost_cb, LV_EVENT_PRESS_LOST, NULL);

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        attach_tap_to_enter(lv_obj_get_child(obj, i));
    }
}

static void splash_on_page_init(bk_lv_ui_t *ui)
{
    ui_touch_tap_reset(&s_tap_state);
    attach_tap_to_enter(ui->page_1);
    (void)ui_nav_register_screen(ui->page_1, &page_1_nav_ops);
}

static void splash_on_page_destroy(bk_lv_ui_t *ui)
{
    ui_nav_unregister_screen(ui->page_1);
}

void page_splash_init_hooks(void)
{
    (void)bk_page_set_init_hook(1, splash_on_page_init);
    (void)bk_page_set_destroy_hook(1, splash_on_page_destroy);
}

int page_splash_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_1,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_1);
    return 0;
}

#else  /* !ROBOT_TEST */

void page_splash_init_hooks(void) {}
int  page_splash_enter(void)     { return 0; }

#endif /* ROBOT_TEST */
