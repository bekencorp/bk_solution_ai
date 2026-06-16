/*************************************************************
 *
 * This is a part of the Agora Media Framework Library.
 * Copyright (C) 2025 Agora IO
 * All rights reserved.
 *
 *************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include "bk_rtos_debug.h"
#include "agora_config.h"
#include "agora_rtc_engine.h"
#include "bk_agora_rtm.h"
#include "cJSON.h"
#include "components/bk_platform.h"
#include "base_64.h"

#if CONFIG_AGORA_RTC_USE_STRING_UID

#define TAG "agora_rtm"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/* ConvoAI custom type for the image-upload flow. */
#define RTM_CUSTOM_TYPE_IMAGE_UPLOAD     "image.upload"

/* ConvoAI custom type: wraps a text "as if" the user just spoke it.
 * The server forwards the text to the LLM, replacing the ASR-from-mic
 * input for one turn. */
#define RTM_CUSTOM_TYPE_USER_TRANSCRIPT  "user.transcription"

/* Hard limit imposed by the SDK on a single RTM payload. */
#define RTM_MSG_MAX_LEN                 (31 * 1024)

#define RTM_PENDING_IMAGE_QUERY_MAX     4

/* Maximum length of the image uuid we generate (img_<8-hex>\0 = 13).
 * Bump for safety; ConvoAI echos it back verbatim in message.info. */
#define RTM_IMAGE_UUID_MAX_LEN          40

typedef struct {
    bool used;
    uint32_t msg_id;
    char uuid[RTM_IMAGE_UUID_MAX_LEN];
    char *peer_uid;
    char *query_text;
} rtm_pending_image_query_t;

static volatile bool s_rtm_login_success = false;
static uint32_t      s_rtm_msg_id = 0;
static rtm_pending_image_query_t s_pending_image_queries[RTM_PENDING_IMAGE_QUERY_MAX] = {0};

static const char *__rtm_state_str(rtm_msg_state_e s)
{
    switch (s)
    {
        case RTM_MSG_STATE_INIT:        return "INIT";
        case RTM_MSG_STATE_RECEIVED:    return "RECEIVED";
        case RTM_MSG_STATE_UNREACHABLE: return "UNREACHABLE";
        case RTM_MSG_STATE_TIMEOUT:     return "TIMEOUT";
        default:                        return "UNKNOWN";
    }
}

static const char *__rtm_event_str(rtm_event_type_e e)
{
    switch (e)
    {
        case RTM_EVENT_TYPE_LOGIN:   return "LOGIN";
        case RTM_EVENT_TYPE_KICKOFF: return "KICKOFF";
        case RTM_EVENT_TYPE_EXIT:    return "EXIT";
        default:                     return "UNKNOWN";
    }
}

static void __on_rtm_event(const char *rtm_uid, rtm_event_type_e event_type, rtm_err_code_e err_code)
{
    LOGI("rtm event uid=%s type=%s err=%d\n",
         rtm_uid ? rtm_uid : "", __rtm_event_str(event_type), (int)err_code);

    if (event_type == RTM_EVENT_TYPE_LOGIN && err_code == ERR_RTM_OK)
    {
        s_rtm_login_success = true;
    }
    else if (event_type == RTM_EVENT_TYPE_KICKOFF || event_type == RTM_EVENT_TYPE_EXIT)
    {
        s_rtm_login_success = false;
    }
}

static void __on_rtm_data(const char *rtm_uid, const void *msg, size_t msg_len, const char *custom_type)
{
    /* Convoai may push acks / future notifications here. Log only for now
     * so the SDK callback stays cheap and non-blocking. */
    LOGI("rtm rx from %s len=%u type=%s\n",
         rtm_uid ? rtm_uid : "", (unsigned)msg_len, custom_type ? custom_type : "");
}

static void __free_pending_image_query(rtm_pending_image_query_t *pending)
{
    if (!pending)
    {
        return;
    }

    if (pending->peer_uid)
    {
        os_free(pending->peer_uid);
    }
    if (pending->query_text)
    {
        os_free(pending->query_text);
    }
    os_memset(pending, 0, sizeof(*pending));
}

static void __remove_pending_image_query(uint32_t msg_id)
{
    for (size_t i = 0; i < RTM_PENDING_IMAGE_QUERY_MAX; i++)
    {
        if (s_pending_image_queries[i].used && s_pending_image_queries[i].msg_id == msg_id)
        {
            __free_pending_image_query(&s_pending_image_queries[i]);
            return;
        }
    }
}

static void __clear_pending_image_queries(void)
{
    for (size_t i = 0; i < RTM_PENDING_IMAGE_QUERY_MAX; i++)
    {
        if (s_pending_image_queries[i].used)
        {
            __free_pending_image_query(&s_pending_image_queries[i]);
        }
    }
}

static bk_err_t __add_pending_image_query(uint32_t msg_id,
                                          const char *uuid,
                                          const char *peer_uid,
                                          const char *query_text)
{
    rtm_pending_image_query_t *slot = NULL;

    if (!query_text || query_text[0] == '\0')
    {
        return BK_OK;
    }
    if (!uuid || uuid[0] == '\0')
    {
        LOGE("pending image query: uuid required\n");
        return BK_FAIL;
    }

    for (size_t i = 0; i < RTM_PENDING_IMAGE_QUERY_MAX; i++)
    {
        if (!s_pending_image_queries[i].used)
        {
            slot = &s_pending_image_queries[i];
            break;
        }
    }
    if (!slot)
    {
        LOGE("pending image query full, msg_id=%u uuid=%s\n", msg_id, uuid);
        return BK_FAIL;
    }

    slot->peer_uid = os_strdup(peer_uid);
    slot->query_text = os_strdup(query_text);
    if (!slot->peer_uid || !slot->query_text)
    {
        LOGE("pending image query OOM, msg_id=%u\n", msg_id);
        __free_pending_image_query(slot);
        return BK_FAIL;
    }

    slot->used = true;
    slot->msg_id = msg_id;
    os_strncpy(slot->uuid, uuid, sizeof(slot->uuid) - 1);
    slot->uuid[sizeof(slot->uuid) - 1] = '\0';
    LOGI("pending image query add msg_id=%u uuid=%s peer=%s\n", msg_id, slot->uuid, peer_uid);
    return BK_OK;
}

static rtm_pending_image_query_t *__find_pending_image_query(uint32_t msg_id)
{
    for (size_t i = 0; i < RTM_PENDING_IMAGE_QUERY_MAX; i++)
    {
        if (s_pending_image_queries[i].used && s_pending_image_queries[i].msg_id == msg_id)
        {
            return &s_pending_image_queries[i];
        }
    }

    return NULL;
}

static rtm_pending_image_query_t *__find_pending_image_query_by_uuid(const char *uuid)
{
    if (!uuid || uuid[0] == '\0')
    {
        return NULL;
    }
    for (size_t i = 0; i < RTM_PENDING_IMAGE_QUERY_MAX; i++)
    {
        if (s_pending_image_queries[i].used &&
            os_strcmp(s_pending_image_queries[i].uuid, uuid) == 0)
        {
            return &s_pending_image_queries[i];
        }
    }
    return NULL;
}

static void __on_rtm_send_data_res(const char *rtm_uid, uint32_t msg_id, rtm_msg_state_e state)
{
    LOGI("rtm tx ack peer=%s msg_id=%u state=%s\n",
         rtm_uid ? rtm_uid : "", msg_id, __rtm_state_str(state));

    rtm_pending_image_query_t *pending = __find_pending_image_query(msg_id);
    if (!pending)
    {
        return;
    }

    /* IMPORTANT: an RTM ACK with state=RECEIVED only means the RTM
     * channel delivered the bytes to the ConvoAI server, NOT that the
     * server has actually ingested the image into the LLM context.
     *
     * The authoritative "image is now usable as context" signal is the
     * data-stream frame:
     *     { "object":"message.info", "module":"context",
     *       "message": "{\"uuid\":\"img_xxx\",\"resource_type\":\"picture\",...}" }
     *
     * Order of arrival for these two events depends on the upload path:
     *   - base64 path: server processes inline payload immediately, so
     *     message.info arrives BEFORE the RTM ACK.
     *   - URL path:    server still has to fetch the image, so the RTM
     *     ACK arrives first and message.info follows ~hundreds of ms
     *     later.
     *
     * So on RECEIVED we keep the pending entry alive and wait for the
     * uuid-keyed dispatch in bk_agora_rtm_on_image_uploaded(). We only
     * tear it down here when the RTM channel itself has given up
     * (UNREACHABLE / TIMEOUT) -- in that case the server never saw the
     * image and message.info will never arrive. */
    if (state == RTM_MSG_STATE_UNREACHABLE || state == RTM_MSG_STATE_TIMEOUT)
    {
        LOGE("image msg_id=%u uuid=%s rtm delivery %s, drop pending query\n",
             msg_id, pending->uuid, __rtm_state_str(state));
        __free_pending_image_query(pending);
    }
}

void bk_agora_rtm_on_image_uploaded(const char *uuid)
{
    if (!uuid || uuid[0] == '\0')
    {
        return;
    }

    rtm_pending_image_query_t *pending = __find_pending_image_query_by_uuid(uuid);
    if (!pending)
    {
        /* Either no follow-up query was attached, or RTM delivery already
         * timed out and we tore the entry down. Nothing to do. */
        LOGD("image uploaded uuid=%s, no pending query\n", uuid);
        return;
    }

    char *peer_uid = pending->peer_uid;
    char *query_text = pending->query_text;
    uint32_t msg_id = pending->msg_id;

    /* Detach the heap buffers before freeing the slot so we can keep
     * using them after the slot is reusable by a concurrent uploader. */
    pending->peer_uid = NULL;
    pending->query_text = NULL;
    __free_pending_image_query(pending);

    LOGI("image uploaded uuid=%s msg_id=%u, send query text to peer=%s\n",
         uuid, msg_id, peer_uid ? peer_uid : "");
    if (BK_OK != bk_agora_rtm_send_user_text(peer_uid, query_text))
    {
        LOGE("image uuid=%s follow-up query text failed\n", uuid);
    }
    if (peer_uid)
    {
        os_free(peer_uid);
    }
    if (query_text)
    {
        os_free(query_text);
    }
}

static const agora_rtm_handler_t s_rtm_handler =
{
    .on_rtm_event            = __on_rtm_event,
    .on_rtm_data             = __on_rtm_data,
    .on_rtm_send_data_result = __on_rtm_send_data_res,
};

bool bk_agora_rtm_is_login(void)
{
    return s_rtm_login_success;
}

bk_err_t bk_agora_rtm_start(const char *self_uid, const char *token)
{
    int rval;

    if (!self_uid || self_uid[0] == '\0')
    {
        LOGE("rtm start: self_uid required\n");
        return BK_FAIL;
    }

    if (s_rtm_login_success)
    {
        LOGW("rtm already login (uid=%s), skip\n", self_uid);
        return BK_OK;
    }

    LOGI("rtm login: uid=%s token=%s\n", self_uid, token ? token : "(null)");

    rval = agora_rtc_login_rtm(self_uid, token, &s_rtm_handler);
    if (rval < 0)
    {
        LOGE("agora_rtc_login_rtm failed: %d, %s\n", rval, agora_rtc_err_2_str(rval));
        return BK_FAIL;
    }

    s_rtm_msg_id = 0;
    __clear_pending_image_queries();
    return BK_OK;
}

bk_err_t bk_agora_rtm_stop(void)
{
    int rval;

    if (!s_rtm_login_success)
    {
        LOGD("rtm not login, skip logout\n");
        return BK_OK;
    }

    rval = agora_rtc_logout_rtm();
    if (rval < 0)
    {
        LOGE("agora_rtc_logout_rtm failed: %d, %s\n", rval, agora_rtc_err_2_str(rval));
    }

    s_rtm_login_success = false;
    __clear_pending_image_queries();
    LOGI("rtm logout done\n");
    return (rval < 0) ? BK_FAIL : BK_OK;
}

static bk_err_t __send_image_url(const char *peer_uid,
                                 const char *image_url,
                                 const char *query_after_ack)
{
    bk_err_t ret = BK_FAIL;
    cJSON *root = NULL;
    char *payload = NULL;
    char uuid_buf[40] = {0};
    int rval;
    size_t payload_len;
    uint32_t msg_id = 0;

    if (!peer_uid || peer_uid[0] == '\0')
    {
        LOGE("send_image: peer_uid required\n");
        return BK_FAIL;
    }
    if (!image_url || image_url[0] == '\0')
    {
        LOGE("send_image: image_url required\n");
        return BK_FAIL;
    }
    if (!s_rtm_login_success)
    {
        LOGE("send_image: RTM not login yet, please start RTC first\n");
        return BK_FAIL;
    }

    os_snprintf(uuid_buf, sizeof(uuid_buf), "img_%08x", (unsigned)bk_rand());

    root = cJSON_CreateObject();
    if (!root)
    {
        LOGE("send_image: cJSON_CreateObject OOM\n");
        goto __exit;
    }
    cJSON_AddStringToObject(root, "uuid", uuid_buf);
    cJSON_AddStringToObject(root, "image_url", image_url);

    payload = cJSON_PrintUnformatted(root);
    if (!payload)
    {
        LOGE("send_image: cJSON_PrintUnformatted OOM\n");
        goto __exit;
    }

    payload_len = os_strlen(payload);
    if (payload_len > RTM_MSG_MAX_LEN)
    {
        LOGE("send_image: payload too large (%u > %u)\n",
             (unsigned)payload_len, (unsigned)RTM_MSG_MAX_LEN);
        goto __exit;
    }

    s_rtm_msg_id++;
    msg_id = s_rtm_msg_id;
    if (BK_OK != __add_pending_image_query(msg_id, uuid_buf, peer_uid, query_after_ack))
    {
        goto __exit;
    }

    LOGI("send_image peer=%s msg_id=%u payload=%s\n", peer_uid, msg_id, payload);

    rval = agora_rtc_send_rtm_data(peer_uid, payload, payload_len,
                                   msg_id, RTM_CUSTOM_TYPE_IMAGE_UPLOAD);
    if (rval < 0)
    {
        LOGE("agora_rtc_send_rtm_data failed: %d, %s\n", rval, agora_rtc_err_2_str(rval));
        __remove_pending_image_query(msg_id);
        goto __exit;
    }

    ret = BK_OK;

__exit:
    if (payload)
    {
        cJSON_free(payload);
    }
    if (root)
    {
        cJSON_Delete(root);
    }
    return ret;
}

bk_err_t bk_agora_rtm_send_image_url(const char *peer_uid, const char *image_url)
{
    return __send_image_url(peer_uid, image_url, NULL);
}

bk_err_t bk_agora_rtm_send_image_url_with_query(const char *peer_uid,
                                                const char *image_url,
                                                const char *query_text)
{
    return __send_image_url(peer_uid, image_url, query_text);
}

/* Strip every '\n' / '\r' in-place. BK's base64_encode() inserts a line
 * break every 72 chars and a trailing newline; the ConvoAI JSON payload
 * wants a flat, single-line base64 string. */
static void __strip_newlines_inplace(char *s)
{
    char *r = s;
    char *w = s;
    while (*r)
    {
        if (*r != '\n' && *r != '\r')
        {
            *w++ = *r;
        }
        r++;
    }
    *w = '\0';
}

static bk_err_t __send_image_base64(const char *peer_uid,
                                    const uint8_t *jpeg,
                                    size_t jpeg_len,
                                    const char *query_after_ack)
{
    bk_err_t ret = BK_FAIL;
    cJSON *root = NULL;
    char *payload = NULL;
    char uuid_buf[40] = {0};
    unsigned char *b64 = NULL;
    unsigned int b64_cap = 0;
    int b64_len = 0;
    int rval;
    size_t payload_len;
    uint32_t msg_id = 0;

    if (!peer_uid || peer_uid[0] == '\0')
    {
        LOGE("send_image_b64: peer_uid required\n");
        return BK_FAIL;
    }
    if (!jpeg || jpeg_len == 0)
    {
        LOGE("send_image_b64: jpeg buffer required\n");
        return BK_FAIL;
    }
    /* Guard the SDK-side limit up front so we never waste a base64 pass
     * (~30 KB) on a payload that the RTM channel would have to drop
     * anyway. The 22 KB cap leaves ~2 KB headroom for the surrounding
     * JSON / framing under the ~32 KB RTM ceiling. */
    if (jpeg_len > BK_AGORA_RTM_IMG_RAW_MAX_LEN)
    {
        LOGE("send_image_b64: jpeg too large (%u > %u bytes, base64 path)\n",
             (unsigned)jpeg_len, (unsigned)BK_AGORA_RTM_IMG_RAW_MAX_LEN);
        return BK_FAIL;
    }
    if (!s_rtm_login_success)
    {
        LOGE("send_image_b64: RTM not login yet, please start RTC first\n");
        return BK_FAIL;
    }

    b64_cap = base64_calc_encode_length((unsigned int)jpeg_len);
    b64 = (unsigned char *)psram_malloc(b64_cap);
    if (!b64)
    {
        LOGE("send_image_b64: psram_malloc(%u) OOM\n", (unsigned)b64_cap);
        goto __exit;
    }

    if (1 != base64_encode(jpeg, (int)jpeg_len, &b64_len, b64))
    {
        LOGE("send_image_b64: base64_encode failed\n");
        goto __exit;
    }
    /* BK's encoder writes a NUL after the final pad; b64_len excludes
     * it but includes any inline '\n' separators. Flatten in place
     * before handing it to cJSON. */
    __strip_newlines_inplace((char *)b64);

    os_snprintf(uuid_buf, sizeof(uuid_buf), "img_%08x", (unsigned)bk_rand());

    root = cJSON_CreateObject();
    if (!root)
    {
        LOGE("send_image_b64: cJSON_CreateObject OOM\n");
        goto __exit;
    }
    cJSON_AddStringToObject(root, "uuid", uuid_buf);
    cJSON_AddStringToObject(root, "image_base64", (const char *)b64);

    /* Release the intermediate base64 buffer eagerly: cJSON_AddString*
     * already strdup'd it. This shaves ~30 KB off the working set
     * while cJSON_PrintUnformatted assembles the final wire string. */
    psram_free(b64);
    b64 = NULL;

    payload = cJSON_PrintUnformatted(root);
    if (!payload)
    {
        LOGE("send_image_b64: cJSON_PrintUnformatted OOM\n");
        goto __exit;
    }

    payload_len = os_strlen(payload);
    if (payload_len > RTM_MSG_MAX_LEN)
    {
        LOGE("send_image_b64: payload too large (%u > %u)\n",
             (unsigned)payload_len, (unsigned)RTM_MSG_MAX_LEN);
        goto __exit;
    }

    s_rtm_msg_id++;
    msg_id = s_rtm_msg_id;
    if (BK_OK != __add_pending_image_query(msg_id, uuid_buf, peer_uid, query_after_ack))
    {
        goto __exit;
    }

    LOGI("send_image_b64 peer=%s msg_id=%u uuid=%s jpeg=%u base64=%u payload=%u\n",
         peer_uid, msg_id, uuid_buf,
         (unsigned)jpeg_len, (unsigned)b64_len, (unsigned)payload_len);

    rval = agora_rtc_send_rtm_data(peer_uid, payload, payload_len,
                                   msg_id, RTM_CUSTOM_TYPE_IMAGE_UPLOAD);
    if (rval < 0)
    {
        LOGE("agora_rtc_send_rtm_data failed: %d, %s\n", rval, agora_rtc_err_2_str(rval));
        __remove_pending_image_query(msg_id);
        goto __exit;
    }

    ret = BK_OK;

__exit:
    if (b64)
    {
        psram_free(b64);
    }
    if (payload)
    {
        cJSON_free(payload);
    }
    if (root)
    {
        cJSON_Delete(root);
    }
    return ret;
}

bk_err_t bk_agora_rtm_send_image_base64(const char *peer_uid,
                                        const uint8_t *jpeg, size_t jpeg_len)
{
    return __send_image_base64(peer_uid, jpeg, jpeg_len, NULL);
}

bk_err_t bk_agora_rtm_send_image_base64_with_query(const char *peer_uid,
                                                   const uint8_t *jpeg,
                                                   size_t jpeg_len,
                                                   const char *query_text)
{
    return __send_image_base64(peer_uid, jpeg, jpeg_len, query_text);
}

bk_err_t bk_agora_rtm_send_user_text(const char *peer_uid, const char *text)
{
    bk_err_t ret = BK_FAIL;
    cJSON *root = NULL;
    char *payload = NULL;
    int rval;
    size_t payload_len;

    if (!peer_uid || peer_uid[0] == '\0')
    {
        LOGE("send_text: peer_uid required\n");
        return BK_FAIL;
    }
    if (!text || text[0] == '\0')
    {
        LOGE("send_text: text required\n");
        return BK_FAIL;
    }
    if (!s_rtm_login_success)
    {
        LOGE("send_text: RTM not login yet, please start RTC first\n");
        return BK_FAIL;
    }

    /* ConvoAI custom-message contract for "user.transcription":
     *   priority=INTERRUPT  -> cut in even if the agent is mid-speech
     *   interruptable=true  -> let later user speech preempt this turn
     *   message=<text>      -> the "spoken" content fed to the LLM */
    root = cJSON_CreateObject();
    if (!root)
    {
        LOGE("send_text: cJSON_CreateObject OOM\n");
        goto __exit;
    }
    cJSON_AddStringToObject(root, "priority", "INTERRUPT");
    cJSON_AddBoolToObject(root, "interruptable", true);
    cJSON_AddStringToObject(root, "message", text);

    payload = cJSON_PrintUnformatted(root);
    if (!payload)
    {
        LOGE("send_text: cJSON_PrintUnformatted OOM\n");
        goto __exit;
    }

    payload_len = os_strlen(payload);
    if (payload_len > RTM_MSG_MAX_LEN)
    {
        LOGE("send_text: payload too large (%u > %u)\n",
             (unsigned)payload_len, (unsigned)RTM_MSG_MAX_LEN);
        goto __exit;
    }

    s_rtm_msg_id++;
    LOGI("send_text peer=%s msg_id=%u payload=%s\n", peer_uid, s_rtm_msg_id, payload);

    rval = agora_rtc_send_rtm_data(peer_uid, payload, payload_len,
                                   s_rtm_msg_id, RTM_CUSTOM_TYPE_USER_TRANSCRIPT);
    if (rval < 0)
    {
        LOGE("agora_rtc_send_rtm_data failed: %d, %s\n", rval, agora_rtc_err_2_str(rval));
        goto __exit;
    }

    ret = BK_OK;

__exit:
    if (payload)
    {
        cJSON_free(payload);
    }
    if (root)
    {
        cJSON_Delete(root);
    }
    return ret;
}

#endif /* CONFIG_AGORA_RTC_USE_STRING_UID */
