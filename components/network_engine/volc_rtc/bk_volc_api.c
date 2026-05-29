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
#include "volc_config.h"
#include "bk_volc_api.h"
#include "volc_agent_engine.h"
#include "volc_device_finger_print.h"
#include <modules/wifi.h>
#include "modules/wifi_types.h"
#include "components/bk_uid.h"
#include <driver/h264.h>
#if CONFIG_APP_EVT
#include "app_event.h"
#endif
#include "volc_memory.h"
#include "cJSON.h"
#include "mbedtls/platform.h"
#include "cli.h"
#include "components/bk_uid.h"
#include <driver/aon_rtc.h>
#include "bk_posix.h"
#include "volc_fileio.h"

#define TAG "volc_main"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

byte_rtc_room_info_t *volc_room_info;
//#define BYTE_RX_SPK_DATA_DUMP


#define VIDEO_FRAME_INTERVAL            500

bool g_connected_flag = false;
bool g_agent_offline = true;

static beken_thread_t  byte_thread_hdl = NULL;
static beken_semaphore_t byte_sem = NULL;
bool byte_runing = false;
static byte_rtc_config_t byte_rtc_config = DEFAULT_BYTE_RTC_CONFIG();
static byte_rtc_option_t byte_rtc_option = DEFAULT_BYTE_RTC_OPTION();
bool byte_rtc_license_valid = false;
static bool bk_byte_rtc_sdcard_is_mount = false;

/* mount sdcard */
static int bk_byte_rtc_mount_sd0_fatfs(void)
{
    int ret = BK_OK;

    if(!bk_byte_rtc_sdcard_is_mount)
    {
        struct bk_fatfs_partition partition;
        char *fs_name = NULL;
        fs_name = "fatfs";
        partition.part_type = FATFS_DEVICE;
        partition.part_dev.device_name = FATFS_DEV_SDCARD;
        partition.mount_path = VFS_SD_0_PATITION_0;
        ret = mount("SOURCE_NONE", partition.mount_path, fs_name, 0, &partition);
        bk_byte_rtc_sdcard_is_mount = true;
        LOGI("func %s, mount /sd0 \n", __func__);
    }

    return ret;
}

/* unmount sdcard */
static bk_err_t bk_byte_rtc_unmount_sd0_fatfs(void)
{
    bk_err_t ret = BK_OK;

    if (!bk_byte_rtc_sdcard_is_mount)
    {
        return BK_OK;
    }

    LOGD("func %s, unmount /sd0 \n", __func__);
    if (BK_OK != umount(VFS_SD_0_PATITION_0))
    {
        LOGE("func %s, unmount /sd0 fail\n", __func__);
        ret = BK_FAIL;
    }
    else
    {
        bk_byte_rtc_sdcard_is_mount = false;
    }

    return ret;
}
static void bk_byte_rtc_license_check(void)
{
    char cFileName[VFS_FILE_MAX_LEN] = {0};
 
    sprintf(cFileName, "%s/%s", VFS_SD_0_PATITION_0, "VolcEngineRTCLite.lic");
    volc_file_exists(cFileName, &byte_rtc_license_valid);
    LOGI("byte_rtc_license_valid:%d\r\n", byte_rtc_license_valid);
}

static void bk_byte_rtc_print_finger()
{
    volc_string_t finger;
    volc_string_init(&finger);
    volc_get_device_finger_print(&finger);
    LOGI("finger length:%d capacity:%d buffer:%s\r\n", finger.length,  finger.capacity, finger.buffer);
    volc_string_deinit(&finger);
}
static void bk_byte_rtc_user_notify_msg_handle(byte_rtc_msg_t *p_msg)
{
    switch (p_msg->code)
    {
        case BYTE_RTC_MSG_JOIN_CHANNEL_SUCCESS:
            g_connected_flag = true;
            LOGI("Join channel success.\n");
            break;
        case BYTE_RTC_MSG_REJOIN_CHANNEL_SUCCESS:
            g_connected_flag = true;
            LOGI("Rejoin channel success.\n");
            #if CONFIG_BK_SMART_CONFIG
            // if (g_agent_offline == false)
            //     network_reconnect_stop_timeout_check();
            #endif
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_RTC_REJOIN_SUCCESS, 0);
            #endif
            break;
        case BYTE_RTC_MSG_USER_JOINED:
            LOGI("User Joined.\n");
            #if CONFIG_BK_SMART_CONFIG
            //network_reconnect_stop_timeout_check();
            #endif
            g_agent_offline = false;
            #if CONFIG_BK_SMART_CONFIG
            //smart_config_running = false;
            #endif
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_AGENT_JOINED, 0);
            #endif
            break;
        case BYTE_RTC_MSG_USER_OFFLINE:
            LOGI("User Offline.\n");
            g_agent_offline = true;
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_AGENT_OFFLINE, 0);

            if (g_connected_flag == true)
               app_event_send_msg(APP_EVT_AGENT_DEVICE_REMOVE, 0);
            #endif
            break;
        case BYTE_RTC_MSG_CONNECTION_LOST:
            LOGE("Lost connection. Please check wifi status.\n");
            g_connected_flag = false;
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_RTC_CONNECTION_LOST, 0);
            #endif
            break;
        case BYTE_RTC_MSG_INVALID_APP_ID:
            LOGE("Invalid App ID. Please double check.\n");
            break;
        case BYTE_RTC_MSG_INVALID_CHANNEL_NAME:
            LOGE("Invalid channel name. Please double check.\n");
            break;
        case BYTE_RTC_MSG_INVALID_TOKEN:
        case BYTE_RTC_MSG_TOKEN_EXPIRED:
            LOGE("Invalid token. Please double check.\n");
            break;
        case BYTE_RTC_MSG_BWE_TARGET_BITRATE_UPDATE:
            //g_target_bps = p_msg->bwe.target_bitrate;
            break;
        case BYTE_RTC_MSG_KEY_FRAME_REQUEST:
            break;
        default:
            break;
    }
}
uint8_t bk_byte_rtc_audio_codec_type_mapping(uint8_t codec_type)
{
    uint8_t aud_data_type = AUDIO_DATA_TYPE_UNKNOWN;
    switch(codec_type)
    {
        case AUDIO_ENC_TYPE_G711A:
        {
            aud_data_type = AUDIO_DATA_TYPE_PCMA;
            break;
        }
        case AUDIO_ENC_TYPE_G711U:
        {
            aud_data_type = AUDIO_DATA_TYPE_PCMU;
            break;
        }
        case AUDIO_ENC_TYPE_PCM:
        {
            aud_data_type = AUDIO_DATA_TYPE_PCM;
            break;
        }
        case AUDIO_ENC_TYPE_G722:
        {
            aud_data_type = AUDIO_DATA_TYPE_G722;
            break;
        }
        case AUDIO_ENC_TYPE_OPUS:
        {
            aud_data_type = AUDIO_DATA_TYPE_OPUS;
            break;
        }
        default:
        {
            LOGE("%s unknown codec type:%d \r\n", __func__,codec_type);
            break;
        }
    }

    return aud_data_type;
}
int bk_byte_rtc_audio_data_send(uint8_t *data_ptr, size_t data_len, audio_enc_type_t audio_type)
{
    // API: send audio data
    audio_frame_info_t info = { 0 };
    byte_rtc_t *rtc = __byte_rtc_get_instance();

    if (!rtc || (rtc->state != BYTE_RTC_STATE_WORKING))
    {
        //LOGI("Failed to send audio data, rtc->state:%d\n", rtc->state);
        return BK_FAIL;
    }
    
    info.data_type = bk_byte_rtc_audio_codec_type_mapping(audio_type);

    int rval = byte_rtc_send_audio_data(rtc->engine, rtc->byte_rtc_option.room->room_id, data_ptr, data_len, &info);
    //LOGI("rtc TX audio, data_ptr=%p, data_len=%d, data_type=%d\r\n", data_ptr, (int)data_len, info.data_type);
    if (rval < 0)
    {
        LOGI("Failed to send audio data, rval:%d, err:%s\n", rval, byte_rtc_err_2_str(rval));
        return BK_FAIL;
    }

    return data_len;
}

int bk_byte_rtc_video_data_send(frame_buffer_t *frame)    
{
    int rval = 0;
    video_frame_info_t info = { 0 };
    static uint64_t before = 0, curr = 0;
    byte_rtc_t *rtc = NULL;
    
    curr = bk_aon_rtc_get_ms();

    if (false == g_connected_flag)
    {
        //LOGI("Failed to send video data, g_connected_flag:%d\n", g_connected_flag);
        /* volc rtc is not running, do not send video. */
        return BK_FAIL;
    }

    /* Validate frame parameter */
    if (frame == NULL || frame->frame == NULL || frame->length == 0)
    {
        return BK_FAIL;
    }

    /* Setup video frame info based on format */
    info.stream_type = VIDEO_STREAM_HIGH;
    if (frame->fmt == PIXEL_FMT_H264)
    {
        /* Check if it's an I-frame (only send I-frames) */
        if ((frame->h264_type & (1 << H264_NAL_I_FRAME)) == 0)
        {
            // LOGD("%s: skip non-I frame\n", __func__);
            return BK_OK;
        }
        
        info.data_type = VIDEO_DATA_TYPE_H264;
        info.frame_type = VIDEO_FRAME_AUTO_DETECT;
        info.frame_rate = 1000/VIDEO_FRAME_INTERVAL;
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

    rtc = __byte_rtc_get_instance();
    if (!rtc || (rtc->state != BYTE_RTC_STATE_WORKING))
    {
        //LOGI("Failed to send video data, rtc->state:%d\n", rtc->state);
        return BK_FAIL;
    }

    /* Send video data to byte_rtc */
    rval = byte_rtc_send_video_data(rtc->engine, rtc->byte_rtc_option.room->room_id, 
                                    (uint8_t *)frame->frame, (size_t)frame->length, &info);
    if (rval < 0)
    {
        LOGW("%s: send video data failed, rval=%d data_type=%d len=%d frame_type=%d\n",
             __func__, rval, info.data_type, (int)frame->length, info.frame_type);
    }
    else
    {
        // LOGD("%s: send video data successfully, len=%d\n", __func__, (int)frame->length);
    }

    /* Update timestamp */
    before = curr;
    
    return BK_OK;
}



static int bk_byte_rtc_user_audio_rx_data_handle(unsigned char *data, unsigned int size, audio_data_type_e data_type)
{
    #if CONFIG_BK_NETWORK_ENGINE
    return ntwk_eng_recv_audio(data, size);
    #else
    LOGE("BK Network transfer not enabled\n");
    return BK_FAIL;
    #endif
}


void bk_byte_rtc_main(void)
{
    bk_err_t ret = BK_OK;

    if (!volc_room_info) {
        LOGE("room info empty!\r\n");
        return;
    }

    if (strlen(volc_room_info->app_id) == 0)
    {
        LOGE("app_id empty!\r\n");
        psram_free((char *)volc_room_info);
        volc_room_info = NULL;
        return;
    }
    //byte_print_finger();
    mbedtls_platform_set_calloc_free(volc_calloc, volc_free);

    cJSON_Hooks hook = {volc_malloc, volc_free};
    cJSON_InitHooks(&hook);

    //service_opt.license_value[0] = '\0';
    byte_rtc_config.p_appid = (char *)psram_malloc(strlen(volc_room_info->app_id) + 1);
    os_strcpy((char *)byte_rtc_config.p_appid, volc_room_info->app_id);
    byte_rtc_config.log_level = BYTE_RTC_LOG_LEVEL_INFO;

    ret = __byte_rtc_create(&byte_rtc_config, (byte_rtc_msg_notify_cb)bk_byte_rtc_user_notify_msg_handle);
    if (ret != BK_OK)
    {
        LOGI("__byte_rtc_create fail \r\n");
    }
    // LOGI("-----start byte rtc process-----\r\n");

    byte_rtc_option.room = volc_room_info;

    byte_rtc_option.audio_data_type = bk_byte_rtc_audio_codec_type_mapping(ntwk_eng_get_audio_encoder_type());

    ret = __byte_rtc_start(&byte_rtc_option);
    if (ret != BK_OK)
    {
        LOGE("__byte_rtc_start fail, ret:%d \r\n", ret);
        goto exit;
    }

    byte_runing = true;

    rtos_set_semaphore(&byte_sem);

    /* wait until we join channel successfully */
    while (!g_connected_flag)
    {
        if (!byte_runing)
        {
            goto exit;
        }
        rtos_delay_milliseconds(100);
    }

    LOGI("-----byte_rtc_join_channel success-----\r\n");

    ret = __byte_rtc_register_audio_rx_handle((byte_rtc_audio_rx_data_handle)bk_byte_rtc_user_audio_rx_data_handle);
    if (ret != BK_OK)
    {
        LOGE("bk_aggora_rtc_register_audio_rx_handle fail, ret:%d \r\n", ret);
    }


    while (byte_runing)
    {
        rtos_delay_milliseconds(100);
    }

exit:
    /* deregister callback to handle audio data received from byte rtc */
    __byte_rtc_register_audio_rx_handle(NULL);

    /* free byte */
    /* stop byte rtc */
    __byte_rtc_stop();

    /* destory byte rtc */
    __byte_rtc_destroy();

    if (byte_rtc_config.p_appid)
    {
        psram_free((char *)byte_rtc_config.p_appid);
        byte_rtc_config.p_appid = NULL;
    }

    g_connected_flag = false;

    /* delete task */
    byte_thread_hdl = NULL;

    byte_runing = false;

    rtos_set_semaphore(&byte_sem);
    LOGE("byte_main exit\n");
    rtos_delete_thread(NULL);
}

bk_err_t bk_byte_rtc_stop(void *device_id)
{
    LOGI("%s, %d\n", __func__, __LINE__);
    if (!byte_runing)
    {
        LOGI("byte not start\n");
        return BK_OK;
    }

    byte_runing = false;

    rtos_get_semaphore(&byte_sem, BEKEN_NEVER_TIMEOUT);

    rtos_deinit_semaphore(&byte_sem);
    byte_sem = NULL;
    LOGI("%s, %d\n", __func__, __LINE__);
    return BK_OK;
}

bk_err_t bk_byte_rtc_start(void *device_id)
{
    bk_err_t ret = BK_OK;

    LOGI("%s, %d\n", __func__, __LINE__);
    if (byte_runing)
    {
        LOGI("byte already start, Please close and then reopens\n");
        return BK_FAIL;
    }

    ret = rtos_init_semaphore(&byte_sem, 1);
    if (ret != BK_OK)
    {
        LOGE("%s, %d, create semaphore fail\n", __func__, __LINE__);
        return BK_FAIL;
    }

    ret = rtos_create_thread(&byte_thread_hdl,
                             4,
                             "bk_byte_rtc",
                             (beken_thread_function_t)bk_byte_rtc_main,
                             6 * 1024,
                             NULL);
    if (ret != kNoErr)
    {
        LOGE("%s, %d, create byte app task fail, ret:%d\n", __func__, __LINE__, ret);
        byte_thread_hdl = NULL;
        goto fail;
    }

    rtos_get_semaphore(&byte_sem, BEKEN_NEVER_TIMEOUT);

    LOGI("create byte app task complete\n");

    return BK_OK;

fail:

    if (byte_sem)
    {
        rtos_deinit_semaphore(&byte_sem);
        byte_sem = NULL;
    }

    LOGI("%s, %d\n", __func__, __LINE__);
    return BK_FAIL;
}
int bk_byte_agent_start(byte_rtc_room_info_t *room_info, void *device_id)
{
    bk_byte_rtc_license_check();
    return volc_agent_start(room_info, device_id);
}
int bk_byte_agent_stop(byte_rtc_room_info_t *room_info, void *device_id)
{
    return volc_agent_stop(room_info, device_id);
}
int bk_byte_start(void *device_id)
{
    int ret = 0;
    LOGI("%s %d device_id:%s\r\n", __func__, __LINE__, device_id);

    ret = bk_byte_rtc_mount_sd0_fatfs();
    if (ret < 0)
    {
        LOGE("bk_byte_rtc_mount_sd0_fatfs fail, ret:%d \r\n", ret);
    }

    if (byte_runing)
    {
        LOGI("bk_byte_start already started\n");
        return BK_FAIL;
    }

    if (!volc_room_info)
    {
        volc_room_info = psram_malloc(sizeof(byte_rtc_room_info_t));
        if (!volc_room_info) {
            LOGE("volc_room_info malloc fail");
            return BK_FAIL;
        }
        os_memset(volc_room_info, 0, sizeof(byte_rtc_room_info_t));
    }

    ret = bk_byte_agent_start(volc_room_info, device_id);
    if (ret < 0)
    {
        LOGE("bk_byte_agent_start fail, ret:%d \r\n", ret);
        #if CONFIG_APP_EVT
        app_event_send_msg(APP_EVT_AGENT_START_FAIL, 0);
        #endif
        return ret;
    }

    ret = bk_byte_rtc_start(device_id);
    if (ret < 0)
    {
        LOGE("bk_byte_rtc_start fail, ret:%d \r\n", ret);
        return ret;
    }
    return ret;
}
int bk_byte_stop(void *device_id)
{
    int ret = 0;

    ret = bk_byte_rtc_stop(device_id);
    if (ret < 0)
    {
        LOGE("bk_byte_rtc_stop fail, ret:%d \r\n", ret);
    }

    ret = bk_byte_agent_stop(volc_room_info, device_id);
    if (ret < 0)
    {
        LOGE("bk_byte_agent_stop fail, ret:%d \r\n", ret);
        return ret;
    }

    if (volc_room_info)
    {
        psram_free((char *)volc_room_info);
        volc_room_info = NULL;
    }

    ret = bk_byte_rtc_unmount_sd0_fatfs();
    if (ret < 0)
    {
        LOGE("bk_byte_rtc_unmount_sd0_fatfs fail, ret:%d \r\n", ret);
    }

    return ret;
}
int bk_byte_update_agent(void *device_id, void *update_info)
{
    int ret = 0;
    LOGI("%s %d device_id:%s\r\n", __func__, __LINE__, device_id);

    ret = bk_byte_rtc_stop(device_id);
    if (ret < 0)
    {
        LOGE("bk_byte_rtc_stop fail, ret:%d \r\n", ret);
    }

    ret = volc_agent_update(volc_room_info, device_id, update_info);
    if (ret < 0)
    {
        LOGE("volc_agent_update fail, ret:%d \r\n", ret);
        return ret;
    }

    ret = bk_byte_rtc_start(device_id);
    if (ret < 0)
    {
        LOGE("bk_byte_rtc_start fail, ret:%d \r\n", ret);
        return ret;
    }
    return BK_OK;
}
#define BYTE_RTC_CMD_CNT   (sizeof(s_byte_rtc_commands) / sizeof(struct cli_command))
static void bk_byte_rtc_cli_help(void)
{
    LOGI("byte_test {start|stop}\n");
}

static void bk_byte_rtc_test_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
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
    if (os_strcmp(argv[1], "start_rtc") == 0)
    {
        LOGI("start rtc\r\n");
        bk_byte_rtc_start(uid_str);
    }
    else if (os_strcmp(argv[1], "stop_rtc") == 0)
    {
        LOGI("stop rtc\r\n");
        bk_byte_rtc_stop(uid_str);
    }
    else if (os_strcmp(argv[1], "start_agent") == 0)
    {
        LOGI("start agent\r\n");
        bk_byte_agent_start(volc_room_info, uid_str);
    }
    else if (os_strcmp(argv[1], "stop_agent") == 0)
    {
        LOGI("stop agent\r\n");
        bk_byte_agent_stop(volc_room_info, uid_str);
    }
    else if (os_strcmp(argv[1], "start") == 0)
    {
        LOGI("start rtc and agent\r\n");
        // extern int audio_engine_init(void);
        // audio_engine_init();
        bk_byte_start(uid_str);
    }
    else if (os_strcmp(argv[1], "stop") == 0)
    {
        LOGI("stop rtc and agent\r\n");
        bk_byte_stop(uid_str);
    }
    else
    {
        goto cmd_fail;
    }

    return;

cmd_fail:
    bk_byte_rtc_cli_help();
}
static const struct cli_command s_byte_rtc_commands[] =
{
    {"volc_rtc", "volc_rtc ...", bk_byte_rtc_test_cmd},
};

int bk_byte_rtc_cli_init(void)
{
    return cli_register_commands(s_byte_rtc_commands, BYTE_RTC_CMD_CNT);
}
bk_err_t bk_byte_pre_config(void *device_id)
{
    bk_byte_rtc_cli_init();
    return BK_OK;
}

