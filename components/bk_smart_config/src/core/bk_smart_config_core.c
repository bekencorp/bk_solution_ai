// Copyright 2020-2025 Beken
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <components/event.h>
#include <components/netif.h>
#include <driver/gpio.h>
#include <driver/trng.h>
#include "bk_wifi.h"
#include "bk_wifi_types.h"
#include "bk_cli.h"
#include "os/str.h"
#include "components/log.h"
#include "cli.h"
#include "bk_smart_config.h"
#include "components/bk_uid.h"
#include "components/system.h"
#include "cJSON.h"
#if CONFIG_APP_EVT
#include "app_event.h"
#endif
#if (CONFIG_EASY_FLASH && CONFIG_EASY_FLASH_V4)
#include "bk_ef.h"
#endif
#if CONFIG_BK_NETWORK_ENGINE
#include "network_engine.h"
#endif
#if CONFIG_BK_AUDIO_ENGINE
#include "audio_engine.h"
#endif
#if CONFIG_BK_VIDEO_ENGINE
#include "video_engine.h"
#endif
#if CONFIG_BK_MODEM
#include "components/modem_driver.h"
#endif
#if CONFIG_NET_PAN
#include "pan_service.h"
#endif
#if CONFIG_BK_FACTORY_CONFIG
#include "bk_factory_config.h"
#endif

#include "phy_client.h"
#include <modules/pm.h>

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define TAG "sconf"

#define BK_SCONF_AGENT_TOKEN_KEY   "robot_identity"
#define BK_SCONF_AGENT_UUID_LEN    65
#define BK_SCONF_AGENT_TOKEN_LEN   64

//split packet to upload wifi scan result
bool g_ble_split_pkt = false;
beken_semaphore_t sync_flash_sema = NULL;
bool smart_config_running = false;
static beken_thread_t config_ir_mode_switch_thread_handle = NULL;
static beken_thread_t s_sconf_cli_mode_thread_handle = NULL;
static beken_thread_t s_sconf_exit_thread_handle = NULL;
static const char *s_sconf_start_model_type = "text";
static char s_agent_token[BK_SCONF_AGENT_TOKEN_LEN];
static bool s_agent_token_loaded = false;

static volatile bool s_network_provisioned = false;

bool bk_sconf_is_network_provisioned(void)
{
    return s_network_provisioned;
}

const char *bk_sconf_get_start_model_type(void)
{
    return s_sconf_start_model_type;
}

static uint8_t bk_sconf_get_supported_engine(void)
{
#ifdef CONFIG_AGORA_IOT_SDK
#if CONFIG_SENSENOVA_ENABLE
    return 5;
#endif
#if !CONFIG_BK_DEV_STARTUP_AGENT
    return 0;
#else
    return 3;
#endif
#elif CONFIG_VOLC_RTC_EN
    return 1;
#elif CONFIG_BK_WSS_TRANS
    //TODO, will be changed to 2
    return 3;
#elif CONFIG_LINGXIN_AI_EN
    return 4;
#else // 3 means device startup agent
    return 3;
#endif
}
int bk_sconf_save_channel_name(char *chan)
{
    bk_sconf_agent_info_t info_tmp = {0};

    bk_config_read("d_agent_info", (void *)&info_tmp, sizeof(bk_sconf_agent_info_t));
    os_memset(info_tmp.channel_name, 0x0, 128);
    os_strcpy(info_tmp.channel_name, chan);
    bk_config_write("d_agent_info", (const void *)&info_tmp, sizeof(bk_sconf_agent_info_t));
    bk_config_sync_flash_safely();
    return 0;
}
int bk_sconf_erase_channel_name(void)
{
    bk_sconf_agent_info_t info_tmp = {0};
    info_tmp.channel_name[0] = '\0';
    bk_config_write("d_agent_info", (const void *)&info_tmp, sizeof(bk_sconf_agent_info_t));
    bk_config_sync_flash_safely();
    return 0;
}
int bk_sconf_get_channel_name(char *chan)
{
    bk_sconf_agent_info_t info_tmp = {0};

    if (bk_config_read("d_agent_info", (void *)&info_tmp, sizeof(bk_sconf_agent_info_t)) <= 0)
    {
        return -1;
    }
    os_strcpy(chan, info_tmp.channel_name);

    return 0;
}

static bk_err_t bk_sconf_load_agent_token(void)
{
    if (s_agent_token_loaded) {
        return BK_OK;
    }

    os_memset(&s_agent_token, 0, sizeof(s_agent_token));
    s_agent_token_loaded = true;

#if CONFIG_BK_FACTORY_CONFIG
    if (bk_config_read(BK_SCONF_AGENT_TOKEN_KEY, s_agent_token, sizeof(s_agent_token)) == sizeof(s_agent_token)
        && s_agent_token[0] != '\0') {
        LOGI("loaded agent token=%s\n", s_agent_token);
        return BK_OK;
    }
#endif

    LOGW("agent token not provisioned\n");
    return BK_OK;
}

int bk_sconf_save_agent_token(const char *token)
{
    if (!token || token[0] == '\0') {
        return BK_ERR_PARAM;
    }

    if (os_strlen(token) >= sizeof(s_agent_token)) {
        return BK_ERR_PARAM;
    }

    os_memset(&s_agent_token, 0, sizeof(s_agent_token));
    os_snprintf(s_agent_token, sizeof(s_agent_token), "%s", token);
    s_agent_token_loaded = true;
    LOGI("updated agent token=%s\n", s_agent_token);

#if CONFIG_BK_FACTORY_CONFIG
    int ret = bk_config_write(BK_SCONF_AGENT_TOKEN_KEY, s_agent_token, sizeof(s_agent_token));
    if (ret != 0) {
        LOGW("store agent token ret=%d\n", ret);
    }
    bk_config_sync_flash_safely();
#endif

    return BK_OK;
}

int bk_sconf_clear_agent_token(void)
{
    os_memset(&s_agent_token, 0, sizeof(s_agent_token));
    s_agent_token_loaded = true;
    LOGI("cleared agent token\n");

#if CONFIG_BK_FACTORY_CONFIG
    int ret = bk_config_write(BK_SCONF_AGENT_TOKEN_KEY, s_agent_token, sizeof(s_agent_token));
    if (ret != 0) {
        LOGW("clear agent token ret=%d\n", ret);
    }
    bk_config_sync_flash_safely();
#endif

    return BK_OK;
}

const char *bk_sconf_get_agent_identity_uuid(void)
{
    static char agent_uuid[BK_SCONF_AGENT_UUID_LEN];

    int ret = bk_sconf_get_agent_uuid(agent_uuid, sizeof(agent_uuid));
    if (ret <= 0) {
        LOGW("get agent uuid failed\r\n");
        return NULL;
    }

    return agent_uuid;
}

const char *bk_sconf_get_agent_identity_token(void)
{
    if (s_agent_token[0] == '\0') {
        bk_sconf_load_agent_token();
    }
    return s_agent_token[0] != '\0' ? s_agent_token : "";
}

int bk_sconf_get_agent_uuid(char *uuid, uint16_t max_len)
{
    unsigned char uid[32] = {0};
    char uid_str[65] = {0};
    int len = 0;

    if (!uuid || max_len < sizeof(uid_str)) {
        LOGW("get agent uuid failed: uuid=%p max_len=%d\r\n", uuid, max_len);
        return 0;
    }

    //bk_uid_get_data(uid);
    /* BK7259: bk_uid_get_data returns all zeros; use MAC as stable substitute, same length (24 bytes -> 48 hex chars) */
    uint8_t hwaddr[6] = {0};
    if (bk_ap_get_mac(hwaddr, MAC_TYPE_BASE) == BK_OK) {
        for (int i = 0; i < 24; i++) {
            uid[i] = hwaddr[i % 6];
        }
    }
    /* else: keep uid all-zero, same as original when UID unavailable */
    for (int i = 0; i < 24; i++)
    {
        sprintf(uid_str + i * 2, "%02x", uid[i]);
    }

    len = os_snprintf(uuid, max_len, "%s", uid_str);
    if (len <= 0 || len >= max_len) {
        LOGW("generate agent token failed: uuid=%p max_len=%d\r\n", uuid, max_len);
        return 0;
    }
    BK_LOGI(TAG, "agent uuid:%s, len:%d\r\n", uid_str, len);
    return len;
}

int bk_sconf_generate_agent_token(char *token, uint16_t max_len)
{
    if (!token || max_len < 33) {
        return BK_ERR_PARAM;
    }

    uint32_t r0 = bk_rand();
    uint32_t r1 = bk_rand();
    uint32_t r2 = bk_rand();
    uint32_t r3 = bk_rand();
    int len = os_snprintf(token, max_len, "%08x%08x%08x%08x", r0, r1, r2, r3);
    if (len <= 0 || len >= max_len) {
        return BK_FAIL;
    }

    return len;
}

static int bk_sconf_build_agent_identity_payload(char *payload, uint16_t max_len)
{
    char uuid[65] = {0};
    char token[65] = {0};
    int len = 0;

    if (!payload || max_len == 0) {
        return BK_ERR_PARAM;
    }

    if (bk_sconf_get_agent_uuid(uuid, sizeof(uuid)) <= 0) {
        LOGW("get agent uuid failed\r\n");
        return BK_FAIL;
    }

    if (bk_sconf_generate_agent_token(token, sizeof(token)) <= 0) {
        LOGW("generate agent token failed\r\n");
        return BK_FAIL;
    }

    if (bk_sconf_save_agent_token(token) != BK_OK) {
        LOGW("save agent token failed\r\n");
        return BK_FAIL;
    }

    len = os_snprintf(payload, max_len, "{\"uuid\":\"%s\",\"token\":\"%s\"}",
                      uuid, token);
    if (len <= 0 || len >= max_len) {
        return BK_FAIL;
    }

    LOGI("agent identity payload built uuid:%s token:%s len:%d\r\n", uuid, token, len);
    return len;
}

static int bk_sconf_wifi_sta_connect(char *ssid, char *key)
{
    int ssid_len, key_len;

    wifi_sta_config_t sta_config = {0};

    ssid_len = os_strlen(ssid);

    if (32 < ssid_len)
    {
        LOGW("ssid name more than 32 Bytes\r\n");
        return BK_FAIL;
    }

    os_strcpy(sta_config.ssid, ssid);

    /* key NULL means open network */
    key_len = key ? os_strlen(key) : 0;

    if (key_len > 63)
    {
        LOGW("Invalid passphrase, max 63 chars\r\n");
        return BK_FAIL;
    }
    if (key_len > 0 && key_len < 8)
    {
        LOGW("Invalid passphrase, length %d (expected: 8..63)\r\n", key_len);
    }

    if (key)
        os_strcpy(sta_config.password, key);
    else
        sta_config.password[0] = '\0';

#if CONFIG_STA_AUTO_RECONNECT
    sta_config.auto_reconnect_count = 5;
    sta_config.disable_auto_reconnect_after_disconnect = true;
#endif
    LOGI("ssid:%s key:%s\r\n", sta_config.ssid, sta_config.password);
    BK_LOG_ON_ERR(bk_wifi_sta_set_config(&sta_config));
    BK_LOG_ON_ERR(bk_wifi_sta_start());

    return BK_OK;
}

static int bk_sconf_wlan_scan_done_handler(void *arg, event_module_t event_module,
								  int event_id, void *event_data)
{
    wifi_scan_result_t scan_result = {0};
    char payload[200];
    uint16 len = 0;
    int i = 0, j = 0;

    BK_LOG_ON_ERR(bk_wifi_scan_get_result(&scan_result));
    if (scan_result.ap_num == 0)
        goto exit;

again:
    os_memset(payload, 0, 200);
    len = os_snprintf(payload, 200, "[");
    for (i = j; i < scan_result.ap_num; i++) {
        if (!os_strlen(scan_result.aps[i].ssid))
            continue;
        if ((len + 5 + os_strlen(scan_result.aps[i].ssid)) > 200) {
            j = i;
            break;
        }
        if ((i != 0) && (len != 1))
            len += os_snprintf(payload+len, 200, ",");
        len += os_snprintf(payload+len, 200, "\"%s\"", scan_result.aps[i].ssid);
        j = i + 1;
    }
    len += os_snprintf(payload+len, 200, "]");
    LOGI("upload scan_rst %s, sended:%d, total:%d\r\n", payload, j, scan_result.ap_num);
    if ((j >= scan_result.ap_num) || (g_ble_split_pkt == false))
        bk_ble_provisioning_event_notify_with_data(BOARDING_OP_START_WIFI_SCAN, 0, payload, len);
    else {
        bk_ble_provisioning_event_notify_with_data(BOARDING_OP_START_WIFI_SCAN, 1, payload, len);
        goto again;
    }

exit:
    bk_wifi_scan_free_result(&scan_result);

    return BK_OK;
}

static int bk_sconf_start_network_transfer(char *device_id)
{
    if (!device_id || os_strlen(device_id) == 0) {
        BK_LOGI(TAG, "%s, device_id is empty, do nothing\r\n", __func__);
        return BK_FAIL;
    }

    BK_LOGI(TAG, "%s, device_id: %s, start network transfer\r\n", __func__, device_id);

// #if CONFIG_BK_AUDIO_ENGINE
//     audio_engine_init();
// #endif

#if CONFIG_BK_NETWORK_ENGINE
    return ntwk_eng_start(device_id);
#else
    LOGW("%s, Network transfer not supported\r\n", __func__);
    return BK_FAIL;
#endif
}

static int bk_sconf_stop_network_transfer(char *device_id)
{
    BK_LOGI(TAG, "%s, device_id: %s, stop network transfer\r\n", __func__, device_id ? device_id : "");

    if (!device_id || os_strlen(device_id) == 0) {
        BK_LOGI(TAG, "%s, device_id is empty, do nothing\r\n", __func__);
        return BK_FAIL;
    }

#if CONFIG_BK_NETWORK_ENGINE
    return ntwk_eng_deinit();
#else
    LOGW("%s, Network transfer not supported\r\n", __func__);
    return BK_FAIL;
#endif
}

int bk_sconf_start_rtc(void)
{
    int ret = 0;
    char device_id[128] = {0};

    ret = bk_sconf_get_channel_name(device_id);
    if ((ret != 0) || (os_strlen(device_id) == 0)) {
        LOGW("No RTC channel, do nothing\r\n");
        return BK_FAIL;
    }

    return bk_sconf_start_network_transfer(device_id);
}

int bk_sconf_stop_rtc(void)
{
    int ret = 0;
    char device_id[128] = {0};

    ret = bk_sconf_get_channel_name(device_id);
    if ((ret != 0) || (os_strlen(device_id) == 0)) {
        LOGW("No RTC channel, do nothing\r\n");
        return BK_FAIL;
    }

    return bk_sconf_stop_network_transfer(device_id);
}
void bk_sconf_prase_agent_info(char *payload, uint8_t reset)
{
    cJSON *json = NULL;
    char *tmp_channel = NULL;

    json = cJSON_Parse(payload);
    if (!json)
    {
        BK_LOGE(TAG, "Error before: [%s]\n", cJSON_GetErrorPtr());
        return;
    }

    cJSON *channel_name = cJSON_GetObjectItem(json, "channel_name");
    if (channel_name && ((channel_name->type & 0xFF) == cJSON_String))
    {
        tmp_channel = channel_name->valuestring;
        BK_LOGI(TAG, "real channel name:%s\r\n", tmp_channel);
    }
    else
    {
        BK_LOGE(TAG, "[Error] not find msg\n");
    }

    if (tmp_channel && (os_strlen(tmp_channel) > 0)) {
        bk_sconf_save_channel_name(tmp_channel);
    }
    #if CONFIG_APP_EVT
    app_event_send_msg(APP_EVT_CLOSE_BLUETOOTH, 0);
    #endif
    cJSON_Delete(json);
}
int bk_sconf_upate_agent_info(char *device_id, char *update_info)
{
    //int ret = 0;
#if CONFIG_BK_NETWORK_ENGINE
    int agent_retry_cnt = 0;
#endif

    LOGI("%s %d, update_info:%s\r\n", __func__, __LINE__, update_info);

    if (!update_info) {
        LOGW("update_info is null\r\n");
        return BK_FAIL;
    }

    if (os_strlen(update_info) == 0) {
        LOGW("update_info is empty\r\n");
        return BK_FAIL;
    }

    if (os_strlen(device_id) == 0) {
        LOGW("device_id is empty\r\n");
        return BK_FAIL;
    }

    #if CONFIG_BK_NETWORK_ENGINE
    LOGI("%s %d, Network transfer stopped\r\n", __func__, __LINE__);

    while (agent_retry_cnt < 3)
    {
        if (ntwk_eng_update(device_id, update_info) == 0)
        {
            break;
        }
        //BK_LOGE(TAG, "bk_sconf_upate_agent_info failed, retry:%d", agent_retry_cnt);
        agent_retry_cnt++;
        rtos_delay_milliseconds(200);
    }

    if (agent_retry_cnt == 3)
    {
        BK_LOGE(TAG, "bk_sconf_upate_agent_info failed, retry:%d", agent_retry_cnt);
        return BK_FAIL;
    }

    LOGI("%s %d, Network transfer started\r\n", __func__, __LINE__);
    return BK_OK;
    #else
    LOGW("%s %d, Network transfer not supported\r\n", __func__, __LINE__);
    return BK_FAIL;
    #endif
}

static int bk_sconf_start_rtc_for_model(char *device_id, const char *model_type, bool *was_running)
{
    int ret = 0;

    if (device_id == NULL || os_strlen(device_id) == 0) {
        LOGW("sconf mode: device_id is empty\r\n");
        return BK_FAIL;
    }

    if (was_running != NULL) {
        *was_running = false;
    }

#if CONFIG_BK_NETWORK_ENGINE
    if (was_running != NULL) {
        *was_running = ntwk_eng_is_started();
    }
#endif

    s_sconf_start_model_type = model_type ? model_type : "text";
    ret = bk_sconf_start_network_transfer(device_id);
    if (ret != BK_OK) {
        LOGW("sconf mode: start RTC failed, ret=%d\r\n", ret);
        return ret;
    }

    return BK_OK;
}

void bk_sconf_switch_ir_mode_handler(void)
{
    static bool is_enable_ir_mode = false;
    static int ir_mode_switching = 0;

    char device_id[128] = {0};
    LOGI("%s %d, begin to switch ir mode\r\n", __func__, __LINE__);
    int ret = 0;

    if (ir_mode_switching == 1) {
        LOGI("%s %d, ir mode switching ongoing\r\n", __func__, __LINE__);
        goto exit;
    }

    ir_mode_switching = 1;
    ret = bk_sconf_get_channel_name(device_id);
    if ((ret == 0) && (os_strlen(device_id) > 0)) {
        LOGI("device_id:%s\n", device_id);
    }
    else {
        LOGW("No device_id, do nothing\r\n");
        goto exit;
    }

    if (is_enable_ir_mode == false) {
        #if CONFIG_BK_VIDEO_ENGINE
        ret = video_engine_init();
        if (ret != BK_OK) {
            LOGE("%s: Failed to initialize video engine, ret=%d\n", __func__, ret);
            goto exit;
        }
        #endif
        
        ret = bk_sconf_upate_agent_info(device_id, "vision");
        if (ret != 0) {
            LOGW("%s %d ret:%d, Failed to update agent info\r\n", __func__, __LINE__, ret);
            goto exit;
        }

        #if (CONFIG_DUAL_SCREEN_AVI_PLAY)
        //TODO: switch to vision screen
        #endif
        is_enable_ir_mode = true;
        LOGI("%s %d, Successfully switched to vision mode\r\n", __func__, __LINE__);
    }
    else {
        ret = bk_sconf_upate_agent_info(device_id, "text");
        if (ret != 0) {
            LOGW("%s %d ret:%d, Failed to update agent info\r\n", __func__, __LINE__, ret);
            goto exit;
        }

        #if CONFIG_BK_VIDEO_ENGINE
        ret = video_engine_deinit();
        if (ret != BK_OK) {
            LOGE("%s: Failed to deinitialize video engine, ret=%d\n", __func__, ret);
            goto exit;
        }
        #endif
        #if (CONFIG_DUAL_SCREEN_AVI_PLAY)
        //TODO: switch to text screen
        #endif
        is_enable_ir_mode = false;
        LOGI("%s %d, Successfully switched to text mode\r\n", __func__, __LINE__);
    }

exit:
    config_ir_mode_switch_thread_handle = NULL;
    ir_mode_switching = 0;
    BK_LOGI(TAG, "ir_mode_switch_main end\r\n");
    rtos_delete_thread(NULL);
}

void bk_sconf_begin_to_switch_ir_mode(void)
{
    int ret = 0;
    if (config_ir_mode_switch_thread_handle) {
        BK_LOGW(TAG, "Last oper for IR_MODE ongoing!\n");
        return;
    }
    BK_LOGW(TAG, "Start to switch image recognition mode!\n");
#if CONFIG_PSRAM_AS_SYS_MEMORY
    ret = rtos_create_psram_thread(&config_ir_mode_switch_thread_handle,
                                CONFIG_IR_MODE_SWITCH_TASK_PRIORITY,
                                "ir_mode_switch",
                                (beken_thread_function_t)bk_sconf_switch_ir_mode_handler,
                                4096,
                                (beken_thread_arg_t)0);
#else
    ret = rtos_create_thread(&config_ir_mode_switch_thread_handle,
                                CONFIG_IR_MODE_SWITCH_TASK_PRIORITY,
                                "ir_mode_switch",
                                (beken_thread_function_t)bk_sconf_switch_ir_mode_handler,
                                4096,
                                (beken_thread_arg_t)0);
#endif
    if (ret != kNoErr)
    {
        BK_LOGE(TAG, "switch image recognition mode fail: %d\r\n", ret);
        config_ir_mode_switch_thread_handle = NULL;
    }
}

void bk_sconf_ble_msg_handler(ble_prov_msg_t *msg)
{
    LOGI("bk_sconf_ble_msg_handler, event:%d\n", msg->event);
    switch (msg->event)
    {
        case BOARDING_OP_STATION_START:
        {
            bk_ble_provisioning_info_t *bk_ble_provisioning_info = bk_ble_provisioning_get_boarding_info();
            bk_sconf_wifi_sta_connect(bk_ble_provisioning_info->ble_prov_info.ssid_value,
                                        bk_ble_provisioning_info->ble_prov_info.password_value);
            bk_event_unregister_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                                        bk_sconf_wlan_scan_done_handler);
        }
        break;

        case BOARDING_OP_START_WIFI_SCAN:
        {
            LOGI("BOARDING_OP_START_WIFI_SCAN\n");
            if (msg->param) {
                if (*(uint8_t *)msg->param == 1)
                    g_ble_split_pkt = true;
                else
                    g_ble_split_pkt = false;
            }
            bk_event_register_cb(EVENT_MOD_WIFI, EVENT_WIFI_SCAN_DONE,
                                        bk_sconf_wlan_scan_done_handler, NULL);
            BK_LOG_ON_ERR(bk_wifi_scan_start(NULL));
        }
        break;

        case BOARDING_OP_BLE_DISABLE:
        {
            LOGI("close bluetooth ing\n");
#if CONFIG_BLUETOOTH
            bk_ble_provisioning_deinit();
            bk_bluetooth_deinit();
            LOGI("close bluetooth finish!\r\n");
#endif
        }
        break;
        case BOARDING_OP_NET_PAN_START:
        {
            LOGI("DBEVT_NET_PAN_REQUEST\n");
            int status = 1;
#if CONFIG_NET_PAN
            bk_bt_enter_pairing_mode(1);
            status = 0;
#endif
            uint8_t bt_mac[6];
            bk_ap_get_mac(bt_mac, MAC_TYPE_BLUETOOTH);
            bk_ble_provisioning_event_notify_with_data(BOARDING_OP_NET_PAN_START, status, (char *)bt_mac, 6);
        }
        break;
#if CONFIG_BK_MODEM
        case BOARDING_OP_START_BK_MODEM:
        {
            int ret = 0;

            // let the 4G module power on
            bk_err_t gpio_dev_unmap(gpio_id_t gpio_id);
            gpio_dev_unmap(GPIO_21);
            bk_gpio_disable_pull(GPIO_21);
            bk_gpio_enable_output(GPIO_21); 
            bk_gpio_set_output_low(GPIO_21);
            
            ret = bk_modem_init(UART_NIC_MODE, UART_IF);
            if (ret) {
                bk_modem_init(UART_NIC_MODE, UART_IF);
            }
        }
        break;
#endif
        case BOARDING_OP_NETWORK_PROVISIONING_FIRST_TIME:
        {
            LOGI("BOARDING_OP_NETWORK_PROVISIONING_FIRST_TIME, %d\r\n", *(uint8_t *)(msg->param));
        }
        break;
        case BOARDING_OP_AGENT_RSP:
        {
            LOGI("BOARDING_OP_AGENT_RSP\n");
            bk_sconf_prase_agent_info((char *)msg->param, 1);
        }
        break;

        case BOARDING_OP_SYNC_SUPPORTED_ENGINE:
        {
            uint8_t val = bk_sconf_get_supported_engine();
            bk_ble_provisioning_event_notify_with_data(BOARDING_OP_SYNC_SUPPORTED_ENGINE, 0, (char *)&val, 1);
        }
        break;

        case BOARDING_OP_SYNC_SUPPORTED_NETWORK:
        {
            uint8_t *val = NULL, len = 0;
            val = bk_sconf_get_supported_network(&len);
            bk_ble_provisioning_event_notify_with_data(BOARDING_OP_SYNC_SUPPORTED_NETWORK, 0, (char *)val, len);
            if (val)
                os_free(val);
        }
        break;

        default:
        {
            LOGI("%s %d, do nothing\r\n", __func__, msg->event);
            unsigned char payload = 'a';
            //100 is beken private definition, means unsupport status code
            bk_ble_provisioning_event_notify_with_data(msg->event, 100, (char *)(&payload), 1);
        }
        break;
    }
}

void bk_sconf_network_provisioning_status_cb(bk_network_provisioning_status_t status, void *user_data)
{
    LOGI("demo network provisioning status: %d\n", status);
    switch (status)
    {
        case BK_NETWORK_PROVISIONING_STATUS_IDLE:
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RUNNING:
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_NETWORK_PROVISIONING, 0);
            #endif
            break;
        case BK_NETWORK_PROVISIONING_STATUS_SUCCEED:
            s_network_provisioned = true;
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_NETWORK_PROVISIONING_SUCCESS, 0);
            #endif
            if (bk_network_provisioning_get_type() == BK_NETWORK_PROVISIONING_TYPE_BLE) {
                netif_if_t netif_idx = (netif_if_t)user_data;
                netif_ip4_config_t ip4_config = {0};

                bk_netif_get_ip4_config(netif_idx, &ip4_config);
                LOGI("netif_idx:%d, ip: %s\n", netif_idx, ip4_config.ip);
                bk_ble_provisioning_event_notify_with_data(BOARDING_OP_STATION_START, BK_OK, ip4_config.ip, strlen(ip4_config.ip));

                char payload[256] = {0};
                int len = bk_sconf_build_agent_identity_payload(payload, sizeof(payload));
                if (len > 0) {
                    bk_ble_provisioning_event_notify_with_data(BOARDING_OP_SET_AGENT_INFO,
                                                               0,
                                                               payload,
                                                               (uint16_t)len);
                    LOGI("agent identity returned by BLE: %s\r\n", payload);
                } else {
                    LOGW("build agent identity payload failed: %d\r\n", len);
                }

                LOGI("Network provisioning success, RTC start is controlled by UI\r\n");
            }
            break;
        case BK_NETWORK_PROVISIONING_STATUS_FAILED:
            s_network_provisioned = false;
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_NETWORK_PROVISIONING_FAIL, 0);
            #endif
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RECONNECTING:
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_RECONNECT_NETWORK, 0);
            #endif
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RECONNECT_FAILED:
            s_network_provisioned = false;
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_RECONNECT_NETWORK_FAIL, 0);
            #endif
            break;
        case BK_NETWORK_PROVISIONING_STATUS_RECONNECT_SUCCEED:
        {
            s_network_provisioned = true;
            #if CONFIG_APP_EVT
            app_event_send_msg(APP_EVT_RECONNECT_NETWORK_SUCCESS, 0);
            #endif

            LOGI("Network reconnect success, RTC start is controlled by UI\r\n");
        }
        break;
        default:
            break;
    }
}
void bk_sconf_erase_smart_config(void)
{
    int ret;

    LOGI("erase smart config\r\n");
    erase_network_auto_reconnect_info();
    bk_sconf_erase_channel_name();
    bk_sconf_clear_agent_token();
    s_network_provisioned = false;
    smart_config_running = false;

    ret = bk_wifi_sta_stop();
    if (ret != BK_OK) {
        LOGW("stop Wi-Fi STA after erase failed, ret=%d\r\n", ret);
    }
}
void bk_sconf_factory_reset(void)
{
    LOGI("factory reset requested\r\n");
    bk_pm_module_vote_power_ctrl(PM_POWER_MODULE_NAME_BTSP, PM_POWER_MODULE_STATE_OFF);
    bk_factory_reset();
    bk_sconf_erase_smart_config();
    bk_reboot();
}
void bk_sconf_prepare_for_smart_config(void)
{
    smart_config_running = true;

    bk_wifi_sta_stop();

#if CONFIG_BK_MODEM
    bk_modem_deinit();
#endif

#if CONFIG_NET_PAN
    bk_bt_enter_pairing_mode(1);
#endif

    bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
}
int bk_sconf_sync_flash_request(void)
{
    int ret = -1;
    BK_LOGD(TAG, "start to sync sconf flash\n");
    ret = rtos_init_semaphore(&sync_flash_sema, 1);
    if (ret)
        goto exit;

    app_event_send_msg(APP_EVT_SYNC_FLASH, 0);

    if (sync_flash_sema) {
        ret = rtos_get_semaphore(&sync_flash_sema, 5000);
        if (ret) {
            goto exit;
        } else {
            ret = 0;
        }
    }
exit:
    if(sync_flash_sema) {
       rtos_deinit_semaphore(&sync_flash_sema);
       sync_flash_sema = NULL;
    }

    return ret;
}

void bk_sconf_sync_flash_handler(void)
{
    bk_config_sync_flash_safely();
    if (sync_flash_sema) {
        rtos_set_semaphore(&sync_flash_sema);
    }
}

/* Encoding of bk_sconf_cli_mode_switch_handler's beken_thread_arg_t:
 *   0 -> text mode
 *   1 -> vision mode (also brings up video_engine for uplink H.264)
 *   2 -> vision mode without video_engine (camera owned by another module
 *        e.g. camera_preview; see bk_sconf_enter_vision_mode_no_video) */
#define SCONF_MODE_ARG_TEXT             0
#define SCONF_MODE_ARG_VISION           1
#define SCONF_MODE_ARG_VISION_NO_VIDEO  2

static void bk_sconf_cli_mode_switch_handler(beken_thread_arg_t arg)
{
    char device_id[128] = {0};
    int mode_arg = (int)(intptr_t)arg;
    int to_vision = (mode_arg == SCONF_MODE_ARG_VISION ||
                     mode_arg == SCONF_MODE_ARG_VISION_NO_VIDEO);
    int with_video = (mode_arg == SCONF_MODE_ARG_VISION);
    int ret;
    bool rtc_was_running = false;

    if (bk_sconf_get_channel_name(device_id) != 0
        || os_strlen(device_id) == 0) {
        LOGW("sconf mode: no RTC channel\r\n");
        goto done;
    }

    if (to_vision) {
#if CONFIG_BK_VIDEO_ENGINE
        if (with_video && !video_engine_is_running()) {
            ret = video_engine_init();
            if (ret != BK_OK) {
                LOGE("sconf vision: video_engine_init failed ret=%d\r\n", ret);
                goto done;
            }
        } else if (!with_video) {
            LOGI("sconf vision (no_video): skip video_engine_init "
                 "(camera owned by another module)\r\n");
        }
#else
        (void)with_video;
#endif
        ret = bk_sconf_start_rtc_for_model(device_id, "vision", &rtc_was_running);
        if (ret != BK_OK) {
            goto done;
        }

        if (rtc_was_running) {
            ret = bk_sconf_upate_agent_info(device_id, "vision");
            if (ret != BK_OK) {
                LOGW("sconf vision: agent update failed ret=%d\r\n", ret);
            } else {
                LOGI("sconf vision%s: OK\r\n",
                     with_video ? "" : " (no_video)");
            }
        } else {
            LOGI("sconf vision%s: started directly\r\n",
                 with_video ? "" : " (no_video)");
        }
    } else {
        /* CLI sconf text: stop UVC/pipeline before Agora/HTTP to avoid URB OOM and stream_stop fault */
#if CONFIG_BK_VIDEO_ENGINE
        if (video_engine_is_running()) {
            ret = video_engine_deinit();
            if (ret != BK_OK) {
                LOGE("sconf text: video_engine_deinit failed ret=%d\r\n", ret);
            } else {
                rtos_delay_milliseconds(50);
            }
        }
#endif
        ret = bk_sconf_start_rtc_for_model(device_id, "text", &rtc_was_running);
        if (ret != BK_OK) {
            goto done;
        }

        if (rtc_was_running) {
            ret = bk_sconf_upate_agent_info(device_id, "text");
            if (ret != BK_OK) {
                LOGW("sconf text: agent update failed ret=%d\r\n", ret);
            } else {
                LOGI("sconf text: OK\r\n");
            }
        } else {
            LOGI("sconf text: started directly\r\n");
        }
    }

done:
    s_sconf_cli_mode_thread_handle = NULL;
    rtos_delete_thread(NULL);
}

static int bk_sconf_begin_cli_mode_switch(int mode_arg)
{
    int ret;

    if (s_sconf_cli_mode_thread_handle) {
        LOGW("sconf mode switch already running\r\n");
        return BK_FAIL;
    }
#if CONFIG_PSRAM_AS_SYS_MEMORY
    ret = rtos_create_psram_thread(&s_sconf_cli_mode_thread_handle,
                                   CONFIG_IR_MODE_SWITCH_TASK_PRIORITY,
                                   "sconf_mode",
                                   (beken_thread_function_t)bk_sconf_cli_mode_switch_handler,
                                   4096,
                                   (beken_thread_arg_t)(intptr_t)mode_arg);
#else
    ret = rtos_create_thread(&s_sconf_cli_mode_thread_handle,
                             CONFIG_IR_MODE_SWITCH_TASK_PRIORITY,
                             "sconf_mode",
                             (beken_thread_function_t)bk_sconf_cli_mode_switch_handler,
                             4096,
                             (beken_thread_arg_t)(intptr_t)mode_arg);
#endif
    if (ret != kNoErr) {
        LOGE("sconf mode thread fail: %d\r\n", ret);
        s_sconf_cli_mode_thread_handle = NULL;
        return BK_FAIL;
    }

    return BK_OK;
}

int bk_sconf_enter_text_mode(void)
{
    return bk_sconf_begin_cli_mode_switch(SCONF_MODE_ARG_TEXT);
}

int bk_sconf_enter_vision_mode(void)
{
    return bk_sconf_begin_cli_mode_switch(SCONF_MODE_ARG_VISION);
}

int bk_sconf_enter_vision_mode_no_video(void)
{
    return bk_sconf_begin_cli_mode_switch(SCONF_MODE_ARG_VISION_NO_VIDEO);
}

int bk_sconf_exit_ai_mode(int from_vision)
{
    int ret = BK_OK;

    LOGI("%s from_vision=%d\r\n", __func__, from_vision);

    ret = bk_sconf_stop_rtc();

#if CONFIG_BK_VIDEO_ENGINE
    if (from_vision && video_engine_is_running()) {
        int video_ret = video_engine_deinit();
        if (video_ret != BK_OK) {
            LOGE("sconf exit: video_engine_deinit failed ret=%d\r\n", video_ret);
            if (ret == BK_OK) {
                ret = video_ret;
            }
        }
    }
#else
    (void)from_vision;
#endif

    return ret;
}

/* ----------------------------------------------------------------------
 * Asynchronous exit
 *
 * The synchronous bk_sconf_exit_ai_mode() runs Agora teardown, video engine
 * deinit, MIPI camera close (which contains a fixed 80ms drain delay) and
 * H.264 encoder shutdown. Measured cost is ~250ms-1s, dominated by the
 * fixed delay and Agora destroy.
 *
 * When invoked from on_screen_prev (page nav callbacks), the LVGL display
 * lock is held for that entire duration, freezing all UI animations. This
 * worker decouples teardown from the caller so the LV tree can be swapped
 * to page_3 immediately and the heavy stop happens in the background.
 * -------------------------------------------------------------------- */

static void bk_sconf_exit_ai_mode_handler(beken_thread_arg_t arg)
{
    int from_vision = (int)(intptr_t)arg;
    LOGI("sconf exit worker start, from_vision=%d\r\n", from_vision);
    (void)bk_sconf_exit_ai_mode(from_vision);
    LOGI("sconf exit worker done\r\n");
    s_sconf_exit_thread_handle = NULL;
    rtos_delete_thread(NULL);
}

int bk_sconf_exit_ai_mode_async(int from_vision)
{
    int ret;

    if (s_sconf_exit_thread_handle) {
        LOGW("sconf exit already running, coalesce\r\n");
        return BK_FAIL;
    }
#if CONFIG_PSRAM_AS_SYS_MEMORY
    ret = rtos_create_psram_thread(&s_sconf_exit_thread_handle,
                                   CONFIG_IR_MODE_SWITCH_TASK_PRIORITY,
                                   "sconf_exit",
                                   (beken_thread_function_t)bk_sconf_exit_ai_mode_handler,
                                   4096,
                                   from_vision ? (beken_thread_arg_t)(void *)1 : (beken_thread_arg_t)0);
#else
    ret = rtos_create_thread(&s_sconf_exit_thread_handle,
                             CONFIG_IR_MODE_SWITCH_TASK_PRIORITY,
                             "sconf_exit",
                             (beken_thread_function_t)bk_sconf_exit_ai_mode_handler,
                             4096,
                             from_vision ? (beken_thread_arg_t)(void *)1 : (beken_thread_arg_t)0);
#endif
    if (ret != kNoErr) {
        LOGE("sconf exit thread fail: %d\r\n", ret);
        s_sconf_exit_thread_handle = NULL;
        return BK_FAIL;
    }

    return BK_OK;
}

static void bk_sconf_cli_handler(char *pcWriteBuffer, int xWriteBufferLen, int argC, char **argV)
{
    if ((argC == 2) && (os_strcmp(argV[1], "start") == 0)) {
        bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
    } else if ((argC == 2) && (os_strcmp(argV[1], "erase") == 0)) {
        bk_sconf_erase_smart_config();
    } else if ((argC == 2) && (os_strcmp(argV[1], "reset") == 0)) {
        bk_sconf_factory_reset();
    } else if ((argC == 2) && (os_strcmp(argV[1], "vision") == 0)) {
        bk_sconf_begin_cli_mode_switch(1);
    } else if ((argC == 2) && (os_strcmp(argV[1], "text") == 0)) {
        bk_sconf_begin_cli_mode_switch(0);
    } else if ((argC == 2) && (os_strcmp(argV[1], "rtc_start") == 0)) {
        bk_sconf_start_rtc();
    } else if ((argC == 2) && (os_strcmp(argV[1], "rtc_stop") == 0)) {
        bk_sconf_stop_rtc();
    } else if (argC == 3) {
        if (os_strcmp(argV[2], "ble") == 0) {
            bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_BLE);
        } else if (os_strcmp(argV[2], "console") == 0) {
            bk_network_provisioning_start(BK_NETWORK_PROVISIONING_TYPE_CONSOLE);
        }
    }
    else {
        LOGI("sconf un-supported command %s\n", argV[1]);
    }
}
#if 0
int bk_sconf_netif_event_cb(void *arg, event_module_t event_module,
					   int event_id, void *event_data)
{
	netif_event_got_ip4_t *got_ip;
    char device_id[128] = {0};
    int ret = 0;

	switch (event_id) {
	case EVENT_NETIF_GOT_IP4:
		got_ip = (netif_event_got_ip4_t *)event_data;
		LOGD("%s got ip\n", got_ip->netif_if == NETIF_IF_STA ? "BK STA" : "unknown netif");
        ret = bk_sconf_get_channel_name(device_id);
        if ((ret == 0) && (os_strlen(device_id) > 0)) {
            bk_sconf_start_network_transfer(device_id);
        }
        else {
            LOGW("No device_id, do nothing\r\n");
        }
		break;
	default:
		LOGD("rx event <%d %d>\n", event_module, event_id);
		break;
	}

	return BK_OK;
}

int bk_sconf_wifi_event_cb(void *arg, event_module_t event_module,
					  int event_id, void *event_data)
{
	wifi_event_sta_disconnected_t *sta_disconnected;
	wifi_event_sta_connected_t *sta_connected;
	wifi_event_ap_disconnected_t *ap_disconnected;
	wifi_event_ap_connected_t *ap_connected;
	wifi_event_network_found_t *network_found;

	switch (event_id) {
	case EVENT_WIFI_STA_CONNECTED:
		sta_connected = (wifi_event_sta_connected_t *)event_data;
		LOGD("BK STA connected %s\n", sta_connected->ssid);
		break;

	case EVENT_WIFI_STA_DISCONNECTED:
		sta_disconnected = (wifi_event_sta_disconnected_t *)event_data;
		LOGD("BK STA disconnected, reason(%d)%s\n", sta_disconnected->disconnect_reason,
			sta_disconnected->local_generated ? ", local_generated" : "");

		break;

	case EVENT_WIFI_AP_CONNECTED:
		ap_connected = (wifi_event_ap_connected_t *)event_data;
		LOGD(BK_MAC_FORMAT" connected to BK AP\n", BK_MAC_STR(ap_connected->mac));
		break;

	case EVENT_WIFI_AP_DISCONNECTED:
		ap_disconnected = (wifi_event_ap_disconnected_t *)event_data;
		LOGD(BK_MAC_FORMAT" disconnected from BK AP\n", BK_MAC_STR(ap_disconnected->mac));
		break;

	case EVENT_WIFI_NETWORK_FOUND:
		network_found = (wifi_event_network_found_t *)event_data;
		LOGD(" target AP: %s, bssid %pm found\n", network_found->ssid, network_found->bssid);
		break;

	default:
		LOGD("rx event <%d %d>\n", event_module, event_id);
		break;
	}

	return BK_OK;
}
#endif
#define BK_SCONF_CMD_COUNT (sizeof(s_bk_sconf_commands) / sizeof(s_bk_sconf_commands[0]))
static const struct cli_command s_bk_sconf_commands[] = {
    {"sconf", "sconf start|erase|reset|vision|text|rtc_start|rtc_stop [ble|console]", bk_sconf_cli_handler},
};

int bk_sconf_cli_network_provisioning_init(void)
{
    return cli_register_commands(s_bk_sconf_commands, BK_SCONF_CMD_COUNT);
}
int bk_sconf_init(void)
{
    g_ble_split_pkt = false;
    //for user to receive network provisioning status change event
    bk_register_network_provisioning_status_cb(bk_sconf_network_provisioning_status_cb);
    //if default provisioning type is ble, then set msg handle cb
    bk_ble_provisioning_set_msg_handle_cb(bk_sconf_ble_msg_handler);
    // bk_event_register_cb(EVENT_MOD_WIFI, EVENT_ID_ALL, bk_sconf_wifi_event_cb, NULL);
    // bk_event_register_cb(EVENT_MOD_NETIF, EVENT_ID_ALL, bk_sconf_netif_event_cb, NULL);

    bk_network_auto_reconnect_init(NULL);
    bk_sconf_cli_network_provisioning_init();

    return BK_OK;
}