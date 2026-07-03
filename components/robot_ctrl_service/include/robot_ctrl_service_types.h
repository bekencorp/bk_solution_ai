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

#define ROBOT_AUDIO_CONFIG_VALUE_LEN 16

typedef struct {
    bool aec;
    char mic_type[ROBOT_AUDIO_CONFIG_VALUE_LEN];
    uint32_t record_sample_rate;
    char record_fmt[ROBOT_AUDIO_CONFIG_VALUE_LEN];
    uint32_t play_sample_rate;
    char play_fmt[ROBOT_AUDIO_CONFIG_VALUE_LEN];
    char spk_type[ROBOT_AUDIO_CONFIG_VALUE_LEN];
    bool asr;
} robot_audio_config_t;

typedef struct {
    robot_lan_transport_mode_t mode;
    robot_video_config_t video;
    robot_audio_config_t audio;
    uint16_t motion_speed;
} robot_solution_config_t;

#ifdef __cplusplus
}
#endif
