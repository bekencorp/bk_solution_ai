/*************************************************************
 *
 * This is a part of the Agora Media Framework Library.
 * Copyright (C) 2025 Agora IO
 * All rights reserved.
 *
 *************************************************************/
#ifndef __BK_AGORA_RTC_H__
#define __BK_AGORA_RTC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "agora_rtc_engine.h"
#include "network_transfer.h"

#define VIDEO_FRAME_INTERVAL            500
#define BANDWIDTH_ESTIMATE_MAX_BITRATE   (2000000)

// DISABLE
#define CONFIG_AUDIO_CODEC_TYPE         AUDIO_CODEC_DISABLED
#define CONFIG_PCM_FRAME_LEN            160
#define CONFIG_PCM_SAMPLE_RATE          8000
#define CONFIG_PCM_CHANNEL_NUM          1



/* Public API */
bk_err_t bk_agora_start(void *device_id);
bk_err_t bk_agora_stop(void *device_id);
bk_err_t bk_agora_pre_config(void *device_id);
int bk_agora_update_agent(void *device_id, void *update_info);
int bk_agora_rtc_audio_data_send(uint8_t *data_ptr, size_t data_len, audio_enc_type_t audio_type);
int bk_agora_rtc_video_data_send(frame_buffer_t *frame);

/**
 * @brief Get the most recently observed AI agent runtime state.
 *
 * The state is updated whenever the remote ConvoAI agent publishes a
 * "message.state" data-stream frame on the joined channel. Until the first
 * such frame is received, this returns AGORA_RTC_AGENT_STATE_UNKNOWN.
 *
 * @return Latest agora_rtc_agent_state_e value.
 */
agora_rtc_agent_state_e bk_agora_get_agent_state(void);

/**
 * @brief Convenience wrapper around bk_agora_get_agent_state() that returns
 *        a short human-readable string ("listening", "thinking", "speaking",
 *        "silent", "unknown"). Never returns NULL.
 */
const char *bk_agora_get_agent_state_str(void);

/**
 * @brief Whether the AI agent currently has a known, active dialogue state
 *        (listening / thinking / speaking).
 *
 * @return true if state != UNKNOWN and state != SILENT.
 */
bool bk_agora_is_agent_active(void);

#ifdef __cplusplus
}
#endif
#endif /* __BK_AGORA_RTC_H__ */


