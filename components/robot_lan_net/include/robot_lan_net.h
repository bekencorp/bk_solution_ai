#pragma once

#include <common/bk_include.h>
#include "robot_lan_net_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t robot_lan_net_init(void);
bk_err_t robot_lan_net_deinit(void);
bk_err_t robot_lan_net_start_discovery(void);
bk_err_t robot_lan_net_stop_discovery(void);
bk_err_t robot_lan_net_stop_all(void);

bk_err_t robot_lan_net_set_event_callback(robot_lan_event_cb_t cb, void *user_data);
bk_err_t robot_lan_net_connect_app_servers(const robot_lan_app_info_t *app);
bk_err_t robot_lan_net_reconnect_from_saved_context(void);
bool robot_lan_net_is_connected(void);
const robot_lan_app_info_t *robot_lan_net_get_app_info(void);

bk_err_t robot_lan_net_save_session_context(const robot_lan_app_info_t *app);
robot_lan_transport_mode_t robot_lan_net_parse_mode(const char *mode);
const char *robot_lan_net_mode_to_string(robot_lan_transport_mode_t mode);

#ifdef __cplusplus
}
#endif
