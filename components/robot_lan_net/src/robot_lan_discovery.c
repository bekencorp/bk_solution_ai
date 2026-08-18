#include "robot_lan_internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>
#include "cJSON.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "bk_smart_config.h"

#define TAG "robot_disc"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#ifndef CONFIG_BK_ROBOT_LAN_DISCOVERY_PORT
#define CONFIG_BK_ROBOT_LAN_DISCOVERY_PORT 52110
#endif

#ifndef CONFIG_BK_ROBOT_LAN_DISCOVERY_STACK_SIZE
#define CONFIG_BK_ROBOT_LAN_DISCOVERY_STACK_SIZE 4096
#endif

#define ROBOT_LAN_DISCOVERY_RX_BUF_SIZE 2048

static void robot_lan_discovery_close_sock(robot_lan_ctx_t *ctx)
{
    int sock;

    if (!ctx) {
        return;
    }

    sock = ctx->discovery_sock;
    if (sock < 0) {
        return;
    }

    ctx->discovery_sock = -1;
    closesocket(sock);
    LOGI("discovery socket closed\n");
}

static int json_get_u16(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItem(root, name);
    if (item && cJSON_IsNumber(item)) {
        return item->valueint;
    }
    if (item && cJSON_IsString(item) && item->valuestring) {
        char *end = NULL;
        unsigned long value = strtoul(item->valuestring, &end, 10);
        if (end != item->valuestring && value <= 0xffff) {
            return (int)value;
        }
    }
    return 0;
}

static const char *json_get_string(cJSON *root, const char *name)
{
    cJSON *item = cJSON_GetObjectItem(root, name);
    if (item && cJSON_IsString(item)) {
        return item->valuestring;
    }
    return NULL;
}

static bool robot_lan_parse_broadcast(const char *buf, const char *src_ip, robot_lan_app_info_t *app)
{
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        LOGW("parse App broadcast failed: invalid json, payload=%s\n", buf ? buf : "");
        return false;
    }

    const char *uuid = json_get_string(root, "uuid");
    const char *local_uuid = bk_sconf_get_agent_identity_uuid();
    if (!uuid || os_strcmp(uuid, local_uuid) != 0) {
        LOGW("ignore App broadcast: uuid mismatch rx=%s local=%s\n",
             uuid ? uuid : "", local_uuid);
        cJSON_Delete(root);
        return false;
    }

    os_memset(app, 0, sizeof(*app));
    const char *app_ip = json_get_string(root, "appIp");
    if (!app_ip) {
        app_ip = json_get_string(root, "ip");
    }
    strncpy(app->app_ip, app_ip ? app_ip : src_ip, sizeof(app->app_ip) - 1);
    app->cmd_port = (uint16_t)json_get_u16(root, "cmdPort");
    if (app->cmd_port == 0) {
        app->cmd_port = (uint16_t)json_get_u16(root, "cmd");
    }
    app->video_port = (uint16_t)json_get_u16(root, "videoPort");
    if (app->video_port == 0) {
        app->video_port = (uint16_t)json_get_u16(root, "img");
    }
    app->audio_up_port = (uint16_t)json_get_u16(root, "audioUpPort");
    app->audio_down_port = (uint16_t)json_get_u16(root, "audioDownPort");
    if (app->audio_up_port == 0) {
        uint16_t aud_port = (uint16_t)json_get_u16(root, "aud");
        app->audio_up_port = aud_port;
        app->audio_down_port = aud_port;
    }

    const char *mode = json_get_string(root, "mode");
    if (!mode) {
        mode = json_get_string(root, "lan_connection_mode");
    }
    if (!mode) {
        mode = json_get_string(root, "serviceType");
    }
    app->mode = robot_lan_net_parse_mode(mode);
    cJSON_Delete(root);

    if (app->cmd_port == 0) {
        LOGW("parse App broadcast failed: missing cmd/cmdPort, src=%s\n", src_ip ? src_ip : "");
        return false;
    }

    LOGI("parsed App broadcast: ip=%s cmd=%u img=%u aud=%u/%u mode=%s\n",
         app->app_ip, app->cmd_port, app->video_port,
         app->audio_up_port, app->audio_down_port,
         robot_lan_net_mode_to_string(app->mode));
    return true;
}

static void robot_lan_discovery_task(beken_thread_arg_t arg)
{
    (void)arg;
    robot_lan_ctx_t *ctx = robot_lan_get_ctx_internal();
    int sock = -1;
    struct sockaddr_in addr = {0};
    char *rx_buf = os_malloc(ROBOT_LAN_DISCOVERY_RX_BUF_SIZE);
    if (!rx_buf) {
        LOGE("alloc discovery rx buffer failed size=%u\n", ROBOT_LAN_DISCOVERY_RX_BUF_SIZE);
        goto exit;
    }

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        LOGE("create discovery socket failed errno=%d\n", errno);
        goto exit;
    }
    ctx->discovery_sock = sock;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONFIG_BK_ROBOT_LAN_DISCOVERY_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGE("bind discovery port %d failed errno=%d\n",
             CONFIG_BK_ROBOT_LAN_DISCOVERY_PORT, errno);
        goto exit;
    }

    LOGI("listening App broadcast on udp/%d\n", CONFIG_BK_ROBOT_LAN_DISCOVERY_PORT);
    while (ctx->discovery_running) {
        struct sockaddr_in from = {0};
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, rx_buf, ROBOT_LAN_DISCOVERY_RX_BUF_SIZE - 1, 0,
                           (struct sockaddr *)&from, &from_len);
        if (len <= 0) {
            if (!ctx->discovery_running) {
                break;
            }
            LOGW("recv App broadcast failed len=%d errno=%d\n", len, errno);
            continue;
        }
        if (!ctx->discovery_running) {
            break;
        }

        rx_buf[len] = '\0';
        robot_lan_app_info_t app = {0};
        const char *src_ip = inet_ntoa(from.sin_addr);
        LOGI("received App broadcast len=%d from=%s:%u payload=%s\n",
             len, src_ip ? src_ip : "", ntohs(from.sin_port), rx_buf);
        if (!robot_lan_parse_broadcast(rx_buf, src_ip, &app)) {
            LOGW("App broadcast parse rejected\n");
            continue;
        }

        LOGI("matched App %s cmd=%u video=%u audio=%u/%u mode=%s\n",
             app.app_ip, app.cmd_port, app.video_port,
             app.audio_up_port, app.audio_down_port,
             robot_lan_net_mode_to_string(app.mode));
        robot_lan_net_connect_app_servers(&app);
    }

exit:
    robot_lan_discovery_close_sock(ctx);
    if (rx_buf) {
        os_free(rx_buf);
    }
    ctx->discovery_running = false;
    ctx->discovery_thread = NULL;
    LOGI("discovery task exit\n");
    rtos_delete_thread(NULL);
}

bk_err_t robot_lan_discovery_start_internal(void)
{
    robot_lan_ctx_t *ctx = robot_lan_get_ctx_internal();
    if (ctx->discovery_running) {
        return BK_OK;
    }
    for (int i = 0; ctx->discovery_thread != NULL && i < 10; i++) {
        rtos_delay_milliseconds(10);
    }
    if (ctx->discovery_thread != NULL) {
        LOGW("discovery task is still stopping\n");
        return BK_FAIL;
    }

    ctx->discovery_running = true;
    ctx->discovery_sock = -1;
    int ret = rtos_create_thread(&ctx->discovery_thread,
                                 BEKEN_DEFAULT_WORKER_PRIORITY,
                                 "robot_disc",
                                 robot_lan_discovery_task,
                                 CONFIG_BK_ROBOT_LAN_DISCOVERY_STACK_SIZE,
                                 NULL);
    if (ret != BK_OK) {
        ctx->discovery_running = false;
        LOGE("create discovery task failed ret=%d\n", ret);
        return BK_FAIL;
    }

    return BK_OK;
}

bk_err_t robot_lan_discovery_stop_internal(void)
{
    robot_lan_ctx_t *ctx = robot_lan_get_ctx_internal();
    ctx->discovery_running = false;
    robot_lan_discovery_close_sock(ctx);
    return BK_OK;
}
