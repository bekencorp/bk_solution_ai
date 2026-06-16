#include "robot_ctrl_internal.h"

#include <os/str.h>
#include "bk_smart_config.h"

bk_err_t robot_ctrl_service_send_hello(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();

    if (!root || !params) {
        if (root) {
            cJSON_Delete(root);
        }
        if (params) {
            cJSON_Delete(params);
        }
        return BK_FAIL;
    }

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "id", "hello");
    cJSON_AddStringToObject(root, "method", "robot.session.hello");
    cJSON_AddStringToObject(params, "token", bk_sconf_get_agent_identity_token());
    cJSON_AddItemToObject(root, "params", params);

    LOGI("send robot.session.hello\n");
    return robot_ctrl_send_json(root);
}

void robot_ctrl_handle_session_response(cJSON *root)
{
    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *error = cJSON_GetObjectItem(root, "error");
    const char *id_str = cJSON_IsString(id) ? id->valuestring : NULL;

    LOGI("robot ctrl service: CMD RX response id=%s\r\n", id_str);

    if (error) {
        cJSON *code = cJSON_GetObjectItem(error, "code");
        if (code && cJSON_IsNumber(code) && code->valueint == -32001) {
            LOGE("App rejected robot token\n");
            robot_ctrl_service_stop_all_runtime();
        }
        return;
    }

    if (id_str && os_strcmp(id_str, "hello") == 0) {
        s_ctrl.authed = true;
        s_ctrl.state = ROBOT_CTRL_STATE_AUTHED;
        LOGI("hello authed by App\n");
    }
}
