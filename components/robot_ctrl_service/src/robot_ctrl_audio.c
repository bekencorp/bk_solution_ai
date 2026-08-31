#include "robot_ctrl_internal.h"

#include <os/str.h>
#include "cJSON.h"
#include "audio_engine.h"
#include "bk_trans_api.h"
#include "network_engine.h"

static audio_enc_type_t s_robot_ctrl_audio_enc_type = AUDIO_ENC_TYPE_INVALID;

static audio_enc_type_t robot_ctrl_audio_enc_type(const char *fmt)
{
    if (fmt && os_strcmp(fmt, "g711a") == 0) {
        return AUDIO_ENC_TYPE_G711A;
    }

    if (fmt && os_strcmp(fmt, "g711u") == 0) {
        return AUDIO_ENC_TYPE_G711U;
    }

    if (fmt && os_strcmp(fmt, "g722") == 0) {
        return AUDIO_ENC_TYPE_G722;
    }

    if (fmt && os_strcmp(fmt, "pcm") == 0) {
        return AUDIO_ENC_TYPE_PCM;
    }

    return AUDIO_ENC_TYPE_INVALID;
}

static audio_dec_type_t robot_ctrl_audio_dec_type(const char *fmt)
{
    if (fmt && os_strcmp(fmt, "g711a") == 0) {
        return AUDIO_DEC_TYPE_G711A;
    }

    if (fmt && os_strcmp(fmt, "g711u") == 0) {
        return AUDIO_DEC_TYPE_G711U;
    }

    if (fmt && os_strcmp(fmt, "g722") == 0) {
        return AUDIO_DEC_TYPE_G722;
    }

    if (fmt && os_strcmp(fmt, "pcm") == 0) {
        return AUDIO_DEC_TYPE_PCM;
    }

    return AUDIO_DEC_TYPE_INVALID;
}

static const char *robot_ctrl_audio_codec_name(void)
{
    if (os_strcmp(s_ctrl.solution.audio.record_fmt, "g711a") == 0 &&
        os_strcmp(s_ctrl.solution.audio.play_fmt, "g711a") == 0) {
        return "G711A";
    }

    if (os_strcmp(s_ctrl.solution.audio.record_fmt, "g711u") == 0 &&
        os_strcmp(s_ctrl.solution.audio.play_fmt, "g711u") == 0) {
        return "G711U";
    }

    if (os_strcmp(s_ctrl.solution.audio.record_fmt, "g722") == 0 &&
        os_strcmp(s_ctrl.solution.audio.play_fmt, "g722") == 0) {
        return "G722";
    }

    if (os_strcmp(s_ctrl.solution.audio.record_fmt, "pcm") == 0 &&
        os_strcmp(s_ctrl.solution.audio.play_fmt, "pcm") == 0) {
        return "PCM";
    }

    return "unknown";
}

static int robot_ctrl_audio_read_cb(unsigned char *data, unsigned int len, void *args)
{
    audio_enc_type_t enc_type = args ? *(audio_enc_type_t *)args : s_robot_ctrl_audio_enc_type;

    return ntwk_eng_send_audio(data, len, enc_type);
}

bk_err_t robot_ctrl_handle_audio(const char *method, cJSON *id, cJSON *params)
{
    if (os_strcmp(method, "robot.audio.turnOn") == 0) {
        if (s_ctrl.ap_keepalive) {
            robot_ctrl_service_handle_wakeup();
        }
        if (!bk_trans_is_audio_channel_connected()) {
            return robot_ctrl_send_error(id, -32603, "audio channel not connected");
        }
        if (!audio_engine_is_running()) {
            robot_audio_config_t audio = s_ctrl.solution.audio;
            robot_ctrl_apply_audio_config(params, &audio);

            audio_engine_cfg_t cfg = {0};
            cfg.mic_sample_rate = audio.record_sample_rate;
            cfg.spk_sample_rate = audio.play_sample_rate;
            cfg.aec_enable = audio.aec ? 1 : 0;
            cfg.enc_type = robot_ctrl_audio_enc_type(audio.record_fmt);
            cfg.dec_type = robot_ctrl_audio_dec_type(audio.play_fmt);
            if (cfg.enc_type == AUDIO_ENC_TYPE_INVALID || cfg.dec_type == AUDIO_DEC_TYPE_INVALID) {
                return robot_ctrl_send_error(id, -32602, "unsupported audio config");
            }
#if CONFIG_AE_ENABLE_PA_CNTRL
            cfg.pa_enable    = true;
            cfg.pa_gpio      = CONFIG_AE_PA_CNTRL_GPIO;
            cfg.pa_on_level  = CONFIG_AE_PA_ON_LEVEL;
            cfg.pa_on_delay  = CONFIG_AE_PA_ON_DELAY;
            cfg.pa_off_delay = CONFIG_AE_PA_OFF_DELAY;
#endif
            s_robot_ctrl_audio_enc_type = cfg.enc_type;
            cfg.read_cb = robot_ctrl_audio_read_cb;
            cfg.user_data = &s_robot_ctrl_audio_enc_type;
            if (audio_engine_start(&cfg) != BK_OK) {
                return robot_ctrl_send_error(id, -32603, "audio start failed");
            }
        }
        s_ctrl.audio_on = true;
        s_ctrl.state = ROBOT_CTRL_STATE_AUDIO_TALKING;
        robot_ctrl_service_refresh_activity();
        return robot_ctrl_send_result(id, NULL);
    }

    if (os_strcmp(method, "robot.audio.turnOff") == 0) {
        audio_engine_stop();
        s_ctrl.audio_on = false;
        robot_ctrl_reload_idle_timer();
        return robot_ctrl_send_result(id, NULL);
    }

    if (os_strcmp(method, "robot.audio.getStatus") == 0) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddBoolToObject(result, "on", s_ctrl.audio_on);
        cJSON_AddStringToObject(result, "codec", robot_ctrl_audio_codec_name());
        return robot_ctrl_send_result(id, result);
    }

    if (os_strcmp(method, "robot.audio.setAcoustics") == 0) {
        return robot_ctrl_send_error(id, -32003, "audio acoustics not supported");
    }

    return BK_FAIL;
}
