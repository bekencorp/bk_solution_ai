#include "robot_ctrl_internal.h"

#include <os/mem.h>
#include <os/str.h>
#include "robot_video_service.h"

void robot_ctrl_default_solution(robot_solution_config_t *solution)
{
    os_memset(solution, 0, sizeof(*solution));
    solution->mode = ROBOT_LAN_MODE_TCP;
    solution->motion_speed = 50;
    robot_video_service_default_config(&solution->video);
}

static const char *robot_ctrl_current_resolution(void)
{
    if (s_ctrl.solution.video.width == 1280 && s_ctrl.solution.video.height == 720) {
        return "1280_720";
    }

    return "640_480";
}

static cJSON *robot_ctrl_create_option(const char *label, const char *value)
{
    cJSON *option = cJSON_CreateObject();

    if (!option) {
        return NULL;
    }

    cJSON_AddStringToObject(option, "label", label);
    cJSON_AddStringToObject(option, "value", value);
    return option;
}

static cJSON *robot_ctrl_create_field(const char *type, const char *id, const char *label)
{
    cJSON *field = cJSON_CreateObject();

    if (!field) {
        return NULL;
    }

    cJSON_AddStringToObject(field, "type", type);
    cJSON_AddStringToObject(field, "id", id);
    cJSON_AddStringToObject(field, "label", label);
    cJSON_AddBoolToObject(field, "required", true);
    return field;
}

static void robot_ctrl_add_option(cJSON *options, const char *label, const char *value)
{
    cJSON *option = robot_ctrl_create_option(label, value);

    if (option) {
        cJSON_AddItemToArray(options, option);
    }
}

static cJSON *robot_ctrl_build_config_result(void)
{
    char fps_default[8] = {0};
    cJSON *result = cJSON_CreateObject();
    cJSON *groups = cJSON_CreateArray();
    cJSON *video_group = cJSON_CreateObject();
    cJSON *video_fields = cJSON_CreateArray();
    cJSON *ctrl_group = cJSON_CreateObject();
    cJSON *ctrl_fields = cJSON_CreateArray();
    cJSON *submit = cJSON_CreateObject();

    if (!result || !groups || !video_group || !video_fields || !ctrl_group || !ctrl_fields || !submit) {
        if (result) cJSON_Delete(result);
        if (groups) cJSON_Delete(groups);
        if (video_group) cJSON_Delete(video_group);
        if (video_fields) cJSON_Delete(video_fields);
        if (ctrl_group) cJSON_Delete(ctrl_group);
        if (ctrl_fields) cJSON_Delete(ctrl_fields);
        if (submit) cJSON_Delete(submit);
        return NULL;
    }

    os_snprintf(fps_default, sizeof(fps_default), "%u", s_ctrl.solution.video.fps);

    cJSON_AddStringToObject(result, "id", "ai_robot_ctrl");
    cJSON_AddStringToObject(result, "title", "参数设置");
    cJSON_AddStringToObject(result, "desc", "");

    cJSON_AddStringToObject(video_group, "id", "video");
    cJSON_AddStringToObject(video_group, "title", "视频配置");
    cJSON_AddStringToObject(video_group, "desc", "");

    cJSON *resolution = robot_ctrl_create_field("radio", "resolution", "分辨率");
    cJSON *resolution_options = cJSON_CreateArray();
    if (resolution && resolution_options) {
        cJSON_AddStringToObject(resolution, "default", robot_ctrl_current_resolution());
        robot_ctrl_add_option(resolution_options, "1280 * 720", "1280_720");
        robot_ctrl_add_option(resolution_options, "640 * 480", "640_480");
        cJSON_AddItemToObject(resolution, "options", resolution_options);
        cJSON_AddItemToArray(video_fields, resolution);
    } else {
        if (resolution) cJSON_Delete(resolution);
        if (resolution_options) cJSON_Delete(resolution_options);
    }

    cJSON *frame_rate = robot_ctrl_create_field("radio", "frame_rate", "帧率");
    cJSON *frame_rate_options = cJSON_CreateArray();
    if (frame_rate && frame_rate_options) {
        cJSON_AddStringToObject(frame_rate, "default", fps_default);
        robot_ctrl_add_option(frame_rate_options, "20 fps", "20");
        robot_ctrl_add_option(frame_rate_options, "25 fps", "25");
        cJSON_AddItemToObject(frame_rate, "options", frame_rate_options);
        cJSON_AddItemToArray(video_fields, frame_rate);
    } else {
        if (frame_rate) cJSON_Delete(frame_rate);
        if (frame_rate_options) cJSON_Delete(frame_rate_options);
    }
    cJSON_AddItemToObject(video_group, "fields", video_fields);
    cJSON_AddItemToArray(groups, video_group);

    cJSON_AddStringToObject(ctrl_group, "id", "ctrl");
    cJSON_AddStringToObject(ctrl_group, "title", "控制配置");
    cJSON_AddStringToObject(ctrl_group, "desc", "");

    cJSON *move_mode = robot_ctrl_create_field("radio", "move_mode", "移动模式");
    cJSON *move_mode_options = cJSON_CreateArray();
    if (move_mode && move_mode_options) {
        cJSON_AddStringToObject(move_mode, "default", "standard");
        robot_ctrl_add_option(move_mode_options, "标准", "standard");
        cJSON_AddItemToObject(move_mode, "options", move_mode_options);
        cJSON_AddItemToArray(ctrl_fields, move_mode);
    } else {
        if (move_mode) cJSON_Delete(move_mode);
        if (move_mode_options) cJSON_Delete(move_mode_options);
    }

    cJSON *speed = robot_ctrl_create_field("number", "speed", "移动速度");
    if (speed) {
        cJSON_AddNumberToObject(speed, "default", s_ctrl.solution.motion_speed);
        cJSON_AddItemToArray(ctrl_fields, speed);
    }

    cJSON *rotate_speed = robot_ctrl_create_field("number", "rotate_speed", "转向速度");
    if (rotate_speed) {
        cJSON_AddNumberToObject(rotate_speed, "default", s_ctrl.solution.motion_speed);
        cJSON_AddItemToArray(ctrl_fields, rotate_speed);
    }

    cJSON *pan_tilt = robot_ctrl_create_field("switch", "pan_tilt", "云台控制");
    if (pan_tilt) {
        cJSON_AddBoolToObject(pan_tilt, "default", true);
        cJSON_AddItemToArray(ctrl_fields, pan_tilt);
    }
    cJSON_AddItemToObject(ctrl_group, "fields", ctrl_fields);
    cJSON_AddItemToArray(groups, ctrl_group);

    cJSON_AddItemToObject(result, "groups", groups);
    cJSON_AddStringToObject(submit, "label", "提交");
    cJSON_AddStringToObject(submit, "method", "robot.solution.setConfig");
    cJSON_AddStringToObject(submit, "build", "tree");
    cJSON_AddItemToObject(result, "submit", submit);
    return result;
}

static void robot_ctrl_apply_set_config(cJSON *params)
{
    if (!params) {
        return;
    }

    const cJSON *mode = cJSON_GetObjectItem(params, "mode");
    if (!mode) {
        mode = cJSON_GetObjectItem(params, "serviceType");
    }
    if (mode && cJSON_IsString(mode)) {
        s_ctrl.solution.mode = robot_lan_net_parse_mode(mode->valuestring);
    }

    cJSON *video = cJSON_GetObjectItem(params, "video");
    if (!video) {
        video = params;
    }

    cJSON *resolution = cJSON_GetObjectItem(video, "resolution");
    if (resolution && cJSON_IsString(resolution)) {
        if (os_strcmp(resolution->valuestring, "1280_720") == 0) {
            s_ctrl.solution.video.width = 1280;
            s_ctrl.solution.video.height = 720;
        } else if (os_strcmp(resolution->valuestring, "640_480") == 0) {
            s_ctrl.solution.video.width = 640;
            s_ctrl.solution.video.height = 480;
        }
    }

    cJSON *width = cJSON_GetObjectItem(video, "width");
    cJSON *height = cJSON_GetObjectItem(video, "height");
    cJSON *fps = cJSON_GetObjectItem(video, "fps");
    if (!fps) {
        fps = cJSON_GetObjectItem(video, "frame_rate");
    }
    cJSON *bitrate = cJSON_GetObjectItem(video, "bitrateKbps");
    if (width && cJSON_IsNumber(width)) s_ctrl.solution.video.width = width->valueint;
    if (height && cJSON_IsNumber(height)) s_ctrl.solution.video.height = height->valueint;
    s_ctrl.solution.video.fps = robot_ctrl_json_int(fps, s_ctrl.solution.video.fps);
    if (bitrate && cJSON_IsNumber(bitrate)) s_ctrl.solution.video.bitrate_kbps = bitrate->valueint;

    cJSON *ctrl = cJSON_GetObjectItem(params, "ctrl");
    if (ctrl) {
        cJSON *speed = cJSON_GetObjectItem(ctrl, "speed");
        if (speed) {
            s_ctrl.solution.motion_speed = (uint16_t)robot_ctrl_json_int(speed, s_ctrl.solution.motion_speed);
        }
    }
}

bk_err_t robot_ctrl_handle_solution(const char *method, cJSON *id, cJSON *params)
{
    if (os_strcmp(method, "robot.solution.getConfig") == 0) {
        s_ctrl.authed = true;
        s_ctrl.state = ROBOT_CTRL_STATE_AUTHED;
        return robot_ctrl_send_result(id, robot_ctrl_build_config_result());
    }

    if (os_strcmp(method, "robot.solution.setConfig") == 0) {
        robot_ctrl_apply_set_config(params);
        s_ctrl.configured = true;
        s_ctrl.state = ROBOT_CTRL_STATE_CONFIGURED;
        robot_ctrl_reload_idle_timer();
        robot_ctrl_notify_video_connected();
        return robot_ctrl_send_result(id, NULL);
    }

    return robot_ctrl_send_error(id, -32601, "method not found");
}
