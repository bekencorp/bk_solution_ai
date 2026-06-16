#include "robot_ctrl_internal.h"

#include <os/mem.h>
#include <os/str.h>
#include <stdlib.h>
#include <string.h>
#include "network_engine.h"

static cJSON *json_create_response(cJSON *id)
{
    cJSON *resp = cJSON_CreateObject();

    if (!resp) {
        return NULL;
    }

    cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
    if (id) {
        cJSON_AddItemToObject(resp, "id", cJSON_Duplicate(id, 1));
    } else {
        cJSON_AddNullToObject(resp, "id");
    }

    return resp;
}

bk_err_t robot_ctrl_send_error(cJSON *id, int code, const char *message)
{
    cJSON *resp = json_create_response(id);
    cJSON *error = cJSON_CreateObject();

    if (!resp || !error) {
        if (resp) {
            cJSON_Delete(resp);
        }
        if (error) {
            cJSON_Delete(error);
        }
        return BK_FAIL;
    }

    cJSON_AddNumberToObject(error, "code", code);
    cJSON_AddStringToObject(error, "message", message ? message : "error");
    cJSON_AddItemToObject(resp, "error", error);
    return robot_ctrl_send_json(resp);
}

bk_err_t robot_ctrl_send_result(cJSON *id, cJSON *result)
{
    cJSON *resp = json_create_response(id);

    if (!resp) {
        if (result) {
            cJSON_Delete(result);
        }
        return BK_FAIL;
    }

    if (result) {
        cJSON_AddItemToObject(resp, "result", result);
    } else {
        cJSON_AddItemToObject(resp, "result", cJSON_CreateObject());
    }

    return robot_ctrl_send_json(resp);
}

bk_err_t robot_ctrl_send_json(cJSON *root)
{
    char *text = NULL;
    char *framed = NULL;
    bk_err_t ret = BK_FAIL;
    size_t len;

    if (!root) {
        return BK_FAIL;
    }

    text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        return BK_FAIL;
    }

    LOGI("robot ctrl service: CMD TX: %s\r\n", text);

    len = os_strlen(text);
    framed = os_malloc(len + 2);
    if (!framed) {
        cJSON_free(text);
        return BK_FAIL;
    }

    os_memcpy(framed, text, len);
    framed[len] = '\n';
    framed[len + 1] = '\0';

    ret = (ntwk_eng_send_ctrl((const uint8_t *)framed, len + 1) == BK_OK) ? BK_OK : BK_FAIL;
    os_free(framed);
    cJSON_free(text);
    return ret;
}

int robot_ctrl_json_int(cJSON *item, int fallback)
{
    if (item && cJSON_IsNumber(item)) {
        return item->valueint;
    }

    if (item && cJSON_IsString(item) && item->valuestring) {
        char *end = NULL;
        long value = strtol(item->valuestring, &end, 10);
        if (end != item->valuestring) {
            return (int)value;
        }
    }

    return fallback;
}

static bk_err_t robot_ctrl_handle_request(cJSON *root)
{
    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *method_item = cJSON_GetObjectItem(root, "method");
    cJSON *params = cJSON_GetObjectItem(root, "params");
    bool has_id = (id != NULL);

    if (!method_item || !cJSON_IsString(method_item)) {
        return robot_ctrl_send_error(id, -32600, "invalid request");
    }

    const char *method = method_item->valuestring;
    robot_ctrl_service_refresh_activity();

    LOGI("robot ctrl service: CMD RX method=%s\r\n", method);

    if (!has_id) {
        if (os_strcmp(method, "robot.power.sleepAck") == 0) {
            return robot_ctrl_handle_power(method, id, params);
        }
        LOGI("ignore JSON-RPC notification method=%s\n", method);
        return BK_OK;
    }

    if (!s_ctrl.authed
        && os_strcmp(method, "robot.solution.getConfig") != 0
        && os_strcmp(method, "robot.power.wake") != 0
        && os_strcmp(method, "robot.power.sleepAck") != 0
        && os_strcmp(method, "robot.session.wakeup") != 0
        && os_strcmp(method, "robot.misc.ping") != 0) {
        return robot_ctrl_send_error(id, -32001, "unauthorized");
    }

    if (os_strcmp(method, "robot.misc.ping") == 0) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddStringToObject(result, "pong", "ok");
        return robot_ctrl_send_result(id, result);
    }

    if (strncmp(method, "robot.power.", 12) == 0 || os_strcmp(method, "robot.session.wakeup") == 0) {
        return robot_ctrl_handle_power(os_strcmp(method, "robot.session.wakeup") == 0 ? "robot.power.wake" : method,
                                       id, params);
    }

    if (strncmp(method, "robot.solution.", 15) == 0) {
        return robot_ctrl_handle_solution(method, id, params);
    }

    if (strncmp(method, "robot.camera.", 13) == 0) {
        return robot_ctrl_handle_camera(method, id, params);
    }

    if (strncmp(method, "robot.motion.", 13) == 0) {
        return robot_ctrl_handle_motion(method, id, params);
    }

    if (strncmp(method, "robot.audio.", 12) == 0) {
        return robot_ctrl_handle_audio(method, id);
    }

    return robot_ctrl_send_error(id, -32601, "method not found");
}

void robot_ctrl_handle_cmd(const char *cmd, size_t cmd_len)
{
    (void)cmd_len;
    cJSON *root = cJSON_Parse(cmd);

    if (!root) {
        LOGW("invalid JSON-RPC: %s\n", cmd);
        robot_ctrl_send_error(NULL, -32700, "parse error");
        return;
    }

    if (cJSON_GetObjectItem(root, "method")) {
        LOGI("robot ctrl service: CMD RX method\r\n");
        robot_ctrl_handle_request(root);
    } else {
        LOGI("robot ctrl service: CMD RX response\r\n");
        robot_ctrl_handle_session_response(root);
    }

    LOGI("robot ctrl service: CMD RX end\r\n");
    cJSON_Delete(root);
}
