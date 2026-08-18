#include "robot_lan_net.h"

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>

#include "lwip/sockets.h"
#include "robot_lan_internal.h"
#include "bk_factory_config.h"
#include "bk_smart_config.h"
#include "network_engine.h"
#include "bk_trans_api.h"

#define TAG "robot_lan"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define ROBOT_LAN_SESSION_MAGIC 0x52425353U
#define ROBOT_LAN_SESSION_KEY   "robot_lan_session"

typedef struct {
    uint32_t magic;
    robot_lan_app_info_t app;
} robot_lan_session_store_t;

static robot_lan_ctx_t s_lan = {
};
#if !CONFIG_NTWK_CTRL_CHAN_JSON
static char s_ctrl_rx_buf[ROBOT_LAN_CMD_MAX_LEN * 2];
static uint32_t s_ctrl_rx_len;
#endif

extern bk_err_t robot_lan_discovery_start_internal(void);
extern bk_err_t robot_lan_discovery_stop_internal(void);

static void robot_lan_reset_connection_state(void)
{
    s_lan.connected = false;
    s_lan.ctrl_connected = false;
    s_lan.video_connected = false;
    s_lan.audio_connected = false;
#if !CONFIG_NTWK_CTRL_CHAN_JSON
    s_ctrl_rx_len = 0;
#endif
}

static void robot_lan_lock(void)
{
    if (s_lan.lock) {
        rtos_lock_mutex(&s_lan.lock);
    }
}

static void robot_lan_unlock(void)
{
    if (s_lan.lock) {
        rtos_unlock_mutex(&s_lan.lock);
    }
}

static bool robot_lan_same_app_locked(const robot_lan_app_info_t *app)
{
    return app &&
           s_lan.app.cmd_port == app->cmd_port &&
           s_lan.app.video_port == app->video_port &&
           s_lan.app.audio_up_port == app->audio_up_port &&
           s_lan.app.audio_down_port == app->audio_down_port &&
           s_lan.app.mode == app->mode &&
           os_strcmp(s_lan.app.app_ip, app->app_ip) == 0;
}

static bk_err_t robot_lan_net_stop_all_internal(bool clear_connecting)
{
    robot_lan_discovery_stop_internal();
    robot_lan_reset_connection_state();
    if (clear_connecting) {
        s_lan.connecting = false;
    }

    if (ntwk_eng_get_network_type() == NETWORK_TYPE_BK_TRANS) {
        ntwk_eng_deinit();
    }

    return BK_OK;
}

robot_lan_ctx_t *robot_lan_get_ctx_internal(void)
{
    return &s_lan;
}

void robot_lan_emit_event_internal(robot_lan_event_t event, const char *cmd, size_t cmd_len)
{
    robot_lan_event_msg_t msg = {
        .event = event,
        .cmd = cmd,
        .cmd_len = cmd_len,
    };

    if (s_lan.event_cb) {
        s_lan.event_cb(&msg, s_lan.event_user_data);
    }
}

#if CONFIG_NTWK_CTRL_CHAN_JSON
static int robot_lan_ctrl_recv(uint8_t *data, uint32_t length)
{
    if (!data || length == 0) {
        return BK_FAIL;
    }

    robot_lan_emit_event_internal(ROBOT_LAN_EVT_CMD_RX, (const char *)data, length);

    return length;
}
#else
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

static int robot_lan_ctrl_recv(uint8_t *data, uint32_t length)
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
#endif

static void robot_lan_bk_trans_event_cb(const bk_trans_event_t *event, void *user_data)
{
    (void)user_data;

    if (!event) {
        return;
    }

    if (event->code == BK_TRANS_EVT_CONNECTED) {
        if (event->channel == BK_TRANS_CHAN_CTRL) {
            LOGI("bk_trans ctrl connected\r\n");
            s_lan.ctrl_connected = true;
        } else if (event->channel == BK_TRANS_CHAN_VIDEO) {
            LOGI("bk_trans video connected\r\n");
            s_lan.video_connected = true;
        } else if (event->channel == BK_TRANS_CHAN_AUDIO) {
            LOGI("bk_trans audio connected\r\n");
            s_lan.audio_connected = true;
        }

        if (!s_lan.connected &&
            s_lan.ctrl_connected &&
            s_lan.video_connected &&
            s_lan.audio_connected) {
            s_lan.connected = true;
            s_lan.connecting = false;
#if !CONFIG_NTWK_CTRL_CHAN_JSON
            s_ctrl_rx_len = 0;
#endif
            robot_lan_emit_event_internal(ROBOT_LAN_EVT_APP_CONNECTED, NULL, 0);
        }
    } else if (event->code == BK_TRANS_EVT_DISCONNECTED ||
               event->code == BK_TRANS_EVT_STOP) {
        LOGI("bk_trans channel %d disconnected or stopped\r\n", event->channel);

        if (event->channel == BK_TRANS_CHAN_CTRL) {
            s_lan.ctrl_connected = false;
        } else if (event->channel == BK_TRANS_CHAN_VIDEO) {
            s_lan.video_connected = false;
        } else if (event->channel == BK_TRANS_CHAN_AUDIO) {
            s_lan.audio_connected = false;
        }

        if (s_lan.connected) {
            robot_lan_reset_connection_state();
            s_lan.connecting = false;
            robot_lan_emit_event_internal(ROBOT_LAN_EVT_APP_DISCONNECTED, NULL, 0);
        } else if (s_lan.connecting) {
            s_lan.connecting = false;
        }
    }
}

static bk_trans_mode_t robot_lan_to_bk_trans_mode(robot_lan_transport_mode_t mode)
{
    switch (mode) {
    case ROBOT_LAN_MODE_UDP:
        return BK_TRANS_MODE_UDP;
    case ROBOT_LAN_MODE_CS2:
        return BK_TRANS_MODE_CS2;
    case ROBOT_LAN_MODE_TCP:
    default:
        return BK_TRANS_MODE_TCP;
    }
}

static const char *robot_lan_ntwk_service_name(robot_lan_transport_mode_t mode)
{
    switch (mode) {
    case ROBOT_LAN_MODE_UDP:
        return "robot_udp";
    case ROBOT_LAN_MODE_CS2:
        return "robot_cs2";
    case ROBOT_LAN_MODE_TCP:
    default:
        return "robot_tcp";
    }
}

static void robot_lan_fill_bk_trans_config(bk_trans_config_t *config,
                                           const robot_lan_app_info_t *app)
{
    os_memset(config, 0, sizeof(*config));
    config->magic = BK_TRANS_CONFIG_MAGIC;
    config->mode = robot_lan_to_bk_trans_mode(app->mode);
    config->start_video = true;
    config->start_audio = true;
    config->has_server_info = true;

    os_snprintf(config->service_name, sizeof(config->service_name), "%s",
                robot_lan_ntwk_service_name(app->mode));
    os_snprintf(config->ip_addr, sizeof(config->ip_addr), "%s", app->app_ip);
    os_snprintf(config->cmd_port, sizeof(config->cmd_port), "%u", app->cmd_port);
    os_snprintf(config->video_port, sizeof(config->video_port), "%u", app->video_port);
    os_snprintf(config->audio_port, sizeof(config->audio_port), "%u", app->audio_up_port);
}

bk_err_t robot_lan_net_init(void)
{
    robot_lan_session_store_t session = {0};

    if (s_lan.initialized) {
        return BK_OK;
    }

    os_memset(&s_lan, 0, sizeof(s_lan));
    s_lan.discovery_sock = -1;
    if (rtos_init_mutex(&s_lan.lock) != BK_OK) {
        LOGE("init LAN mutex failed\n");
        return BK_FAIL;
    }
#if CONFIG_BK_FACTORY_CONFIG
    if (bk_config_read(ROBOT_LAN_SESSION_KEY, &session, sizeof(session)) == sizeof(session)
        && session.magic == ROBOT_LAN_SESSION_MAGIC
        && session.app.app_ip[0] != '\0'
        && session.app.cmd_port != 0) {
        os_memcpy(&s_lan.app, &session.app, sizeof(s_lan.app));
        LOGI("loaded LAN session %s:%u mode=%s\n",
             s_lan.app.app_ip, s_lan.app.cmd_port,
             robot_lan_net_mode_to_string(s_lan.app.mode));
    }
#endif
    s_lan.initialized = true;
    LOGI("robot LAN net initialized, uuid=%s token=%s\n",
         bk_sconf_get_agent_identity_uuid(), bk_sconf_get_agent_identity_token());
    return BK_OK;
}

bk_err_t robot_lan_net_deinit(void)
{
    robot_lan_net_stop_all();
    if (s_lan.lock) {
        rtos_deinit_mutex(&s_lan.lock);
        s_lan.lock = NULL;
    }
    os_memset(&s_lan, 0, sizeof(s_lan));
    return BK_OK;
}

bk_err_t robot_lan_net_start_discovery(void)
{
    if (!s_lan.initialized) {
        BK_LOG_ON_ERR(robot_lan_net_init());
    }

    return robot_lan_discovery_start_internal();
}

bk_err_t robot_lan_net_stop_discovery(void)
{
    return robot_lan_discovery_stop_internal();
}

bk_err_t robot_lan_net_stop_all(void)
{
    bk_err_t ret;

    robot_lan_lock();
    ret = robot_lan_net_stop_all_internal(true);
    robot_lan_unlock();

    return ret;
}

bk_err_t robot_lan_net_set_event_callback(robot_lan_event_cb_t cb, void *user_data)
{
    s_lan.event_cb = cb;
    s_lan.event_user_data = user_data;
    return BK_OK;
}

bk_err_t robot_lan_net_connect_app_servers(const robot_lan_app_info_t *app)
{
    bk_trans_config_t config = {0};
    bk_err_t ret;

    if (!app) {
        return BK_ERR_PARAM;
    }

    robot_lan_lock();
    if (s_lan.connecting || s_lan.connected) {
        LOGI("ignore App broadcast while %s%s\r\n",
             s_lan.connecting ? "connecting" : "connected",
             robot_lan_same_app_locked(app) ? " to same App" : "");
        robot_lan_unlock();
        return BK_OK;
    }

    robot_lan_net_stop_all_internal(false);
    s_lan.connecting = true;
    robot_lan_unlock();

    robot_lan_fill_bk_trans_config(&config, app);
    bk_trans_register_event_cb(robot_lan_bk_trans_event_cb, NULL);
    bk_trans_register_ctrl_recv_cb(robot_lan_ctrl_recv);

    ret = bk_trans_set_config(&config);
    if (ret != BK_OK) {
        LOGE("set bk_trans config failed ret=%d\n", ret);
        robot_lan_lock();
        s_lan.connecting = false;
        robot_lan_unlock();
        return ret;
    }

    ret = ntwk_eng_bk_trans_init();
    if (ret != BK_OK) {
        LOGE("init network engine bk_trans failed ret=%d\n", ret);
        robot_lan_lock();
        s_lan.connecting = false;
        robot_lan_unlock();
        return ret;
    }

    ret = ntwk_eng_start(NULL);
    if (ret != BK_OK) {
        LOGE("start network engine bk_trans failed ret=%d\n", ret);
        ntwk_eng_deinit();
        robot_lan_lock();
        s_lan.connecting = false;
        robot_lan_unlock();
        return ret;
    }

    robot_lan_net_save_session_context(app);
    LOGI("started network_engine bk_trans ip=%s cmd=%u video=%u audio=%u mode=%s\n",
         app->app_ip, app->cmd_port, app->video_port, app->audio_up_port,
         robot_lan_net_mode_to_string(app->mode));
    return BK_OK;
}

bk_err_t robot_lan_net_reconnect_from_saved_context(void)
{
    if (s_lan.app.app_ip[0] == '\0' || s_lan.app.cmd_port == 0) {
        LOGW("no saved App context for reconnect\n");
        return BK_FAIL;
    }

    return robot_lan_net_connect_app_servers(&s_lan.app);
}

bool robot_lan_net_is_connected(void)
{
    return s_lan.connected;
}

const robot_lan_app_info_t *robot_lan_net_get_app_info(void)
{
    return &s_lan.app;
}

bk_err_t robot_lan_net_save_session_context(const robot_lan_app_info_t *app)
{
    robot_lan_session_store_t session = {0};

    if (!app) {
        return BK_ERR_PARAM;
    }

    os_memcpy(&s_lan.app, app, sizeof(s_lan.app));
#if CONFIG_BK_FACTORY_CONFIG
    session.magic = ROBOT_LAN_SESSION_MAGIC;
    os_memcpy(&session.app, app, sizeof(session.app));
    int ret = bk_config_write(ROBOT_LAN_SESSION_KEY, &session, sizeof(session));
    if (ret != 0) {
        LOGW("store LAN session ret=%d\n", ret);
    }
    bk_config_sync_flash_safely();
#endif
    return BK_OK;
}

robot_lan_transport_mode_t robot_lan_net_parse_mode(const char *mode)
{
    if (!mode) {
        return ROBOT_LAN_MODE_TCP;
    }

    if (os_strcmp(mode, "udp") == 0) {
        return ROBOT_LAN_MODE_UDP;
    }
    if (os_strcmp(mode, "cs2") == 0) {
        return ROBOT_LAN_MODE_CS2;
    }

    return ROBOT_LAN_MODE_TCP;
}

const char *robot_lan_net_mode_to_string(robot_lan_transport_mode_t mode)
{
    switch (mode) {
    case ROBOT_LAN_MODE_UDP:
        return "udp";
    case ROBOT_LAN_MODE_CS2:
        return "cs2";
    case ROBOT_LAN_MODE_TCP:
    default:
        return "tcp";
    }
}

