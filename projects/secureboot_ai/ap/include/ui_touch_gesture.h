/**
 * @file ui_touch_gesture.h
 * @brief Shared touch gesture helpers for LVGL pages.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_TOUCH_TAP_MOVE_LIMIT_DEFAULT       14
#define UI_TOUCH_EDGE_START_X_DEFAULT         42
#define UI_TOUCH_EDGE_SWIPE_DEBOUNCE_MS       500U

typedef struct {
    bool tracking;
    lv_point_t press_point;
} ui_touch_tap_state_t;

void ui_touch_tap_reset(ui_touch_tap_state_t *state);
void ui_touch_tap_press(lv_event_t *e, ui_touch_tap_state_t *state);
bool ui_touch_tap_release(lv_event_t *e, ui_touch_tap_state_t *state,
                          int move_limit);
void ui_touch_tap_cancel(lv_event_t *e, ui_touch_tap_state_t *state);

typedef void (*ui_touch_edge_swipe_cb_t)(void *user_data);

void ui_touch_attach_edge_swipe(lv_obj_t *screen,
                                ui_touch_edge_swipe_cb_t cb,
                                void *user_data);
void ui_touch_attach_nav_back_edge_swipe(lv_obj_t *screen);

#ifdef __cplusplus
}
#endif
