#pragma once

#include <common/bk_include.h>
#include "robot_ctrl_service_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t robot_ctrl_service_init(void);
bk_err_t robot_ctrl_service_deinit(void);
bk_err_t robot_ctrl_service_start(void);
bk_err_t robot_ctrl_service_stop(void);
bk_err_t robot_ctrl_service_stop_all_runtime(void);
bk_err_t robot_ctrl_service_send_hello(void);
bk_err_t robot_ctrl_service_handle_wakeup(void);
bk_err_t robot_ctrl_service_notify(const char *method, const char *params_json);
bool robot_ctrl_service_is_authed(void);
bool robot_ctrl_service_is_connected(void);
bool robot_ctrl_service_is_configured(void);
bool robot_ctrl_service_is_video_on(void);
void robot_ctrl_service_refresh_activity(void);

#ifdef __cplusplus
}
#endif
