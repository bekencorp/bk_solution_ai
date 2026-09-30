#include "robot_keepalive.h"

#include <errno.h>
#include <string.h>
#include <components/log.h>
#include <driver/aon_rtc.h>
#include <driver/aon_rtc_types.h>
#include <driver/pwr_clk.h>
#include <os/mem.h>
#include <os/os.h>
#include <os/str.h>
#include <modules/wifi.h>
#include "bk_pm_internal_api.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "modules/cif_common.h"

#define TAG "robot_cp_ka"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define ROBOT_IPC_CMD_KEEPALIVE_START        0x0002
#define ROBOT_IPC_CMD_KEEPALIVE_STOP         0x0003
#define ROBOT_IPC_CMD_GET_WAKEUP_ENV_ADDR    0x0004

#define ROBOT_POWERUP_NORMAL                 1
#define ROBOT_POWERUP_MULTIMEDIA_WAKEUP      2
#define ROBOT_POWERUP_KEEPALIVE_DISCONNECT   3
#define ROBOT_POWERUP_KEEPALIVE_FAIL_WAKEUP  4

#define ROBOT_KEEPALIVE_TASK_PRIO            4
#define ROBOT_KEEPALIVE_INTERVAL_MS          (30 * 1000)
#define ROBOT_KEEPALIVE_SOCKET_TIMEOUT_MS    3000
#define ROBOT_KEEPALIVE_MAX_RETRY_CNT        5
#define ROBOT_KEEPALIVE_RX_BUF_SIZE          1024
#define ROBOT_KEEPALIVE_CP_BIND_PORT_START   0x1000
#define ROBOT_KEEPALIVE_CP_BIND_PORT_END     0x1010
#define ROBOT_KEEPALIVE_CP_BIND_PORT_COUNT   (ROBOT_KEEPALIVE_CP_BIND_PORT_END - ROBOT_KEEPALIVE_CP_BIND_PORT_START + 1)

typedef struct {
    uint8_t infotype;
    char server[32];
    uint16_t port;
    uint8_t reserved[99];
} robot_ipc_keepalive_cfg_t;

typedef struct {
    uint32_t wakeup_reason;
    beken2_timer_t timer;
    uint8_t delay_action;
    uint32_t delay_arg1;
    uint32_t delay_arg2;
} robot_wakeup_env_t;

typedef struct {
    uint16_t cmd_id;
    uint16_t len;
    uint8_t payload[];
} robot_ipc_msg_hdr_t;

typedef struct {
    bool initialized;
    bool ongoing;
    int sock;
    char server[32];
    uint16_t port;
    beken_thread_t tx_thread;
    beken_thread_t rx_thread;
    beken_semaphore_t sema;
    beken_timer_t timer;
    alarm_info_t rtc;
    uint8_t *rx_buf;
    uint8_t lp_state;
} robot_keepalive_env_t;

static robot_keepalive_env_t s_keepalive;
static robot_wakeup_env_t s_wakeup_env;

static void robot_cp_set_wakeup_reason(uint32_t reason)
{
    LOGI("set wakeup reason=%u\n", reason);
    s_wakeup_env.wakeup_reason = reason;
}

static void robot_cp_clear_wakeup_reason(void)
{
    s_wakeup_env.wakeup_reason = 0;
}

static void robot_cp_wakeup_host(uint32_t reason)
{
    robot_cp_set_wakeup_reason(reason);
    bk_pm_module_vote_boot_ap_ctrl(PM_BOOT_AP_MODULE_NAME_APP, PM_POWER_MODULE_STATE_ON);
    cif_power_up_host();
    bk_wifi_send_listen_interval_req(1);
}

static void robot_cp_power_down_host(void)
{
    robot_cp_clear_wakeup_reason();
    bk_pm_module_vote_boot_ap_ctrl(PM_BOOT_AP_MODULE_NAME_APP, PM_POWER_MODULE_STATE_OFF);
    cif_power_down_host();
    bk_wifi_send_listen_interval_req(10);
}

static void robot_cp_keepalive_sema_post(void)
{
    uint32_t flags = rtos_disable_int();
    if (s_keepalive.sema) {
        rtos_set_semaphore(&s_keepalive.sema);
    }
    rtos_enable_int(flags);
}

static void robot_cp_keepalive_rtc_handler(aon_rtc_id_t id, uint8_t *name_p, void *param)
{
    (void)id;
    (void)name_p;
    (void)param;
    if (s_keepalive.ongoing) {
        robot_cp_keepalive_sema_post();
    }
}

static void robot_cp_keepalive_set_rtc(void)
{
    alarm_info_t rtc = {
        "robot_ka",
        ROBOT_KEEPALIVE_INTERVAL_MS * AON_RTC_MS_TICK_CNT,
        1,
        robot_cp_keepalive_rtc_handler,
        NULL
    };

    os_memcpy(&s_keepalive.rtc, &rtc, sizeof(rtc));
    bk_alarm_unregister(AON_RTC_ID_1, rtc.name);
    bk_alarm_register(AON_RTC_ID_1, &rtc);
    bk_pm_wakeup_source_set(PM_WAKEUP_SOURCE_INT_RTC, NULL);
    bk_pm_sleep_mode_set(PM_MODE_LOW_VOLTAGE);
}

static void robot_cp_keepalive_stop_rtc(void)
{
    if (s_keepalive.rtc.name[0] != '\0') {
        bk_alarm_unregister(AON_RTC_ID_1, s_keepalive.rtc.name);
    }
}

static void robot_cp_keepalive_enter_lv_sleep(void)
{
    if (s_keepalive.lp_state == PM_MODE_LOW_VOLTAGE) {
        return;
    }
    s_keepalive.lp_state = PM_MODE_LOW_VOLTAGE;
    cif_start_lv_sleep();
}

static void robot_cp_keepalive_exit_lv_sleep(void)
{
    if (s_keepalive.lp_state != PM_MODE_LOW_VOLTAGE) {
        return;
    }
    s_keepalive.lp_state = PM_MODE_NORMAL_SLEEP;
    cif_exit_sleep();
}

static int robot_cp_set_socket_timeout(int sock, int timeout_ms)
{
    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    return 0;
}

static bk_err_t robot_cp_keepalive_connect(void)
{
    struct sockaddr_in remote = {0};
    struct sockaddr_in local = {0};
    static uint32_t bind_seed;

    for (uint8_t retry = 0; retry < ROBOT_KEEPALIVE_MAX_RETRY_CNT; retry++) {
        s_keepalive.sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s_keepalive.sock < 0) {
            LOGE("socket failed err=%d\n", errno);
            rtos_delay_milliseconds(1000);
            continue;
        }

        robot_cp_set_socket_timeout(s_keepalive.sock, ROBOT_KEEPALIVE_SOCKET_TIMEOUT_MS);

        local.sin_family = AF_INET;
        local.sin_addr.s_addr = INADDR_ANY;
        int bind_ret = -1;
        uint16_t bind_port = 0;
        for (uint8_t i = 0; i < ROBOT_KEEPALIVE_CP_BIND_PORT_COUNT; i++) {
            bind_seed = (bind_seed * 1103515245u + 12345u) & 0x7fffffffu;
            bind_port = (uint16_t)(ROBOT_KEEPALIVE_CP_BIND_PORT_START +
                                   (bind_seed % ROBOT_KEEPALIVE_CP_BIND_PORT_COUNT));
            local.sin_port = htons(bind_port);
            bind_ret = bind(s_keepalive.sock, (const struct sockaddr *)&local, sizeof(local));
            if (bind_ret == 0) {
                break;
            }
        }
        if (bind_ret != 0) {
            LOGE("bind CP port range failed err=%d\n", errno);
            closesocket(s_keepalive.sock);
            s_keepalive.sock = -1;
            rtos_delay_milliseconds(1000);
            continue;
        }

        remote.sin_family = AF_INET;
        remote.sin_port = htons(s_keepalive.port);
        remote.sin_addr.s_addr = inet_addr(s_keepalive.server);
        if (connect(s_keepalive.sock, (const struct sockaddr *)&remote, sizeof(remote)) == 0) {
            LOGI("CP keepalive connected %s:%u local=%u\n",
                 s_keepalive.server, s_keepalive.port, bind_port);
            return BK_OK;
        }

        LOGE("connect %s:%u failed err=%d\n", s_keepalive.server, s_keepalive.port, errno);
        closesocket(s_keepalive.sock);
        s_keepalive.sock = -1;
        rtos_delay_milliseconds(1000);
    }

    return BK_FAIL;
}

static void robot_cp_json_extract_id(const char *json, char *id_json, size_t id_json_len)
{
    const char *id = strstr(json, "\"id\"");
    if (!id || id_json_len == 0) {
        os_snprintf(id_json, id_json_len, "null");
        return;
    }

    id = strchr(id, ':');
    if (!id) {
        os_snprintf(id_json, id_json_len, "null");
        return;
    }
    id++;
    while (*id == ' ' || *id == '\t') {
        id++;
    }

    const char *end = id;
    if (*id == '"') {
        end++;
        while (*end && *end != '"') {
            end++;
        }
        if (*end == '"') {
            end++;
        }
    } else {
        while (*end && *end != ',' && *end != '}' && *end != '\r' && *end != '\n') {
            end++;
        }
    }

    size_t len = (size_t)(end - id);
    if (len == 0 || len >= id_json_len) {
        os_snprintf(id_json, id_json_len, "null");
        return;
    }
    os_memcpy(id_json, id, len);
    id_json[len] = '\0';
}

static void robot_cp_send_wakeup_response(const char *json)
{
    char id_json[64] = {0};
    char response[192] = {0};

    robot_cp_json_extract_id(json, id_json, sizeof(id_json));
    int len = os_snprintf(response, sizeof(response),
                          "{\"jsonrpc\":\"2.0\",\"id\":%s,\"result\":{\"state\":\"awake\",\"reason\":\"wake success\"}}\n",
                          id_json);
    if (len > 0 && s_keepalive.sock >= 0) {
        send(s_keepalive.sock, response, (size_t)len, 0);
    }
}

static void robot_cp_send_power_notify(const char *state, const char *reason)
{
    char notify[160] = {0};
    int len = os_snprintf(notify, sizeof(notify),
                          "{\"jsonrpc\":\"2.0\",\"method\":\"robot.notify.power\",\"params\":{\"state\":\"%s\",\"reason\":\"%s\"}}\n",
                          state, reason);
    if (len > 0 && s_keepalive.sock >= 0) {
        send(s_keepalive.sock, notify, (size_t)len, 0);
    }
}

static void robot_cp_handle_cmd_data(char *data, int len)
{
    data[len] = '\0';
    LOGI("CP cmd rx len=%d data=%s\n", len, data);

    if (strstr(data, "robot.session.wakeup") || strstr(data, "robot.power.wake")) {
        robot_cp_send_wakeup_response(data);
        robot_cp_wakeup_host(ROBOT_POWERUP_MULTIMEDIA_WAKEUP);
        s_keepalive.ongoing = false;
        robot_cp_keepalive_sema_post();
        return;
    }

    if (strstr(data, "\"method\"")) {
        LOGI("cmd requires AP, wake host\n");
        robot_cp_wakeup_host(ROBOT_POWERUP_MULTIMEDIA_WAKEUP);
        s_keepalive.ongoing = false;
        robot_cp_keepalive_sema_post();
    }
}

static void robot_cp_keepalive_rx_thread(void *arg)
{
    (void)arg;

    while (s_keepalive.ongoing && s_keepalive.sock >= 0) {
        int len = recv(s_keepalive.sock, s_keepalive.rx_buf, ROBOT_KEEPALIVE_RX_BUF_SIZE - 1, 0);
        if (len > 0) {
            robot_cp_handle_cmd_data((char *)s_keepalive.rx_buf, len);
            continue;
        }

        if (len == 0) {
            LOGE("App cmd connection closed in CP keepalive\n");
            robot_cp_wakeup_host(ROBOT_POWERUP_KEEPALIVE_DISCONNECT);
            break;
        }

        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            continue;
        }

        LOGE("CP keepalive recv failed err=%d\n", errno);
        robot_cp_wakeup_host(ROBOT_POWERUP_KEEPALIVE_FAIL_WAKEUP);
        break;
    }

    s_keepalive.rx_thread = NULL;
    s_keepalive.ongoing = false;
    robot_cp_keepalive_sema_post();
    rtos_delete_thread(NULL);
}

static void robot_cp_keepalive_tx_thread(void *arg)
{
    (void)arg;

    cif_filter_add_customer_filter(inet_addr(s_keepalive.server), s_keepalive.port);

    if (robot_cp_keepalive_connect() != BK_OK) {
        LOGE("CP keepalive connection failed, wake AP\n");
        robot_cp_wakeup_host(ROBOT_POWERUP_KEEPALIVE_DISCONNECT);
        goto exit;
    }

    if (rtos_create_thread(&s_keepalive.rx_thread,
                           ROBOT_KEEPALIVE_TASK_PRIO,
                           "robot_ka_rx",
                           (beken_thread_function_t)robot_cp_keepalive_rx_thread,
                           2048,
                           NULL) != BK_OK) {
        LOGE("create CP keepalive RX thread failed\n");
        robot_cp_wakeup_host(ROBOT_POWERUP_KEEPALIVE_FAIL_WAKEUP);
        goto exit;
    }

    robot_cp_send_power_notify("asleep", "idle");
    robot_cp_power_down_host();
    robot_cp_keepalive_enter_lv_sleep();

    while (s_keepalive.ongoing) {
        robot_cp_keepalive_set_rtc();
        if (rtos_get_semaphore(&s_keepalive.sema, BEKEN_WAIT_FOREVER) != BK_OK) {
            break;
        }
        if (s_keepalive.ongoing && s_keepalive.sock >= 0) {
            robot_cp_send_power_notify("asleep", "keepalive");
        }
    }

exit:
    s_keepalive.tx_thread = NULL;
    robot_cp_keepalive_stop();
    rtos_delete_thread(NULL);
}

static bk_err_t robot_cp_keepalive_start(const robot_ipc_keepalive_cfg_t *cfg)
{
    if (!cfg || cfg->server[0] == '\0' || cfg->port == 0) {
        return BK_ERR_PARAM;
    }

    robot_cp_keepalive_stop();
    os_memset(&s_keepalive, 0, sizeof(s_keepalive));
    os_snprintf(s_keepalive.server, sizeof(s_keepalive.server), "%s", cfg->server);
    s_keepalive.port = cfg->port;
    s_keepalive.sock = -1;
    s_keepalive.lp_state = PM_MODE_NORMAL_SLEEP;
    s_keepalive.rx_buf = os_malloc(ROBOT_KEEPALIVE_RX_BUF_SIZE);
    if (!s_keepalive.rx_buf) {
        return BK_ERR_NO_MEM;
    }
    if (rtos_init_semaphore(&s_keepalive.sema, 1) != BK_OK) {
        os_free(s_keepalive.rx_buf);
        os_memset(&s_keepalive, 0, sizeof(s_keepalive));
        return BK_FAIL;
    }

    s_keepalive.ongoing = true;
    if (rtos_create_thread(&s_keepalive.tx_thread,
                           ROBOT_KEEPALIVE_TASK_PRIO,
                           "robot_ka_tx",
                           (beken_thread_function_t)robot_cp_keepalive_tx_thread,
                           3072,
                           NULL) != BK_OK) {
        robot_cp_keepalive_stop();
        return BK_FAIL;
    }

    return BK_OK;
}

bk_err_t robot_cp_keepalive_stop(void)
{
    s_keepalive.ongoing = false;
    robot_cp_keepalive_sema_post();

    robot_cp_keepalive_stop_rtc();
    robot_cp_keepalive_exit_lv_sleep();

    if (s_keepalive.timer.handle) {
        rtos_stop_timer(&s_keepalive.timer);
        rtos_deinit_timer(&s_keepalive.timer);
        s_keepalive.timer.handle = NULL;
    }

    if (s_keepalive.sock >= 0) {
        closesocket(s_keepalive.sock);
        s_keepalive.sock = -1;
    }

    if (!s_keepalive.tx_thread && !s_keepalive.rx_thread) {
        if (s_keepalive.rx_buf) {
            os_free(s_keepalive.rx_buf);
            s_keepalive.rx_buf = NULL;
        }
        if (s_keepalive.sema) {
            rtos_deinit_semaphore(&s_keepalive.sema);
            s_keepalive.sema = NULL;
        }
        cif_filter_add_customer_filter(0, 0);
    }

    return BK_OK;
}

static int robot_cp_ipc_msg_handler(struct bk_msg_hdr *msg)
{
    robot_ipc_msg_hdr_t *hdr = NULL;
    uint8_t *cfm_data = NULL;
    uint16_t cfm_len = 0;

    if (!msg) {
        return BK_FAIL;
    }

    hdr = (robot_ipc_msg_hdr_t *)(msg + 1);
    LOGI("IPC cmd=0x%x len=%u\n", hdr->cmd_id, hdr->len);

    switch (hdr->cmd_id) {
    case ROBOT_IPC_CMD_KEEPALIVE_START:
        robot_cp_keepalive_start((const robot_ipc_keepalive_cfg_t *)(hdr + 1));
        break;
    case ROBOT_IPC_CMD_KEEPALIVE_STOP:
        robot_cp_keepalive_stop();
        break;
    case ROBOT_IPC_CMD_GET_WAKEUP_ENV_ADDR: {
        static uint32_t addr;
        addr = (uint32_t)(uintptr_t)&s_wakeup_env;
        cfm_data = (uint8_t *)&addr;
        cfm_len = sizeof(addr);
        break;
    }
    default:
        break;
    }

    return cif_send_customer_cmd_cfm(cfm_data, cfm_len, msg);
}

bk_err_t robot_cp_keepalive_init(void)
{
    os_memset(&s_keepalive, 0, sizeof(s_keepalive));
    os_memset(&s_wakeup_env, 0, sizeof(s_wakeup_env));
    robot_cp_set_wakeup_reason(ROBOT_POWERUP_NORMAL);
    cif_register_customer_msg_handler(robot_cp_ipc_msg_handler);
    LOGI("robot CP keepalive initialized\n");
    return BK_OK;
}

bk_err_t robot_cp_keepalive_mark_ap_wakeup(void)
{
    robot_cp_wakeup_host(ROBOT_POWERUP_MULTIMEDIA_WAKEUP);
    return BK_OK;
}
