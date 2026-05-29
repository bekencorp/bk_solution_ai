#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "robot_video_service_types.h"
#include "robot_lan_net_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROBOT_CTRL_STATE_DISCONNECTED = 0,
    ROBOT_CTRL_STATE_CONNECTED,
    ROBOT_CTRL_STATE_AUTHED,
    ROBOT_CTRL_STATE_CONFIGURED,
    ROBOT_CTRL_STATE_AP_KEEPALIVE,
    ROBOT_CTRL_STATE_STREAMING,
    ROBOT_CTRL_STATE_AUDIO_TALKING,
} robot_ctrl_state_t;

typedef struct {
    robot_lan_transport_mode_t mode;
    robot_video_config_t video;
    uint16_t motion_speed;
} robot_solution_config_t;

#ifdef __cplusplus
}
#endif
