#ifndef __BK_SMART_CONFIG_H__
#define __BK_SMART_CONFIG_H__

#include <stdbool.h>

#include "bk_network_provisioning.h"

#define CONFIG_IR_MODE_SWITCH_TASK_PRIORITY 4

#define MAX_APPID_LEN       64
#define MAX_ROOMID_LEN      128
#define MAX_USERID_LEN      128
#define MAX_TOKEN_LEN       256
#define MAX_TASKID_LEN      128
#define MAX_BOTUID_LEN      128

typedef enum
{
    BOARDING_OP_UNKNOWN = 0,
    BOARDING_OP_STATION_START = 1,
    BOARDING_OP_SOFT_AP_START = 2,
    BOARDING_OP_SERVICE_UDP_START = 3,
    BOARDING_OP_SERVICE_TCP_START = 4,
    BOARDING_OP_SET_CS2_DID = 5,
    BOARDING_OP_SET_CS2_APILICENSE = 6,
    BOARDING_OP_SET_CS2_KEY = 7,
    BOARDING_OP_SET_CS2_INIT_STRING = 8,
    BOARDING_OP_SRRVICE_CS2_START = 9,
    BOARDING_OP_BLE_DISABLE = 10,
    BOARDING_OP_SET_WIFI_CHANNEL = 11,
    BOARDING_OP_AGENT_RSP = 12,
    BOARDING_OP_SET_AGENT_INFO = 13,
    BOARDING_OP_NET_PAN_START = 14,
    BOARDING_OP_NETWORK_PROVISIONING_FIRST_TIME = 15,
    BOARDING_OP_RESERVED = 16,
    BOARDING_OP_START_BK_MODEM = 23,
    BOARDING_OP_START_WIFI_SCAN = 24,
    BOARDING_OP_SYNC_SUPPORTED_ENGINE = 25,
    BOARDING_OP_SYNC_SUPPORTED_NETWORK = 26,

    //device reserved opcode
    BOARDING_OP_NFC_GOT_ID = 150,

    //server reserved opcode
    BOARDING_OP_SERVER_CHECK_VERSION = 500,
    BOARDING_OP_MAX
} boarding_opcode_t;

typedef struct
{
    uint8_t valid;
    char channel_name[128];
} bk_sconf_agent_info_t;

int bk_sconf_get_channel_name(char *chan);
int bk_sconf_init(void);
void bk_sconf_prepare_for_smart_config(void);
int bk_sconf_start_rtc(void);
int bk_sconf_stop_rtc(void);
int bk_sconf_enter_text_mode(void);
int bk_sconf_enter_vision_mode(void);
int bk_sconf_exit_ai_mode(int from_vision);

/**
 * @brief Asynchronous variant of bk_sconf_exit_ai_mode.
 *
 * Spawns a worker thread that runs bk_sconf_exit_ai_mode(from_vision) in the
 * background and returns immediately. Use from contexts that hold a lock the
 * heavy teardown should not block — most importantly the LVGL display lock
 * held by ui_nav_dispatch_event during page nav callbacks. The synchronous
 * teardown chain (Agora RTC destroy + video_engine_deinit + camera close)
 * can take 250 ms - 1 s depending on RTC activity, which would otherwise
 * freeze the UI.
 *
 * Re-entry: if a previous async exit worker is still running, this call
 * returns BK_FAIL without spawning a second one. The first invocation wins;
 * subsequent presses while teardown is in flight are coalesced.
 *
 * @return BK_OK if the worker was successfully scheduled; BK_FAIL if a prior
 *         worker is still running or the OS could not create the thread.
 */
int bk_sconf_exit_ai_mode_async(int from_vision);
const char *bk_sconf_get_start_model_type(void);
int bk_sconf_sync_flash_request(void);
void bk_sconf_sync_flash_handler(void);
void bk_sconf_erase_smart_config(void);
void bk_sconf_begin_to_switch_ir_mode(void);

/**
 * @brief Whether the device is currently provisioned with a working network link.
 *
 * The flag is set true by bk_sconf_network_provisioning_status_cb on
 * BK_NETWORK_PROVISIONING_STATUS_SUCCEED / RECONNECT_SUCCEED, and reset to
 * false on FAILED / RECONNECT_FAILED.
 */
bool bk_sconf_is_network_provisioned(void);
#endif
