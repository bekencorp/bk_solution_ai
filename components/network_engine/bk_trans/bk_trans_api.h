#ifndef __BK_TRANS_API_H__
#define __BK_TRANS_API_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "network_engine.h"

#define BK_TRANS_CONFIG_MAGIC 0x4254524eU
#define BK_TRANS_SERVICE_NAME_LEN 100
#define BK_TRANS_IP_ADDR_LEN 16
#define BK_TRANS_PORT_LEN 6

typedef enum {
    BK_TRANS_MODE_TCP = 0,
    BK_TRANS_MODE_UDP,
    BK_TRANS_MODE_CS2,
} bk_trans_mode_t;

typedef struct {
    uint32_t magic;
    bk_trans_mode_t mode;
    char service_name[BK_TRANS_SERVICE_NAME_LEN];
    bool start_video;
    bool start_audio;
    bool has_server_info;
    char ip_addr[BK_TRANS_IP_ADDR_LEN];
    char cmd_port[BK_TRANS_PORT_LEN];
    char video_port[BK_TRANS_PORT_LEN];
    char audio_port[BK_TRANS_PORT_LEN];
} bk_trans_config_t;

bk_err_t bk_trans_set_config(const bk_trans_config_t *config);
bk_err_t bk_trans_start(void *user_data);
bk_err_t bk_trans_deinit(void *user_data);
bk_err_t bk_trans_stop(void *user_data);
bk_err_t bk_trans_pre_config(void *user_data);
int bk_trans_update(void *user_data, void *update_info);
int bk_trans_audio_data_send(uint8_t *data_ptr, size_t data_len, audio_enc_type_t audio_type);
int bk_trans_video_data_send(frame_buffer_t *frame);
bool bk_trans_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK_TRANS_API_H__ */
