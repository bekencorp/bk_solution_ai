/**
 * @file palm_tracking.h
 * @brief Palm-tracking demo (palm_detection NN pipeline + full-screen
 *        camera preview).
 *
 * Note: start() calls lv_vendor_stop() to pause LVGL; stop() goes
 * through palm_detection_exit_to_menu() which resumes LVGL and
 * navigates back to page_3.
 */
#ifndef __BK_DEMO_PALM_TRACKING_H__
#define __BK_DEMO_PALM_TRACKING_H__

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int palm_tracking_init(void);
int palm_tracking_start(void);
int palm_tracking_stop(void);

extern const bk_demo_iface_t g_demo_palm_tracking;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_PALM_TRACKING_H__ */
