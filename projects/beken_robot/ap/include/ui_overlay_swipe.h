#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ui_overlay_swipe_back_cb_t)(void *arg);
typedef void (*ui_overlay_tap_cb_t)(void *arg);

/* Configure the raw TP -> screen coordinate transform to match LVGL's
 * display rotation. `rotation` uses the SDK ROTATE_* enum values. */
void ui_overlay_swipe_set_display_transform(int raw_w, int raw_h, int rotation);

/* Start/stop raw-TP right-swipe detection for demos that pause LVGL and
 * therefore cannot receive LV_EVENT_GESTURE. The callback runs on the
 * internal swipe worker thread, so it should only trigger async teardown. */
int ui_overlay_swipe_back_start(ui_overlay_swipe_back_cb_t cb, void *arg);
void ui_overlay_swipe_back_stop(void);

/* Optional single-tap detection sharing the same raw-TP worker. A short
 * press+release with little movement (i.e. not a swipe) invokes `cb`. Unlike
 * the right-swipe (which tears the overlay down), a tap keeps the worker
 * running so it can be triggered repeatedly. Register AFTER
 * ui_overlay_swipe_back_start(); pass cb=NULL to disable. The callback runs on
 * the worker thread, so it should only kick off short/async work. */
void ui_overlay_swipe_set_tap_cb(ui_overlay_tap_cb_t cb, void *arg);

#ifdef __cplusplus
}
#endif
