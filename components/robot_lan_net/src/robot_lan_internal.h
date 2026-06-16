#pragma once

#include "robot_lan_net.h"
#include <os/os.h>

typedef struct {
    bool initialized;
    bool connected;
    bool ctrl_connected;
    bool video_connected;
    bool audio_connected;
    bool discovery_running;
    beken_thread_t discovery_thread;
    robot_lan_app_info_t app;
    robot_lan_event_cb_t event_cb;
    void *event_user_data;
} robot_lan_ctx_t;

robot_lan_ctx_t *robot_lan_get_ctx_internal(void);
void robot_lan_emit_event_internal(robot_lan_event_t event, const char *cmd, size_t cmd_len);
