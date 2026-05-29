/*************************************************************
 *
 * This is a part of the Agora Media Framework Library.
 * Copyright (C) 2025 Agora IO
 * All rights reserved.
 *
 *************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include "bk_rtos_debug.h"
#include "agora_rtc_engine.h"
#include "agora_config.h"
#include <components/log.h>
#include "cJSON.h"
#include "base_64.h"
#if CONFIG_AGORA_RTC_USE_STRING_UID
#include "bk_agora_rtm.h"
#endif
#if CONFIG_APP_EVT
#include "app_event.h"
#include "audio_engine.h"
#endif
#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#define TAG "agora_rtc_engine"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)

static agora_rtc_t agora_rtc = {0};

/* ============================= Agent Data-Stream Parser =============================
 *
 * The Agora ConvoAI agent publishes runtime status frames over the channel
 * data-stream (see on_stream_message). Each logical JSON message is base64
 * encoded and may be split across up to RTC_DATASTREAM_MAX_SPLIT physical
 * packets that share the same msg_id but increment cur_index. Format:
 *
 *     <msg_id>|<cur_index>|<total_num>|<base64_payload>
 *
 * We re-assemble the fragments off the RTC SDK callback (which must stay
 * non-blocking), base64-decode the payload, parse the JSON and translate
 * "message.state" frames into agora_rtc_agent_state_e. The latest state is
 * cached on agora_rtc_t so callers (UI / app event bus) can poll it via
 * __agora_rtc_get_agent_state().
 *
 * The agora_rtc_msg_process.* demo files in this directory remain as the
 * original reference.
 * ===================================================================== */

#define RTC_DATASTREAM_MAX_SPLIT      4
#define RTC_DATASTREAM_MAX_LEN        (1024 * 8)
#define RTC_DATASTREAM_QUEUE_DEPTH    4
#define RTC_DATASTREAM_TASK_PRIO      4
#define RTC_DATASTREAM_TASK_STACK     (4 * 1024)
#define RTC_DATASTREAM_TASK_NAME      "agora_ds_parse"

typedef struct
{
    char *payload;     /* psram-allocated, NUL-terminated, owned by parser */
} agora_ds_msg_t;

static beken_queue_t       s_ds_queue       = NULL;
static beken_thread_t      s_ds_thread      = NULL;
static volatile bool       s_ds_thread_quit = false;

static agora_rtc_agent_state_e __agora_rtc_state_from_str(const char *s)
{
    if (!s)
    {
        return AGORA_RTC_AGENT_STATE_UNKNOWN;
    }
    if (os_strcmp(s, "listening") == 0)
    {
        return AGORA_RTC_AGENT_STATE_LISTENING;
    }
    if (os_strcmp(s, "thinking") == 0)
    {
        return AGORA_RTC_AGENT_STATE_THINKING;
    }
    if (os_strcmp(s, "speaking") == 0)
    {
        return AGORA_RTC_AGENT_STATE_SPEAKING;
    }
    if (os_strcmp(s, "silent") == 0)
    {
        return AGORA_RTC_AGENT_STATE_SILENT;
    }
    return AGORA_RTC_AGENT_STATE_UNKNOWN;
}

const char *__agora_rtc_agent_state_to_str(agora_rtc_agent_state_e state)
{
    switch (state)
    {
        case AGORA_RTC_AGENT_STATE_LISTENING: return "listening";
        case AGORA_RTC_AGENT_STATE_THINKING:  return "thinking";
        case AGORA_RTC_AGENT_STATE_SPEAKING:  return "speaking";
        case AGORA_RTC_AGENT_STATE_SILENT:    return "silent";
        case AGORA_RTC_AGENT_STATE_UNKNOWN:
        default:                              return "unknown";
    }
}

agora_rtc_agent_state_e __agora_rtc_get_agent_state(void)
{
    return agora_rtc.agent_state;
}

#if CONFIG_APP_EVT
/* Hybrid policy: route only the two server states where the RTC channel
 * is more authoritative than the local audio_engine inference:
 *
 *   - "thinking" : only the server knows the LLM is busy; local can at
 *                  best guess it from "VAD STOP & spk idle >= 1s".
 *   - "speaking" : arrives ~500ms before the first downlink PCM frame
 *                  reaches the local DAC, so it lights up SPEAKING and
 *                  the orange EQ early.
 *
 * "listening" and "silent" from the server lag the local VAD / spk-linger
 * by 0.9-1.9s and would otherwise overwrite the live UI state with a
 * stale value. We drop them on purpose and let the local sources own
 * those transitions.
 */
static void __agora_rtc_emit_app_event_for_state(agora_rtc_agent_state_e state)
{
    switch (state)
    {
        case AGORA_RTC_AGENT_STATE_THINKING:
            audio_engine_hint_thinking();
            break;
        case AGORA_RTC_AGENT_STATE_SPEAKING:
            audio_engine_hint_speaking_start();
            break;
        case AGORA_RTC_AGENT_STATE_LISTENING:
        case AGORA_RTC_AGENT_STATE_SILENT:
            /* Intentionally dropped -- local state machine wins. */
            break;
        default:
            break;
    }
}
#endif

static void __agora_rtc_notify_agent_state(agora_rtc_t *rtc, agora_rtc_agent_state_e state)
{
    if (state == AGORA_RTC_AGENT_STATE_UNKNOWN)
    {
        return;
    }
    if (rtc->agent_state == state)
    {
        return; /* idempotent: skip duplicate state updates */
    }

    rtc->agent_state = state;
    LOGI("agent state -> %s\n", __agora_rtc_agent_state_to_str(state));

#if CONFIG_APP_EVT
    __agora_rtc_emit_app_event_for_state(state);
#endif

    if (rtc->user_message_callback)
    {
        agora_rtc_msg_t msg = {
            .code = AGORA_RTC_MSG_AGENT_STATE_CHANGED,
        };
        msg.data.agent_state = state;
        rtc->user_message_callback(&msg);
    }
}

static void __agora_rtc_dispatch_decoded_msg(agora_rtc_t *rtc, const char *json_str)
{
    cJSON *root = NULL;
    cJSON *object = NULL;

    if (!json_str)
    {
        return;
    }

    root = cJSON_Parse(json_str);
    if (!root)
    {
        LOGW("agent msg: not JSON: %s\n", json_str);
        return;
    }

    object = cJSON_GetObjectItem(root, "object");
    if (!object || !cJSON_IsString(object))
    {
        LOGW("agent msg: missing 'object' field\n");
        goto cleanup;
    }

    if (os_strcmp(object->valuestring, "message.state") == 0)
    {
        cJSON *state = cJSON_GetObjectItem(root, "state");
        if (!state || !cJSON_IsString(state))
        {
            LOGW("agent msg: 'message.state' missing 'state' string\n");
            goto cleanup;
        }
        __agora_rtc_notify_agent_state(rtc, __agora_rtc_state_from_str(state->valuestring));
    }
    else if (os_strcmp(object->valuestring, "message.user") == 0)
    {
        /* Reserved for future user-message routing (subtitles, etc.). */
        LOGD("agent msg: message.user (ignored)\n");
    }
    else if (os_strcmp(object->valuestring, "message.info") == 0)
    {
        /* ConvoAI emits message.info / module=context whenever a new
         * piece of context (image, file, ...) has actually been ingested
         * into the LLM working memory. For the image-upload flow that is
         * the authoritative "the picture is now visible to the model"
         * signal -- which is what gates the follow-up trigger text in
         * bk_agora_rtm.c. The "message" field is itself a JSON string
         * (e.g. {"uuid":"img_xxxxxxxx","resource_type":"picture",...}). */
        cJSON *module = cJSON_GetObjectItem(root, "module");
        cJSON *inner_msg = cJSON_GetObjectItem(root, "message");
        if (!module || !cJSON_IsString(module) ||
            os_strcmp(module->valuestring, "context") != 0 ||
            !inner_msg || !cJSON_IsString(inner_msg))
        {
            LOGD("agent msg: message.info (no context payload)\n");
            goto cleanup;
        }

        cJSON *inner = cJSON_Parse(inner_msg->valuestring);
        if (!inner)
        {
            LOGW("agent msg: message.info inner not JSON: %s\n", inner_msg->valuestring);
            goto cleanup;
        }

        cJSON *res_type = cJSON_GetObjectItem(inner, "resource_type");
        cJSON *uuid     = cJSON_GetObjectItem(inner, "uuid");
        if (res_type && cJSON_IsString(res_type) &&
            os_strcmp(res_type->valuestring, "picture") == 0 &&
            uuid && cJSON_IsString(uuid))
        {
            LOGI("agent msg: image ingested uuid=%s\n", uuid->valuestring);
#if CONFIG_AGORA_RTC_USE_STRING_UID
            bk_agora_rtm_on_image_uploaded(uuid->valuestring);
#endif
        }
        else
        {
            LOGD("agent msg: message.info ctx (non-picture or no uuid)\n");
        }
        cJSON_Delete(inner);
    }
    else
    {
        LOGD("agent msg: unhandled object='%s'\n", object->valuestring);
    }

cleanup:
    cJSON_Delete(root);
}

static void __agora_rtc_datastream_parse_task(beken_thread_arg_t arg)
{
    agora_rtc_t *rtc          = __get_rtc_instance();
    char        *save_ptr     = NULL;
    char        *store_str    = NULL;
    char        *decode_str   = NULL;
    char        *last_msg_id  = NULL;
    int          remaining_len;
    int          decode_len   = 0;
    bk_err_t     ret;
    agora_ds_msg_t msg = {0};

    (void)arg;

    while (!s_ds_thread_quit)
    {
        ret = rtos_pop_from_queue(&s_ds_queue, &msg, BEKEN_WAIT_FOREVER);
        if (ret != BK_OK || s_ds_thread_quit)
        {
            break;
        }
        /* Sentinel pushed by deinit to wake us up so we can exit. */
        if (!msg.payload)
        {
            continue;
        }

        const char *delim         = "|";
        char       *msg_id        = strtok_r(msg.payload, delim, &save_ptr);
        char       *cur_index_str = strtok_r(NULL, delim, &save_ptr);
        char       *total_num_str = strtok_r(NULL, delim, &save_ptr);
        char       *msg_payload   = strtok_r(NULL, delim, &save_ptr);

        if (!msg_id || !cur_index_str || !total_num_str || !msg_payload)
        {
            LOGW("agent ds: malformed packet, skip\n");
            goto frag_reset;
        }

        uint8_t cur_index = (uint8_t)os_strtoul(cur_index_str, NULL, 10);
        uint8_t total_num = (uint8_t)os_strtoul(total_num_str, NULL, 10);

        if (total_num == 0 || total_num > RTC_DATASTREAM_MAX_SPLIT ||
            cur_index < 1 || cur_index > total_num)
        {
            LOGW("agent ds: invalid index/total %u/%u\n", cur_index, total_num);
            goto frag_reset;
        }

        /* New message id -> drop any in-progress reassembly. */
        if (last_msg_id && os_strcmp(msg_id, last_msg_id) != 0)
        {
            os_free(last_msg_id);
            last_msg_id = NULL;
            if (store_str)
            {
                psram_free(store_str);
                store_str = NULL;
            }
            if (decode_str)
            {
                psram_free(decode_str);
                decode_str = NULL;
            }
        }
        if (!last_msg_id)
        {
            last_msg_id = os_strdup(msg_id);
            if (!last_msg_id)
            {
                LOGE("agent ds: OOM (msg_id)\n");
                goto frag_reset;
            }
        }

        if (!store_str)
        {
            store_str = psram_zalloc(RTC_DATASTREAM_MAX_LEN + 1);
            if (!store_str)
            {
                LOGE("agent ds: OOM (store)\n");
                goto frag_reset;
            }
        }

        remaining_len = RTC_DATASTREAM_MAX_LEN - (int)os_strlen(store_str);
        if (remaining_len <= 0)
        {
            LOGW("agent ds: store buffer full\n");
            goto frag_reset;
        }
        os_snprintf(store_str + os_strlen(store_str), remaining_len, "%s", msg_payload);

        if (cur_index == total_num)
        {
            int store_len = (int)os_strlen(store_str);
            int est_decode_len = (store_len * 3) / 4 + 4;
            decode_str = psram_zalloc(est_decode_len + 1);
            if (!decode_str)
            {
                LOGE("agent ds: OOM (decode)\n");
                goto frag_reset;
            }
            base64_decode((unsigned char *)store_str, store_len,
                          &decode_len, (unsigned char *)decode_str);
            decode_str[decode_len] = '\0';
            LOGD("agent ds: decoded %d bytes\n", decode_len);
            __agora_rtc_dispatch_decoded_msg(rtc, decode_str);
            goto frag_reset;
        }

        psram_free(msg.payload);
        msg.payload = NULL;
        continue;

frag_reset:
        if (msg.payload)
        {
            psram_free(msg.payload);
            msg.payload = NULL;
        }
        if (last_msg_id)
        {
            os_free(last_msg_id);
            last_msg_id = NULL;
        }
        if (store_str)
        {
            psram_free(store_str);
            store_str = NULL;
        }
        if (decode_str)
        {
            psram_free(decode_str);
            decode_str = NULL;
        }
    }

    /* Drain remaining items so we don't leak on shutdown. */
    while (rtos_pop_from_queue(&s_ds_queue, &msg, BEKEN_NO_WAIT) == BK_OK)
    {
        if (msg.payload)
        {
            psram_free(msg.payload);
        }
    }
    if (last_msg_id) os_free(last_msg_id);
    if (store_str)   psram_free(store_str);
    if (decode_str)  psram_free(decode_str);

    s_ds_thread = NULL;
    rtos_delete_thread(NULL);
}

static int __agora_rtc_msg_process_init(void)
{
    bk_err_t ret;

    if (s_ds_thread)
    {
        return BK_OK; /* idempotent */
    }

    s_ds_thread_quit = false;

    ret = rtos_init_queue(&s_ds_queue, "agora_ds_q",
                          sizeof(agora_ds_msg_t), RTC_DATASTREAM_QUEUE_DEPTH);
    if (ret != BK_OK)
    {
        LOGE("agent ds: queue init fail: %d\n", ret);
        s_ds_queue = NULL;
        return ret;
    }

#if CONFIG_PSRAM_AS_SYS_MEMORY
    ret = rtos_create_psram_thread(&s_ds_thread,
                                   RTC_DATASTREAM_TASK_PRIO,
                                   RTC_DATASTREAM_TASK_NAME,
                                   (beken_thread_function_t)__agora_rtc_datastream_parse_task,
                                   RTC_DATASTREAM_TASK_STACK,
                                   (beken_thread_arg_t)0);
#else
    ret = rtos_create_thread(&s_ds_thread,
                             RTC_DATASTREAM_TASK_PRIO,
                             RTC_DATASTREAM_TASK_NAME,
                             (beken_thread_function_t)__agora_rtc_datastream_parse_task,
                             RTC_DATASTREAM_TASK_STACK,
                             (beken_thread_arg_t)0);
#endif
    if (ret != BK_OK)
    {
        LOGE("agent ds: thread create fail: %d\n", ret);
        rtos_deinit_queue(&s_ds_queue);
        s_ds_queue = NULL;
        s_ds_thread = NULL;
        return ret;
    }

    LOGI("agent ds parser started\n");
    return BK_OK;
}

static void __agora_rtc_msg_process_deinit(void)
{
    if (!s_ds_queue && !s_ds_thread)
    {
        return;
    }

    s_ds_thread_quit = true;

    /* Push a sentinel so the worker wakes from BEKEN_WAIT_FOREVER and
     * notices the quit flag. */
    if (s_ds_queue)
    {
        agora_ds_msg_t sentinel = { .payload = NULL };
        rtos_push_to_queue(&s_ds_queue, &sentinel, BEKEN_NO_WAIT);
    }

    /* Best-effort wait for the worker to exit (it self-deletes). */
    for (int i = 0; i < 50 && s_ds_thread; i++)
    {
        rtos_delay_milliseconds(10);
    }

    if (s_ds_queue)
    {
        rtos_deinit_queue(&s_ds_queue);
        s_ds_queue = NULL;
    }

    LOGI("agent ds parser stopped\n");
}

static void __agora_rtc_msg_process_submit(const char *data, size_t length)
{
    if (!s_ds_queue || !data || length == 0)
    {
        return;
    }
    /* Cap at the max single-fragment payload to keep memory bounded. */
    if (length > RTC_DATASTREAM_MAX_LEN)
    {
        LOGW("agent ds: oversize fragment %u, drop\n", (unsigned)length);
        return;
    }

    agora_ds_msg_t msg = {0};
    msg.payload = (char *)psram_malloc(length + 1);
    if (!msg.payload)
    {
        LOGE("agent ds: OOM submit (%u bytes)\n", (unsigned)length);
        return;
    }
    os_memcpy(msg.payload, data, length);
    msg.payload[length] = '\0';

    if (rtos_push_to_queue(&s_ds_queue, &msg, BEKEN_NO_WAIT) != BK_OK)
    {
        LOGW("agent ds: queue full, drop\n");
        psram_free(msg.payload);
    }
}

/* ============================= Private Functions ============================= */

agora_rtc_t *__get_rtc_instance(void)
{
    return &agora_rtc;
}

static void __send_message_2_user(agora_rtc_t *rtc, agora_rtc_msg_t *msg)
{
    if (rtc->user_message_callback)
    {
        rtc->user_message_callback(msg);
    }
}

static void __rtc_started(agora_rtc_t *rtc)
{
    rtc->state = AGORA_RTC_STATE_WORKING;
}

static bool __is_rtc_started(agora_rtc_t *rtc)
{
    if (rtc->state == AGORA_RTC_STATE_WORKING)
    {
        return true;
    }
    else
    {
        return false;
    }
}

static bool __get_rtc_status(agora_rtc_t *rtc)
{
    return rtc->state;
}


static void __rtc_stopped(agora_rtc_t *rtc)
{
    rtc->state = AGORA_RTC_STATE_IDLE;
}

static void __deep_copy_items_destroy(agora_rtc_t *rtc);

static void __key_frame_req_msg(agora_rtc_t *rtc)
{
    agora_rtc_msg_t msg;
    msg.code = AGORA_RTC_MSG_KEY_FRAME_REQUEST;
    __send_message_2_user(rtc, &msg);
}

static void __change_encode_bitrate_req_msg(agora_rtc_t *rtc, uint32_t target_bps)
{
    agora_rtc_msg_t msg;
    msg.code =  AGORA_RTC_MSG_BWE_TARGET_BITRATE_UPDATE;
    msg.data.bwe.target_bitrate = target_bps;
    __send_message_2_user(rtc, &msg);
}

static void __register_message_router(agora_rtc_t *rtc, agora_rtc_msg_notify_cb message_callback)
{
    rtc->user_message_callback = message_callback;
}

static void __on_join_channel_success(connection_id_t conn_id, uint32_t uid, int elapsed_ms)
{
    LOGD("conn_id: %d, uid: %d, elapsed_ms: %d \n", conn_id, uid, elapsed_ms);
    agora_rtc_t *rtc = __get_rtc_instance();

    rtc->b_channel_joined = true;
    agora_rtc_msg_t msg = { .code = AGORA_RTC_MSG_JOIN_CHANNEL_SUCCESS };
    __send_message_2_user(rtc, &msg);

    __rtc_started(rtc);
}

static void __error_msg(agora_rtc_t *rtc, agora_rtc_msg_e err)
{
    agora_rtc_msg_t msg;
    msg.code = err;
    __send_message_2_user(rtc, &msg);
}

/* ============================= Event Callbacks ============================= */

static void __on_error(connection_id_t conn_id, int code, const char *msg)
{
    LOGE("Agora error: conn_id=%d, code=%d, msg=%s\n", conn_id, code, msg ? msg : "null");
    
    agora_rtc_t *rtc = __get_rtc_instance();
    switch (code)
    {
        case ERR_INVALID_APP_ID:
            __error_msg(rtc, AGORA_RTC_MSG_INVALID_APP_ID);
            break;
        case ERR_INVALID_CHANNEL_NAME:
            __error_msg(rtc, AGORA_RTC_MSG_INVALID_CHANNEL_NAME);
            break;
        case ERR_INVALID_TOKEN:
            __error_msg(rtc, AGORA_RTC_MSG_INVALID_TOKEN);
            break;
        case ERR_TOKEN_EXPIRED:
            __error_msg(rtc, AGORA_RTC_MSG_TOKEN_EXPIRED);
            break;
        default:
            LOGW("Unknown error code: %d\n", code);
            break;
    }
}

static void __on_user_joined(connection_id_t conn_id, uint32_t uid, int elapsed_ms)
{
    LOGD("conn_id: %d, uid: %d, elapsed_ms: %d \n", conn_id, uid, elapsed_ms);
    agora_rtc_t *rtc = __get_rtc_instance();

    rtc->b_user_joined = true;
    agora_rtc_msg_t msg =
    {
        .code     = AGORA_RTC_MSG_USER_JOINED,
        .data.uid = uid
    };
    __send_message_2_user(rtc, &msg);
}

static void __on_user_offline(connection_id_t conn_id, uint32_t uid, int reason)
{
    LOGD("conn_id: %d, uid: %d, reason: %d \n", conn_id, uid, reason);
    agora_rtc_t *rtc = __get_rtc_instance();

    rtc->b_user_joined = false;
    agora_rtc_msg_t msg =
    {
        .code     = AGORA_RTC_MSG_USER_OFFLINE,
        .data.uid = uid
    };
    __send_message_2_user(rtc, &msg);
}

#if CONFIG_AGORA_RTC_USE_STRING_UID
/* String-uid variants: the SDK still delivers the numeric uid alongside
 * the user_account, so we keep the upstream message protocol (uint32_t
 * uid) unchanged and just log the human-readable account for tracing. */
static void __on_user_joined_with_user_account(connection_id_t conn_id, const user_info_t *user, int elapsed_ms)
{
    if (!user)
    {
        return;
    }
    LOGD("conn_id: %d, uid: %u, user_account: %s, elapsed_ms: %d \n",
         conn_id, user->uid, user->user_account, elapsed_ms);

    agora_rtc_t *rtc = __get_rtc_instance();
    rtc->b_user_joined = true;
    agora_rtc_msg_t msg =
    {
        .code     = AGORA_RTC_MSG_USER_JOINED,
        .data.uid = user->uid
    };
    __send_message_2_user(rtc, &msg);
}

static void __on_user_offline_with_user_account(connection_id_t conn_id, const user_info_t *user, int reason)
{
    if (!user)
    {
        return;
    }
    LOGD("conn_id: %d, uid: %u, user_account: %s, reason: %d \n",
         conn_id, user->uid, user->user_account, reason);

    agora_rtc_t *rtc = __get_rtc_instance();
    rtc->b_user_joined = false;
    agora_rtc_msg_t msg =
    {
        .code     = AGORA_RTC_MSG_USER_OFFLINE,
        .data.uid = user->uid
    };
    __send_message_2_user(rtc, &msg);
}

static void __on_user_info_updated(connection_id_t conn_id, const user_info_t *user)
{
    if (!user)
    {
        return;
    }
    LOGI("conn_id: %d, user_info updated: uid=%u user_account=%s \n",
         conn_id, user->uid, user->user_account);
}
#endif /* CONFIG_AGORA_RTC_USE_STRING_UID */

static void __on_key_frame_gen_req(connection_id_t conn_id, uint32_t uid, video_stream_type_e stream_type)
{
    LOGD("Key frame request: conn_id=%d, uid=%u, stream_type=%d\n", conn_id, uid, stream_type);
    
    agora_rtc_t *rtc = __get_rtc_instance();
    __key_frame_req_msg(rtc);
}

static void __on_audio_data(connection_id_t conn_id, uint32_t uid, uint16_t sent_ts,
                            const void *data_ptr, size_t data_len, const audio_frame_info_t *info_ptr)
{
    agora_rtc_t *rtc = __get_rtc_instance();
    if (!__is_rtc_started(rtc) || !rtc->b_channel_joined || !rtc->b_user_joined)
    {
        return;
    }

    if (rtc->audio_rx_data_handle)
    {
        rtc->audio_rx_data_handle((unsigned char *)data_ptr, data_len, info_ptr);
    }
}

static void __on_video_data(connection_id_t conn_id, uint32_t uid, uint16_t sent_ts, 
                            const void *data_ptr, size_t data_len, const video_frame_info_t *info_ptr)
{
    agora_rtc_t *rtc = __get_rtc_instance();
    if (!__is_rtc_started(rtc) || !rtc->b_channel_joined || !rtc->b_user_joined)
    {
        LOGI("Wrong rtc state, rtc_started: %s, channel_joined: %s, user_joined: %s \n",
            __is_rtc_started(rtc) ? "true" : "false", rtc->b_channel_joined ? "true" : "false", rtc->b_user_joined ? "true" : "false");
        return;
    }
    
    LOGD("ch=%s, uid=%d sent_ts=%u codec=%u, data_ptr=%p, data_len=%d",
        rtc->agora_rtc_option.p_channel_name, (int)uid, sent_ts, info_ptr->data_type, data_ptr, (int)data_len);

    if (rtc->video_rx_data_handle)
    {
        rtc->video_rx_data_handle((const uint8_t *)data_ptr, data_len, info_ptr);
    }
    else
    {
        LOGD("aud_rx_data_handle is NULL \n");
    }
}

static void __on_user_mute_audio(connection_id_t conn_id, uint32_t uid, bool muted)
{
    LOGI("User mute audio: conn_id=%u, uid=%u, muted=%d\n", conn_id, uid, muted);  // Disabled: event log
}

static void __on_user_mute_video(connection_id_t conn_id, uint32_t uid, bool muted)
{
     LOGI("User mute video: conn_id=%u, uid=%u, muted=%d\n", conn_id, uid, muted);  // Disabled: event log
}

static void __on_target_bitrate_changed(connection_id_t conn_id, uint32_t target_bps)
{
    agora_rtc_t *rtc = __get_rtc_instance();
    if (!__is_rtc_started(rtc))
    {
        // LOGI("Pipeline not started. Skip bitrate changed process\n");  // Disabled: too verbose
        return;
    }
    
    LOGD("Target bitrate changed: conn_id=%d, from %u to %u bps\n", 
         conn_id, rtc->target_bitrate, target_bps);
    
    rtc->target_bitrate = target_bps;
    __change_encode_bitrate_req_msg(rtc, target_bps);
}

static void __on_connection_lost(connection_id_t conn_id)
{
    LOGE("Connection lost: conn_id=%d\n", conn_id);
    
    agora_rtc_t *rtc = __get_rtc_instance();
    rtc->b_channel_joined = false;
    
    agora_rtc_msg_t msg = {
        .code = AGORA_RTC_MSG_CONNECTION_LOST
    };
    __send_message_2_user(rtc, &msg);
}

static void __on_rejoin_channel_success(connection_id_t conn_id, uint32_t uid, int elapsed_ms)
{
    LOGI("Rejoin channel success: conn_id=%d, uid=%u, elapsed_ms=%d\n", conn_id, uid, elapsed_ms);
    
    agora_rtc_t *rtc = __get_rtc_instance();
    rtc->b_channel_joined = true;
    
    agora_rtc_msg_t msg = { .code = AGORA_RTC_MSG_REJOIN_CHANNEL_SUCCESS };
    __send_message_2_user(rtc, &msg);
}

/* Stream-message hook: payloads from the ConvoAI agent arrive here as
 * "<msg_id>|<idx>|<total>|<base64>" fragments. We hand the raw bytes off
 * to the data-stream parser thread so the SDK callback stays non-blocking;
 * the parser then reassembles, base64-decodes, JSON-parses and updates
 * the cached agent state plus broadcasts APP_EVT_AI_*. */
static void __on_stream_message(connection_id_t conn_id, uint32_t uid, int stream_id,
                                const char *data, size_t length, uint64_t sent_ts)
{
    int n = (length < 128u) ? (int)length : 128;
    LOGD("on_stream_message conn=%u uid=%u sid=%d ts=%llu len=%u text='%.*s'\n",
         (unsigned)conn_id, (unsigned)uid, stream_id, (unsigned long long)sent_ts,
         (unsigned)length, n, data ? data : "");

    __agora_rtc_msg_process_submit(data, length);
}

static void __register_agora_rtc_event_handler(agora_rtc_t *rtc)
{
    rtc->agora_rtc_event_handler.on_join_channel_success = __on_join_channel_success;
    rtc->agora_rtc_event_handler.on_error = __on_error;
    rtc->agora_rtc_event_handler.on_user_joined = __on_user_joined;
    rtc->agora_rtc_event_handler.on_user_offline = __on_user_offline;
#if CONFIG_AGORA_RTC_USE_STRING_UID
    rtc->agora_rtc_event_handler.on_user_joined_with_user_account = __on_user_joined_with_user_account;
    rtc->agora_rtc_event_handler.on_user_offline_with_user_account = __on_user_offline_with_user_account;
    rtc->agora_rtc_event_handler.on_user_info_updated = __on_user_info_updated;
#endif
    rtc->agora_rtc_event_handler.on_key_frame_gen_req = __on_key_frame_gen_req;
    rtc->agora_rtc_event_handler.on_audio_data = __on_audio_data;
    rtc->agora_rtc_event_handler.on_video_data = __on_video_data;
    rtc->agora_rtc_event_handler.on_target_bitrate_changed = __on_target_bitrate_changed;
    rtc->agora_rtc_event_handler.on_connection_lost = __on_connection_lost;
    rtc->agora_rtc_event_handler.on_rejoin_channel_success = __on_rejoin_channel_success;
    rtc->agora_rtc_event_handler.on_user_mute_audio = __on_user_mute_audio;
    rtc->agora_rtc_event_handler.on_user_mute_video = __on_user_mute_video;
    /* Phase C: hook agent signaling. */
    rtc->agora_rtc_event_handler.on_stream_message = __on_stream_message;
}

static void __deep_copy_items_destroy(agora_rtc_t *rtc)
{
    if (rtc->agora_rtc_option.p_channel_name != NULL)
    {
        psram_free((void *)rtc->agora_rtc_option.p_channel_name);
        rtc->agora_rtc_option.p_channel_name = NULL;
    }

    if (rtc->agora_rtc_option.p_token != NULL)
    {
        psram_free((void *)rtc->agora_rtc_option.p_token);
        rtc->agora_rtc_option.p_token = NULL;
    }

#if CONFIG_AGORA_RTC_USE_STRING_UID
    if (rtc->agora_rtc_option.p_user_account != NULL)
    {
        psram_free((void *)rtc->agora_rtc_option.p_user_account);
        rtc->agora_rtc_option.p_user_account = NULL;
    }
#endif
}



static int32_t __agora_init(agora_rtc_config_t *p_config)
{
    int rval;
    uint32_t bwe_min_bps = 500000;
    uint32_t bwe_max_bps = 2000000;
    uint32_t bwe_start_bps = 800000;
    rtc_service_option_t service_opt = { 0 };

    agora_rtc_t *rtc = __get_rtc_instance();

    rtc->state = AGORA_RTC_STATE_NULL;
    rtc->audio_rx_data_handle = NULL;
    rtc->conn_id = (connection_id_t)0;
    rtc->user_message_callback = NULL;
    rtc->b_user_joined = false;
    rtc->b_channel_joined = false;
    rtc->agora_rtc_event_handler.on_audio_data = NULL;
    rtc->agora_rtc_event_handler.on_connection_lost = NULL;
    rtc->agora_rtc_event_handler.on_error = NULL;
    rtc->agora_rtc_event_handler.on_join_channel_success = NULL;
#ifdef CONFIG_RTCM
    rtc->agora_rtc_event_handler.on_media_ctrl_receive = NULL;
#endif
    rtc->agora_rtc_event_handler.on_mixed_audio_data = NULL;
    rtc->agora_rtc_event_handler.on_rejoin_channel_success = NULL;
    rtc->agora_rtc_event_handler.on_token_privilege_will_expire = NULL;
    rtc->agora_rtc_event_handler.on_user_joined = NULL;
    rtc->agora_rtc_event_handler.on_user_mute_audio = NULL;
    rtc->agora_rtc_event_handler.on_user_mute_video = NULL;
    rtc->agora_rtc_event_handler.on_video_data = NULL;
    rtc->agora_rtc_event_handler.on_target_bitrate_changed = NULL;
    rtc->agora_rtc_event_handler.on_key_frame_gen_req = NULL;
    rtc->agora_rtc_event_handler.on_user_offline = NULL;
#if CONFIG_AGORA_RTC_USE_STRING_UID
    rtc->agora_rtc_event_handler.on_user_joined_with_user_account = NULL;
    rtc->agora_rtc_event_handler.on_user_offline_with_user_account = NULL;
    rtc->agora_rtc_event_handler.on_user_info_updated = NULL;
#endif
    rtc->target_bitrate = 0;
    rtc->audio_rx_data_handle = NULL;
    rtc->video_rx_data_handle = NULL;
    rtc->agent_state = AGORA_RTC_AGENT_STATE_UNKNOWN;

    rtc->agora_rtc_config.license[0] = '\0';
    rtc->agora_rtc_config.enable_bwe_param = true;
    rtc->agora_rtc_config.bwe_param_max_bps = bwe_max_bps;
    rtc->agora_rtc_config.log_disable = true;
    rtc->agora_rtc_config.area_code = AREA_CODE_GLOB;
    LOGI("Agora RTC SDK area_code: %d\n", rtc->agora_rtc_config.area_code);


    rtc->agora_rtc_config.p_appid = (char *)psram_malloc(os_strlen(p_config->p_appid) + 1);
    if (rtc->agora_rtc_config.p_appid == NULL)
    {
        LOGE("malloc app_id fail, size: %d \n", (os_strlen(p_config->p_appid) + 1));
        goto agora_init_error;
    }
    os_strcpy((char *)rtc->agora_rtc_config.p_appid, p_config->p_appid);
    LOGI("Agora RTC SDK app_id: %s \n", rtc->agora_rtc_config.p_appid);

    os_memcpy(rtc->agora_rtc_config.license, p_config->license, 32);
    LOGI("Agora RTC SDK license: ");
    for (int n = 0; n < 32; n++)
    {
        os_printf("%c", rtc->agora_rtc_config.license[n]);
    }
    os_printf("\n");

    rtc->agora_rtc_config.enable_bwe_param = p_config->enable_bwe_param;
    rtc->agora_rtc_config.bwe_param_max_bps = p_config->bwe_param_max_bps;
    rtc->agora_rtc_config.log_disable = p_config->log_disable;
    rtc->agora_rtc_config.area_code = p_config->area_code;

    LOGI("Agora RTC SDK enable_bwe_param: %d\n", rtc->agora_rtc_config.enable_bwe_param);
    LOGI("Agora RTC SDK bwe_param_max_bps: %d\n", rtc->agora_rtc_config.bwe_param_max_bps);
    LOGI("Agora RTC SDK log_disable: %d\n", rtc->agora_rtc_config.log_disable);
    LOGI("Agora RTC SDK area_code: %d\n", rtc->agora_rtc_config.area_code);


    if (rtc->agora_rtc_config.enable_bwe_param)
    {
        bwe_max_bps = p_config->bwe_param_max_bps;
    }
    else
    {
        rtc->agora_rtc_config.bwe_param_max_bps = bwe_max_bps;
    }

    // step1. agora sdk version.
    LOGI("Agora RTC SDK v%s \n", agora_rtc_get_version());
    //step4. register event handler
    __register_agora_rtc_event_handler(rtc);

    // return BK_OK;

    //step6. agora rtc init.
    service_opt.area_code = p_config->area_code;
    service_opt.log_cfg.log_disable = p_config->log_disable;
    service_opt.log_cfg.log_path = DEFAULT_SDK_LOG_PATH;
    service_opt.log_cfg.log_level = RTC_LOG_WARNING;
    os_memcpy(service_opt.license_value, p_config->license, 33);
#if CONFIG_AGORA_RTC_USE_STRING_UID
    service_opt.use_string_uid = true;
#else
    service_opt.use_string_uid = false;
#endif

    LOGI("Agora RTC SDK area_code: %d\n", service_opt.area_code);
    LOGI("Agora RTC SDK log_disable: %d\n", service_opt.log_cfg.log_disable);
    LOGI("Agora RTC SDK log_path: %s\n", service_opt.log_cfg.log_path);
    LOGI("Agora RTC SDK log_level: %d\n", service_opt.log_cfg.log_level);
    LOGI("Agora RTC SDK license_value: %s\n", service_opt.license_value);
    LOGI("Agora RTC SDK p_appid: %s\n", rtc->agora_rtc_config.p_appid);
    LOGI("Agora RTC SDK use_string_uid: %d\n", service_opt.use_string_uid);


    // return BK_OK;

    rval = agora_rtc_init(rtc->agora_rtc_config.p_appid, &rtc->agora_rtc_event_handler, &service_opt);
    if (rval < 0)
    {
        LOGI("agora rtc init failed, rval=%d error=%s\n", rval, agora_rtc_err_2_str(rval));
        goto agora_init_error;
    }

    return BK_OK;
    
    LOGI("BWE[%u,%u,%u]\n", bwe_min_bps, bwe_max_bps, bwe_start_bps);
    rval = agora_rtc_set_bwe_param(CONNECTION_ID_ALL, bwe_min_bps, bwe_max_bps, bwe_start_bps);
    if (rval < 0)
    {
        LOGI("set BWE failed \n");
    }
    else
    {
        LOGI("agora_rtc_set_bwe_param ok \n");
    }

    return BK_OK;

agora_init_error:
    if (rtc->agora_rtc_config.p_appid != NULL)
    {
        psram_free((void *)rtc->agora_rtc_config.p_appid);
        rtc->agora_rtc_config.p_appid = NULL;
    }

    return BK_FAIL;
}



bk_err_t __agora_rtc_create(agora_rtc_config_t *p_config, agora_rtc_msg_notify_cb message_callback)
{
    if (0 != __agora_init(p_config))
    {
        LOGE("__agora_init fail.\n");
        return BK_FAIL;
    }
    else
    {
        LOGI("__agora_init ok.\n");
    }

    agora_rtc_t *rtc = __get_rtc_instance();
    if (rtc)
    {
        __register_message_router(rtc, message_callback);
    }
    else
    {
        return BK_FAIL;
    }

    /* Spin up the data-stream parser so we can surface AI agent state.
     * Failure here is non-fatal: media still works, only state polling is
     * disabled. */
    if (__agora_rtc_msg_process_init() != BK_OK)
    {
        LOGW("agent ds parser init failed (state polling disabled)\n");
    }

    LOGI("rtc create successfully.\n");
    return BK_OK;
}

bk_err_t __agora_rtc_destroy(void)
{
    /* Stop the parser first so no further callbacks land on a half-torn-down
     * instance. */
    __agora_rtc_msg_process_deinit();

    int rval = agora_rtc_fini();
    if (rval != 0)
    {
        LOGE("agora_rtc_fini failed: %d, %s\n", rval, agora_rtc_err_2_str(rval));
    }
    
    agora_rtc_t *rtc = __get_rtc_instance();
    
    if (rtc->agora_rtc_config.p_appid != NULL)
    {
        psram_free((void *)rtc->agora_rtc_config.p_appid);
        rtc->agora_rtc_config.p_appid = NULL;
    }
    
    rtc->state = AGORA_RTC_STATE_NULL;
    rtc->agent_state = AGORA_RTC_AGENT_STATE_UNKNOWN;
    
    LOGI("Agora RTC destroyed successfully\n");
    return BK_OK;
}


bk_err_t __agora_rtc_start(agora_rtc_option_t *option)
{
    int rval = 0;
    agora_rtc_t *rtc = __get_rtc_instance();
    if (!rtc)
    {
        return BK_FAIL;
    }

    /*copy channel name */
    if (rtc->agora_rtc_option.p_channel_name)
    {
        psram_free((void *)rtc->agora_rtc_option.p_channel_name);
        rtc->agora_rtc_option.p_channel_name = NULL;
    }
    rtc->agora_rtc_option.p_channel_name = (char *)psram_malloc(os_strlen(option->p_channel_name) + 1);
    if (rtc->agora_rtc_option.p_channel_name == NULL)
    {
        LOGE("malloc channel_name fail, size: %d \n", (os_strlen(option->p_channel_name) + 1));
        goto agora_rtc_start_fail;
    }
    os_strcpy((void *)rtc->agora_rtc_option.p_channel_name, option->p_channel_name);
    LOGI("Agora RTC SDK channel_name: %s \n", rtc->agora_rtc_option.p_channel_name);

    /*copy token */
    if (option->p_token)
    {
        if (rtc->agora_rtc_option.p_token)
        {
            psram_free((void *)rtc->agora_rtc_option.p_token);
            rtc->agora_rtc_option.p_token = NULL;
        }
        rtc->agora_rtc_option.p_token = (char *)psram_malloc(os_strlen(option->p_token) + 1);
        if (rtc->agora_rtc_option.p_token == NULL)
        {
            LOGE("malloc token fail, size: %d \n", (os_strlen(option->p_token) + 1));
            goto agora_rtc_start_fail;
        }
        os_strcpy((void *)rtc->agora_rtc_option.p_token, option->p_token);
    }
    else
    {
        rtc->agora_rtc_option.p_token = NULL;
    }
    LOGI("Agora RTC SDK token: %s \n", rtc->agora_rtc_option.p_token);

    rtc->agora_rtc_option.uid = option->uid;
    rtc->agora_rtc_option.auto_subscribe_audio = option->auto_subscribe_audio;
    rtc->agora_rtc_option.auto_subscribe_video = option->auto_subscribe_video;
    rtc->agora_rtc_option.audio_config.audio_data_type = option->audio_config.audio_data_type;//AUDIO_DATA_TYPE_PCMU;
    rtc->agora_rtc_option.audio_config.pcm_sample_rate = option->audio_config.pcm_sample_rate;
    rtc->agora_rtc_option.audio_config.pcm_channel_num = option->audio_config.pcm_channel_num;

#if CONFIG_AGORA_RTC_USE_STRING_UID
    /* Deep-copy the string user account so the caller can free its own
     * buffer immediately. Required by agora_rtc_join_channel_with_user_account. */
    if (rtc->agora_rtc_option.p_user_account)
    {
        psram_free((void *)rtc->agora_rtc_option.p_user_account);
        rtc->agora_rtc_option.p_user_account = NULL;
    }
    if (!option->p_user_account || option->p_user_account[0] == '\0')
    {
        LOGE("p_user_account is required when CONFIG_AGORA_RTC_USE_STRING_UID is enabled\n");
        goto agora_rtc_start_fail;
    }
    rtc->agora_rtc_option.p_user_account = (char *)psram_malloc(os_strlen(option->p_user_account) + 1);
    if (rtc->agora_rtc_option.p_user_account == NULL)
    {
        LOGE("malloc user_account fail, size: %d \n", (os_strlen(option->p_user_account) + 1));
        goto agora_rtc_start_fail;
    }
    os_strcpy((char *)rtc->agora_rtc_option.p_user_account, option->p_user_account);
    LOGI("Agora RTC SDK user_account: %s \n", rtc->agora_rtc_option.p_user_account);
#endif

    rtc_channel_options_t channel_options = { 0 };

    channel_options.auto_subscribe_audio = rtc->agora_rtc_option.auto_subscribe_audio;
    channel_options.auto_subscribe_video = rtc->agora_rtc_option.auto_subscribe_video;
    channel_options.audio_codec_opt.audio_codec_type = option->audio_config.audio_data_type;
    //channel_options.audio_codec_opt.audio_codec_type = AUDIO_CODEC_DISABLED;
    channel_options.audio_codec_opt.pcm_sample_rate = option->audio_config.pcm_sample_rate;
    channel_options.audio_codec_opt.pcm_channel_num = option->audio_config.pcm_channel_num;
    channel_options.audio_codec_opt.pcm_duration = 20;//CONFIG_AE_AUDIO_FRAME_DURATION_MS;

    // open jitter buffer
    if (channel_options.audio_codec_opt.audio_codec_type != AUDIO_CODEC_DISABLED)
    {
        channel_options.enable_audio_jitter_buffer = true;
    }

    LOGI("auto_subscribe_audio: %d, auto_subscribe_video: %d \n",
         channel_options.auto_subscribe_audio ? 1 : 0, channel_options.auto_subscribe_video ? 1 : 0);
    LOGI("audio_codec_type: %d, pcm_sample_rate: %d, pcm_channel_num: %d, pcm_duration: %d\n",
         channel_options.audio_codec_opt.audio_codec_type, channel_options.audio_codec_opt.pcm_sample_rate, channel_options.audio_codec_opt.pcm_channel_num, channel_options.audio_codec_opt.pcm_duration);

    // step8. join channel
    rval = agora_rtc_create_connection(&(rtc->conn_id));
    if (0 != rval)
    {
        LOGI("agora_rtc_create_connection failure: %d, %s \n", rval, agora_rtc_err_2_str(rval));
        goto agora_rtc_start_fail;
    }

#if CONFIG_AGORA_RTC_USE_STRING_UID
    LOGI("Agora RTC SDK user_account: %s \n", rtc->agora_rtc_option.p_user_account);
    rval = agora_rtc_join_channel_with_user_account(rtc->conn_id,
                                                    rtc->agora_rtc_option.p_channel_name,
                                                    rtc->agora_rtc_option.p_user_account,
                                                    rtc->agora_rtc_option.p_token,
                                                    &channel_options);
    if (rval < 0)
    {
        LOGI("join channel %s with user_account %s failed, rval=%d error=%s \n",
             rtc->agora_rtc_option.p_channel_name ? rtc->agora_rtc_option.p_channel_name : "",
             rtc->agora_rtc_option.p_user_account ? rtc->agora_rtc_option.p_user_account : "",
             rval, agora_rtc_err_2_str(rval));
        goto agora_rtc_start_fail;
    }
#else
    LOGI("Agora RTC SDK uid: %d \n", rtc->agora_rtc_option.uid);
    rval = agora_rtc_join_channel(rtc->conn_id, rtc->agora_rtc_option.p_channel_name, rtc->agora_rtc_option.uid, rtc->agora_rtc_option.p_token, &channel_options);
    if (rval < 0)
    {
        LOGI("join channel %s failed, rval=%d error=%s \n",
             rtc->agora_rtc_option.p_channel_name ? rtc->agora_rtc_option.p_channel_name : "", rval, agora_rtc_err_2_str(rval));
        goto agora_rtc_start_fail;
    }
#endif

    LOGI("Joining channel %s ... \n", rtc->agora_rtc_option.p_channel_name);
    return BK_OK;

agora_rtc_start_fail:
    __deep_copy_items_destroy(rtc);
    return BK_FAIL;
}


bk_err_t __agora_rtc_stop(void)
{
    int rval = 0;
    agora_rtc_t *rtc = __get_rtc_instance();
    
    if (!rtc || rtc->state == AGORA_RTC_STATE_NULL)
    {
        LOGW("RTC not initialized\n");
        return BK_FAIL;
    }
    
    __rtc_stopped(rtc);
    
    // Leave channel
    rval = agora_rtc_leave_channel(rtc->conn_id);
    if (rval < 0)
    {
        LOGE("agora_rtc_leave_channel failed, rval=%d error=%s\n", rval, agora_rtc_err_2_str(rval));
    }
    
    // Destroy connection
    rval = agora_rtc_destroy_connection(rtc->conn_id);
    if (rval < 0)
    {
        LOGE("agora_rtc_destroy_connection failed, rval=%d error=%s\n", rval, agora_rtc_err_2_str(rval));
    }
    
    __deep_copy_items_destroy(rtc);
    
    LOGI("Agora RTC stopped successfully\n");
    return BK_OK;
}

bk_err_t __agora_rtc_register_audio_rx_handle(agora_rtc_audio_rx_data_handle audio_rx_handle)
{
    agora_rtc_t *rtc = __get_rtc_instance();
    
    if (!rtc)
    {
        return BK_FAIL;
    }
    
    rtc->audio_rx_data_handle = audio_rx_handle;
    return BK_OK;
}

bk_err_t __agora_rtc_register_video_rx_handle(agora_rtc_video_rx_data_handle video_rx_handle)
{
    agora_rtc_t *rtc = __get_rtc_instance();
    
    if (!rtc)
    {
        return BK_FAIL;
    }
    
    rtc->video_rx_data_handle = video_rx_handle;
    return BK_OK;
}



