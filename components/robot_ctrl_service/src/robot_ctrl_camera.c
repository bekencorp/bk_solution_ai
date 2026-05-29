#include "robot_ctrl_internal.h"

#include <os/str.h>
#include "robot_video_service.h"

static void robot_ctrl_apply_camera_params(cJSON *params)
{
    if (!params) {
        return;
    }

    cJSON *streams = cJSON_GetObjectItem(params, "streams");
    cJSON *stream = (streams && cJSON_IsArray(streams)) ? cJSON_GetArrayItem(streams, 0) : NULL;
    cJSON *camera_config = stream ? cJSON_GetObjectItem(stream, "cameraConfig") : NULL;
    if (!camera_config) {
        camera_config = params;
    }

    cJSON *width = cJSON_GetObjectItem(camera_config, "width");
    cJSON *height = cJSON_GetObjectItem(camera_config, "height");
    cJSON *fps = cJSON_GetObjectItem(camera_config, "fps");
    cJSON *bitrate = cJSON_GetObjectItem(camera_config, "bitrateKbps");

    if (width && cJSON_IsNumber(width)) s_ctrl.solution.video.width = width->valueint;
    if (height && cJSON_IsNumber(height)) s_ctrl.solution.video.height = height->valueint;
    if (fps) s_ctrl.solution.video.fps = robot_ctrl_json_int(fps, s_ctrl.solution.video.fps);
    if (bitrate && cJSON_IsNumber(bitrate)) s_ctrl.solution.video.bitrate_kbps = bitrate->valueint;
}

bk_err_t robot_ctrl_handle_camera(const char *method, cJSON *id, cJSON *params)
{
    if (os_strcmp(method, "robot.camera.turnOn") == 0) {
        if (s_ctrl.ap_keepalive) {
            robot_ctrl_service_handle_wakeup();
        }
        robot_ctrl_apply_camera_params(params);
        if (robot_lan_net_start_video_channel() != BK_OK) {
            return robot_ctrl_send_error(id, -32603, "video channel start failed");
        }
        if (robot_video_service_start(&s_ctrl.solution.video) != BK_OK) {
            robot_lan_net_stop_video_channel();
            return robot_ctrl_send_error(id, -32603, "camera start failed");
        }
        s_ctrl.video_on = true;
        s_ctrl.state = ROBOT_CTRL_STATE_STREAMING;
        robot_ctrl_service_refresh_activity();
        return robot_ctrl_send_result(id, NULL);
    }

    if (os_strcmp(method, "robot.camera.turnOff") == 0) {
        robot_video_service_stop();
        robot_lan_net_stop_video_channel();
        s_ctrl.video_on = false;
        robot_ctrl_reload_idle_timer();
        return robot_ctrl_send_result(id, NULL);
    }

    if (os_strcmp(method, "robot.camera.getStatus") == 0) {
        robot_video_status_t st = {0};
        robot_video_service_get_status(&st);
        cJSON *result = cJSON_CreateObject();
        cJSON_AddBoolToObject(result, "on", st.running);
        cJSON_AddNumberToObject(result, "width", st.config.width);
        cJSON_AddNumberToObject(result, "height", st.config.height);
        cJSON_AddNumberToObject(result, "fps", st.config.fps);
        cJSON_AddNumberToObject(result, "bitrateKbps", st.config.bitrate_kbps);
        return robot_ctrl_send_result(id, result);
    }

    if (os_strcmp(method, "robot.camera.requestKeyFrame") == 0) {
        LOGW("request key frame is accepted but not supported by video backend\n");
        return robot_ctrl_send_result(id, NULL);
    }

    if (os_strcmp(method, "robot.camera.setBitrate") == 0) {
        return robot_ctrl_send_error(id, -32003, "camera capability not supported");
    }

    return BK_FAIL;
}
