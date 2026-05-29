#include "robot_lan_net.h"

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>
#include <components/system.h>
#include <driver/trng.h>
#include "bk_factory_config.h"

#define TAG "robot_id"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define ROBOT_ID_MAGIC 0x52424944U
#define ROBOT_ID_KEY   "robot_identity"

typedef struct {
    uint32_t magic;
    char uuid[ROBOT_LAN_UUID_LEN];
    char token[ROBOT_LAN_TOKEN_LEN];
} robot_identity_store_t;

static robot_identity_store_t s_identity;

static bool robot_identity_valid(const robot_identity_store_t *identity)
{
    return identity
        && identity->magic == ROBOT_ID_MAGIC
        && identity->uuid[0] != '\0'
        && identity->token[0] != '\0';
}

static void robot_identity_generate_uuid(char *uuid, size_t len)
{
    uint8_t mac[6] = {0};

    if (bk_get_mac(mac, MAC_TYPE_BASE) != BK_OK) {
        uint32_t seed = bk_rand();
        os_snprintf(uuid, len, "bk7259-robot-%08x", seed);
        return;
    }

    os_snprintf(uuid, len, "bk7259-robot-%02x%02x%02x%02x%02x%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void robot_identity_generate_token(char *token, size_t len)
{
    uint32_t r0 = bk_rand();
    uint32_t r1 = bk_rand();
    uint32_t r2 = bk_rand();
    uint32_t r3 = bk_rand();

    os_snprintf(token, len, "%08x%08x%08x%08x", r0, r1, r2, r3);
}

static bk_err_t robot_identity_store(void)
{
#if CONFIG_BK_FACTORY_CONFIG
    int ret = bk_config_write(ROBOT_ID_KEY, &s_identity, sizeof(s_identity));
    if (ret != 0) {
        LOGW("store identity ret=%d\n", ret);
    }
    bk_config_sync_flash_safely();
#endif
    return BK_OK;
}

bk_err_t robot_lan_net_identity_init(void)
{
    os_memset(&s_identity, 0, sizeof(s_identity));

#if CONFIG_BK_FACTORY_CONFIG
    if (bk_config_read(ROBOT_ID_KEY, &s_identity, sizeof(s_identity)) == sizeof(s_identity)
        && robot_identity_valid(&s_identity)) {
        LOGI("loaded robot identity uuid=%s token=%s\n", s_identity.uuid, s_identity.token);
        return BK_OK;
    }
#endif

    s_identity.magic = ROBOT_ID_MAGIC;
    robot_identity_generate_uuid(s_identity.uuid, sizeof(s_identity.uuid));
    robot_identity_generate_token(s_identity.token, sizeof(s_identity.token));
    LOGI("generated robot identity uuid=%s token=%s\n", s_identity.uuid, s_identity.token);
    return robot_identity_store();
}

bk_err_t robot_lan_net_identity_reset_token(void)
{
    if (s_identity.uuid[0] == '\0') {
        robot_identity_generate_uuid(s_identity.uuid, sizeof(s_identity.uuid));
    }

    s_identity.magic = ROBOT_ID_MAGIC;
    robot_identity_generate_token(s_identity.token, sizeof(s_identity.token));
    LOGI("reset robot token uuid=%s token=%s\n", s_identity.uuid, s_identity.token);
    return robot_identity_store();
}

bk_err_t robot_lan_net_identity_build_ble_payload(char *buf, uint16_t buf_len)
{
    if (!buf || buf_len == 0) {
        return BK_ERR_PARAM;
    }

    if (!robot_identity_valid(&s_identity)) {
        BK_LOG_ON_ERR(robot_lan_net_identity_init());
    }

    int len = os_snprintf(buf, buf_len,
                          "{\"uuid\":\"%s\",\"token\":\"%s\"}",
                          s_identity.uuid, s_identity.token);
    if (len <= 0 || len >= buf_len) {
        LOGE("identity payload overflow len=%d buf_len=%u\n", len, buf_len);
        return BK_FAIL;
    }

    return len;
}

const char *robot_lan_net_get_uuid(void)
{
    if (!robot_identity_valid(&s_identity)) {
        robot_lan_net_identity_init();
    }
    return s_identity.uuid;
}

const char *robot_lan_net_get_token(void)
{
    if (!robot_identity_valid(&s_identity)) {
        robot_lan_net_identity_init();
    }
    return s_identity.token;
}
