#include "robot_ctrl_internal.h"

extern void page_11_set_video_connected(void) __attribute__((weak));

void robot_ctrl_notify_video_connected(void)
{
    if (page_11_set_video_connected) {
        page_11_set_video_connected();
    }
}

bk_err_t robot_ctrl_service_notify(const char *method, const char *params_json)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *params = params_json ? cJSON_Parse(params_json) : NULL;

    if (!root) {
        return BK_FAIL;
    }

    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", method ? method : "robot.notify.event");
    if (params) {
        cJSON_AddItemToObject(root, "params", params);
    }

    return robot_ctrl_send_json(root);
}
