#include "robot_ctrl_internal.h"

#include <stddef.h>
#include <os/mem.h>
#include <os/str.h>
#include "robot_video_service.h"

void robot_ctrl_default_solution(robot_solution_config_t *solution)
{
    os_memset(solution, 0, sizeof(*solution));
    solution->mode = ROBOT_LAN_MODE_TCP;
    solution->motion_speed = 50;
    robot_video_service_default_config(&solution->video);
    solution->audio.aec = true;
    os_snprintf(solution->audio.mic_type, sizeof(solution->audio.mic_type), "%s", "dmic");
    solution->audio.record_sample_rate = 16000;
    os_snprintf(solution->audio.record_fmt, sizeof(solution->audio.record_fmt), "%s", "g711a");
    solution->audio.play_sample_rate = 16000;
    os_snprintf(solution->audio.play_fmt, sizeof(solution->audio.play_fmt), "%s", "g711a");
    os_snprintf(solution->audio.spk_type, sizeof(solution->audio.spk_type), "%s", "speaker");
    solution->audio.asr = false;
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

static cJSON *robot_ctrl_create_radio_field(const char *id,
                                            const char *label,
                                            const char *default_value)
{
    cJSON *field = robot_ctrl_create_field("radio", id, label);
    cJSON *options = cJSON_CreateArray();

    if (field && options) {
        cJSON_AddStringToObject(field, "default", default_value);
        cJSON_AddItemToObject(field, "options", options);
        return field;
    }

    if (field) cJSON_Delete(field);
    if (options) cJSON_Delete(options);
    return NULL;
}

static void robot_ctrl_radio_add_option(cJSON *field, const char *label, const char *value)
{
    cJSON *options = field ? cJSON_GetObjectItem(field, "options") : NULL;

    if (options) {
        robot_ctrl_add_option(options, label, value);
    }
}

static cJSON *robot_ctrl_build_config_result(void)
{
    char fps_default[8] = {0};
    char record_rate_default[8] = {0};
    char play_rate_default[8] = {0};
    cJSON *result = cJSON_CreateObject();
    cJSON *groups = cJSON_CreateArray();
    cJSON *video_group = cJSON_CreateObject();
    cJSON *video_fields = cJSON_CreateArray();
    cJSON *audio_group = cJSON_CreateObject();
    cJSON *audio_fields = cJSON_CreateArray();
    cJSON *ctrl_group = cJSON_CreateObject();
    cJSON *ctrl_fields = cJSON_CreateArray();
    cJSON *submit = cJSON_CreateObject();

    if (!result || !groups || !video_group || !video_fields || !audio_group || !audio_fields ||
        !ctrl_group || !ctrl_fields || !submit) {
        if (result) cJSON_Delete(result);
        if (groups) cJSON_Delete(groups);
        if (video_group) cJSON_Delete(video_group);
        if (video_fields) cJSON_Delete(video_fields);
        if (audio_group) cJSON_Delete(audio_group);
        if (audio_fields) cJSON_Delete(audio_fields);
        if (ctrl_group) cJSON_Delete(ctrl_group);
        if (ctrl_fields) cJSON_Delete(ctrl_fields);
        if (submit) cJSON_Delete(submit);
        return NULL;
    }

    os_snprintf(fps_default, sizeof(fps_default), "%u", s_ctrl.solution.video.fps);
    os_snprintf(record_rate_default, sizeof(record_rate_default), "%u", s_ctrl.solution.audio.record_sample_rate);
    os_snprintf(play_rate_default, sizeof(play_rate_default), "%u", s_ctrl.solution.audio.play_sample_rate);

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

    cJSON_AddStringToObject(audio_group, "id", "audio");
    cJSON_AddStringToObject(audio_group, "title", "语音配置");
    cJSON_AddStringToObject(audio_group, "desc", "setConfig 保存；turnOn 时作为设备 mic 采集参数");

    cJSON *aec = robot_ctrl_create_field("switch", "aec", "回声消除");
    if (aec) {
        cJSON_AddBoolToObject(aec, "default", s_ctrl.solution.audio.aec);
        cJSON_AddItemToArray(audio_fields, aec);
    }

    cJSON *record_fmt = robot_ctrl_create_radio_field("record_fmt", "编码",
                                                      s_ctrl.solution.audio.record_fmt);
    if (record_fmt) {
        robot_ctrl_radio_add_option(record_fmt, "G711A", "g711a");
        robot_ctrl_radio_add_option(record_fmt, "PCM", "pcm");
#if CONFIG_VOICE_SERVICE_G722_ENCODER
        robot_ctrl_radio_add_option(record_fmt, "G722", "g722");
#endif
        cJSON_AddItemToArray(audio_fields, record_fmt);
    }

    cJSON *record_sample_rate = robot_ctrl_create_radio_field("record_sample_rate", "采样率",
                                                              record_rate_default);
    if (record_sample_rate) {
        robot_ctrl_radio_add_option(record_sample_rate, "16 kHz", "16000");
        robot_ctrl_radio_add_option(record_sample_rate, "8 kHz", "8000");
        cJSON_AddItemToArray(audio_fields, record_sample_rate);
    }

    cJSON *mic_type = robot_ctrl_create_radio_field("mic_type", "麦克风",
                                                    s_ctrl.solution.audio.mic_type);
    if (mic_type) {
        robot_ctrl_radio_add_option(mic_type, "DMIC", "dmic");
        cJSON_AddItemToArray(audio_fields, mic_type);
    }

    cJSON *play_fmt = robot_ctrl_create_radio_field("play_fmt", "播放编码",
                                                    s_ctrl.solution.audio.play_fmt);
    if (play_fmt) {
        robot_ctrl_radio_add_option(play_fmt, "G711A", "g711a");
        robot_ctrl_radio_add_option(play_fmt, "PCM", "pcm");
#if CONFIG_VOICE_SERVICE_G722_DECODER
        robot_ctrl_radio_add_option(play_fmt, "G722", "g722");
#endif
        cJSON_AddItemToArray(audio_fields, play_fmt);
    }

    cJSON *play_sample_rate = robot_ctrl_create_radio_field("play_sample_rate", "播放采样率",
                                                            play_rate_default);
    if (play_sample_rate) {
        robot_ctrl_radio_add_option(play_sample_rate, "16 kHz", "16000");
        robot_ctrl_radio_add_option(play_sample_rate, "8 kHz", "8000");
        cJSON_AddItemToArray(audio_fields, play_sample_rate);
    }

    cJSON *spk_type = robot_ctrl_create_radio_field("spk_type", "喇叭",
                                                    s_ctrl.solution.audio.spk_type);
    if (spk_type) {
        robot_ctrl_radio_add_option(spk_type, "Speaker", "speaker");
        cJSON_AddItemToArray(audio_fields, spk_type);
    }

    cJSON_AddItemToObject(audio_group, "fields", audio_fields);
    cJSON_AddItemToArray(groups, audio_group);

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

static bool robot_ctrl_string_is_one_of(const char *value, const char *supported)
{
    return value && supported && os_strcmp(value, supported) == 0;
}

static bool robot_ctrl_audio_record_fmt_supported(const char *value)
{
    if (!value) {
        return false;
    }

    if (os_strcmp(value, "g711a") == 0 || os_strcmp(value, "pcm") == 0) {
        return true;
    }

#if CONFIG_VOICE_SERVICE_G722_ENCODER
    if (os_strcmp(value, "g722") == 0) {
        return true;
    }
#endif

    return false;
}

static bool robot_ctrl_audio_play_fmt_supported(const char *value)
{
    if (!value) {
        return false;
    }

    if (os_strcmp(value, "g711a") == 0 || os_strcmp(value, "pcm") == 0) {
        return true;
    }

#if CONFIG_VOICE_SERVICE_G722_DECODER
    if (os_strcmp(value, "g722") == 0) {
        return true;
    }
#endif

    return false;
}

static void robot_ctrl_set_string_if_supported(char *dst,
                                               size_t dst_len,
                                               const cJSON *item,
                                               const char *supported)
{
    if (item && cJSON_IsString(item) &&
        robot_ctrl_string_is_one_of(item->valuestring, supported)) {
        os_snprintf(dst, dst_len, "%s", item->valuestring);
    }
}

static void robot_ctrl_set_audio_fmt_if_supported(char *dst,
                                                  size_t dst_len,
                                                  const cJSON *item,
                                                  bool record_fmt)
{
    if (!item || !cJSON_IsString(item)) {
        return;
    }

    bool supported = record_fmt ?
                     robot_ctrl_audio_record_fmt_supported(item->valuestring) :
                     robot_ctrl_audio_play_fmt_supported(item->valuestring);
    if (supported) {
        os_snprintf(dst, dst_len, "%s", item->valuestring);
    }
}

static uint32_t robot_ctrl_json_u32(cJSON *item, uint32_t fallback)
{
    int value = robot_ctrl_json_int(item, (int)fallback);

    return value > 0 ? (uint32_t)value : fallback;
}

static bool robot_ctrl_json_bool(cJSON *item, bool fallback)
{
    if (!item) {
        return fallback;
    }

    if ((item->type & 0xFF) == cJSON_True) {
        return true;
    }

    if ((item->type & 0xFF) == cJSON_False) {
        return false;
    }

    if (cJSON_IsNumber(item)) {
        return item->valueint != 0;
    }

    return fallback;
}

void robot_ctrl_apply_audio_config(cJSON *audio, robot_audio_config_t *cfg)
{
    if (!audio || !cfg) {
        return;
    }

    cJSON *aec = cJSON_GetObjectItem(audio, "aec");
    if (aec) {
        cfg->aec = robot_ctrl_json_bool(aec, cfg->aec);
    }

    cJSON *record_fmt = cJSON_GetObjectItem(audio, "record_fmt");
    if (!record_fmt) {
        record_fmt = cJSON_GetObjectItem(audio, "recordFmt");
    }
    robot_ctrl_set_audio_fmt_if_supported(cfg->record_fmt, sizeof(cfg->record_fmt),
                                          record_fmt, true);

    cJSON *play_fmt = cJSON_GetObjectItem(audio, "play_fmt");
    if (!play_fmt) {
        play_fmt = cJSON_GetObjectItem(audio, "playFmt");
    }
    robot_ctrl_set_audio_fmt_if_supported(cfg->play_fmt, sizeof(cfg->play_fmt),
                                          play_fmt, false);

    cJSON *record_sample_rate = cJSON_GetObjectItem(audio, "record_sample_rate");
    if (!record_sample_rate) {
        record_sample_rate = cJSON_GetObjectItem(audio, "recordSampleRate");
    }
    uint32_t record_rate = robot_ctrl_json_u32(record_sample_rate, cfg->record_sample_rate);
    if (record_rate == 16000 || record_rate == 8000) {
        cfg->record_sample_rate = record_rate;
    }

    cJSON *play_sample_rate = cJSON_GetObjectItem(audio, "play_sample_rate");
    if (!play_sample_rate) {
        play_sample_rate = cJSON_GetObjectItem(audio, "playSampleRate");
    }
    uint32_t play_rate = robot_ctrl_json_u32(play_sample_rate, cfg->play_sample_rate);
    if (play_rate == 16000 || play_rate == 8000) {
        cfg->play_sample_rate = play_rate;
    }

    cJSON *mic_type = cJSON_GetObjectItem(audio, "mic_type");
    if (!mic_type) {
        mic_type = cJSON_GetObjectItem(audio, "micType");
    }
    robot_ctrl_set_string_if_supported(cfg->mic_type, sizeof(cfg->mic_type),
                                       mic_type, "dmic");

    cJSON *spk_type = cJSON_GetObjectItem(audio, "spk_type");
    if (!spk_type) {
        spk_type = cJSON_GetObjectItem(audio, "spkType");
    }
    robot_ctrl_set_string_if_supported(cfg->spk_type, sizeof(cfg->spk_type),
                                       spk_type, "speaker");

    cJSON *asr = cJSON_GetObjectItem(audio, "asr");
    if (asr) {
        cfg->asr = robot_ctrl_json_bool(asr, cfg->asr);
    }
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

    cJSON *audio = cJSON_GetObjectItem(params, "audio");
    if (audio) {
        robot_ctrl_apply_audio_config(audio, &s_ctrl.solution.audio);
    }

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
