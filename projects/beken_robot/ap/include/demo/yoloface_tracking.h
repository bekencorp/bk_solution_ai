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
#include <stdint.h>

#include "demo/demo_iface.h"

#define YOLOFACE_ARCHIVE_MAX_ITEMS 8

typedef struct {
    uint32_t profile_id;
    uint32_t feature_count;
    uint32_t ppm_count;
    uint32_t last_sample;
} yoloface_archive_item_t;

typedef struct {
    uint32_t profile_count;
    uint32_t total_features;
    uint32_t total_ppm;
    uint32_t incomplete_count;
    yoloface_archive_item_t items[YOLOFACE_ARCHIVE_MAX_ITEMS];
} yoloface_archive_info_t;

#ifdef __cplusplus
extern "C" {
#endif

int yoloface_tracking_init(void);
int yoloface_tracking_start(void);
int yoloface_tracking_stop(void);
void yoloface_tracking_set_return_to_edge_ai(bool enable);
void yoloface_tracking_set_lvgl_camera_blend(bool enable);
int yoloface_solution_enroll_request(void);
int yoloface_solution_enroll_cancel(void);
bool yoloface_solution_enroll_is_active(void);
int yoloface_solution_verify_request(void);
int yoloface_solution_archive_enter_request(void);
int yoloface_solution_archive_query(yoloface_archive_info_t *info);
int yoloface_solution_archive_delete(uint32_t profile_id);
int yoloface_solution_archive_clear_all(void);
bool yoloface_detection_is_active(void);
int yoloface_detection_exit_to_menu(void);

extern const bk_demo_iface_t g_demo_yoloface_tracking;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_YOLOFACE_TRACKING_H__ */
