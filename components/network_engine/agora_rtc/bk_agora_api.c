/*************************************************************
 *
 * This is a part of the Agora Media Framework Library.
 * Copyright (C) 2025 Agora IO
 * All rights reserved.
 *
 *************************************************************/
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/shell_task.h>
#include <components/event.h>
#include <components/netif_types.h>
#include "bk_rtos_debug.h"
#include "agora_config.h"
#include "bk_agora_api.h"
#include "bk_agora_rtm.h"
#include "agora_agent_engine.h"
#include "bk_image_upload.h"
#include <modules/wifi.h>
#include "modules/wifi_types.h"
#include "components/bk_uid.h"
#include <driver/h264.h>
#include <driver/aon_rtc.h>
#include <modules/vcenc/vcenc_common.h>
#if CONFIG_APP_EVT
#include "app_event.h"
#endif
#include "cli.h"
#if CONFIG_FATFS
#include "ff.h"
#endif

#define TAG "agora_main"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

// agora_rtc_room_info_t *agora_room_info = NULL;

bool g_connected_flag = false;
bool g_agent_offline = true;
// static char agora_appid[33] = {0};
static char channel_name[128] = {0};
static char token[256] = CONFIG_AGORA_TOKEN;
static uint32_t uid = CONFIG_AGORA_UID;

static beken_thread_t  agora_thread_hdl = NULL;
static beken_semaphore_t agora_sem = NULL;
bool agora_runing = false;
static agora_rtc_config_t agora_rtc_config = DEFAULT_AGORA_RTC_CONFIG();
static agora_rtc_option_t agora_rtc_option = DEFAULT_AGORA_RTC_OPTION();

agora_rtc_agent_info_t *agora_rtc_agent_info;

static bool bk_agora_is_h264_key_frame(uint32_t h264_type)
{
    if (h264_type == VCENC_OUT_IFRAME) {
        return true;
    }

    if (h264_type & (1U << H264_NAL_I_FRAME)) {
        return true;
    }

    return false;
}

static uint8_t bk_agora_audio_codec_type_mapping(audio_enc_type_t codec_type)
{
    uint8_t aud_data_type = AUDIO_DATA_TYPE_GENERIC;
    
    switch(codec_type)
    {
        case AUDIO_ENC_TYPE_G711A:
            aud_data_type = AUDIO_DATA_TYPE_PCMA;
            break;
        case AUDIO_ENC_TYPE_G711U:
            aud_data_type = AUDIO_DATA_TYPE_PCMU;
            break;
        case AUDIO_ENC_TYPE_PCM:
            aud_data_type = AUDIO_DATA_TYPE_PCM;
            break;
        case AUDIO_ENC_TYPE_G722:
            aud_data_type = AUDIO_DATA_TYPE_G722;
            break;
        case AUDIO_ENC_TYPE_OPUS:
            aud_data_type = AUDIO_DATA_TYPE_OPUS;
            break;
        default:
            LOGE("Unknown codec type:%d\n", codec_type);
            break;
    }
    
    return aud_data_type;
}

/* ============================= Message Handler ============================= */

static void bk_agora_user_notify_msg_handle(agora_rtc_msg_t *p_msg)
{
    switch (p_msg->code)
    {
        case AGORA_RTC_MSG_JOIN_CHANNEL_SUCCESS:
            g_connected_flag = true;
            LOGI("Join channel success\n");
            break;
            
        case AGORA_RTC_MSG_REJOIN_CHANNEL_SUCCESS:
            g_connected_flag = true;
            LOGI("Rejoin channel success\n");
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_RTC_REJOIN_SUCCESS, 0);
            #endif
            break;
            
        case AGORA_RTC_MSG_USER_JOINED:
            LOGI("User joined\n");
            g_agent_offline = false;
            // #if CONFIG_APP_EVT
            // app_event_send_msg(APP_EVT_AGENT_JOINED, 0);
            // #endif
            break;
            
        case AGORA_RTC_MSG_USER_OFFLINE:
            LOGI("User offline\n");
            g_agent_offline = true;
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_AGENT_OFFLINE, 0);
            #endif
            break;
            
        case AGORA_RTC_MSG_CONNECTION_LOST:
            LOGE("Connection lost. Please check WiFi status\n");
            g_connected_flag = false;
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_RTC_CONNECTION_LOST, 0);
            #endif
            break;
            
        case AGORA_RTC_MSG_INVALID_APP_ID:
            LOGE("Invalid App ID. Please double check\n");
            break;
            
        case AGORA_RTC_MSG_INVALID_CHANNEL_NAME:
            LOGE("Invalid channel name. Please double check\n");
            break;
            
        case AGORA_RTC_MSG_INVALID_TOKEN:
        case AGORA_RTC_MSG_TOKEN_EXPIRED:
            LOGE("Invalid or expired token. Please double check\n");
            break;
            
        case AGORA_RTC_MSG_BWE_TARGET_BITRATE_UPDATE:
            // Handle bitrate update if needed
            // g_target_bps = p_msg->data.bwe.target_bitrate;
            break;
            
        case AGORA_RTC_MSG_KEY_FRAME_REQUEST:
            // Handle key frame request if needed
            break;

        case AGORA_RTC_MSG_AGENT_STATE_CHANGED:
            LOGI("AI agent state changed -> %s\n",
                 __agora_rtc_agent_state_to_str(p_msg->data.agent_state));
            break;

        default:
            break;
    }
}

/* ============================= Data Send Functions ============================= */

int bk_agora_rtc_audio_data_send(uint8_t *data_ptr, size_t data_len, audio_enc_type_t audio_type)
{
    int rval = 0;
    audio_frame_info_t info = { 0 };
    agora_rtc_t *rtc = NULL;
    
    if (false == g_connected_flag)
    {
        // LOGI("Failed to send audio data, g_connected_flag:%d\n", g_connected_flag);  // Disabled: too verbose
        return BK_FAIL;
    }
    
    rtc = __get_rtc_instance();
    if (!rtc || (rtc->state != AGORA_RTC_STATE_WORKING))
    {
        return BK_FAIL;
    }
    
    info.data_type = bk_agora_audio_codec_type_mapping(audio_type);
    
    rval = agora_rtc_send_audio_data(rtc->conn_id, data_ptr, data_len, &info);
    if (rval < 0)
    {
        // LOGW("Failed to send audio data, rval:%d, err:%s\n", rval, agora_rtc_err_2_str(rval));  // Disabled: too verbose
        return BK_FAIL;
    }
    
    return data_len;
}

int bk_agora_rtc_video_data_send(frame_buffer_t *frame)
{
    int rval = 0;
    video_frame_info_t info = { 0 };
    static uint64_t before = 0, curr = 0;
    agora_rtc_t *rtc = NULL;
    
    curr = bk_aon_rtc_get_ms();
    
    if (false == g_connected_flag)
    {
        // LOGI("Failed to send video data, g_connected_flag:%d\n", g_connected_flag);  // Disabled: too verbose
        return BK_FAIL;
    }
    
    /* Validate frame parameter */
    if (frame == NULL || frame->frame == NULL || frame->length == 0)
    {
        return BK_FAIL;
    }
    
    /* Setup video frame info based on format */
    info.stream_type = VIDEO_STREAM_HIGH;
    if ((frame->fmt == PIXEL_FMT_H264) || (frame->fmt == IMAGE_H264))
    {
        /* Check if it's an I-frame (only send I-frames) */
        if (!bk_agora_is_h264_key_frame(frame->h264_type))
        {
            // LOGD("%s: skip non-I frame\n", __func__);
            return BK_OK;
        }
        
        info.data_type = VIDEO_DATA_TYPE_H264;
        info.frame_type = VIDEO_FRAME_AUTO_DETECT;
         //info.frame_rate = 1000/VIDEO_FRAME_INTERVAL;
    }
    else if ((frame->fmt == PIXEL_FMT_JPEG) || (frame->fmt == IMAGE_MJPEG))
    {
        info.data_type = VIDEO_DATA_TYPE_GENERIC_JPEG;
        info.frame_type = VIDEO_FRAME_KEY;
    }
    else if ((frame->fmt == PIXEL_FMT_H265) || (frame->fmt == IMAGE_H265))
    {
        info.data_type = VIDEO_DATA_TYPE_H265;
        info.frame_type = VIDEO_FRAME_AUTO_DETECT;
    }
    else
    {
        LOGE("%s: Unsupported format: %d\n", __func__, frame->fmt);
        return BK_FAIL;
    }
    
    /* Frame rate control - send every VIDEO_FRAME_INTERVAL ms */
    if (curr <= before || curr - before < VIDEO_FRAME_INTERVAL)
    {
        return BK_OK;
    }
    
    rtc = __get_rtc_instance();
    if (!rtc || (rtc->state != AGORA_RTC_STATE_WORKING))
    {
        // LOGI("Failed to send video data, rtc->state:%d\n", rtc->state);  // Disabled: too verbose
        return BK_FAIL;
    }
    
    /* Send video data to agora_rtc */
    rval = agora_rtc_send_video_data(rtc->conn_id, (uint8_t *)frame->frame, (size_t)frame->length, &info);
    //LOGI("send video data to agora_rtc, rval:%d, rtc->conn_id:%d, frame->length:%d, frame->fmt:%d\n", rval,rtc->conn_id,frame->length,frame->fmt,info.data_type,info.frame_type);
    if (rval < 0)
    {
         LOGW("%s: send video data failed, rval=%d data_type=%d len=%d frame_type=%d fmt=%d\n",
             __func__, rval, info.data_type, (int)frame->length, info.frame_type,frame->fmt);  // Disabled: too verbose
         return BK_FAIL;
    }
    else
    {
         //LOGD("%s: send video data successfully, len=%d\n", __func__, (int)frame->length);
    }
    
    /* Update timestamp */
    before = curr;
    
    return (int)frame->length;
}

/* ============================= Audio RX Handler ============================= */

static int bk_agora_user_audio_rx_data_handle(unsigned char *data, unsigned int size, const audio_frame_info_t *info_ptr)
{
    #if CONFIG_BK_NETWORK_ENGINE
    return ntwk_eng_recv_audio(data, size);
    #else
    LOGE("BK Network transfer not enabled\n");
    return BK_FAIL;
    #endif
}

/* ============================= Main RTC Thread ============================= */

void bk_agora_rtc_main(void)
{
    bk_err_t ret = BK_OK;

    // Allocate and copy app_id
    agora_rtc_config.p_appid = (char *)psram_malloc(strlen(agora_rtc_agent_info->appid) + 1);
    os_strcpy((char *)agora_rtc_config.p_appid, agora_rtc_agent_info->appid);

    LOGI("agora_main use record appid:%s %s\r\n", agora_rtc_config.p_appid,agora_rtc_agent_info->appid);
    
    agora_rtc_config.log_disable = true;
    agora_rtc_config.bwe_param_max_bps = BANDWIDTH_ESTIMATE_MAX_BITRATE;
    
    // Create Agora RTC instance
    ret = __agora_rtc_create(&agora_rtc_config, (agora_rtc_msg_notify_cb)bk_agora_user_notify_msg_handle);
    if (ret != BK_OK)
    {
        LOGE("__agora_rtc_create failed\n");
        goto exit;
    }
    
    agora_rtc_option.p_channel_name = (char *)psram_malloc(os_strlen(channel_name) + 1);

    os_strcpy((char *)agora_rtc_option.p_channel_name, channel_name);
    agora_rtc_option.audio_config.audio_data_type = CONFIG_AUDIO_CODEC_TYPE;

#if CONFIG_AGORA_RTC_USE_STRING_UID
    /* Local (device) string uid = "r_<channel>". Short "r_" / "a_" prefixes
     * keep the final uid under the 64-byte ceiling enforced by
     * agora_rtc_login_rtm(), even when the backend hands us a long
     * channel name. Channel name itself is preserved as the RTC room id;
     * only the uid switches from the legacy 32-bit integer form to a
     * human-readable string. The agent side is expected to mirror this
     * with "a_<channel>". */
    {
        size_t ch_len = os_strlen(channel_name);
        size_t ua_len = ch_len + os_strlen("r_") + 1;
        if (agora_rtc_option.p_user_account)
        {
            psram_free((char *)agora_rtc_option.p_user_account);
            agora_rtc_option.p_user_account = NULL;
        }
        agora_rtc_option.p_user_account = (char *)psram_malloc(ua_len);
        if (agora_rtc_option.p_user_account)
        {
            os_snprintf((char *)agora_rtc_option.p_user_account, ua_len, "r_%s", channel_name);
            LOGI("agora_main local user_account: %s \r\n", agora_rtc_option.p_user_account);
        }
        else
        {
            LOGE("malloc user_account failed (len=%u)\r\n", (unsigned)ua_len);
            goto exit;
        }
    }
#endif

    if (strlen(token))
    {
        LOGI("agora_main use record token:%s \r\n", token);
        agora_rtc_option.p_token = token;
    }
    else
    {
        LOGI("agora_main use default token:%s \r\n", CONFIG_AGORA_TOKEN);
        agora_rtc_option.p_token = CONFIG_AGORA_TOKEN;
    }

    if (uid != CONFIG_AGORA_UID)
    {
        LOGI("agora_main use record uid:%d \r\n", uid);
        agora_rtc_option.uid = uid;
    }
    else
    {
        LOGI("agora_main use default uid:%d \r\n", CONFIG_AGORA_UID);
        agora_rtc_option.uid = CONFIG_AGORA_UID;
    }

    // return;
    // Start Agora RTC
    ret = __agora_rtc_start(&agora_rtc_option);
    if (ret != BK_OK)
    {
        LOGE("__agora_rtc_start failed, ret:%d\n", ret);
        goto exit;
    }
    
   

    agora_runing = true;
    rtos_set_semaphore(&agora_sem);
    
    /* Wait until we join channel successfully */
    while (!g_connected_flag)
    {
        if (!agora_runing)
        {
            goto exit;
        }
        rtos_delay_milliseconds(100);
    }
    
    LOGI("-----Agora RTC join channel success-----\n");
    /* Main loop */

#if CONFIG_AGORA_RTC_USE_STRING_UID
    /* RTM rides on the same App ID. Use the local string user_account as
     * RTM uid; pass the configured RTC token if non-empty (the SDK
     * treats NULL as "no token auth"). RTM login is best-effort: failure
     * only disables image.upload, media still works. */
    if (BK_OK != bk_agora_rtm_start(agora_rtc_option.p_user_account,
                                    (os_strlen(token) > 0) ? token : NULL))
    {
        LOGW("bk_agora_rtm_start failed; image.upload disabled\n");
    }
#endif

    ret = __agora_rtc_register_audio_rx_handle((agora_rtc_audio_rx_data_handle)bk_agora_user_audio_rx_data_handle);
    if (ret != BK_OK)
    {
       LOGE("Failed to register audio RX handle, ret:%d\n", ret);
    }    

     while (agora_runing)
     {
         rtos_delay_milliseconds(100);
     }


exit:
    __agora_rtc_register_audio_rx_handle(NULL);

#if CONFIG_AGORA_RTC_USE_STRING_UID
    /* Logout RTM before tearing down RTC so the SDK has a clean session
     * close. Safe even if start failed (no-op when not logged in). */
    bk_agora_rtm_stop();
#endif

    /* Stop Agora RTC */
    __agora_rtc_stop();
    
    /* Destroy Agora RTC */
    __agora_rtc_destroy();
    
    if (agora_rtc_config.p_appid)
    {
        psram_free((char *)agora_rtc_config.p_appid);
        agora_rtc_config.p_appid = NULL;
    }
    
    if (agora_rtc_option.p_channel_name)
    {
        psram_free((char *)agora_rtc_option.p_channel_name);
        agora_rtc_option.p_channel_name = NULL;
    }

#if CONFIG_AGORA_RTC_USE_STRING_UID
    if (agora_rtc_option.p_user_account)
    {
        psram_free((char *)agora_rtc_option.p_user_account);
        agora_rtc_option.p_user_account = NULL;
    }
#endif

    g_connected_flag = false;
    agora_thread_hdl = NULL;
    agora_runing = false;
    
    rtos_set_semaphore(&agora_sem);
    rtos_delete_thread(NULL);
}

/* ============================= Public API ============================= */

bk_err_t bk_agora_rtc_stop(void *device_id)
{
    if (!agora_runing)
    {
        LOGI("Agora not started\n");
        return BK_OK;
    }
    
    agora_runing = false;
    rtos_get_semaphore(&agora_sem, BEKEN_NEVER_TIMEOUT);
    
    rtos_deinit_semaphore(&agora_sem);
    agora_sem = NULL;
    
    return BK_OK;
}

bk_err_t bk_agora_rtc_start(void *device_id)
{
    bk_err_t ret = BK_OK;
    
    if (agora_runing)
    {
        LOGI("Agora already started, please close and then reopen\n");
        return BK_FAIL;
    }
    
    LOGI("bk_agora_rtc_start device_id:%s\r\n", device_id);

    ret = rtos_init_semaphore(&agora_sem, 1);
    if (ret != BK_OK)
    {
        LOGE("Failed to create semaphore\n");
        return BK_FAIL;
    }

    LOGI("rtos_create_thread success\r\n");

    ret = rtos_create_thread(&agora_thread_hdl,
                             4,
                             "agora",
                             (beken_thread_function_t)bk_agora_rtc_main,
                             8 * 1024,
                             NULL);
    if (ret != kNoErr)
    {
        LOGI("Failed to create agora app task, ret:%d\n", ret);
        agora_thread_hdl = NULL;
        goto fail;
    }
    

    rtos_get_semaphore(&agora_sem, BEKEN_NEVER_TIMEOUT);
    
    LOGI("Create agora app task complete\n");
    return BK_OK;
    
fail:
    if (agora_sem)
    {
        rtos_deinit_semaphore(&agora_sem);
        agora_sem = NULL;
    }
    
    return BK_FAIL;
}

int bk_agora_agent_start(agora_rtc_agent_info_t *option_info, void *device_id)
{
    return agora_agent_start(option_info, device_id);
}
int bk_agora_agent_stop(agora_rtc_agent_info_t *option_info, void *device_id)
{
    return agora_agent_stop(option_info, device_id);
}

int bk_agora_start(void *device_id)
{
    int ret = 0;
    LOGI("%s %d device_id:%s\r\n", __func__, __LINE__, device_id);

    if (agora_runing)
    {
        LOGI("bk_agora_start already started\n");
        return BK_FAIL;
    }

    if (!agora_rtc_agent_info)
    {
        agora_rtc_agent_info = psram_malloc(sizeof(agora_rtc_agent_info_t));
        if (!agora_rtc_agent_info) {
            LOGE("agora_rtc_agent_info malloc fail");
            return BK_FAIL;
        }
        os_memset(agora_rtc_agent_info, 0, sizeof(agora_rtc_agent_info_t));
    }

    ret = bk_agora_agent_start(agora_rtc_agent_info, device_id);
    LOGI("bk_agora_agent_start ret:%d\r\n", ret);
    if (ret < 0)
    {
        LOGE("bk_agora_agent_start fail, ret:%d \r\n", ret);
        app_event_send_msg(APP_EVT_AGENT_START_FAIL, 0);
        return ret;
    }
    os_strcpy(channel_name, device_id);


    ret = bk_agora_rtc_start(device_id);
    if (ret < 0)
    {
        LOGE("bk_agora_rtc_start fail, ret:%d \r\n", ret);
        return ret;
    }

    return ret;
}

int bk_agora_stop(void *device_id)
{
    int ret = 0;

    ret = bk_agora_rtc_stop(device_id);
    if (ret < 0)
    {
        LOGE("bk_agora_rtc_stop fail, ret:%d \r\n", ret);
    }

    ret = bk_agora_agent_stop(agora_rtc_agent_info, device_id);
    if (ret < 0)
    {
        LOGE("bk_agora_agent_stop fail, ret:%d \r\n", ret);
        return ret;
    }

    if (agora_rtc_agent_info)
    {
        psram_free(agora_rtc_agent_info);
        agora_rtc_agent_info = NULL;
    }

    return ret;
}

/* ============================= Agent State Query ============================= */

agora_rtc_agent_state_e bk_agora_get_agent_state(void)
{
    return __agora_rtc_get_agent_state();
}

const char *bk_agora_get_agent_state_str(void)
{
    return __agora_rtc_agent_state_to_str(__agora_rtc_get_agent_state());
}

bool bk_agora_is_agent_active(void)
{
    agora_rtc_agent_state_e s = __agora_rtc_get_agent_state();
    return (s == AGORA_RTC_AGENT_STATE_LISTENING ||
            s == AGORA_RTC_AGENT_STATE_THINKING  ||
            s == AGORA_RTC_AGENT_STATE_SPEAKING);
}

int bk_agora_update_agent(void *device_id, void *update_info)
{
    int ret = 0;
    LOGI("%s %d device_id:%s\r\n", __func__, __LINE__, device_id);

    ret =  bk_agora_rtc_stop(device_id);
    if (ret < 0)
    {
        LOGE("bk_agora_rtc_stop fail, ret:%d \r\n", ret);
    }

    ret = agora_agent_update(agora_rtc_agent_info, device_id, update_info);
    if (ret < 0)
    {
        LOGE("agora_agent_update fail, ret:%d \r\n", ret);
        return ret;
    }

    ret = bk_agora_rtc_start(device_id);
    if (ret < 0)
    {
        LOGE("bk_byte_rtc_start fail, ret:%d \r\n", ret);
        return ret;
    }
    return BK_OK;
}


#define AGORA_RTC_CMD_CNT   (sizeof(s_agora_rtc_commands) / sizeof(struct cli_command))

#define DEFAULT_TEST_IMAGE_URL    "https://docs.bekencorp.com/doctest/giraffe.jpg"

/* Default text sent after the image-upload RTM ack reports RECEIVED to
 * nudge the LLM into a vision-recognition turn. The convoai backend
 * takes this as if the user just spoke it (customType=user.transcription).
 *
 * Encoded as explicit UTF-8 hex escapes so the .rodata literal carries
 * the exact byte sequence regardless of the compiler's source-charset
 * interpretation; an earlier raw "描述图片内容" literal got mangled
 * through an implicit UTF-8<->GBK round-trip somewhere in the build
 * toolchain and arrived at the serial log as garbled CJK. The bytes
 * below are: 描 (E6 8F 8F) 述 (E8 BF B0) 图 (E5 9B BE) 片 (E7 89 87)
 * 内 (E5 86 85) 容 (E5 AE B9). */
#define DEFAULT_IMAGE_QUERY_TEXT  "\xe6\x8f\x8f\xe8\xbf\xb0\xe5\x9b\xbe\xe7\x89\x87\xe5\x86\x85\xe5\xae\xb9"

#if CONFIG_AGORA_RTC_USE_STRING_UID
bk_err_t bk_agora_rtc_send_image_with_query(const uint8_t *jpeg, size_t jpeg_len,
                                            const char *query)
{
    char peer_uid[AGORA_RTC_USER_ACCOUNT_MAX_LEN] = {0};
    const char *q = (query && query[0] != '\0') ? query : DEFAULT_IMAGE_QUERY_TEXT;
    bk_err_t ret;

    if (!jpeg || jpeg_len == 0) {
        LOGE("send_image_with_query: jpeg buffer required\r\n");
        return BK_FAIL;
    }
    if (os_strlen(channel_name) == 0) {
        LOGE("send_image_with_query: channel not set, RTC not joined?\r\n");
        return BK_FAIL;
    }
    if (!bk_agora_rtm_is_login()) {
        LOGE("send_image_with_query: RTM not login, image upload disabled\r\n");
        return BK_FAIL;
    }

    /* Local uid joined RTC as "r_<channel>"; the convoai agent side
     * mirrors that with "a_<channel>" -- same short prefixes used by
     * the `agora_rtc test send_image*` CLI paths. Keep this in sync if
     * the prefix is ever changed (search for "r_" / "a_" pair). */
    os_snprintf(peer_uid, sizeof(peer_uid), "a_%s", channel_name);

    LOGI("send_image_with_query peer=%s jpeg=%u query=\"%s\"\r\n",
         peer_uid, (unsigned)jpeg_len, q);

    /* Two image-submit paths into ConvoAI:
     *   - base64 inline through RTM customType="image.upload" -- cheap
     *     and self-contained, but capped by the RTM payload ceiling
     *     (BK_AGORA_RTM_IMG_RAW_MAX_LEN ~= 22 KB raw JPEG).
     *   - URL-only through RTM customType="image.upload"        -- the
     *     device first POSTs the JPEG to the Beken image-upload HTTPS
     *     server (multipart/form-data, see bk_image_upload_jpeg), then
     *     pushes only the returned "image_url" through RTM. No size
     *     cap from RTM, just from network/storage on the server side.
     *
     * Pick the URL path automatically whenever the JPEG is too big for
     * the base64 channel; that way camera_preview / take_photo callers
     * never have to know which underlying flow is in use. */
    if (jpeg_len > BK_AGORA_RTM_IMG_RAW_MAX_LEN)
    {
        char image_url[BK_IMAGE_URL_MAX_LEN] = {0};

        LOGI("send_image_with_query: jpeg=%u > %u, use server-upload + URL flow\r\n",
             (unsigned)jpeg_len, (unsigned)BK_AGORA_RTM_IMG_RAW_MAX_LEN);

        ret = bk_image_upload_jpeg(jpeg, jpeg_len, image_url, sizeof(image_url));
        if (ret != BK_OK)
        {
            LOGE("send_image_with_query: server upload failed, query skipped\r\n");
            return BK_FAIL;
        }

        ret = bk_agora_rtm_send_image_url_with_query(peer_uid, image_url, q);
    }
    else
    {
        ret = bk_agora_rtm_send_image_base64_with_query(peer_uid, jpeg, jpeg_len, q);
    }
    if (ret != BK_OK) {
        LOGE("send_image_with_query: image submit failed\r\n");
        return BK_FAIL;
    }
    return BK_OK;
}
#endif /* CONFIG_AGORA_RTC_USE_STRING_UID */

/* Default file path on the auto-mounted SD-NAND volume for
 * `agora_rtc send_image_b64` when the caller omits <path>. Matches the
 * filenames written by camera_preview_take_photo() on each session. */
#define DEFAULT_TEST_IMAGE_B64_PATH  "1:/photos/0001/photo.jpg"

#if CONFIG_AGORA_RTC_USE_STRING_UID
#define IMAGE_LOOP_INTERVAL_MS  15000

typedef struct {
    const char *name;
    const char *url;
} agora_image_url_item_t;

static const agora_image_url_item_t s_image_url_items[] = {
    {"bottle",   "https://apics.aclsemi.com/images/20260526/7084f5cd213a43768f1410f2cd8a196b.jpg"},
    {"computer", "https://apics.aclsemi.com/images/20260526/da36244fdb654a78ad3d96f4b1b0d3a2.jpg"},
    {"cup",      "https://apics.aclsemi.com/images/20260526/cf3d65071c6646e2b53335f6bad31f3e.jpg"},
    {"keyboard", "https://apics.aclsemi.com/images/20260526/436bac44111d4ce7bbcf729608a0e74d.jpg"},
};
#define IMAGE_URL_ITEM_COUNT  (sizeof(s_image_url_items) / sizeof(s_image_url_items[0]))

static beken_thread_t  s_image_loop_thread = NULL;
static volatile bool   s_image_loop_running = false;
static beken_semaphore_t s_image_loop_sem = NULL;

static void bk_agora_image_loop_wait_ms(uint32_t ms)
{
    uint32_t steps = ms / 100;

    if (ms % 100)
    {
        steps++;
    }
    while (steps-- > 0 && s_image_loop_running)
    {
        rtos_delay_milliseconds(100);
    }
}

static const char *bk_agora_get_image_url_by_name(const char *name)
{
    for (size_t i = 0; i < IMAGE_URL_ITEM_COUNT; i++)
    {
        if (os_strcmp(name, s_image_url_items[i].name) == 0)
        {
            return s_image_url_items[i].url;
        }
    }

    return NULL;
}

static bk_err_t bk_agora_send_image_url_to_agent(const char *cmd_name, const char *url)
{
    char peer_uid[AGORA_RTC_USER_ACCOUNT_MAX_LEN] = {0};

    if (os_strlen(channel_name) == 0)
    {
        LOGE("%s: channel not set, run 'agora_rtc start' first\r\n", cmd_name);
        return BK_FAIL;
    }

    os_snprintf(peer_uid, sizeof(peer_uid), "a_%s", channel_name);
    LOGI("%s: send image url=%s to peer=%s\r\n", cmd_name, url, peer_uid);
    if (BK_OK != bk_agora_rtm_send_image_url_with_query(peer_uid, url, DEFAULT_IMAGE_QUERY_TEXT))
    {
        LOGE("%s: send_image_url failed\r\n", cmd_name);
        return BK_FAIL;
    }

    return BK_OK;
}

static void bk_agora_image_loop_thread(void *arg)
{
    unsigned int idx = 0;

    (void)arg;

    while (s_image_loop_running)
    {
        const agora_image_url_item_t *item = &s_image_url_items[idx % IMAGE_URL_ITEM_COUNT];

        LOGI("send_image_loop: name=%s idx=%u\r\n", item->name, idx);
        if (BK_OK != bk_agora_send_image_url_to_agent("send_image_loop", item->url))
        {
            LOGE("send_image_loop: send %s failed\r\n", item->name);
        }

        idx++;
        bk_agora_image_loop_wait_ms(IMAGE_LOOP_INTERVAL_MS);
    }

    s_image_loop_thread = NULL;
    rtos_set_semaphore(&s_image_loop_sem);
    rtos_delete_thread(NULL);
}

static bk_err_t bk_agora_image_loop_start(void)
{
    bk_err_t ret;

    if (s_image_loop_thread != NULL || s_image_loop_running)
    {
        LOGW("send_image_loop already running\r\n");
        return BK_FAIL;
    }
    if (os_strlen(channel_name) == 0)
    {
        LOGE("send_image_loop: channel not set, run 'agora_rtc start' first\r\n");
        return BK_FAIL;
    }

    ret = rtos_init_semaphore_ex(&s_image_loop_sem, 1, 0);
    if (ret != BK_OK)
    {
        LOGE("send_image_loop: init sem fail\r\n");
        return BK_FAIL;
    }

    s_image_loop_running = true;
    ret = rtos_create_thread(&s_image_loop_thread,
                             4,
                             "img_loop",
                             (beken_thread_function_t)bk_agora_image_loop_thread,
                             4 * 1024,
                             NULL);
    if (ret != BK_OK)
    {
        LOGE("send_image_loop: create thread fail\r\n");
        s_image_loop_running = false;
        rtos_deinit_semaphore(&s_image_loop_sem);
        s_image_loop_sem = NULL;
        return BK_FAIL;
    }

    LOGI("send_image_loop started, interval=%ums urls=%u\r\n",
         (unsigned)IMAGE_LOOP_INTERVAL_MS, (unsigned)IMAGE_URL_ITEM_COUNT);
    return BK_OK;
}

static bk_err_t bk_agora_image_loop_stop(void)
{
    if (!s_image_loop_running && s_image_loop_thread == NULL)
    {
        LOGI("send_image_loop not running\r\n");
        return BK_OK;
    }

    s_image_loop_running = false;
    if (s_image_loop_sem)
    {
        rtos_get_semaphore(&s_image_loop_sem, BEKEN_WAIT_FOREVER);
        rtos_deinit_semaphore(&s_image_loop_sem);
        s_image_loop_sem = NULL;
    }

    LOGI("send_image_loop stopped\r\n");
    return BK_OK;
}
#endif /* CONFIG_AGORA_RTC_USE_STRING_UID */

static void bk_agora_rtc_cli_help(void)
{
    LOGI("agora_rtc {start|stop|start_agora|stop_agora|start_agent|stop_agent|agent_state|"
         "send_image [url] [query]|send_image_url <cup|bottle|computer|keyboard>|"
         "send_image_b64 [path] [query]|send_text <text>|send_image_loop {start|stop}}\n");
}

static void bk_agora_rtc_test_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    unsigned char uid[32] = {0};
    char uid_str[65] = {0};

    bk_uid_get_data(uid);
    for (int i = 0; i < 24; i++)
    {
        sprintf(uid_str + i * 2, "%02x", uid[i]);
    }

    if (argc < 2)
    {
        goto cmd_fail;
    }

    /* audio test */
    if (os_strcmp(argv[1], "start_agora") == 0)
    {
        LOGI("start rtc\r\n");
        bk_agora_rtc_start(uid_str);
    }
    else if (os_strcmp(argv[1], "stop_agora") == 0)
    {
        LOGI("stop rtc\r\n");
        bk_agora_rtc_stop(uid_str);
    }
    else if (os_strcmp(argv[1], "start_agent") == 0)
    {
        LOGI("start agent\r\n");
        bk_agora_agent_start(agora_rtc_agent_info, uid_str);
    }
    else if (os_strcmp(argv[1], "stop_agent") == 0)
    {
        LOGI("stop agent\r\n");
        bk_agora_agent_stop(agora_rtc_agent_info, uid_str);
    }
    else if (os_strcmp(argv[1], "start") == 0)
    {
        LOGI("start rtc and agent\r\n");
        // extern int audio_engine_init(void);
        // audio_engine_init();
        bk_agora_start(uid_str);
    }
    else if (os_strcmp(argv[1], "stop") == 0)
    {
        LOGI("stop rtc and agent\r\n");
        bk_agora_stop(uid_str);
    }
    else if (os_strcmp(argv[1], "agent_state") == 0)
    {
        LOGI("AI agent state: %s\r\n", bk_agora_get_agent_state_str());
    }
    else if (os_strcmp(argv[1], "send_image") == 0)
    {
#if CONFIG_AGORA_RTC_USE_STRING_UID
        const char *url   = (argc >= 3) ? argv[2] : DEFAULT_TEST_IMAGE_URL;
        const char *query = (argc >= 4) ? argv[3] : DEFAULT_IMAGE_QUERY_TEXT;
        char peer_uid[AGORA_RTC_USER_ACCOUNT_MAX_LEN] = {0};

        if (os_strlen(channel_name) == 0)
        {
            LOGE("send_image: channel not set, run 'agora_rtc start' first\r\n");
            return;
        }
        os_snprintf(peer_uid, sizeof(peer_uid), "a_%s", channel_name);
        LOGI("send image url=%s to peer=%s\r\n", url, peer_uid);
        if (BK_OK != bk_agora_rtm_send_image_url_with_query(peer_uid, url, query))
        {
            LOGE("send_image failed\r\n");
            return;
        }
#else
        LOGE("send_image requires CONFIG_AGORA_RTC_USE_STRING_UID=y\r\n");
#endif
    }
    else if (os_strcmp(argv[1], "send_image_url") == 0)
    {
#if CONFIG_AGORA_RTC_USE_STRING_UID
        const char *url = NULL;

        if (argc < 3 || argv[2] == NULL || argv[2][0] == '\0')
        {
            LOGE("send_image_url: missing <cup|bottle|computer|keyboard>\r\n");
            goto cmd_fail;
        }

        url = bk_agora_get_image_url_by_name(argv[2]);
        if (url == NULL)
        {
            LOGE("send_image_url: unknown image name=%s\r\n", argv[2]);
            goto cmd_fail;
        }

        (void)bk_agora_send_image_url_to_agent("send_image_url", url);
#else
        LOGE("send_image_url requires CONFIG_AGORA_RTC_USE_STRING_UID=y\r\n");
#endif
    }
    else if (os_strcmp(argv[1], "send_image_b64") == 0)
    {
#if CONFIG_AGORA_RTC_USE_STRING_UID && CONFIG_FATFS
        const char *path  = (argc >= 3) ? argv[2] : DEFAULT_TEST_IMAGE_B64_PATH;
        const char *query = (argc >= 4) ? argv[3] : NULL;
        FIL *fp = NULL;
        uint8_t *buf = NULL;
        FSIZE_t f_sz = 0;
        UINT br = 0;
        FRESULT fr;

        /* CLI side only does file IO; peer_uid build, RTM login check
         * and the actual transport selection (base64 vs. server upload
         * + URL) are delegated to bk_agora_rtc_send_image_with_query()
         * so this CLI path stays in sync with the camera_preview path
         * used in production. Oversize images no longer hard-fail
         * here -- the unified API will route them through the
         * upload-then-URL flow instead. */

        /* FATFS FIL is ~580 B; keep it off-stack so we don't blow the
         * shell task. */
        fp = (FIL *)os_malloc(sizeof(FIL));
        if (!fp)
        {
            LOGE("send_image_b64: FIL malloc fail\r\n");
            return;
        }
        os_memset(fp, 0, sizeof(FIL));

        fr = f_open(fp, path, FA_READ);
        if (fr != FR_OK)
        {
            LOGE("send_image_b64: f_open(%s) fr=%d\r\n", path, fr);
            goto __b64_exit;
        }
        f_sz = f_size(fp);
        if (f_sz == 0)
        {
            LOGE("send_image_b64: %s is empty\r\n", path);
            (void)f_close(fp);
            goto __b64_exit;
        }

        buf = (uint8_t *)psram_malloc((size_t)f_sz);
        if (!buf)
        {
            LOGE("send_image_b64: psram_malloc(%u) OOM\r\n", (unsigned)f_sz);
            (void)f_close(fp);
            goto __b64_exit;
        }
        fr = f_read(fp, buf, (UINT)f_sz, &br);
        (void)f_close(fp);
        if (fr != FR_OK || br != (UINT)f_sz)
        {
            LOGE("send_image_b64: f_read fr=%d br=%u/%u\r\n",
                 fr, (unsigned)br, (unsigned)f_sz);
            goto __b64_exit;
        }

        LOGI("send image (b64) path=%s size=%u\r\n", path, (unsigned)br);
        if (BK_OK != bk_agora_rtc_send_image_with_query(buf, (size_t)br, query))
        {
            LOGE("send_image_b64 failed\r\n");
        }

__b64_exit:
        if (buf)
        {
            psram_free(buf);
        }
        if (fp)
        {
            os_free(fp);
        }
#else
        LOGE("send_image_b64 requires CONFIG_AGORA_RTC_USE_STRING_UID=y && CONFIG_FATFS=y\r\n");
#endif
    }
    else if (os_strcmp(argv[1], "send_text") == 0)
    {
#if CONFIG_AGORA_RTC_USE_STRING_UID
        char peer_uid[AGORA_RTC_USER_ACCOUNT_MAX_LEN] = {0};

        if (argc < 3 || argv[2] == NULL || argv[2][0] == '\0')
        {
            LOGE("send_text: missing <text>\r\n");
            goto cmd_fail;
        }
        if (os_strlen(channel_name) == 0)
        {
            LOGE("send_text: channel not set, run 'agora_rtc start' first\r\n");
            return;
        }
        os_snprintf(peer_uid, sizeof(peer_uid), "a_%s", channel_name);
        LOGI("send text=\"%s\" to peer=%s\r\n", argv[2], peer_uid);
        if (BK_OK != bk_agora_rtm_send_user_text(peer_uid, argv[2]))
        {
            LOGE("send_text failed\r\n");
        }
#else
        LOGE("send_text requires CONFIG_AGORA_RTC_USE_STRING_UID=y\r\n");
#endif
    }
    else if (os_strcmp(argv[1], "send_image_loop") == 0)
    {
#if CONFIG_AGORA_RTC_USE_STRING_UID
        if (argc >= 3 && os_strcmp(argv[2], "stop") == 0)
        {
            bk_agora_image_loop_stop();
        }
        else if (argc < 3 || os_strcmp(argv[2], "start") == 0)
        {
            bk_agora_image_loop_start();
        }
        else
        {
            goto cmd_fail;
        }
#else
        LOGE("send_image_loop requires CONFIG_AGORA_RTC_USE_STRING_UID=y\r\n");
#endif
    }
    else
    {
        goto cmd_fail;
    }

    return;

cmd_fail:
    bk_agora_rtc_cli_help();
}
static const struct cli_command s_agora_rtc_commands[] =
{
    {"agora_rtc", "agora_rtc ...", bk_agora_rtc_test_cmd},
};

int bk_agora_rtc_cli_init(void)
{
    return cli_register_commands(s_agora_rtc_commands, AGORA_RTC_CMD_CNT);
}
bk_err_t bk_agora_pre_config(void *device_id)
{
    bk_agora_rtc_cli_init();
    return BK_OK;
}

