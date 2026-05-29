#include "robot_ctrl_internal.h"

#include <os/str.h>
#if CONFIG_MOTOR
#include "motor.h"
#endif

void robot_ctrl_motion_stop_now(void)
{
#if CONFIG_MOTOR
    motor_close(PWM_MOTOR_CH_3);
#endif
    s_ctrl.motion_on = false;
}

static void robot_ctrl_motion_timeout(void *arg1, void *arg2)
{
    (void)arg1;
    (void)arg2;

    LOGW("motion duration timeout, stop\n");
    robot_ctrl_motion_stop_now();
}

bk_err_t robot_ctrl_handle_motion(const char *method, cJSON *id, cJSON *params)
{
    if (os_strcmp(method, "robot.motion.stop") == 0) {
        robot_ctrl_motion_stop_now();
        return robot_ctrl_send_result(id, NULL);
    }

    if (os_strcmp(method, "robot.motion.getStatus") == 0) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddBoolToObject(result, "moving", s_ctrl.motion_on);
        cJSON_AddNumberToObject(result, "speed", s_ctrl.solution.motion_speed);
        return robot_ctrl_send_result(id, result);
    }

    if (!s_ctrl.video_on) {
        return robot_ctrl_send_error(id, -32002, "motion requires video on");
    }
#if CONFIG_BK_ROBOT_AUDIO_BUSY_DISABLE_MOTION
    if (s_ctrl.audio_on) {
        return robot_ctrl_send_error(id, -32002, "audio busy");
    }
#endif

    if (os_strcmp(method, "robot.motion.move") == 0 || os_strcmp(method, "robot.motion.rotate") == 0) {
        cJSON *dir = params ? cJSON_GetObjectItem(params, "dir") : NULL;
        cJSON *speed = params ? cJSON_GetObjectItem(params, "speed") : NULL;
        bool is_move = (os_strcmp(method, "robot.motion.move") == 0);

        if (!dir || !cJSON_IsString(dir)) {
            return robot_ctrl_send_error(id, -32602, "missing motion dir");
        }
        if ((is_move && os_strcmp(dir->valuestring, "forward") != 0 && os_strcmp(dir->valuestring, "backward") != 0)
            || (!is_move && os_strcmp(dir->valuestring, "left") != 0 && os_strcmp(dir->valuestring, "right") != 0)) {
            return robot_ctrl_send_error(id, -32602, "invalid motion dir");
        }
        if (speed) {
            s_ctrl.solution.motion_speed = (uint16_t)robot_ctrl_json_int(speed, s_ctrl.solution.motion_speed);
        }

        LOGI("motion %s dir=%s speed=%u\n", is_move ? "move" : "rotate",
             dir->valuestring, s_ctrl.solution.motion_speed);
#if CONFIG_MOTOR
        motor_open(PWM_MOTOR_CH_3);
#endif
        s_ctrl.motion_on = true;
        robot_ctrl_service_refresh_activity();

        uint32_t duration = 200;
        cJSON *duration_ms = params ? cJSON_GetObjectItem(params, "durationMs") : NULL;
        if (duration_ms) {
            duration = (uint32_t)robot_ctrl_json_int(duration_ms, duration);
        }
        if (duration > 0) {
            if (!rtos_is_oneshot_timer_init(&s_ctrl.motion_timer)) {
                rtos_init_oneshot_timer(&s_ctrl.motion_timer, duration, robot_ctrl_motion_timeout, NULL, NULL);
                rtos_start_oneshot_timer(&s_ctrl.motion_timer);
            } else {
                rtos_oneshot_reload_timer_ex(&s_ctrl.motion_timer, duration, robot_ctrl_motion_timeout, NULL, NULL);
            }
        }
        return robot_ctrl_send_result(id, NULL);
    }

    if (os_strcmp(method, "robot.motion.setSpeed") == 0) {
        cJSON *speed = params ? cJSON_GetObjectItem(params, "speed") : NULL;
        if (speed && cJSON_IsNumber(speed)) {
            s_ctrl.solution.motion_speed = speed->valueint;
        }
        return robot_ctrl_send_result(id, NULL);
    }

    return BK_FAIL;
}
