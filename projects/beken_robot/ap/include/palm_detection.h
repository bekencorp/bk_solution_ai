#pragma once

#include <avdk_check.h>
#include <common/avdk_pixel_types.h>
#include <components/bk_display_bus.h>
#include <components/bk_lcd_panel.h>
#include <os/os.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int palm_detection_start();

/**
 * @brief Query whether the palm-tracking pipeline is currently active.
 *
 * Returns true after a successful palm_detection_start() and stays true
 * until palm_detection_stop()/palm_detection_exit_to_menu() finishes.
 *
 * Safe to call from any task (e.g. the key thread, before deciding how
 * to interpret a double-press).
 */
bool palm_detection_is_active(void);

/**
 * @brief Query whether palm tracking can be started right now.
 *
 * Returns false while the pipeline is already busy:
 *   - a previous palm_detection_start() is still running its start task;
 *   - the pipeline is up;
 *   - an exit task from the previous session is still tearing things down.
 *
 * Used by the page_3 "palm tracking" handler to debounce double-presses
 * and to avoid calling lv_vendor_stop() when start would just no-op.
 */
bool palm_detection_can_start(void);

/**
 * @brief Tear down the palm-tracking pipeline (worker, camera, GPU bond,
 *        servos, model) without touching LVGL.
 *
 * Use this when the caller wants to free the resources but stay on the
 * current (non-LVGL) screen. For "exit back to the LVGL menu" callers
 * should prefer palm_detection_exit_to_menu().
 *
 * Idempotent: returns 0 immediately if no pipeline is running.
 */
int palm_detection_stop(void);

/**
 * @brief Stop palm tracking and return the UI to page_3.
 *
 * This is the counterpart of the page_3 -> palm entry path: it stops the
 * pipeline, re-starts LVGL (which was suspended by lv_vendor_stop() at
 * entry) and loads page_3. Intended as the handler for "double-press S4
 * while palm tracking is active".
 */
int palm_detection_exit_to_menu(void);


#ifdef __cplusplus
}
#endif
