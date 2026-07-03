/**
 * @file car_tracking.h
 * @brief Car-following demo (face_detection NN pipeline + full-screen
 *        camera preview + Hiwonder chassis tracking).
 *
 * Note: start() calls lv_vendor_stop() to pause LVGL; stop() goes
 * through car_detection_exit_to_menu() which resumes LVGL and
 * navigates back (to the edge-AI page when launched from there, else
 * page_3 by default).
 */
#ifndef __BK_DEMO_CAR_TRACKING_H__
#define __BK_DEMO_CAR_TRACKING_H__

#include <stdbool.h>

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int car_tracking_init(void);
int car_tracking_start(void);
int car_tracking_stop(void);
void car_tracking_set_return_to_edge_ai(bool enable);
bool car_detection_is_active(void);
int car_detection_exit_to_menu(void);

extern const bk_demo_iface_t g_demo_car_tracking;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_CAR_TRACKING_H__ */
