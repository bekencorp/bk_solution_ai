#include "robot_ctrl_internal.h"

#include <os/str.h>
#include <modules/wdrv_common.h>

#define ROBOT_IPC_CMD_KEEPALIVE_START        0x0002
#define ROBOT_IPC_CMD_KEEPALIVE_STOP         0x0003
#define ROBOT_IPC_CMD_GET_WAKEUP_ENV_ADDR    0x0004

#define ROBOT_POWERUP_MULTIMEDIA_WAKEUP      2
#define ROBOT_POWERUP_KEEPALIVE_DISCONNECT   3
#define ROBOT_POWERUP_KEEPALIVE_FAIL_WAKEUP  4

typedef struct {
    uint8_t infotype;
    char server[32];
    uint16_t port;
    uint8_t reserved[99];
} robot_ipc_keepalive_cfg_t;

typedef struct {
    uint32_t wakeup_reason;
    beken2_timer_t timer;
    uint8_t delay_action;
    uint32_t delay_arg1;
    uint32_t delay_arg2;
} robot_wakeup_env_t;

#if CONFIG_BK_ROBOT_AP_KEEPALIVE
static robot_wakeup_env_t *s_wakeup_env;
#endif

bk_err_t robot_ctrl_keepalive_get_wakeup_env(void)
{
#if CONFIG_BK_ROBOT_AP_KEEPALIVE
    uint8_t response_buf[sizeof(uint32_t)] = {0};
    uint16_t response_len = 0;

    if (bk_wdrv_customer_transfer_rsp(ROBOT_IPC_CMD_GET_WAKEUP_ENV_ADDR,
                                      NULL,
                                      0,
                                      response_buf,
                                      sizeof(response_buf),
                                      &response_len) != BK_OK
        || response_len < sizeof(uint32_t)) {
        LOGW("get CP wakeup env failed\n");
        return BK_FAIL;
    }

    uint32_t addr = *(uint32_t *)response_buf;
    s_wakeup_env = (robot_wakeup_env_t *)(uintptr_t)addr;
    LOGI("CP wakeup env=%p\n", s_wakeup_env);
#endif
    return BK_OK;
}

static void robot_ctrl_keepalive_clear_wakeup_reason(void)
{
#if CONFIG_BK_ROBOT_AP_KEEPALIVE
    if (s_wakeup_env) {
        s_wakeup_env->wakeup_reason = 0;
    }
#endif
}

static bk_err_t robot_ctrl_keepalive_start_cp(void)
{
#if CONFIG_BK_ROBOT_AP_KEEPALIVE
    robot_ipc_keepalive_cfg_t cfg = {0};
    const robot_lan_app_info_t *app = robot_lan_net_get_app_info();

    if (!app || app->app_ip[0] == '\0' || app->cmd_port == 0) {
        LOGW("skip AP keepalive: no saved App cmd server\n");
        return BK_FAIL;
    }

    if (app->mode != ROBOT_LAN_MODE_TCP) {
        LOGW("skip AP keepalive: CP keepalive currently supports TCP only, mode=%s\n",
             robot_lan_net_mode_to_string(app->mode));
        return BK_FAIL;
    }

    os_snprintf(cfg.server, sizeof(cfg.server), "%s", app->app_ip);
    cfg.port = app->cmd_port;
    LOGI("start CP keepalive %s:%u\n", cfg.server, cfg.port);
    return bk_wdrv_customer_transfer(ROBOT_IPC_CMD_KEEPALIVE_START,
                                     (uint8_t *)&cfg,
                                     sizeof(cfg));
#else
    return BK_FAIL;
#endif
}

bk_err_t robot_ctrl_keepalive_stop_cp(void)
{
#if CONFIG_BK_ROBOT_AP_KEEPALIVE
    robot_ipc_keepalive_cfg_t cfg = {0};
    return bk_wdrv_customer_transfer(ROBOT_IPC_CMD_KEEPALIVE_STOP,
                                     (uint8_t *)&cfg,
                                     sizeof(cfg));
#else
    return BK_OK;
#endif
}

static void robot_ctrl_enter_ap_keepalive(void)
{
#if CONFIG_BK_ROBOT_AP_KEEPALIVE
    if (s_ctrl.video_on || s_ctrl.audio_on || s_ctrl.ap_keepalive) {
        return;
    }

    robot_ctrl_service_notify("robot.notify.power", "{\"state\":\"asleep\",\"reason\":\"idle\"}");
    robot_lan_net_stop_all();

    if (robot_ctrl_keepalive_start_cp() != BK_OK) {
        LOGE("start CP keepalive failed, keep AP alive\n");
        s_ctrl.ap_keepalive = false;
        s_ctrl.state = s_ctrl.authed ? ROBOT_CTRL_STATE_AUTHED : ROBOT_CTRL_STATE_CONFIGURED;
        robot_lan_net_reconnect_from_saved_context();
        return;
    }

    s_ctrl.ap_keepalive = true;
    s_ctrl.state = ROBOT_CTRL_STATE_AP_KEEPALIVE;
    LOGI("CP keepalive started, AP will be powered down by CP\n");
#endif
}

void robot_ctrl_handle_boot_wakeup_reason(void)
{
#if CONFIG_BK_ROBOT_AP_KEEPALIVE
    if (!s_wakeup_env || s_wakeup_env->wakeup_reason == 0) {
        return;
    }

    uint32_t reason = s_wakeup_env->wakeup_reason;
    LOGI("handle AP wakeup reason=%u\n", reason);
    robot_ctrl_keepalive_stop_cp();
    robot_ctrl_keepalive_clear_wakeup_reason();

    switch (reason) {
    case ROBOT_POWERUP_MULTIMEDIA_WAKEUP:
    case ROBOT_POWERUP_KEEPALIVE_DISCONNECT:
    case ROBOT_POWERUP_KEEPALIVE_FAIL_WAKEUP:
        if (robot_lan_net_reconnect_from_saved_context() != BK_OK) {
            LOGW("reconnect App after wake failed, restart discovery\n");
            robot_lan_net_start_discovery();
        }
        break;
    default:
        break;
    }
#endif
}

static void robot_ctrl_idle_timeout(void *arg1, void *arg2)
{
    (void)arg1;
    (void)arg2;

#if CONFIG_BK_ROBOT_AP_KEEPALIVE
    if (!s_ctrl.video_on && !s_ctrl.audio_on) {
        robot_ctrl_enter_ap_keepalive();
    }
#endif
}

void robot_ctrl_reload_idle_timer(void)
{
#if CONFIG_BK_ROBOT_AP_KEEPALIVE
    if (!rtos_is_oneshot_timer_init(&s_ctrl.idle_timer)) {
        if (rtos_init_oneshot_timer(&s_ctrl.idle_timer,
                                    CONFIG_BK_ROBOT_AP_KEEPALIVE_IDLE_MS,
                                    robot_ctrl_idle_timeout,
                                    NULL,
                                    NULL) != BK_OK) {
            return;
        }
        rtos_start_oneshot_timer(&s_ctrl.idle_timer);
    } else {
        rtos_oneshot_reload_timer_ex(&s_ctrl.idle_timer,
                                     CONFIG_BK_ROBOT_AP_KEEPALIVE_IDLE_MS,
                                     robot_ctrl_idle_timeout,
                                     NULL,
                                     NULL);
    }
#endif
}

bk_err_t robot_ctrl_service_handle_wakeup(void)
{
    if (s_ctrl.ap_keepalive) {
        s_ctrl.ap_keepalive = false;
        LOGI("exit logical AP keepalive state\n");
    }

    robot_ctrl_keepalive_stop_cp();
    return robot_lan_net_reconnect_from_saved_context();
}

bk_err_t robot_ctrl_handle_power(const char *method, cJSON *id, cJSON *params)
{
    if (os_strcmp(method, "robot.power.sleepAck") == 0) {
        cJSON *reason = params ? cJSON_GetObjectItem(params, "reason") : NULL;
        LOGI("sleep ack from App, reason=%s\n",
             (reason && cJSON_IsString(reason)) ? reason->valuestring : "");
        if (id) {
            return robot_ctrl_send_result(id, NULL);
        }
        return BK_OK;
    }

    if (os_strcmp(method, "robot.power.wake") == 0) {
        bk_err_t ret = BK_OK;
        cJSON *result = cJSON_CreateObject();

        if (!robot_lan_net_is_connected()) {
            ret = robot_ctrl_service_handle_wakeup();
        } else if (s_ctrl.ap_keepalive) {
            s_ctrl.ap_keepalive = false;
            LOGI("exit logical AP keepalive state\n");
        }
        if (ret != BK_OK && !robot_lan_net_is_connected()) {
            if (result) {
                cJSON_Delete(result);
            }
            return robot_ctrl_send_error(id, -32000, "wake failed");
        }
        if (!result) {
            return BK_FAIL;
        }

        s_ctrl.ap_keepalive = false;
        s_ctrl.state = s_ctrl.authed ? ROBOT_CTRL_STATE_AUTHED : ROBOT_CTRL_STATE_CONNECTED;
        cJSON_AddStringToObject(result, "state", "awake");
        cJSON_AddStringToObject(result, "reason", "wake success");
        return robot_ctrl_send_result(id, result);
    }

    return BK_FAIL;
}
