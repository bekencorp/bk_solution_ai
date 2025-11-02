#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include "driver/trng.h"
#include "volc_agent_engine.h"
#include "RtcWebClientUtils.h"
#include "volc_config.h"
#include "volc_fileio.h"
#include "volc_memory.h"
#include "cJSON.h"
#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#define TAG "volc_agent"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

extern char *bk_get_bk_server_url(uint8_t index);

/* Common utility functions for agent operations */
static int volc_agent_send_request(const char *uri, const char *post_data, 
                                  char *response_buffer, int *response_code)
{
    struct webclient_session *session = NULL;
    rtc_post_config_t post_config = {0};
    rtc_req_result_t result = {-1, NULL};
    int ret = BK_FAIL;
    
    if (!uri || !post_data || !response_buffer || !response_code) {
        LOGE("Invalid parameters\r\n");
        return BK_FAIL;
    }

    session = webclient_session_create(VOLC_AGENT_SEND_HEADER_SIZE);
    if (!session) {
        LOGE("Failed to create webclient session\r\n");
        return BK_FAIL;
    }
    
    webclient_header_fields_add(session, "Content-Length: %d\r\n", os_strlen(post_data));
    webclient_header_fields_add(session, "Content-Type: application/json\r\n");
    #if !CONFIG_STARTUP_AGENT_FROM_BK_SERVER
    webclient_header_fields_add(session, "Authorization: af78e30%s\r\n", CONFIG_RTC_APP_ID);
    #endif

    post_config.uri = uri;
    post_config.session = session;
    post_config.post_data = post_data;

    result.response = response_buffer;

    ret = rtc_http_post(&post_config, &result);
    
    if (response_code) {
        *response_code = result.code;
    }
    
    if (result.code == 200 && result.response) {
        ret = BK_OK;
    } else {
        LOGE("HTTP request failed, code: %d\r\n", result.code);
    }
    
    if (session) {
        webclient_close(session);
    }

    return ret;
}


#if CONFIG_STARTUP_AGENT_FROM_BK_SERVER
static int volc_parse_server_rsp(char *buffer, byte_rtc_room_info_t *room_info)
{
    int code_update = 0;
    cJSON *json = cJSON_Parse(buffer);
    if (!json)
    {
        BK_LOGE(TAG, "Error before: [%s]\n", cJSON_GetErrorPtr());
        return BK_FAIL;
    }
    cJSON *code = cJSON_GetObjectItem(json, "code");
    if (code && ((code->type & 0xFF) == cJSON_Number)) {
        code_update = code->valueint;
        switch(code_update)
        {
            case HTTP_STATUS_SUCCESS:
                break;
            case HTTP_STATUS_TRIAL_LIMIT_EXCEEDED:
                BK_LOGI(TAG, "[UPDATE] the maximum number of agent expiriences has been reached, please contact armino_support@bekencorp.com for in-depth communication !\n");
            case HTTP_STATUS_PARAM_ERROR:
            case HTTP_STATUS_MAX_AGENT_UPTIME_EXCEEDED:
            case HTTP_STATUS_DEVICE_REMOVED:
            case HTTP_STATUS_AGENT_START_FAILED:
                cJSON_Delete(json);
            return BK_FAIL;
                break;
            default:
                cJSON_Delete(json);
                return BK_FAIL;
                break;
        }
    }
    else {
        BK_LOGE(TAG, "[Error] not find code msg\n");
        cJSON_Delete(json);
        return BK_FAIL;
    }

    cJSON* data = cJSON_GetObjectItem(json, "data");
    
    if (data == NULL) {
        cJSON_Delete(json);
        BK_LOGE(TAG, "Not found data object.");
        return -1;
    }
    
    cJSON* app_id_item = cJSON_GetObjectItem(data, "app_id");
    const char* app_id = cJSON_GetStringValue(app_id_item);
    os_strcpy(room_info->app_id, app_id);
    
    cJSON* uid_item = cJSON_GetObjectItem(data, "uid");
    const char* uid = cJSON_GetStringValue(uid_item);
    os_strcpy(room_info->uid, uid);
    
    cJSON* room_id_item = cJSON_GetObjectItem(data, "room_id");
    const char* room_id = cJSON_GetStringValue(room_id_item);
    os_strcpy(room_info->room_id, room_id);
    
    cJSON* token_item = cJSON_GetObjectItem(data, "token");
    const char* token = cJSON_GetStringValue(token_item);
    os_strcpy(room_info->token, token);
    cJSON_Delete(json);

    return BK_OK;
}
int volc_start_agent_from_bk_server(byte_rtc_room_info_t *room_info, void *device_id)
{
    char *post_data = NULL;
    char *response_buffer = NULL;
    char uri[256] = {0};
    //char *device_id = (char *)device_id;
    int url_len = 0, ret = BK_FAIL;
    uint32_t rand_flag = 0;
    cJSON *root = NULL, *agent_param = NULL;

    LOGI("volc_start_agent_from_bk_server device_id:%s\r\n", device_id);
    if (!room_info)
    {
        LOGE("room_info is null\r\n");
        return BK_FAIL;
    }


    if (os_strlen(device_id) == 0)
    {
        LOGE("device_id is empty\r\n");
        return BK_FAIL;
    }

    url_len = os_snprintf(uri, VOLC_AGENT_MAX_URL_LEN, "%s/activate_agent/", bk_get_bk_server_url(1));
    if ((url_len < 0) || (url_len >= VOLC_AGENT_MAX_URL_LEN))
    {
        LOGE("URL len overflow\r\n");
        return BK_FAIL;
    }

    /* Generate JSON data */
    root = cJSON_CreateObject();
    agent_param = cJSON_CreateObject(); /* Will be automatically deleted when root is deleted */
    
    if (!root || !agent_param)
    {
        LOGE("no memory for cJSON objects\n");
        ret = BK_FAIL;
        goto __exit;
    }

    // Add channel
    cJSON_AddStringToObject(root, "channel", device_id);
    
    // Add mode to agent_param
    #if CONFIG_VOLC_ENABLE_VISION_BY_DEFAULT
    cJSON_AddStringToObject(agent_param, "mode", "vision");
    #else
    cJSON_AddStringToObject(agent_param, "mode", "text");
    #endif
    
    bool volc_license_valid = false;
    volc_file_exists("/VolcEngineRTCLite.lic", &volc_license_valid);

    // license and TTS burst function are only supported after VOLC RTC v1.0.6
    if (BYTE_RTC_API_VERSION_NUM >= 0x1006)
    {
        cJSON_AddBoolToObject(agent_param, "enable_burst", true);
        if (volc_license_valid)
        {
            cJSON_AddStringToObject(agent_param, "enable_license", "true");
        }
    }

    // Add audio codec
    cJSON_AddStringToObject(agent_param, "audio_codec", CONFIG_AE_AUDIO_ENCODER_TYPE);

    #if CONFIG_VOLC_ENABLE_SUBTITLE_BY_DEFAULT
    cJSON_AddBoolToObject(agent_param, "disable_rts_subtitle", false);
    #endif

    // Add agent_param to root
    cJSON_AddItemToObject(root, "agent_param", agent_param);

    // Add rand_flag
    rand_flag = bk_rand();
    char rand_flag_str[32] = {0};
    sprintf(rand_flag_str, "%u", rand_flag);
    cJSON_AddStringToObject(root, "rand_flag", rand_flag_str);
    
    // Generate JSON string
    post_data = cJSON_Print(root);
    if (!post_data)
    {
        LOGE("no memory for post_data buffer\n");
        ret = BK_FAIL;
        goto __exit;
    }
    
    LOGI("%s, post_data:%s\r\n", __func__, post_data);
    response_buffer = volc_malloc(VOLC_AGENT_RCV_BUF_SIZE);
    if (!response_buffer) {
        LOGE("Failed to malloc response buffer\r\n");
        ret = BK_FAIL;
        goto __exit;
    }
    os_memset(response_buffer, 0, VOLC_AGENT_RCV_BUF_SIZE);
    int response_code = 0;
    /* Send HTTP request using common utility */
    ret = volc_agent_send_request(uri, post_data, response_buffer, &response_code);

    if (ret != BK_OK || !response_buffer)
    {
        LOGE("Failed to send agent start request, ret:%d, response_code:%d\r\n", ret, response_code);
        ret = BK_FAIL;
        goto __exit;
    }

    LOGI("Response: %s\r\n", response_buffer);

    /* Parse response */
    ret = volc_parse_server_rsp(response_buffer, room_info);

__exit:

    if (response_buffer)
    {
        volc_free(response_buffer);
    }

    if (root)
    {
        cJSON_Delete(root);
    }
    /* agent_param is a child of root, no need to delete separately */

    LOGD("volc_agent_start_from_bk_server end, ret:%d\n", ret);
    return ret;
}

int volc_upate_agent_from_bk_server(byte_rtc_room_info_t *room_info, void *device_id, void *update_info)
{
    char *post_data = NULL;
    char *response_buffer = NULL;
    char uri[256] = {0};
    //char *device_id = (char *)device_id;
    int url_len = 0, ret = BK_FAIL;
    uint32_t rand_flag = 0;
    bool volc_license_valid = false;
    cJSON *root = NULL, *agent_param = NULL;

    if (!room_info)
    {
        LOGE("room_info is null\r\n");
        return BK_FAIL;
    }

    if (os_strlen(device_id) == 0)
    {
        LOGE("device_id is empty\r\n");
        return BK_FAIL;
    }

    LOGI("volc_upate_agent_from_bk_server device_id:%s\r\n", device_id);

    url_len = os_snprintf(uri, VOLC_AGENT_MAX_URL_LEN, "%s/activate_agent/", bk_get_bk_server_url(1));
    if ((url_len < 0) || (url_len >= VOLC_AGENT_MAX_URL_LEN))
    {
        LOGE("URL len overflow\r\n");
        return BK_FAIL;
    }

    /* Generate JSON data */
    root = cJSON_CreateObject();
    agent_param = cJSON_CreateObject();
    
    if (!root || !agent_param)
    {
        LOGE("no memory for cJSON objects\n");
        ret = BK_FAIL;
        goto __exit;
    }

    // Add channel
    cJSON_AddStringToObject(root, "channel", device_id);
    
    // Add mode to agent_param
    cJSON_AddStringToObject(agent_param, "mode", update_info);
    
    volc_file_exists("/VolcEngineRTCLite.lic", &volc_license_valid);
    
    // license and TTS burst function are only supported after VOLC RTC v1.0.6
    if (BYTE_RTC_API_VERSION_NUM >= 0x1006)
    {
        cJSON_AddBoolToObject(agent_param, "enable_burst", true);
        if (volc_license_valid)
        {
            cJSON_AddStringToObject(agent_param, "enable_license", "true");
        }
    }

    // Add audio codec
    cJSON_AddStringToObject(agent_param, "audio_codec", CONFIG_AE_AUDIO_ENCODER_TYPE);

    #if CONFIG_VOLC_ENABLE_SUBTITLE_BY_DEFAULT
    cJSON_AddBoolToObject(agent_param, "disable_rts_subtitle", false);
    #endif

    // Add agent_param to root
    cJSON_AddItemToObject(root, "agent_param", agent_param);
    
    // Add rand_flag
    rand_flag = bk_rand();
    char rand_flag_str[32] = {0};
    sprintf(rand_flag_str, "%u", rand_flag);
    cJSON_AddStringToObject(root, "rand_flag", rand_flag_str);
    
    
    // Generate JSON string
    post_data = cJSON_PrintUnformatted(root);
    if (!post_data)
    {
        LOGE("no memory for post_data buffer\n");
        ret = BK_FAIL;
        goto __exit;
    }
    
    LOGI("%s, %s\r\n", __func__, post_data);

    response_buffer = volc_malloc(VOLC_AGENT_RCV_BUF_SIZE);
    if (!response_buffer) {
        LOGE("Failed to malloc response buffer\r\n");
        ret = BK_FAIL;
        goto __exit;
    }
    os_memset(response_buffer, 0, VOLC_AGENT_RCV_BUF_SIZE);
    int response_code = 0;
    /* Send HTTP request using common utility */
    ret = volc_agent_send_request(uri, post_data, response_buffer, &response_code);
    if (ret != BK_OK || !response_buffer)
    {
        LOGE("Failed to send agent update request, ret:%d, response_code:%d\r\n", ret, response_code);
        ret = BK_FAIL;
        goto __exit;
    }

    LOGI("Response: %s\r\n", response_buffer);

    /* Parse response */
    ret = volc_parse_server_rsp(response_buffer, room_info);

__exit:
    if (response_buffer)
    {
        volc_free(response_buffer);
    }
    
    if (root)
    {
        cJSON_Delete(root);
    }

    return ret;
}
int volc_stop_agent_from_bk_server(byte_rtc_room_info_t *room_info, void *device_id)
{
    return BK_OK;

    // server is not supported now, return success directly
    #if 0
    char *post_data = NULL;
    char *response_buffer = NULL;
    char uri[256] = {0};
    //char *device_id = (char *)device_id;
    int url_len = 0, ret = BK_FAIL;
    cJSON *root = NULL;

    if (!room_info)
    {
        LOGE("room_info is null\r\n");
        return BK_FAIL;
    }

    if (os_strlen(device_id) == 0)
    {
        LOGE("device_id is empty\r\n");
        return BK_FAIL;
    }

    url_len = os_snprintf(uri, VOLC_AGENT_MAX_URL_LEN, "%s/deactivate_agent/", bk_get_bk_server_url(1));
    if ((url_len < 0) || (url_len >= VOLC_AGENT_MAX_URL_LEN))
    {
        LOGE("URL len overflow\r\n");
        return BK_FAIL;
    }

    /* Generate JSON data */
    root = cJSON_CreateObject();
    if (!root)
    {
        LOGE("no memory for cJSON object\n");
        ret = BK_FAIL;
        goto __exit;
    }
    
    // Add channel
    cJSON_AddStringToObject(root, "channel", device_id);
    
    post_data = cJSON_PrintUnformatted(root);
    if (!post_data)
    {
        LOGE("Failed to generate JSON string\r\n");
        ret = BK_FAIL;
        goto __exit;
    }

    LOGI("Stop agent request: %s\r\n", post_data);

    response_buffer = volc_malloc(VOLC_AGENT_RCV_BUF_SIZE);
    if (!response_buffer) {
        LOGE("Failed to malloc response buffer\r\n");
        ret = BK_FAIL;
        goto __exit;
    }
    os_memset(response_buffer, 0, VOLC_AGENT_RCV_BUF_SIZE);
    int response_code = 0;
    /* Use common utility for HTTP request */
    ret = volc_agent_send_request(uri, post_data, response_buffer, &response_code);
    
    if (ret == BK_OK && response_buffer) {
        /* Parse successful response */
        cJSON* json = cJSON_Parse(response_buffer);
        if (json) {
            cJSON_Delete(json);
        }
        ret = BK_OK;
    } else {
        LOGE("Stop agent request failed, ret:%d, response_code:%d\r\n", ret, response_code);
        ret = BK_FAIL;
    }

__exit:

    if (response_buffer)
    {
        volc_free(response_buffer);
    }
    
    if (root)
    {
        cJSON_Delete(root);
    }
    
    LOGD("volc_stop_agent_from_bk_server end, ret:%d\n", ret);
    return ret;
    #endif
}

// uint16_t volc_generate_start_agent_req_payload(char *payload, uint16_t max_len)
// {
//     unsigned char uid[32] = {0};
//     char uid_str[65] = {0};
//     uint16 len = 0;

//     bk_uid_get_data(uid);
//     for (int i = 0; i < 24; i++)
//     {
//         sprintf(uid_str + i * 2, "%02x", uid[i]);
//     }
//     len = os_snprintf(payload, max_len, "{\"channel\":\"%s\",\"agent_param\": {", uid_str);
//     len += os_snprintf(payload+len, max_len, "\"audio_duration\": %d,", CONFIG_AUDIO_FRAME_DURATION_MS);
//     len += os_snprintf(payload+len, max_len, "\"out_acodec\": \"%s\"",CONFIG_AUDIO_ENCODER_TYPE);
//     len += os_snprintf(payload+len, max_len, "}}");
//     BK_LOGI(TAG, "ori channel name:%s, %s, %d\r\n", uid_str, payload, len);
//     return len;
// }
#else

/* Common utility for customer server operations */
static int volc_customer_server_request(const char *endpoint, const char *post_data, 
                                       int *response_code, char *response_buffer)
{
    char uri[256] = {0};
    int ret = BK_FAIL;
    
    os_snprintf(uri, sizeof(uri), "http://%s/%s", CONFIG_AGENT_SERVER_HOST, endpoint);
    
    ret = volc_agent_send_request(uri, post_data, response_buffer, response_code);
    
    return ret;
}


static int volc_parse_customer_response(const char *response, byte_rtc_room_info_t *room_info)
{
    LOGI("volc_parse_customer_response, response:%s\n", response);
    cJSON *root = cJSON_Parse(response);
    if (!root) {
        LOGE("Error parsing JSON response\r\n");
        return BK_FAIL;
    }
    
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) {
        LOGE("No data object in response\r\n");
        cJSON_Delete(root);
        return BK_FAIL;
    }
    
    cJSON *app_id_item = cJSON_GetObjectItem(data, "app_id");
    cJSON *uid_item = cJSON_GetObjectItem(data, "uid");
    cJSON *room_id_item = cJSON_GetObjectItem(data, "room_id");
    cJSON *token_item = cJSON_GetObjectItem(data, "token");
    cJSON *task_id_item = cJSON_GetObjectItem(data, "task_id");
    cJSON *bot_uid_item = cJSON_GetObjectItem(data, "bot_uid");
    
    if (app_id_item) os_strcpy(room_info->app_id, cJSON_GetStringValue(app_id_item));
    if (uid_item) os_strcpy(room_info->uid, cJSON_GetStringValue(uid_item));
    if (room_id_item) os_strcpy(room_info->room_id, cJSON_GetStringValue(room_id_item));
    if (token_item) os_strcpy(room_info->token, cJSON_GetStringValue(token_item));
    if (task_id_item) os_strcpy(room_info->task_id, cJSON_GetStringValue(task_id_item));
    if (bot_uid_item) os_strcpy(room_info->bot_uid, cJSON_GetStringValue(bot_uid_item));
    
    cJSON_Delete(root);
    return BK_OK;
}
int volc_start_agent_from_customer_server(byte_rtc_room_info_t *room_info, void *device_id)
{
    char *post_data = NULL;
    char *response_buffer = NULL;
    cJSON *post_jobj = cJSON_CreateObject();
    int response_code = 0;
    int ret = BK_FAIL;

    if (!post_jobj) {
        LOGE("Failed to create JSON object\r\n");
        return BK_FAIL;
    }

    cJSON_AddStringToObject(post_jobj, "audio_codec", CONFIG_AE_AUDIO_ENCODER_TYPE);
    //cJSON_AddNumberToObject(post_jobj, "asr_type", 1);

    #if CONFIG_VOLC_ENABLE_VISION_BY_DEFAULT
    cJSON_AddBoolToObject(post_jobj, "vision_enable", true);
    cJSON_AddNumberToObject(post_jobj, "image_height", CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT);
    cJSON_AddStringToObject(post_jobj, "image_detail", "high");
    #endif

    cJSON_AddStringToObject(post_jobj, "room_identifier", "OPUSLOW");

    #if CONFIG_VOLC_ENABLE_SUBTITLE_BY_DEFAULT
    cJSON_AddBoolToObject(post_jobj, "disable_rts_subtitle", false);
    #else
    cJSON_AddBoolToObject(post_jobj, "disable_rts_subtitle", true);
    #endif

    cJSON_AddBoolToObject(post_jobj, "enable_burst", true);
    cJSON_AddNumberToObject(post_jobj, "burst_buffer_size", 500);
    cJSON_AddNumberToObject(post_jobj, "burst_interval", 20);
    
    post_data = cJSON_PrintUnformatted(post_jobj);
    cJSON_Delete(post_jobj);
    
    if (!post_data) {
        LOGE("Failed to generate JSON string\r\n");
        return BK_FAIL;
    }

    LOGI("Start agent request: %s\r\n", post_data);

    response_buffer = volc_malloc(VOLC_AGENT_RCV_BUF_SIZE);
    if (!response_buffer) {
        LOGE("Failed to malloc response buffer\r\n");
        ret = BK_FAIL;
        goto __failed_exit;
    }
    os_memset(response_buffer, 0, VOLC_AGENT_RCV_BUF_SIZE);
    /* Use common utility for HTTP request */
    ret = volc_customer_server_request("startvoicechat", post_data, &response_code, response_buffer);
    
    if (ret == BK_OK && response_code == 200 && response_buffer) {
        /* Parse successful response */
        ret = volc_parse_customer_response(response_buffer, room_info);
    } else if (response_buffer) {
        /* Handle error response */
        cJSON* root = cJSON_Parse(response_buffer);
        if (root) {
            cJSON* message_item = cJSON_GetObjectItem(root, "message");
            if (message_item) {
                LOGE("Error: %s\r\n", cJSON_GetStringValue(message_item));
            }
            cJSON_Delete(root);
        }
        ret = BK_FAIL;
    } else {
        LOGE("Request failed, code: %d\r\n", response_code);
        ret = BK_FAIL;
    }

__failed_exit:

    /* Cleanup */
    if (post_data) {
        cJSON_free(post_data);
    }
    
    if (response_buffer) {
        volc_free(response_buffer);
    }

    return (ret == BK_OK) ? 200 : response_code;
}
int volc_stop_agent_from_customer_server(const byte_rtc_room_info_t* room_info, void *device_id)
{
    char *post_data = NULL;
    char *response_buffer = NULL;
    cJSON *post_jobj = cJSON_CreateObject();
    int response_code = 0;
    int ret = BK_FAIL;

    if (!post_jobj) {
        LOGE("Failed to create JSON object\r\n");
        return BK_FAIL;
    }

    cJSON_AddStringToObject(post_jobj, "task_id", room_info->task_id);
    
    post_data = cJSON_PrintUnformatted(post_jobj);
    cJSON_Delete(post_jobj);
    
    if (!post_data) {
        LOGE("Failed to generate JSON string\r\n");
        return BK_FAIL;
    }

    LOGI("Stop agent request: %s\r\n", post_data);

    response_buffer = volc_malloc(VOLC_AGENT_RCV_BUF_SIZE);
    if (!response_buffer) {
        LOGE("Failed to malloc response buffer\r\n");
        ret = BK_FAIL;
        goto __failed_exit;
    }
    os_memset(response_buffer, 0, VOLC_AGENT_RCV_BUF_SIZE);
    /* Use common utility for HTTP request */
    ret = volc_customer_server_request("stopvoicechat", post_data, &response_code, response_buffer);
    
    if (ret == BK_OK && response_code == 200 && response_buffer) {
        /* Parse successful response */
        cJSON* root = cJSON_Parse(response_buffer);
        if (root) {
            cJSON_Delete(root);
        }
        ret = BK_OK;
    } else if (response_buffer) {
        /* Handle error response */
        cJSON* root = cJSON_Parse(response_buffer);
        if (root) {
            cJSON* message_item = cJSON_GetObjectItem(root, "message");
            if (message_item) {
                LOGE("Error: %s\r\n", cJSON_GetStringValue(message_item));
            }
            cJSON_Delete(root);
        }
        ret = BK_FAIL;
    } else {
        LOGE("Request failed, code: %d\r\n", response_code);
        ret = BK_FAIL;
    }

__failed_exit:
    /* Cleanup */
    if (post_data) {
        cJSON_free(post_data);
    }
    
    if (response_buffer) {
        volc_free(response_buffer);
    }

    return (ret == BK_OK) ? 200 : response_code;
}

int volc_update_agent_from_customer_server(byte_rtc_room_info_t *room_info, void *device_id, void *update_info)
{
    char *post_data = NULL;
    char *response_buffer = NULL;
    cJSON *post_jobj = cJSON_CreateObject();
    int response_code = 0;
    int ret = BK_FAIL;

    if (!post_jobj) {
        LOGE("Failed to create JSON object\r\n");
        return BK_FAIL;
    }

    cJSON_AddStringToObject(post_jobj, "task_id", room_info->task_id);
    cJSON_AddStringToObject(post_jobj, "audio_codec", CONFIG_AE_AUDIO_ENCODER_TYPE);
    //cJSON_AddNumberToObject(post_jobj, "asr_type", 1);

    #if CONFIG_VOLC_ENABLE_VISION_BY_DEFAULT
    cJSON_AddBoolToObject(post_jobj, "vision_enable", true);
    cJSON_AddNumberToObject(post_jobj, "image_height", CONFIG_VIDEO_ENGINE_RESOLUTION_HEIGHT);
    cJSON_AddStringToObject(post_jobj, "image_detail", "high");
    #endif

    cJSON_AddStringToObject(post_jobj, "room_identifier", "OPUSLOW");

    #if CONFIG_VOLC_ENABLE_SUBTITLE_BY_DEFAULT
    cJSON_AddBoolToObject(post_jobj, "disable_rts_subtitle", false);
    #else
    cJSON_AddBoolToObject(post_jobj, "disable_rts_subtitle", true);
    #endif

    cJSON_AddBoolToObject(post_jobj, "enable_burst", true);
    cJSON_AddNumberToObject(post_jobj, "burst_buffer_size", 500);
    cJSON_AddNumberToObject(post_jobj, "burst_interval", 20);
    
    post_data = cJSON_PrintUnformatted(post_jobj);
    cJSON_Delete(post_jobj);
    
    if (!post_data) {
        LOGE("Failed to generate JSON string\r\n");
        return BK_FAIL;
    }

    LOGI("Update agent request: %s\r\n", post_data);

    response_buffer = volc_malloc(VOLC_AGENT_RCV_BUF_SIZE);
    if (!response_buffer) {
        LOGE("Failed to malloc response buffer\r\n");
        ret = BK_FAIL;
        goto __failed_exit;
    }
    os_memset(response_buffer, 0, VOLC_AGENT_RCV_BUF_SIZE);
    /* Use common utility for HTTP request */
    ret = volc_customer_server_request("updatevoicechat", post_data, &response_code, response_buffer);
    
    if (ret == BK_OK && response_code == 200 && response_buffer) {
        /* Parse successful response */
        ret = volc_parse_customer_response(response_buffer, room_info);
    } else if (response_buffer) {
        /* Handle error response */
        cJSON* root = cJSON_Parse(response_buffer);
        if (root) {
            cJSON* message_item = cJSON_GetObjectItem(root, "message");
            if (message_item) {
                LOGE("Error: %s\r\n", cJSON_GetStringValue(message_item));
            }
            cJSON_Delete(root);
        }
        ret = BK_FAIL;
    } else {
        LOGE("Request failed, code: %d\r\n", response_code);
        ret = BK_FAIL;
    }

__failed_exit:

    /* Cleanup */
    if (post_data) {
        cJSON_free(post_data);
    }
    
    if (response_buffer) {
        volc_free(response_buffer);
    }

    return (ret == BK_OK) ? 200 : response_code;
}
#endif
int volc_agent_update(byte_rtc_room_info_t *room_info, void *device_id, void *update_info)
{
    #if CONFIG_STARTUP_AGENT_FROM_BK_SERVER
    return volc_upate_agent_from_bk_server(room_info, device_id, update_info);
    #else
    return volc_update_agent_from_customer_server(room_info, device_id, update_info);
    #endif
}
int volc_agent_start(byte_rtc_room_info_t *room_info, void *device_id)
{
    #if CONFIG_STARTUP_AGENT_FROM_BK_SERVER
    return volc_start_agent_from_bk_server(room_info, device_id);
    #else
    return volc_start_agent_from_customer_server(room_info, device_id);
    #endif
}
int volc_agent_stop(byte_rtc_room_info_t *room_info, void *device_id)
{
    #if CONFIG_STARTUP_AGENT_FROM_BK_SERVER
    return volc_stop_agent_from_bk_server(room_info, device_id);
    #else
    return volc_stop_agent_from_customer_server(room_info, device_id);
    #endif
}