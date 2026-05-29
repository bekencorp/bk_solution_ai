#include "robot_lan_internal.h"

#include <os/mem.h>
#include <os/str.h>
#include <string.h>
#include <components/log.h>
#include "network_transfer.h"
#include "common/network_transfer_common.h"

#define TAG "robot_ntwk"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static bool s_sdk_inited;
static robot_lan_transport_mode_t s_sdk_mode = ROBOT_LAN_MODE_TCP;
static bool s_video_chan_started;
static bool s_audio_chan_started;
static char s_ctrl_rx_buf[ROBOT_LAN_CMD_MAX_LEN * 2];
static uint32_t s_ctrl_rx_len;

static void robot_lan_emit_ctrl_line(void)
{
    uint32_t line_len = s_ctrl_rx_len;

    while (line_len > 0 && s_ctrl_rx_buf[line_len - 1] == '\r') {
        line_len--;
    }

    s_ctrl_rx_buf[line_len] = '\0';
    if (line_len > 0) {
        robot_lan_emit_event_internal(ROBOT_LAN_EVT_CMD_RX, s_ctrl_rx_buf, line_len);
    }
    s_ctrl_rx_len = 0;
}

static int robot_lan_sdk_ctrl_recv(uint8_t *data, uint32_t length)
{
    if (!data || length == 0) {
        return BK_FAIL;
    }

    for (uint32_t i = 0; i < length; i++) {
        char ch = (char)data[i];
        if (ch == '\n') {
            robot_lan_emit_ctrl_line();
            continue;
        }

        if (s_ctrl_rx_len >= sizeof(s_ctrl_rx_buf) - 1) {
            LOGW("cmd rx buffer overflow, drop buffered data len=%u\n", s_ctrl_rx_len);
            s_ctrl_rx_len = 0;
        }
        s_ctrl_rx_buf[s_ctrl_rx_len++] = ch;
    }

    return length;
}

static void robot_lan_sdk_event_cb(ntwk_trans_event_t *event)
{
    robot_lan_ctx_t *ctx = robot_lan_get_ctx_internal();

    if (!event) {
        LOGW("robot_lan_sdk_event_cb: null event\r\n");
        return;
    }

    if (event->chan_type == NTWK_TRANS_CHAN_VIDEO || event->chan_type == NTWK_TRANS_CHAN_AUDIO) {
        LOGI("robot_lan_sdk_event_cb: media event=%d, channel type=%d\r\n",
             event->code, event->chan_type);
        if (event->code == NTWK_TRANS_EVT_STOP) {
            if (event->chan_type == NTWK_TRANS_CHAN_VIDEO) {
                s_video_chan_started = false;
            } else {
                s_audio_chan_started = false;
            }
        }
        return;
    }

    if (event->chan_type != NTWK_TRANS_CHAN_CTRL) {
        LOGW("robot_lan_sdk_event_cb: invalid channel type=%d event=%d\r\n",
             event->chan_type, event->code);
        return;
    }

    LOGI("robot_lan_sdk_event_cb: event=%d, channel type=%d\r\n", event->code, event->chan_type);
    if (event->code == NTWK_TRANS_EVT_CONNECTED) {
        LOGI("NTWK_TRANS_EVT_CONNECTED\r\n");
        ctx->connected = true;
        s_ctrl_rx_len = 0;
        robot_lan_emit_event_internal(ROBOT_LAN_EVT_APP_CONNECTED, NULL, 0);
    } else if (event->code == NTWK_TRANS_EVT_DISCONNECTED || event->code == NTWK_TRANS_EVT_STOP) {
        LOGI("NTWK_TRANS_EVT_DISCONNECTED or NTWK_TRANS_EVT_STOP\r\n");
        s_ctrl_rx_len = 0;
        if (ctx->connected) {
            ctx->connected = false;
            robot_lan_emit_event_internal(ROBOT_LAN_EVT_APP_DISCONNECTED, NULL, 0);
        }
    }
}

static void robot_lan_fill_server_info(ntwk_server_net_info_t *info, const robot_lan_app_info_t *app)
{
    os_memset(info, 0, sizeof(*info));
    os_snprintf((char *)info->ip_addr, sizeof(info->ip_addr), "%s", app->app_ip);
    os_snprintf((char *)info->cmd_port, sizeof(info->cmd_port), "%u", app->cmd_port);
    os_snprintf((char *)info->video_port, sizeof(info->video_port), "%u", app->video_port);
    os_snprintf((char *)info->audio_port, sizeof(info->audio_port), "%u", app->audio_up_port);
}

static bk_err_t robot_lan_sdk_init(robot_lan_transport_mode_t mode)
{
    bk_err_t ret = BK_FAIL;

    if (s_sdk_inited) {
        if (s_sdk_mode == mode) {
            return BK_OK;
        }
        robot_lan_transfer_close_internal();
    }

    switch (mode) {
    case ROBOT_LAN_MODE_TCP:
        ret = bk_tcp_trans_service_init("robot_tcp");
        break;
    case ROBOT_LAN_MODE_UDP:
        ret = bk_udp_trans_service_init("robot_udp");
        break;
    case ROBOT_LAN_MODE_CS2:
#if (CONFIG_CS2_P2P_SERVER || CONFIG_CS2_P2P_CLIENT)
        ret = bk_cs2_trans_service_init("robot_cs2");
#else
        LOGW("CS2 network transfer is not enabled\n");
        ret = BK_FAIL;
#endif
        break;
    default:
        ret = BK_FAIL;
        break;
    }

    if (ret != BK_OK) {
        LOGE("init sdk network transfer mode=%s failed ret=%d\n",
             robot_lan_net_mode_to_string(mode), ret);
        return ret;
    }

    s_sdk_inited = true;
    s_sdk_mode = mode;
    ntwk_trans_register_msg_event_cb(robot_lan_sdk_event_cb);
    ntwk_trans_register_ctrl_recv_cb(robot_lan_sdk_ctrl_recv);
    LOGI("sdk network transfer initialized mode=%s\n", robot_lan_net_mode_to_string(mode));
    return BK_OK;
}

bk_err_t robot_lan_transfer_connect_internal(const robot_lan_app_info_t *app)
{
    ntwk_server_net_info_t server_info = {0};
    bk_err_t ret;

    if (!app || app->app_ip[0] == '\0' || app->cmd_port == 0) {
        return BK_ERR_PARAM;
    }

    ret = robot_lan_sdk_init(app->mode);
    if (ret != BK_OK) {
        return ret;
    }

    robot_lan_fill_server_info(&server_info, app);
    BK_LOG_ON_ERR(ntwk_trans_set_server_net_info(&server_info));
    robot_lan_net_save_session_context(app);

    s_video_chan_started = false;
    s_audio_chan_started = false;

    BK_LOG_ON_ERR(ntwk_trans_chan_start(NTWK_TRANS_CHAN_CTRL, NULL));

    LOGI("started sdk network ctrl client ip=%s cmd=%u video=%u audio=%u mode=%s\n",
         app->app_ip, app->cmd_port, app->video_port, app->audio_up_port,
         robot_lan_net_mode_to_string(app->mode));
    return BK_OK;
}

static bk_err_t robot_lan_transfer_start_media_channel(chan_type_t chan_type)
{
    robot_lan_ctx_t *ctx = robot_lan_get_ctx_internal();
    bk_err_t ret;

    if (!s_sdk_inited) {
        LOGW("sdk network transfer is not initialized\n");
        return BK_FAIL;
    }

    if (!ctx->connected) {
        LOGW("control channel is not connected, skip media channel start type=%d\n", chan_type);
        return BK_FAIL;
    }

    if (chan_type == NTWK_TRANS_CHAN_VIDEO) {
        if (ctx->app.video_port == 0) {
            LOGW("video port is not configured\n");
            return BK_ERR_PARAM;
        }
        if (s_video_chan_started) {
            return BK_OK;
        }
    } else if (chan_type == NTWK_TRANS_CHAN_AUDIO) {
        if (ctx->app.audio_up_port == 0) {
            LOGW("audio port is not configured\n");
            return BK_ERR_PARAM;
        }
        if (s_audio_chan_started) {
            return BK_OK;
        }
    } else {
        return BK_ERR_PARAM;
    }

    ret = ntwk_trans_chan_start(chan_type, NULL);
    if (ret != BK_OK) {
        LOGE("start media channel type=%d failed ret=%d\n", chan_type, ret);
        return ret;
    }

    if (chan_type == NTWK_TRANS_CHAN_VIDEO) {
        s_video_chan_started = true;
        LOGI("started sdk network video channel port=%u\n", ctx->app.video_port);
    } else {
        s_audio_chan_started = true;
        LOGI("started sdk network audio channel port=%u\n", ctx->app.audio_up_port);
    }

    return BK_OK;
}

static bk_err_t robot_lan_transfer_stop_media_channel(chan_type_t chan_type)
{
    bk_err_t ret;
    bool *started;

    if (chan_type == NTWK_TRANS_CHAN_VIDEO) {
        started = &s_video_chan_started;
    } else if (chan_type == NTWK_TRANS_CHAN_AUDIO) {
        started = &s_audio_chan_started;
    } else {
        return BK_ERR_PARAM;
    }

    if (!*started) {
        return BK_OK;
    }

    if (!s_sdk_inited) {
        *started = false;
        return BK_OK;
    }

    ret = ntwk_trans_chan_stop(chan_type);
    if (ret != BK_OK) {
        LOGW("stop media channel type=%d failed ret=%d\n", chan_type, ret);
    }
    *started = false;
    return ret;
}

void robot_lan_transfer_close_internal(void)
{
    robot_lan_ctx_t *ctx = robot_lan_get_ctx_internal();

    ctx->connected = false;
    if (!s_sdk_inited) {
        return;
    }

    robot_lan_transfer_stop_media_channel(NTWK_TRANS_CHAN_AUDIO);
    robot_lan_transfer_stop_media_channel(NTWK_TRANS_CHAN_VIDEO);
    ntwk_trans_chan_stop(NTWK_TRANS_CHAN_CTRL);

    switch (s_sdk_mode) {
    case ROBOT_LAN_MODE_TCP:
        bk_tcp_trans_service_deinit();
        break;
    case ROBOT_LAN_MODE_UDP:
        bk_udp_trans_service_deinit();
        break;
    case ROBOT_LAN_MODE_CS2:
#if (CONFIG_CS2_P2P_SERVER || CONFIG_CS2_P2P_CLIENT)
        bk_cs2_trans_service_deinit();
#endif
        break;
    default:
        break;
    }

    s_sdk_inited = false;
    s_video_chan_started = false;
    s_audio_chan_started = false;
}

bk_err_t robot_lan_net_start_video_channel(void)
{
    return robot_lan_transfer_start_media_channel(NTWK_TRANS_CHAN_VIDEO);
}

bk_err_t robot_lan_net_stop_video_channel(void)
{
    return robot_lan_transfer_stop_media_channel(NTWK_TRANS_CHAN_VIDEO);
}

bk_err_t robot_lan_net_start_audio_channel(void)
{
    return robot_lan_transfer_start_media_channel(NTWK_TRANS_CHAN_AUDIO);
}

bk_err_t robot_lan_net_stop_audio_channel(void)
{
    return robot_lan_transfer_stop_media_channel(NTWK_TRANS_CHAN_AUDIO);
}

int robot_lan_net_send_cmd(const char *data, size_t len)
{
    if (!data || len == 0) {
        return BK_FAIL;
    }
    return ntwk_trans_ctrl_send((uint8_t *)data, len);
}

int robot_lan_net_send_video(frame_buffer_t *frame)
{
    image_format_t fmt = IMAGE_H264;

    if (!frame || !frame->frame || frame->length == 0) {
        return BK_FAIL;
    }

    if (frame->fmt == IMAGE_MJPEG || frame->fmt == IMAGE_H264 || frame->fmt == IMAGE_H265) {
        fmt = (image_format_t)frame->fmt;
    }

    return ntwk_trans_video_send((uint8_t *)frame, frame->length, fmt);
}

int robot_lan_net_send_audio(uint8_t *data, size_t len, audio_enc_type_t type)
{
    if (!data || len == 0) {
        return BK_FAIL;
    }
    return ntwk_trans_audio_send(data, len, type);
}
