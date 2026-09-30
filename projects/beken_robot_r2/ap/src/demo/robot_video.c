/**
 * @file robot_video.c
 * @brief Backend for the page_11 robot video demo. LVGL-free wrapper
 *        around robot_ctrl_service start/stop/state queries. UI hooks
 *        live in beken_generated/page_robot_video/.
 *
 * page_11_set_video_connected() is forwarded to the UI hook through a
 * registered sink so robot_ctrl_service callers do not have to touch
 * LVGL types/APIs.
 */
#include "demo/robot_video.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef ROBOT_TEST

#include <components/log.h>

#if CONFIG_BK_ROBOT_CTRL_SERVICE
#include "robot_ctrl_service.h"
#endif

#define TAG "robot_video"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

/* UI bridge: page_robot_video_hooks.c registers a sink for live status
 * updates pushed from robot_ctrl_service (any task). */
static void (*s_connected_sink)(void);
static void (*s_connecting_sink)(void);

void robot_video_register_connected_sink(void (*sink)(void))
{
    s_connected_sink = sink;
}

void robot_video_register_connecting_sink(void (*sink)(void))
{
    s_connecting_sink = sink;
}

int robot_video_start_service(void)
{
#if CONFIG_BK_ROBOT_CTRL_SERVICE
    LOGI("start robot video connection service\r\n");
    return (robot_ctrl_service_start() == BK_OK) ? 0 : -1;
#else
    LOGI("robot ctrl service is disabled\r\n");
    return 0;
#endif
}

int robot_video_stop_service(void)
{
#if CONFIG_BK_ROBOT_CTRL_SERVICE
    robot_ctrl_service_stop();
#endif
    return 0;
}

bool robot_video_is_connected(void)
{
#if CONFIG_BK_ROBOT_CTRL_SERVICE
    return (robot_ctrl_service_is_connected() ||
            robot_ctrl_service_is_configured());
#else
    return false;
#endif
}

void page_11_set_video_connected(void)
{
    void (*sink)(void) = s_connected_sink;
    if (sink != NULL) {
        sink();
    }
}

void page_11_set_video_connecting(void)
{
    void (*sink)(void) = s_connecting_sink;
    if (sink != NULL) {
        sink();
    }
}

int robot_video_init(void) { return 0; }

extern int page_robot_video_enter(void);

int robot_video_start(void)
{
    LOGI("Robot video playback -> page_11\r\n");
    return page_robot_video_enter();
}

int robot_video_stop(void)
{
    (void)robot_video_stop_service();
    return 0;
}

#else  /* !ROBOT_TEST */

void robot_video_register_connected_sink(void (*sink)(void)) { (void)sink; }
void robot_video_register_connecting_sink(void (*sink)(void)) { (void)sink; }
int  robot_video_start_service(void)   { return 0; }
int  robot_video_stop_service(void)    { return 0; }
bool robot_video_is_connected(void)    { return false; }
void page_11_set_video_connected(void) {}
void page_11_set_video_connecting(void) {}

int robot_video_init(void)  { return 0; }
int robot_video_start(void) { return 0; }
int robot_video_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_robot_video = {
    .name  = "robot_video",
    .init  = robot_video_init,
    .start = robot_video_start,
    .stop  = robot_video_stop,
};
