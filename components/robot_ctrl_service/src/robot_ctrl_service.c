#include "robot_ctrl_internal.h"

#include <os/mem.h>
#include "audio_engine.h"
#include "robot_video_service.h"

robot_ctrl_ctx_t s_ctrl;

static void robot_ctrl_handle_lan_event(const robot_lan_event_msg_t *msg, void *user_data)
{
    (void)user_data;

    if (!msg) {
        return;
    }

    switch (msg->event) {
    case ROBOT_LAN_EVT_APP_CONNECTED:
        LOGI("ROBOT_LAN_EVT_APP_CONNECTED\r\n");
        s_ctrl.state = ROBOT_CTRL_STATE_CONNECTED;
        s_ctrl.authed = false;
        robot_ctrl_notify_video_connected();
        robot_ctrl_service_send_hello();
        break;

    case ROBOT_LAN_EVT_CMD_RX:
        LOGI("robot ctrl service: CMD RX\r\n");
        if (msg->cmd) {
            LOGI("robot ctrl service: CMD RX: %s\r\n", msg->cmd);
            robot_ctrl_handle_cmd(msg->cmd, msg->cmd_len);
        }
        break;

    case ROBOT_LAN_EVT_APP_DISCONNECTED:
        LOGI("ROBOT_LAN_EVT_APP_DISCONNECTED\r\n");
        robot_ctrl_service_stop_all_runtime();
        robot_ctrl_notify_video_disconnected();
        if (s_ctrl.started) {
            robot_lan_net_start_discovery();
        }
        break;

    default:
        break;
    }
}

void robot_ctrl_service_refresh_activity(void)
{
    s_ctrl.last_activity_ms = rtos_get_time();
    if (!s_ctrl.video_on && !s_ctrl.audio_on) {
        robot_ctrl_reload_idle_timer();
    }
}

bk_err_t robot_ctrl_service_init(void)
{
    if (s_ctrl.initialized) {
        return BK_OK;
    }

    os_memset(&s_ctrl, 0, sizeof(s_ctrl));
    robot_ctrl_default_solution(&s_ctrl.solution);
    s_ctrl.state = ROBOT_CTRL_STATE_DISCONNECTED;

    BK_LOG_ON_ERR(robot_lan_net_set_event_callback(robot_ctrl_handle_lan_event, NULL));
    robot_ctrl_keepalive_get_wakeup_env();
    s_ctrl.initialized = true;
    LOGI("robot ctrl initialized\n");
    robot_ctrl_handle_boot_wakeup_reason();
    return BK_OK;
}

bk_err_t robot_ctrl_service_deinit(void)
{
    robot_ctrl_service_stop();
    os_memset(&s_ctrl, 0, sizeof(s_ctrl));
    return BK_OK;
}

bk_err_t robot_ctrl_service_start(void)
{
    if (!s_ctrl.initialized) {
        BK_LOG_ON_ERR(robot_ctrl_service_init());
    }
    s_ctrl.started = true;
    return robot_lan_net_start_discovery();
}

bk_err_t robot_ctrl_service_stop(void)
{
    s_ctrl.started = false;
    return robot_ctrl_service_stop_all_runtime();
}

bk_err_t robot_ctrl_service_stop_all_runtime(void)
{
    robot_ctrl_motion_stop_now();
    if (s_ctrl.video_on) {
        robot_video_service_stop();
    }
    if (s_ctrl.audio_on) {
        audio_engine_stop();
    }
    s_ctrl.video_on = false;
    s_ctrl.audio_on = false;
    s_ctrl.authed = false;
    s_ctrl.configured = false;
    s_ctrl.ap_keepalive = false;
    s_ctrl.state = ROBOT_CTRL_STATE_DISCONNECTED;
    robot_ctrl_keepalive_stop_cp();
    robot_lan_net_stop_all();
    return BK_OK;
}

bool robot_ctrl_service_is_authed(void)
{
    return s_ctrl.authed;
}

bool robot_ctrl_service_is_connected(void)
{
    return robot_lan_net_is_connected()
           || s_ctrl.state == ROBOT_CTRL_STATE_CONNECTED
           || s_ctrl.state == ROBOT_CTRL_STATE_AUTHED
           || s_ctrl.state == ROBOT_CTRL_STATE_CONFIGURED
           || s_ctrl.state == ROBOT_CTRL_STATE_STREAMING
           || s_ctrl.state == ROBOT_CTRL_STATE_AUDIO_TALKING;
}

bool robot_ctrl_service_is_configured(void)
{
    return s_ctrl.configured;
}

bool robot_ctrl_service_is_video_on(void)
{
    return s_ctrl.video_on;
}
