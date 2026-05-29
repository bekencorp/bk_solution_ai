/**
 * @file vision.c
 * @brief Backend for the vision recognition demo (page_7). LVGL-free:
 *        wraps bk_smart_config "vision mode" start/exit.
 */
#include "demo/vision.h"

#ifdef ROBOT_TEST

#include "bk_smart_config.h"
#include <components/log.h>

#define TAG "vision_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

int vision_start_service(void)
{
    return (bk_sconf_enter_vision_mode() == BK_OK) ? 0 : -1;
}

int vision_request_exit(void)
{
    return (bk_sconf_exit_ai_mode_async(1) == BK_OK) ? 0 : -1;
}

int vision_init(void)  { return 0; }

extern int page_vision_enter(void);

int vision_start(void)
{
    LOGI("Vision recognition -> page_7\r\n");
    return page_vision_enter();
}

int vision_stop(void)
{
    (void)vision_request_exit();
    return 0;
}

#else  /* !ROBOT_TEST */

int vision_start_service(void) { return 0; }
int vision_request_exit(void)  { return 0; }
int vision_init(void)  { return 0; }
int vision_start(void) { return 0; }
int vision_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_vision = {
    "vision",
    vision_init,
    vision_start,
    vision_stop,
};
