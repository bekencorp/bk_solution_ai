#include <components/log.h>
#include <os/mem.h>
#include <os/str.h>

#include "network_transfer.h"
#include "bk_trans_api.h"

bk_err_t bk_udp_trans_service_init(char *service_name);
bk_err_t bk_udp_trans_service_deinit(void);
bk_err_t bk_tcp_trans_service_init(char *service_name);
bk_err_t bk_tcp_trans_service_deinit(void);
bk_err_t bk_cs2_trans_service_init(char *service_name);
bk_err_t bk_cs2_trans_service_deinit(void);

#define TAG "bk_trans"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static bool s_bk_trans_connected = false;
static bool s_bk_trans_inited = false;
static bool s_ctrl_chan_started = false;
static bool s_video_chan_started = false;
static bool s_video_chan_connected = false;
static bool s_audio_chan_started = false;
static bool s_audio_chan_connected = false;
static bool s_config_valid = false;
static bk_trans_config_t s_config;
static bk_trans_ctrl_recv_callback_t s_ctrl_recv_cb;
static bk_trans_event_callback_t s_event_cb;
static void *s_event_user_data;

static const char *bk_trans_mode_name(bk_trans_mode_t mode)
{
    switch (mode) {
    case BK_TRANS_MODE_TCP:
        return "tcp";
    case BK_TRANS_MODE_UDP:
        return "udp";
    case BK_TRANS_MODE_CS2:
        return "cs2";
    default:
        return "unknown";
    }
}

static const char *bk_trans_default_service_name(bk_trans_mode_t mode)
{
    switch (mode) {
    case BK_TRANS_MODE_TCP:
        return "bk_trans_tcp";
    case BK_TRANS_MODE_UDP:
        return "bk_trans_udp";
    case BK_TRANS_MODE_CS2:
        return "bk_trans_cs2";
    default:
        return "bk_trans";
    }
}

static void bk_trans_load_default_config(void)
{
    os_memset(&s_config, 0, sizeof(s_config));
    s_config.magic = BK_TRANS_CONFIG_MAGIC;
    s_config.mode = BK_TRANS_MODE_TCP;
    os_snprintf(s_config.service_name, sizeof(s_config.service_name), "%s",
                bk_trans_default_service_name(s_config.mode));
    s_config.start_video = true;
    s_config.start_audio = true;
    s_config_valid = true;
}

bk_err_t bk_trans_set_config(const bk_trans_config_t *config)
{
    if (!config || config->magic != BK_TRANS_CONFIG_MAGIC) {
        return BK_ERR_PARAM;
    }

    if (s_bk_trans_inited) {
        LOGW("bk_trans already started, ignore config update\n");
        return BK_FAIL;
    }

    os_memcpy(&s_config, config, sizeof(s_config));
    if (s_config.service_name[0] == '\0') {
        os_snprintf(s_config.service_name, sizeof(s_config.service_name), "%s",
                    bk_trans_default_service_name(s_config.mode));
    }
    s_config.service_name[sizeof(s_config.service_name) - 1] = '\0';
    s_config.magic = BK_TRANS_CONFIG_MAGIC;
    s_config_valid = true;

    return BK_OK;
}

static bk_err_t bk_trans_service_init(void)
{
    switch (s_config.mode) {
    case BK_TRANS_MODE_TCP:
        return bk_tcp_trans_service_init(s_config.service_name);
    case BK_TRANS_MODE_UDP:
        return bk_udp_trans_service_init(s_config.service_name);
    case BK_TRANS_MODE_CS2:
#if (CONFIG_CS2_P2P_SERVER || CONFIG_CS2_P2P_CLIENT)
        return bk_cs2_trans_service_init(s_config.service_name);
#else
        LOGW("CS2 network transfer is not enabled\n");
        return BK_FAIL;
#endif
    default:
        return BK_ERR_PARAM;
    }
}

static bk_err_t bk_trans_service_deinit(void)
{
    switch (s_config.mode) {
    case BK_TRANS_MODE_TCP:
        return bk_tcp_trans_service_deinit();
    case BK_TRANS_MODE_UDP:
        return bk_udp_trans_service_deinit();
    case BK_TRANS_MODE_CS2:
#if (CONFIG_CS2_P2P_SERVER || CONFIG_CS2_P2P_CLIENT)
        return bk_cs2_trans_service_deinit();
#else
        return BK_OK;
#endif
    default:
        return BK_ERR_PARAM;
    }
}

static bk_err_t bk_trans_start_channel(chan_type_t chan_type)
{
    bool *started = NULL;
    bk_err_t ret = BK_FAIL;

    switch (chan_type) {
    case NTWK_TRANS_CHAN_CTRL:
        started = &s_ctrl_chan_started;
        break;
    case NTWK_TRANS_CHAN_VIDEO:
        started = &s_video_chan_started;
        break;
    case NTWK_TRANS_CHAN_AUDIO:
        started = &s_audio_chan_started;
        break;
    default:
        return BK_ERR_PARAM;
    }

    if (*started) {
        return BK_OK;
    }

    ret = ntwk_trans_chan_start(chan_type, NULL);
    if (ret == BK_OK) {
        *started = true;
    } else {
        LOGE("start channel %d failed ret=%d\n", chan_type, ret);
    }

    return ret;
}

static bk_trans_channel_t bk_trans_channel_from_sdk(chan_type_t channel)
{
    switch (channel) {
    case NTWK_TRANS_CHAN_CTRL:
        return BK_TRANS_CHAN_CTRL;
    case NTWK_TRANS_CHAN_VIDEO:
        return BK_TRANS_CHAN_VIDEO;
    case NTWK_TRANS_CHAN_AUDIO:
        return BK_TRANS_CHAN_AUDIO;
    default:
        return BK_TRANS_CHAN_CTRL;
    }
}

static bk_trans_event_code_t bk_trans_event_from_sdk(evt_code_t code)
{
    switch (code) {
    case NTWK_TRANS_EVT_CONNECTED:
        return BK_TRANS_EVT_CONNECTED;
    case NTWK_TRANS_EVT_DISCONNECTED:
        return BK_TRANS_EVT_DISCONNECTED;
    case NTWK_TRANS_EVT_STOP:
        return BK_TRANS_EVT_STOP;
    default:
        return BK_TRANS_EVT_OTHER;
    }
}

static void bk_trans_emit_event(ntwk_trans_event_t *event)
{
    bk_trans_event_t trans_event = {0};

    if (!event || !s_event_cb) {
        return;
    }

    trans_event.channel = bk_trans_channel_from_sdk(event->chan_type);
    trans_event.code = bk_trans_event_from_sdk(event->code);
    s_event_cb(&trans_event, s_event_user_data);
}

static void bk_trans_event_cb(ntwk_trans_event_t *event)
{
    if (!event) {
        return;
    }

    LOGI("event channel=%d code=%d\n", event->chan_type, event->code);

    if (event->chan_type == NTWK_TRANS_CHAN_CTRL) {
        if (event->code == NTWK_TRANS_EVT_CONNECTED) {
            s_bk_trans_connected = true;
        } else if (event->code == NTWK_TRANS_EVT_DISCONNECTED
                   || event->code == NTWK_TRANS_EVT_STOP) {
            s_bk_trans_connected = false;
            if (event->code == NTWK_TRANS_EVT_STOP) {
                s_ctrl_chan_started = false;
            }
        }
        bk_trans_emit_event(event);
        return;
    }

    if (event->chan_type == NTWK_TRANS_CHAN_VIDEO) {
        if (event->code == NTWK_TRANS_EVT_CONNECTED) {
            s_video_chan_connected = true;
        } else if (event->code == NTWK_TRANS_EVT_DISCONNECTED ||
                   event->code == NTWK_TRANS_EVT_STOP) {
            s_video_chan_connected = false;
            if (event->code == NTWK_TRANS_EVT_STOP) {
                s_video_chan_started = false;
            }
        }
    } else if (event->chan_type == NTWK_TRANS_CHAN_AUDIO) {
        if (event->code == NTWK_TRANS_EVT_CONNECTED) {
            s_audio_chan_connected = true;
        } else if (event->code == NTWK_TRANS_EVT_DISCONNECTED ||
                   event->code == NTWK_TRANS_EVT_STOP) {
            s_audio_chan_connected = false;
            if (event->code == NTWK_TRANS_EVT_STOP) {
                s_audio_chan_started = false;
            }
        }
    }

    bk_trans_emit_event(event);
}

static int bk_trans_ctrl_recv(uint8_t *data, uint32_t length)
{
    if (!data || length == 0) {
        return BK_FAIL;
    }

    if (s_ctrl_recv_cb) {
        return s_ctrl_recv_cb(data, length);
    }

    return (int)length;
}

static int bk_trans_video_recv(uint8_t *data, uint32_t length)
{
    if (!data || length == 0) {
        return BK_FAIL;
    }

    return (int)length;
}

static int bk_trans_audio_recv(uint8_t *data, uint32_t length)
{
    if (!data || length == 0) {
        return BK_FAIL;
    }

    return ntwk_eng_recv_audio(data, length);
}

static void bk_trans_reset_runtime_state(void)
{
    s_bk_trans_connected = false;
    s_ctrl_chan_started = false;
    s_video_chan_started = false;
    s_video_chan_connected = false;
    s_audio_chan_started = false;
    s_audio_chan_connected = false;
}

static image_format_t bk_trans_video_format_from_frame(frame_buffer_t *frame)
{
    if (!frame) {
        return IMAGE_H264;
    }

    switch (frame->fmt) {
    case IMAGE_MJPEG:
    case PIXEL_FMT_JPEG:
        return IMAGE_MJPEG;
    case IMAGE_H264:
    case PIXEL_FMT_H264:
        return IMAGE_H264;
    case IMAGE_H265:
    case PIXEL_FMT_H265:
        return IMAGE_H265;
    default:
        return IMAGE_H264;
    }
}

bk_err_t bk_trans_pre_config(void *user_data)
{
    (void)user_data;

    if (!s_config_valid) {
        bk_trans_load_default_config();
    }

    LOGI("bk_trans pre_config\n");
    return BK_OK;
}

bk_err_t bk_trans_register_ctrl_recv_cb(bk_trans_ctrl_recv_callback_t cb)
{
    s_ctrl_recv_cb = cb;
    return BK_OK;
}

bk_err_t bk_trans_register_event_cb(bk_trans_event_callback_t cb, void *user_data)
{
    s_event_cb = cb;
    s_event_user_data = user_data;
    return BK_OK;
}

bk_err_t bk_trans_start(void *user_data)
{
    bk_err_t ret = BK_FAIL;

    (void)user_data;

    if (!s_config_valid) {
        bk_trans_load_default_config();
    }

    if (s_bk_trans_inited) {
        return BK_OK;
    }

    bk_trans_reset_runtime_state();

    ret = bk_trans_service_init();
    if (ret != BK_OK) {
        LOGE("init service mode=%s failed ret=%d\n",
             bk_trans_mode_name(s_config.mode), ret);
        return ret;
    }

    s_bk_trans_inited = true;

    ret = ntwk_trans_register_msg_event_cb(bk_trans_event_cb);
    if (ret != BK_OK) {
        goto fail;
    }
    ret = ntwk_trans_register_ctrl_recv_cb(bk_trans_ctrl_recv);
    if (ret != BK_OK) {
        goto fail;
    }
    ret = ntwk_trans_register_video_recv_cb(bk_trans_video_recv);
    if (ret != BK_OK) {
        goto fail;
    }
    ret = ntwk_trans_register_audio_recv_cb(bk_trans_audio_recv);
    if (ret != BK_OK) {
        goto fail;
    }

#if CONFIG_NTWK_CLIENT_SERVICE_ENABLE
    if (s_config.has_server_info) {
        ntwk_server_net_info_t server_info = {0};

        os_snprintf((char *)server_info.ip_addr, sizeof(server_info.ip_addr), "%s",
                    s_config.ip_addr);
        os_snprintf((char *)server_info.cmd_port, sizeof(server_info.cmd_port), "%s",
                    s_config.cmd_port);
        os_snprintf((char *)server_info.video_port, sizeof(server_info.video_port), "%s",
                    s_config.video_port);
        os_snprintf((char *)server_info.audio_port, sizeof(server_info.audio_port), "%s",
                    s_config.audio_port);

        ret = ntwk_trans_set_server_net_info(&server_info);
        if (ret != BK_OK) {
            goto fail;
        }
    }
#endif

    ret = bk_trans_start_channel(NTWK_TRANS_CHAN_CTRL);
    if (ret != BK_OK) {
        goto fail;
    }
    if (s_config.start_video) {
        ret = bk_trans_start_channel(NTWK_TRANS_CHAN_VIDEO);
        if (ret != BK_OK) {
            goto fail;
        }
    }
    if (s_config.start_audio) {
        ret = bk_trans_start_channel(NTWK_TRANS_CHAN_AUDIO);
        if (ret != BK_OK) {
            goto fail;
        }
    }

    LOGI("bk_trans started mode=%s service=%s\n",
         bk_trans_mode_name(s_config.mode), s_config.service_name);
    return BK_OK;

fail:
    (void)bk_trans_deinit(NULL);
    return ret;
}

bk_err_t bk_trans_deinit(void *user_data)
{
    (void)user_data;
    if (!s_bk_trans_inited) {
        bk_trans_reset_runtime_state();
        return BK_OK;
    }

    (void)bk_trans_stop(NULL);
    (void)bk_trans_service_deinit();
    s_bk_trans_inited = false;
    bk_trans_reset_runtime_state();
    LOGI("bk_trans deinit\n");
    return BK_OK;
}

bk_err_t bk_trans_stop(void *user_data)
{
    (void)user_data;
    if (!s_bk_trans_inited) {
        bk_trans_reset_runtime_state();
        return BK_OK;
    }

    if (s_audio_chan_started) {
        (void)ntwk_trans_chan_stop(NTWK_TRANS_CHAN_AUDIO);
    }
    if (s_video_chan_started) {
        (void)ntwk_trans_chan_stop(NTWK_TRANS_CHAN_VIDEO);
    }
    if (s_ctrl_chan_started) {
        (void)ntwk_trans_chan_stop(NTWK_TRANS_CHAN_CTRL);
    }

    bk_trans_reset_runtime_state();
    LOGI("bk_trans stop\n");
    return BK_OK;
}

int bk_trans_update(void *user_data, void *update_info)
{
    (void)user_data;
    (void)update_info;
    return BK_OK;
}

int bk_trans_ctrl_send(uint8_t *data, size_t len)
{
    if (!s_bk_trans_inited || !s_bk_trans_connected) {
        LOGE("%s %d %d\n", __func__, s_bk_trans_inited, s_bk_trans_connected);
        return BK_FAIL;
    }

    if (!data || len == 0) {
        LOGE("Invalid ctrl data parameters: data=%p, len=%zu\n", data, len);
        return BK_FAIL;
    }

    return ntwk_trans_ctrl_send(data, (uint32_t)len);
}

bk_err_t bk_trans_start_video_channel(void)
{
    if (!s_bk_trans_inited || !s_bk_trans_connected) {
        return BK_FAIL;
    }

    return bk_trans_start_channel(NTWK_TRANS_CHAN_VIDEO);
}

bk_err_t bk_trans_stop_video_channel(void)
{
    if (!s_video_chan_started) {
        s_video_chan_connected = false;
        return BK_OK;
    }

    if (ntwk_trans_chan_stop(NTWK_TRANS_CHAN_VIDEO) != BK_OK) {
        return BK_FAIL;
    }

    s_video_chan_started = false;
    s_video_chan_connected = false;
    return BK_OK;
}

bool bk_trans_is_video_channel_connected(void)
{
    return s_video_chan_started && s_video_chan_connected;
}

bk_err_t bk_trans_start_audio_channel(void)
{
    if (!s_bk_trans_inited || !s_bk_trans_connected) {
        return BK_FAIL;
    }

    return bk_trans_start_channel(NTWK_TRANS_CHAN_AUDIO);
}

bk_err_t bk_trans_stop_audio_channel(void)
{
    if (!s_audio_chan_started) {
        s_audio_chan_connected = false;
        return BK_OK;
    }

    if (ntwk_trans_chan_stop(NTWK_TRANS_CHAN_AUDIO) != BK_OK) {
        return BK_FAIL;
    }

    s_audio_chan_started = false;
    s_audio_chan_connected = false;
    return BK_OK;
}

bool bk_trans_is_audio_channel_connected(void)
{
    return s_audio_chan_started && s_audio_chan_connected;
}

int bk_trans_audio_data_send(uint8_t *data_ptr, size_t data_len, audio_enc_type_t audio_type)
{
    if (!s_bk_trans_inited || !s_bk_trans_connected) {
        return BK_FAIL;
    }

    if (!s_audio_chan_connected) {
        return BK_FAIL;
    }

    return ntwk_trans_audio_send(data_ptr, (uint32_t)data_len, audio_type);
}

int bk_trans_video_data_send(frame_buffer_t *frame)
{
    image_format_t video_type = IMAGE_H264;

    if (!s_bk_trans_inited || !s_bk_trans_connected) {
        return BK_FAIL;
    }

    if (!frame || !frame->frame || frame->length == 0) {
        return BK_FAIL;
    }

    if (!s_video_chan_connected) {
        return BK_FAIL;
    }

    video_type = bk_trans_video_format_from_frame(frame);
    return ntwk_trans_video_send((uint8_t *)frame, frame->length, video_type);
}

bk_err_t bk_trans_abort_video_send(bool abort)
{
    if (!s_bk_trans_inited) {
        return BK_FAIL;
    }

    return ntwk_trans_chan_abort(NTWK_TRANS_CHAN_VIDEO, abort);
}

bk_err_t bk_trans_abort_audio_send(bool abort)
{
    if (!s_bk_trans_inited) {
        return BK_FAIL;
    }

    return ntwk_trans_chan_abort(NTWK_TRANS_CHAN_AUDIO, abort);
}

bool bk_trans_is_connected(void)
{
    return s_bk_trans_connected;
}
