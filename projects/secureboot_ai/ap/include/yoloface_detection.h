#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Query whether the yoloface-detection pipeline is currently active.
 *
 * Returns true after a successful start and stays true until stop/exit
 * finishes (including while the async exit worker is running).
 */
bool yoloface_detection_is_active(void);

/**
 * @brief Stop yoloface detection and return the UI to the caller page.
 *
 * Resumes LVGL (suspended at entry), tears down camera/GPU/model, and
 * navigates back to the edge-AI menu when launched from there, else page_3.
 * Safe to call from the key thread or overlay swipe callback.
 */
int yoloface_detection_exit_to_menu(void);

#ifdef __cplusplus
}
#endif
