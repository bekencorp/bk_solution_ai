/**
 * @file yoloface_tracking.h
 * @brief Face-detection overlay demo (yoloface NN pipeline + full-screen
 *        camera preview). Display-only: it draws the detected face boxes on
 *        the OSD but, unlike palm_tracking, does NOT drive any servo.
 *
 * Note: start() calls lv_vendor_stop() to pause LVGL; stop() goes
 * through yoloface_detection_exit_to_menu() which resumes LVGL and
 * navigates back (to the edge-AI page when launched from there, else
 * page_3 by default).
 */
#ifndef __BK_DEMO_YOLOFACE_TRACKING_H__
#define __BK_DEMO_YOLOFACE_TRACKING_H__

#include <stdbool.h>

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int yoloface_tracking_init(void);
int yoloface_tracking_start(void);
int yoloface_tracking_stop(void);
void yoloface_tracking_set_return_to_edge_ai(bool enable);

extern const bk_demo_iface_t g_demo_yoloface_tracking;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_YOLOFACE_TRACKING_H__ */
