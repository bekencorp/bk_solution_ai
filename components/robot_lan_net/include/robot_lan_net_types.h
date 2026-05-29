#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <components/media_types.h>
#include <components/bk_voice_service_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROBOT_LAN_UUID_LEN          64
#define ROBOT_LAN_TOKEN_LEN         64
#define ROBOT_LAN_IP_LEN            16
#define ROBOT_LAN_MODE_LEN          8
#define ROBOT_LAN_CMD_MAX_LEN       1024

typedef enum {
    ROBOT_LAN_MODE_TCP = 0,
    ROBOT_LAN_MODE_UDP,
    ROBOT_LAN_MODE_CS2,
} robot_lan_transport_mode_t;

typedef enum {
    ROBOT_LAN_EVT_APP_CONNECTED = 0,
    ROBOT_LAN_EVT_APP_DISCONNECTED,
    ROBOT_LAN_EVT_CMD_RX,
} robot_lan_event_t;

typedef struct {
    char app_ip[ROBOT_LAN_IP_LEN];
    uint16_t cmd_port;
    uint16_t video_port;
    uint16_t audio_up_port;
    uint16_t audio_down_port;
    robot_lan_transport_mode_t mode;
} robot_lan_app_info_t;

typedef struct {
    robot_lan_event_t event;
    const char *cmd;
    size_t cmd_len;
} robot_lan_event_msg_t;

typedef void (*robot_lan_event_cb_t)(const robot_lan_event_msg_t *msg, void *user_data);

#ifdef __cplusplus
}
#endif
