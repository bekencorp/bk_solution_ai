/**
 * @file robot_video.h
 * @brief Backend API for the page_11 robot video demo (wraps
 *        robot_ctrl_service). UI hooks live in
 *        beken_generated/page_robot_video/.
 */
#ifndef __BK_DEMO_ROBOT_VIDEO_H__
#define __BK_DEMO_ROBOT_VIDEO_H__

#include <stdbool.h>

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int robot_video_init(void);
int robot_video_start(void);
int robot_video_stop(void);

/** Start the robot_ctrl_service. */
int  robot_video_start_service(void);
/** Stop the robot_ctrl_service. */
int  robot_video_stop_service(void);
/** @return true when the video link is already connected/configured. */
bool robot_video_is_connected(void);

/**
 * @brief UI sink for the "video connected" notification. The page
 *        hook in beken_generated/page_robot_video registers a function
 *        that updates the status label under the LVGL lock.
 */
void robot_video_register_connected_sink(void (*sink)(void));

/**
 * @brief Called by robot_ctrl_service when the link comes up. Forwards
 *        to the UI hook (no-op if the page is not active).
 *
 * Historically declared in beken_ui.h; the symbol is kept to keep
 * existing call sites (e.g. robot_ctrl_service) compiling.
 */
void page_11_set_video_connected(void);

extern const bk_demo_iface_t g_demo_robot_video;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_ROBOT_VIDEO_H__ */
