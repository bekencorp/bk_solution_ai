#ifndef __BK_VOLC_RTC_H__
#define __BK_VOLC_RTC_H__

#ifdef __cplusplus
extern "C" {
#endif

//#include <stdbool.h>

#include "volc_rtc_engine.h"
#include "network_engine.h"

#define VIDEO_FRAME_INTERVAL            500

bk_err_t bk_byte_start(void *device_id);
bk_err_t bk_byte_stop(void *device_id);
bk_err_t bk_byte_pre_config(void *device_id);
int bk_byte_update_agent(void *device_id, void *update_info);
int bk_byte_rtc_audio_data_send(uint8_t *data_ptr, size_t data_len, audio_enc_type_t audio_type);
int bk_byte_rtc_video_data_send(frame_buffer_t *frame);

#ifdef __cplusplus
}
#endif
#endif /* __BK_VOLC_RTC_H__ */
