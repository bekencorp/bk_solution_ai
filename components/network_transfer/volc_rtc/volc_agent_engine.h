#ifndef __VOLC_AGENT_ENGINE_H__
#define __VOLC_AGENT_ENGINE_H__

#ifdef __cplusplus
extern "C" {
#endif
#include "RtcWebClientUtils.h"
#include "volc_rtc_engine.h"

#if CONFIG_STARTUP_AGENT_FROM_BK_SERVER && !CONFIG_BK_SMART_CONFIG
    //# error "CONFIG_BK_SMART_CONFIG must be enabled when CONFIG_STARTUP_AGENT_FROM_BK_SERVER is enable"
#endif

typedef enum
{
	HTTP_STATUS_SUCCESS = 200,
	HTTP_STATUS_PARAM_ERROR = 400,
	HTTP_STATUS_MAX_AGENT_UPTIME_EXCEEDED = 403,
	HTTP_STATUS_TRIAL_LIMIT_EXCEEDED = 404,
	HTTP_STATUS_DEVICE_REMOVED = 405,
	HTTP_STATUS_AGENT_START_FAILED = 406,
}agent_status_code;

#define VOLC_AGENT_RCV_BUF_SIZE                2048
#define VOLC_AGENT_SEND_HEADER_SIZE            1024
#define VOLC_AGENT_POST_DATA_MAX_SIZE          1024*2
#define VOLC_AGENT_MAX_URL_LEN                 256

/* API */
int volc_agent_start(byte_rtc_room_info_t *room_info, void *device_id);
int volc_agent_stop(byte_rtc_room_info_t *room_info, void *device_id);
int volc_agent_update(byte_rtc_room_info_t *room_info, void *device_id, void *update_info);
#if CONFIG_STARTUP_AGENT_FROM_BK_SERVER
//uint16_t volc_generate_start_agent_req_payload(char *payload, uint16_t max_len);
#endif

#ifdef __cplusplus
}
#endif
#endif /* __VOLC_AGENT_ENGINE_H__ */
