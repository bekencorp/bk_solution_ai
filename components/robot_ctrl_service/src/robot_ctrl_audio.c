#include "robot_ctrl_internal.h"

#include <os/str.h>
#include "cJSON.h"
#include "audio_engine.h"
#include "bk_trans_api.h"

bk_err_t robot_ctrl_handle_audio(const char *method, cJSON *id)
{
    if (os_strcmp(method, "robot.audio.turnOn") == 0) {
        if (s_ctrl.ap_keepalive) {
            robot_ctrl_service_handle_wakeup();
        }
        if (!bk_trans_is_audio_channel_connected()) {
            return robot_ctrl_send_error(id, -32603, "audio channel not connected");
        }
        if (!audio_engine_is_running()) {
            audio_engine_cfg_t cfg = {0};
            cfg.mic_sample_rate = 16000;
            cfg.spk_sample_rate = 16000;
            cfg.aec_enable = 1;
            cfg.enc_type = AUDIO_ENC_TYPE_G722;
            cfg.dec_type = AUDIO_DEC_TYPE_G722;
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
        cJSON_AddStringToObject(result, "codec", "G722");
        return robot_ctrl_send_result(id, result);
    }

    if (os_strcmp(method, "robot.audio.setAcoustics") == 0) {
        return robot_ctrl_send_error(id, -32003, "audio acoustics not supported");
    }

    return BK_FAIL;
}
