/**
 * @file ui_touch_gesture.c
 * @brief Shared LVGL touch gesture helpers.
 */
#include "ui_touch_gesture.h"

#include "ui_nav_router.h"

static bool s_edge_press_valid;
static lv_point_t s_edge_press_point;
static uint32_t s_last_edge_swipe_ms;
static ui_touch_edge_swipe_cb_t s_edge_swipe_cb;
static void *s_edge_swipe_user_data;

static int abs_i(int value)
{
    return value < 0 ? -value : value;
}

void ui_touch_tap_reset(ui_touch_tap_state_t *state)
{
    if (state == NULL) {
        return;
    }
    state->tracking = false;
    state->press_point.x = 0;
    state->press_point.y = 0;
}

void ui_touch_tap_press(lv_event_t *e, ui_touch_tap_state_t *state)
{
    if (state == NULL) {
        return;
    }

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        indev = lv_indev_active();
    }
    if (indev == NULL) {
        ui_touch_tap_reset(state);
        return;
    }

    state->tracking = true;
    lv_indev_get_point(indev, &state->press_point);
}

bool ui_touch_tap_release(lv_event_t *e, ui_touch_tap_state_t *state,
                          int move_limit)
{
    if (state == NULL) {
        return false;
    }

    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        indev = lv_indev_active();
    }
    if (indev == NULL) {
        ui_touch_tap_reset(state);
        return false;
    }

    /* If LVGL scrolled any container during this press, treat the release as
     * the end of a swipe rather than a tap. This catches fast "flicks" whose
     * raw finger displacement stays under move_limit but still trigger a
     * (momentum) scroll. At LV_EVENT_RELEASED time the indev scroll_obj is not
     * cleared yet, which is exactly how LVGL itself suppresses LV_EVENT_CLICKED
     * after a scroll. */
    if (lv_indev_get_scroll_obj(indev) != NULL) {
        ui_touch_tap_reset(state);
        return false;
    }

    lv_point_t release_point;
    lv_indev_get_point(indev, &release_point);
    bool accepted = state->tracking &&
                    abs_i(release_point.x - state->press_point.x) <= move_limit &&
                    abs_i(release_point.y - state->press_point.y) <= move_limit;
    ui_touch_tap_reset(state);
    return accepted;
}

void ui_touch_tap_cancel(lv_event_t *e, ui_touch_tap_state_t *state)
{
    (void)e;
    ui_touch_tap_reset(state);
}

static void nav_back_async(void *user_data)
{
    (void)user_data;
    ui_nav_dispatch_event_from_lvgl(UI_NAV_EVENT_SCREEN_PREV);
}

static void nav_back_cb(void *user_data)
{
    (void)user_data;
    /* The gesture cb runs inside lv_event_send() on an object of the page
     * tree; switching page here would delete that tree (and the event list
     * being iterated) before the send returns. Defer like bt_music does. */
    if (lv_async_call(nav_back_async, NULL) != LV_RESULT_OK) {
        ui_nav_dispatch_event_from_lvgl(UI_NAV_EVENT_SCREEN_PREV);
    }
}

static void edge_press_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        indev = lv_indev_active();
    }
    if (indev == NULL) {
        s_edge_press_valid = false;
        return;
    }
    s_edge_press_valid = true;
    lv_indev_get_point(indev, &s_edge_press_point);
}

static void edge_gesture_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_event_get_indev(e);
    if (indev == NULL) {
        indev = lv_indev_active();
    }

    if (indev == NULL || lv_indev_get_gesture_dir(indev) != LV_DIR_RIGHT) {
        return;
    }

    uint32_t now = lv_tick_get();
    if (!s_edge_press_valid ||
        s_edge_press_point.x > UI_TOUCH_EDGE_START_X_DEFAULT) {
        s_edge_press_valid = false;
        return;
    }
    if (s_last_edge_swipe_ms != 0 &&
        now - s_last_edge_swipe_ms < UI_TOUCH_EDGE_SWIPE_DEBOUNCE_MS) {
        s_edge_press_valid = false;
        return;
    }

    s_edge_press_valid = false;
    s_last_edge_swipe_ms = now;
    if (s_edge_swipe_cb != NULL) {
        s_edge_swipe_cb(s_edge_swipe_user_data);
    }
}

static void attach_edge_swipe_recursive(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }

    if (!lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_add_event_cb(obj, edge_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(obj, edge_gesture_cb, LV_EVENT_GESTURE, NULL);

    uint32_t child_count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_count; i++) {
        attach_edge_swipe_recursive(lv_obj_get_child(obj, i));
    }
}

void ui_touch_attach_edge_swipe(lv_obj_t *screen,
                                ui_touch_edge_swipe_cb_t cb,
                                void *user_data)
{
    if (screen == NULL || cb == NULL) {
        return;
    }

    s_edge_swipe_cb = cb;
    s_edge_swipe_user_data = user_data;

    /* The screen root must not be scrollable, otherwise LVGL consumes a
     * horizontal drag as an (elastic) scroll and never emits LV_EVENT_GESTURE,
     * which suppresses the edge swipe-back. Inner scroll panels are left
     * untouched so list menus keep scrolling vertically. */
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    attach_edge_swipe_recursive(screen);
}

void ui_touch_attach_nav_back_edge_swipe(lv_obj_t *screen)
{
    ui_touch_attach_edge_swipe(screen, nav_back_cb, NULL);
}
