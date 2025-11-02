// Copyright (2025) Beijing Volcano Engine Technology Ltd.
// SPDX-License-Identifier: MIT

#include "RtcWebClientUtils.h"
#include <common/sys_config.h>
#include <components/log.h>
#include <string.h>
#include <components/system.h>
#include <os/os.h>
#include "volc_memory.h"
#include "volc_agent_engine.h"

#define TAG "RTC_WEBCLIENT"

int rtc_http_post(rtc_post_config_t* config, rtc_req_result_t *req_result) {
    int resp_status = -1;
    int bytes_read = 0;

    if (!config || !config->uri || !config->session || !config->post_data) {
        BK_LOGE(TAG, "Invalid parameters: config\r\n");
        return -1;
    }

    if (!req_result || !req_result->response) {
        BK_LOGE(TAG, "Invalid parameters: req_result or req_result->response\r\n");
        return -2;
    }

    /* send POST request by default header */
    if ((resp_status = webclient_post(config->session, config->uri, config->post_data, os_strlen(config->post_data))) != 200) {
        BK_LOGE(TAG,"webclient POST request failed, response(%d) error.\n", resp_status);
        return -3;
    }

    BK_LOGI(TAG,"webclient post response data: \n");

    do 
    {
		bytes_read = webclient_read(config->session, req_result->response, VOLC_AGENT_RCV_BUF_SIZE);
		if (bytes_read > 0)
		{
			break;
		}
	} while (1);

    req_result->code = resp_status;
    req_result->response[bytes_read] = '\0';

    return BK_OK;
}