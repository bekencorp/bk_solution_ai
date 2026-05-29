#include "robot_lan_net.h"

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>

#include "lwip/sockets.h"
#include "robot_lan_internal.h"
#include "bk_factory_config.h"

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

extern bk_err_t robot_lan_discovery_start_internal(void);
extern bk_err_t robot_lan_discovery_stop_internal(void);

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

bk_err_t robot_lan_net_init(void)
{
    robot_lan_session_store_t session = {0};

    if (s_lan.initialized) {
        return BK_OK;
    }

    os_memset(&s_lan, 0, sizeof(s_lan));
    BK_LOG_ON_ERR(robot_lan_net_identity_init());
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
         robot_lan_net_get_uuid(), robot_lan_net_get_token());
    return BK_OK;
}

bk_err_t robot_lan_net_deinit(void)
{
    robot_lan_net_stop_all();
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
    robot_lan_discovery_stop_internal();
    robot_lan_transfer_close_internal();
    return BK_OK;
}

bk_err_t robot_lan_net_set_event_callback(robot_lan_event_cb_t cb, void *user_data)
{
    s_lan.event_cb = cb;
    s_lan.event_user_data = user_data;
    return BK_OK;
}

bk_err_t robot_lan_net_connect_app_servers(const robot_lan_app_info_t *app)
{
    if (!app) {
        return BK_ERR_PARAM;
    }

    return robot_lan_transfer_connect_internal(app);
}

bk_err_t robot_lan_net_reconnect_from_saved_context(void)
{
    if (s_lan.app.app_ip[0] == '\0' || s_lan.app.cmd_port == 0) {
        LOGW("no saved App context for reconnect\n");
        return BK_FAIL;
    }

    return robot_lan_transfer_connect_internal(&s_lan.app);
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

