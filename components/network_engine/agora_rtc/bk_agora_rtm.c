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
#define RTM_CUSTOM_TYPE_MESSAGE_INTERRUPT "message.interrupt"

/* ConvoAI custom type: wraps a text "as if" the user just spoke it.
 * The server forwards the text to the LLM, replacing the ASR-from-mic
 * input for one turn. */
#define RTM_CUSTOM_TYPE_USER_TRANSCRIPT  "user.transcription"

/* Hard limit imposed by the SDK on a single RTM payload. */
#define RTM_MSG_MAX_LEN                 (31 * 1024)

#define RTM_PENDING_IMAGE_QUERY_MAX     8

/* Age-out threshold for a pending image query. If neither the RTM
 * UNREACHABLE/TIMEOUT ack nor the server-side message.info (uuid dispatch)
 * ever arrives (e.g. the peer/agent is abnormal), the entry would linger
 * forever, eventually filling the pool and blocking new image queries. We
 * reclaim any entry older than this on the next enqueue attempt. */
#define RTM_PENDING_IMAGE_QUERY_AGE_MS  10000

/* Maximum length of the image uuid we generate (img_<8-hex>\0 = 13).
 * Bump for safety; ConvoAI echos it back verbatim in message.info. */
#define RTM_IMAGE_UUID_MAX_LEN          40

typedef struct {
    bool used;
    uint32_t msg_id;
    uint32_t create_ms;
    char uuid[RTM_IMAGE_UUID_MAX_LEN];
    char *peer_uid;
    char *query_text;
    /* Set when the user cancelled (resume-live) while this image was still
     * in flight (uploaded but not yet ingested). We still send the paired
     * query text once the image is ingested -- so the image<->query turn is
     * well-formed and never dangles as a text-less image in the agent's
     * multi-turn context -- and then immediately barge-in to suppress the
     * spoken answer. See bk_agora_rtm_interrupt() / _on_image_uploaded(). */
    bool cancel_after_query;
} rtm_pending_image_query_t;

static volatile bool s_rtm_login_success = false;
static uint32_t      s_rtm_msg_id = 0;

/* Pending image-query pool. Allocated from PSRAM at RTM init
 * (bk_agora_rtm_start) and released at RTM deinit (bk_agora_rtm_stop);
 * NULL when RTM is not running. */
static rtm_pending_image_query_t *s_pending_image_queries = NULL;

/* Raw barge-in send (message.interrupt). Split out from the public
 * bk_agora_rtm_interrupt() so the deferred-cancel path in
 * bk_agora_rtm_on_image_uploaded() can fire it directly. */
static bk_err_t __rtm_send_interrupt(const char *peer_uid);

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
    if (!s_pending_image_queries)
    {
        return;
    }
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
    if (!s_pending_image_queries)
    {
        return;
    }
    for (size_t i = 0; i < RTM_PENDING_IMAGE_QUERY_MAX; i++)
    {
        if (s_pending_image_queries[i].used)
        {
            __free_pending_image_query(&s_pending_image_queries[i]);
        }
    }
}

/* Mark every in-flight pending image query (uploaded, awaiting the server's
 * message.info ingest signal) as "cancelled": its paired query text will
 * still be sent to close the turn cleanly, followed by a barge-in. Returns
 * true if at least one such entry exists -- in which case the caller must
 * NOT send the interrupt now (it is deferred to _on_image_uploaded()). */
static bool __mark_pending_image_queries_cancel(void)
{
    bool any = false;

    if (!s_pending_image_queries)
    {
        return false;
    }
    for (size_t i = 0; i < RTM_PENDING_IMAGE_QUERY_MAX; i++)
    {
        if (s_pending_image_queries[i].used)
        {
            s_pending_image_queries[i].cancel_after_query = true;
            any = true;
        }
    }
    return any;
}

/* Reclaim entries that have out-lived RTM_PENDING_IMAGE_QUERY_AGE_MS. This
 * is the safety net for the "RTM ack=RECEIVED but server never emits
 * message.info" case (peer/agent abnormal), which otherwise never frees
 * the slot. Swept lazily on each enqueue so a stuck entry can never
 * permanently occupy the pool. */
static void __age_out_pending_image_queries(void)
{
    if (!s_pending_image_queries)
    {
        return;
    }

    uint32_t now = (uint32_t)rtos_get_time();
    for (size_t i = 0; i < RTM_PENDING_IMAGE_QUERY_MAX; i++)
    {
        if (s_pending_image_queries[i].used &&
            (now - s_pending_image_queries[i].create_ms) >= RTM_PENDING_IMAGE_QUERY_AGE_MS)
        {
            LOGW("pending image query age-out msg_id=%u uuid=%s age=%ums\n",
                 s_pending_image_queries[i].msg_id, s_pending_image_queries[i].uuid,
                 (unsigned)(now - s_pending_image_queries[i].create_ms));
            __free_pending_image_query(&s_pending_image_queries[i]);
        }
    }
}

static bk_err_t __pending_image_queries_alloc(void)
{
    if (s_pending_image_queries)
    {
        /* Already allocated (e.g. re-login without a prior stop): just
         * reset to a clean state, keeping the same PSRAM block. */
        __clear_pending_image_queries();
        return BK_OK;
    }

    size_t bytes = sizeof(rtm_pending_image_query_t) * RTM_PENDING_IMAGE_QUERY_MAX;
    s_pending_image_queries = (rtm_pending_image_query_t *)psram_malloc(bytes);
    if (!s_pending_image_queries)
    {
        LOGE("pending image query pool psram_malloc(%u) OOM\n", (unsigned)bytes);
        return BK_FAIL;
    }
    os_memset(s_pending_image_queries, 0, bytes);
    return BK_OK;
}

static void __pending_image_queries_free(void)
{
    if (!s_pending_image_queries)
    {
        return;
    }
    __clear_pending_image_queries();
    psram_free(s_pending_image_queries);
    s_pending_image_queries = NULL;
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
    if (!s_pending_image_queries)
    {
        LOGE("pending image query pool not ready\n");
        return BK_FAIL;
    }

    /* Reclaim stale entries first so an abnormal peer can never keep the
     * pool permanently full. */
    __age_out_pending_image_queries();

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
    slot->create_ms = (uint32_t)rtos_get_time();
    os_strncpy(slot->uuid, uuid, sizeof(slot->uuid) - 1);
    slot->uuid[sizeof(slot->uuid) - 1] = '\0';
    LOGI("pending image query add msg_id=%u uuid=%s peer=%s\n", msg_id, slot->uuid, peer_uid);
    return BK_OK;
}

static rtm_pending_image_query_t *__find_pending_image_query(uint32_t msg_id)
{
    if (!s_pending_image_queries)
    {
        return NULL;
    }
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
    if (!s_pending_image_queries)
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
    bool cancel_after_query = pending->cancel_after_query;

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

    /* Cancelled while in flight: the query above closed the image turn so it
     * cannot merge with the next capture's image, and this barge-in (issued
     * right after the query, before the agent starts TTS) keeps the discarded
     * shot from being spoken. */
    if (cancel_after_query && peer_uid)
    {
        LOGI("cancelled image uuid=%s: paired query sent, barge-in now\n", uuid);
        (void)__rtm_send_interrupt(peer_uid);
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

    /* Allocate the pending image-query pool in PSRAM before login so it is
     * ready for the very first image.upload. */
    if (BK_OK != __pending_image_queries_alloc())
    {
        return BK_FAIL;
    }

    LOGI("rtm login: uid=%s token=%s\n", self_uid, token ? token : "(null)");

    rval = agora_rtc_login_rtm(self_uid, token, &s_rtm_handler);
    if (rval < 0)
    {
        LOGE("agora_rtc_login_rtm failed: %d, %s\n", rval, agora_rtc_err_2_str(rval));
        __pending_image_queries_free();
        return BK_FAIL;
    }

    s_rtm_msg_id = 0;
    return BK_OK;
}

bk_err_t bk_agora_rtm_stop(void)
{
    int rval;

    if (!s_rtm_login_success)
    {
        LOGD("rtm not login, skip logout\n");
        /* Release the pool even if login never succeeded, so a failed
         * start does not leak the PSRAM block. */
        __pending_image_queries_free();
        return BK_OK;
    }

    rval = agora_rtc_logout_rtm();
    if (rval < 0)
    {
        LOGE("agora_rtc_logout_rtm failed: %d, %s\n", rval, agora_rtc_err_2_str(rval));
    }

    s_rtm_login_success = false;
    __pending_image_queries_free();
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

static bk_err_t __rtm_send_interrupt(const char *peer_uid)
{
    static const char payload[] = "{\"customType\":\"message.interrupt\"}";
    int rval;

    if (!peer_uid || peer_uid[0] == '\0')
    {
        LOGE("interrupt: peer_uid required\n");
        return BK_FAIL;
    }
    if (!s_rtm_login_success)
    {
        LOGE("interrupt: RTM not login yet\n");
        return BK_FAIL;
    }

    s_rtm_msg_id++;
    LOGI("interrupt peer=%s msg_id=%u\n", peer_uid, s_rtm_msg_id);
    rval = agora_rtc_send_rtm_data(peer_uid, payload, sizeof(payload) - 1,
                                   s_rtm_msg_id, RTM_CUSTOM_TYPE_MESSAGE_INTERRUPT);
    if (rval < 0)
    {
        LOGE("agora_rtc_send_rtm_data interrupt failed: %d, %s\n",
             rval, agora_rtc_err_2_str(rval));
        return BK_FAIL;
    }

    return BK_OK;
}

bk_err_t bk_agora_rtm_interrupt(const char *peer_uid)
{
    if (!peer_uid || peer_uid[0] == '\0')
    {
        LOGE("interrupt: peer_uid required\n");
        return BK_FAIL;
    }
    if (!s_rtm_login_success)
    {
        LOGE("interrupt: RTM not login yet\n");
        return BK_FAIL;
    }

    /* If an image is still in flight (uploaded but its message.info ingest
     * signal has not come back yet), do NOT drop its follow-up query. Dropping
     * it would leave a text-less image dangling in the agent's multi-turn
     * context, and the NEXT capture's "describe this image" query would then
     * describe that stale image together with the new one -- exactly the
     * "AI analyzes the previously cancelled photo too" bug.
     *
     * Instead we let the image<->query pair complete (turn stays well-formed)
     * and defer the barge-in to bk_agora_rtm_on_image_uploaded(), which fires
     * it right after the query goes out. Only when nothing is in flight do we
     * interrupt immediately (the common "stop the agent mid-speech" case). */
    if (__mark_pending_image_queries_cancel())
    {
        LOGI("interrupt deferred: image in flight, barge-in after paired query\n");
        return BK_OK;
    }

    return __rtm_send_interrupt(peer_uid);
}

#endif /* CONFIG_AGORA_RTC_USE_STRING_UID */
