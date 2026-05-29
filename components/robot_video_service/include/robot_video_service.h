#pragma once

#include <common/bk_include.h>
#include "robot_video_service_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t robot_video_service_init(void);
bk_err_t robot_video_service_deinit(void);
bk_err_t robot_video_service_start(const robot_video_config_t *cfg);
bk_err_t robot_video_service_stop(void);
bk_err_t robot_video_service_request_keyframe(void);
bk_err_t robot_video_service_set_bitrate(uint16_t bitrate_kbps);
bool robot_video_service_is_running(void);
bk_err_t robot_video_service_get_status(robot_video_status_t *status);
void robot_video_service_default_config(robot_video_config_t *cfg);

#ifdef __cplusplus
}
#endif
