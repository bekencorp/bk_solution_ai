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

#if CONFIG_AGORA_RTC_USE_STRING_UID
/**
 * @brief One-shot: hand a JPEG to the agent and immediately trigger an
 *        LLM image-recognition turn, auto-picking the best transport.
 *
 * This is the camera_preview "take photo -> describe" convenience wrapper.
 * Internally it:
 *   1. Builds the peer rtm uid as "a_<channel>" (the agent side mirrors
 *      "r_<channel>" used by the local device when joining RTC).
 *   2. Submits the JPEG to ConvoAI through RTM customType="image.upload"
 *      using one of two paths, chosen automatically by @p jpeg_len:
 *        - jpeg_len <= BK_AGORA_RTM_IMG_RAW_MAX_LEN (~22 KB):
 *          inline base64 via bk_agora_rtm_send_image_base64() -- a
 *          single self-contained RTM message, no extra network round
 *          trip.
 *        - jpeg_len >  BK_AGORA_RTM_IMG_RAW_MAX_LEN:
 *          first POST the JPEG to the shared Beken image-upload HTTPS
 *          server via bk_image_upload_jpeg() (multipart/form-data),
 *          then push only the returned URL through RTM via
 *          bk_agora_rtm_send_image_url(). Bypasses the 22 KB RTM cap.
 *   3. Pushes a user.transcription right after so convoai runs an LLM
 *      turn on the freshly staged image without waiting for the user to
 *      speak. If @p query is NULL/empty, a built-in Chinese default
 *      ("describe the image contents") is used.
 *
 * Both RTM submits go out synchronously on the caller thread, but the
 * actual delivery is acknowledged asynchronously via the on_rtm_send_
 * data_result callback (logged at INFO level).
 *
 * Prerequisites: RTC must already be joined and RTM logged in (i.e. one
 * of bk_agora_start / `agora_rtc start` was issued and succeeded). When
 * the channel name is empty or RTM is not yet login, this returns
 * BK_FAIL without retry. For the oversize/URL path, network connectivity
 * to BK_IMAGE_UPLOAD_URL is also required.
 *
 * @param[in] jpeg      Pointer to raw JPEG bytes (NOT base64).
 * @param[in] jpeg_len  Size of jpeg in bytes; must be > 0. There is no
 *                      hard upper bound any more -- callers do not need
 *                      to gate on BK_AGORA_RTM_IMG_RAW_MAX_LEN.
 * @param[in] query     Optional UTF-8 prompt fed to the LLM together
 *                      with the image. Pass NULL to use the default.
 *
 * @return BK_OK if image submit AND follow-up text both succeeded;
 *         BK_FAIL otherwise. The text step is best-effort: a text
 *         failure after a successful image submit still returns BK_FAIL
 *         but the image is already staged on the agent side.
 */
bk_err_t bk_agora_rtc_send_image_with_query(const uint8_t *jpeg, size_t jpeg_len,
                                            const char *query);
#endif /* CONFIG_AGORA_RTC_USE_STRING_UID */

#ifdef __cplusplus
}
#endif
#endif /* __BK_AGORA_RTC_H__ */


