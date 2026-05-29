// Copyright (2025) Beijing Volcano Engine Technology Ltd.
// SPDX-License-Identifier: MIT

#ifndef __RTC_WEBCLIENT_UTILS_H__
#define __RTC_WEBCLIENT_UTILS_H__
#include "components/webclient.h"

typedef struct webclient_session rtc_session;

typedef struct {
    int code;
    char* response;
} rtc_req_result_t;

typedef struct {
    const char* uri;
    struct webclient_session* session;  // key1,value1,key2,value2....keyn,valuen,NULL
    const char* post_data;
} rtc_post_config_t;

int rtc_http_post(rtc_post_config_t* config, rtc_req_result_t *req_result);

#endif // __RTC_WEBCLIENT_UTILS_H__