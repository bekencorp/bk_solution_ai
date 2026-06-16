#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_overlay_swipe_back_cb_t)(void *arg);

/* Configure the raw TP -> screen coordinate transform to match LVGL's
 * display rotation. `rotation` uses the SDK ROTATE_* enum values. */
void ui_overlay_swipe_set_display_transform(int raw_w, int raw_h, int rotation);

/* Start/stop raw-TP right-swipe detection for demos that pause LVGL and
 * therefore cannot receive LV_EVENT_GESTURE. The callback runs on the
 * internal swipe worker thread, so it should only trigger async teardown. */
int ui_overlay_swipe_back_start(ui_overlay_swipe_back_cb_t cb, void *arg);
void ui_overlay_swipe_back_stop(void);

#ifdef __cplusplus
}
#endif
