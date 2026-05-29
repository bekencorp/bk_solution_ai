/*************************************************************
 *
 * This is a part of the Agora Media Framework Library.
 * Copyright (C) 2025 Agora IO
 * All rights reserved.
 *
 *************************************************************/
#ifndef __BK_AGORA_RTM_H__
#define __BK_AGORA_RTM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "agora_rtc_engine.h"

#if CONFIG_AGORA_RTC_USE_STRING_UID

/**
 * Login Agora RTM channel.
 *
 * RTM is required by the ConvoAI multi-modal image-upload flow (custom
 * type "image.upload"). It piggy-backs on the same App ID as the RTC
 * connection, but uses a separate RTM uid / token namespace.
 *
 * @param[in] self_uid  Local RTM uid string (length < 64). Typically the
 *                      same string user_account used to join RTC, e.g.
 *                      "r_<channel>" (kept short on purpose to leave room
 *                      for long backend-issued channel names).
 * @param[in] token     RTM token. Pass NULL when the App ID has no token
 *                      auth enabled.
 *
 * @return BK_OK on success; BK_FAIL otherwise.
 */
bk_err_t bk_agora_rtm_start(const char *self_uid, const char *token);

/**
 * Logout the RTM channel. Safe to call when not logged in.
 */
bk_err_t bk_agora_rtm_stop(void);

/**
 * Whether RTM login has succeeded and the channel is usable for send.
 */
bool bk_agora_rtm_is_login(void);

/**
 * Send an image URL to the peer (ConvoAI agent) using custom type
 * "image.upload". The convoai server accumulates these images and
 * injects them into the next LLM turn for multimodal recognition.
 *
 * The on-wire payload is the JSON string:
 *   {"uuid":"img_<rand>","image_url":"<image_url>"}
 *
 * @param[in] peer_uid   Peer RTM uid string, e.g. "a_<channel>".
 * @param[in] image_url  Publicly reachable http/https image URL.
 *
 * @return BK_OK on submit success; BK_FAIL otherwise. Delivery state is
 *         reported asynchronously through on_rtm_send_data_result.
 */
bk_err_t bk_agora_rtm_send_image_url(const char *peer_uid, const char *image_url);

/**
 * Send an image URL and trigger a user text only after the image message
 * delivery ack reports RTM_MSG_STATE_RECEIVED.
 */
bk_err_t bk_agora_rtm_send_image_url_with_query(const char *peer_uid,
                                                const char *image_url,
                                                const char *query_text);

/**
 * Maximum raw JPEG size accepted by bk_agora_rtm_send_image_base64().
 *
 * The Agora RTSA RTM channel caps a single payload at 32 KB, and the
 * ConvoAI customType="image.upload" base64 path is documented to work
 * for images up to "about 23 KB". A raw JPEG of 22 KB base64-encodes to
 * ~29.4 KB, which leaves ~2 KB headroom for the surrounding JSON / uuid
 * and the SDK's own framing. Anything larger should use the URL path.
 */
#define BK_AGORA_RTM_IMG_RAW_MAX_LEN     (22 * 1024)

/**
 * Send a JPEG image inline to the ConvoAI agent through RTM, using
 * custom type "image.upload" with a base64-encoded payload:
 *
 *   { "uuid": "img_<rand>", "image_base64": "<base64-of-jpeg>" }
 *
 * Use this when the device has no easy way to publish the image to an
 * http/https URL. For larger images, prefer bk_agora_rtm_send_image_url
 * which only pushes the URL through RTM.
 *
 * @param[in] peer_uid  Peer RTM uid string, e.g. "a_<channel>".
 * @param[in] jpeg      Pointer to the raw JPEG bytes.
 * @param[in] jpeg_len  Length of jpeg in bytes. MUST be in
 *                      (0, BK_AGORA_RTM_IMG_RAW_MAX_LEN]; values outside
 *                      that range are rejected up-front so the caller
 *                      sees the error before any allocation happens.
 *
 * @return BK_OK on submit success; BK_FAIL otherwise (oversize, OOM,
 *         encode failure, SDK error). Delivery state is reported
 *         asynchronously through on_rtm_send_data_result.
 */
bk_err_t bk_agora_rtm_send_image_base64(const char *peer_uid,
                                        const uint8_t *jpeg, size_t jpeg_len);

/**
 * Send an inline JPEG and trigger a user text only after the image message
 * delivery ack reports RTM_MSG_STATE_RECEIVED.
 */
bk_err_t bk_agora_rtm_send_image_base64_with_query(const char *peer_uid,
                                                   const uint8_t *jpeg,
                                                   size_t jpeg_len,
                                                   const char *query_text);

/**
 * Send a user-side text message to the ConvoAI agent through RTM with
 * custom type "user.transcription". The convoai server treats the text
 * as if it were the user's freshly transcribed ASR output and feeds it
 * straight into the next LLM turn, together with any context already
 * staged on the agent side (e.g. previously uploaded images).
 *
 * Reference: Agora ConvoAI custom-message protocol -- customType
 * "user.transcription" wraps the message as
 *
 *   { "priority": "INTERRUPT", "interruptable": true, "message": "<text>" }
 *
 * Typical use: use bk_agora_rtm_send_image_url_with_query() so the
 * trigger text is sent after the image-upload ack reports RECEIVED.
 *
 * @param[in] peer_uid Peer RTM uid string, e.g. "a_<channel>".
 * @param[in] text     UTF-8 text. Will be put into "message" verbatim;
 *                     callers must keep payload below RTM_MSG_MAX_LEN
 *                     once JSON-encoded.
 *
 * @return BK_OK on submit success; BK_FAIL otherwise. Delivery state is
 *         reported asynchronously through on_rtm_send_data_result.
 */
bk_err_t bk_agora_rtm_send_user_text(const char *peer_uid, const char *text);

/**
 * Notify the RTM layer that a previously sent image has actually been
 * ingested by the ConvoAI server (i.e. it is now part of the LLM
 * context). This is called from the RTC data-stream parser when a
 * frame of the form
 *
 *   { "object": "message.info",
 *     "module": "context",
 *     "message": "{\"uuid\":\"<img_uuid>\",
 *                  \"resource_type\":\"picture\", ...}" }
 *
 * is decoded. Why this matters: the RTM "send data ack" only confirms
 * channel delivery, NOT server-side image ingestion. For both the
 * base64 and URL upload paths we want the follow-up "describe this
 * image" text to fire ONLY after the server confirms the image is
 * available, so the LLM never sees the trigger sentence before the
 * picture.
 *
 * No-op if uuid is unknown (no pending query attached, or the RTM
 * delivery already timed out).
 */
void bk_agora_rtm_on_image_uploaded(const char *uuid);

#endif /* CONFIG_AGORA_RTC_USE_STRING_UID */

#ifdef __cplusplus
}
#endif
#endif /* __BK_AGORA_RTM_H__ */
