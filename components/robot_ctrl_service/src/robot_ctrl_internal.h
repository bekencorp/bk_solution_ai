#pragma once

#include "robot_ctrl_service.h"

#include <os/os.h>
#include <components/log.h>
#include "cJSON.h"
#include "robot_lan_net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TAG "robot_ctrl"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#ifndef CONFIG_BK_ROBOT_AP_KEEPALIVE_IDLE_MS
#define CONFIG_BK_ROBOT_AP_KEEPALIVE_IDLE_MS 5000
#endif

typedef struct {
    bool initialized;
    bool started;
    bool authed;
    bool configured;
    bool video_on;
    bool audio_on;
    bool motion_on;
    bool ap_keepalive;
    uint32_t last_activity_ms;
    robot_ctrl_state_t state;
    robot_solution_config_t solution;
    beken2_timer_t motion_timer;
    beken2_timer_t idle_timer;
} robot_ctrl_ctx_t;

extern robot_ctrl_ctx_t s_ctrl;

bk_err_t robot_ctrl_send_json(cJSON *root);
bk_err_t robot_ctrl_send_error(cJSON *id, int code, const char *message);
bk_err_t robot_ctrl_send_result(cJSON *id, cJSON *result);
int robot_ctrl_json_int(cJSON *item, int fallback);

void robot_ctrl_default_solution(robot_solution_config_t *solution);
void robot_ctrl_notify_video_connected(void);
void robot_ctrl_motion_stop_now(void);
void robot_ctrl_reload_idle_timer(void);

bk_err_t robot_ctrl_keepalive_get_wakeup_env(void);
bk_err_t robot_ctrl_keepalive_stop_cp(void);
void robot_ctrl_handle_boot_wakeup_reason(void);

void robot_ctrl_handle_cmd(const char *cmd, size_t cmd_len);
void robot_ctrl_handle_session_response(cJSON *root);
bk_err_t robot_ctrl_handle_power(const char *method, cJSON *id, cJSON *params);
bk_err_t robot_ctrl_handle_solution(const char *method, cJSON *id, cJSON *params);
bk_err_t robot_ctrl_handle_camera(const char *method, cJSON *id, cJSON *params);
bk_err_t robot_ctrl_handle_motion(const char *method, cJSON *id, cJSON *params);
bk_err_t robot_ctrl_handle_audio(const char *method, cJSON *id);

#ifdef __cplusplus
}
#endif
