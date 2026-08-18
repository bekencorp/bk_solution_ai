/**
 * @file page_robot_video_hooks.c
 * @brief page_11 robot video UI hooks. Owns the status label updates
 *        and defers robot_ctrl_service startup to a one-shot LVGL timer
 *        so the heavy connect path runs after the page is on screen.
 */
#include <stddef.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "demo/robot_video.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "ui_list_menu.h"
#include "lv_vendor.h"
#include <components/log.h>

#define TAG "page_robot_video"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

static lv_timer_t *s_start_timer;

static void start_service_cb(lv_timer_t *timer)
{
    (void)timer;
    s_start_timer = NULL;
    if (robot_video_start_service() != 0) {
        LOGI("robot ctrl service start failed\r\n");
    }
}

/* Called from src/demo/robot_video.c (any task). */
static void connected_sink(void)
{
    lv_vendor_disp_lock();
    if (bk_lv_tool_ui.page_11_label_state != NULL &&
        lv_obj_is_valid(bk_lv_tool_ui.page_11_label_state)) {
        lv_label_set_text(bk_lv_tool_ui.page_11_label_state, "video connected");
    }
    lv_vendor_disp_unlock();
}

/* Called from src/demo/robot_video.c (any task) when the App link drops;
 * resets the label so the page reflects the discovery/reconnect state. */
static void connecting_sink(void)
{
    lv_vendor_disp_lock();
    if (bk_lv_tool_ui.page_11_label_state != NULL &&
        lv_obj_is_valid(bk_lv_tool_ui.page_11_label_state)) {
        lv_label_set_text(bk_lv_tool_ui.page_11_label_state, "video connecting...");
    }
    lv_vendor_disp_unlock();
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page11 back -> page_3\r\n");
    (void)robot_video_stop_service();
    (void)ui_demo_return_to_menu();
}

static const ui_page_nav_ops_t page_11_nav_ops = {
    .on_focus_prev  = NULL,
    .on_focus_next  = NULL,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = NULL,
};

static void page_robot_video_on_init(bk_lv_ui_t *ui)
{
    /* page_11_init.c writes "video connecting..." by default. If the
     * link is already up, flip it to "video connected" right away. */
    if (robot_video_is_connected()) {
        if (ui->page_11_label_state != NULL &&
            lv_obj_is_valid(ui->page_11_label_state)) {
            lv_label_set_text(ui->page_11_label_state, "video connected");
        }
    }

    if (s_start_timer != NULL) {
        lv_timer_del(s_start_timer);
        s_start_timer = NULL;
    }
    s_start_timer = lv_timer_create(start_service_cb, 80, NULL);
    lv_timer_set_repeat_count(s_start_timer, 1);
    (void)ui_nav_register_screen(ui->page_11, &page_11_nav_ops);

    robot_video_register_connected_sink(connected_sink);
    robot_video_register_connecting_sink(connecting_sink);
}

static void page_robot_video_on_destroy(bk_lv_ui_t *ui)
{
    robot_video_register_connected_sink(NULL);
    robot_video_register_connecting_sink(NULL);
    ui_nav_unregister_screen(ui->page_11);
    if (s_start_timer != NULL) {
        lv_timer_del(s_start_timer);
        s_start_timer = NULL;
    }
}

void page_robot_video_init_hooks(void)
{
    (void)bk_page_set_init_hook(11, page_robot_video_on_init);
    (void)bk_page_set_destroy_hook(11, page_robot_video_on_destroy);
}

int page_robot_video_enter(void)
{
    if (bk_lv_tool_ui.page_11 != NULL &&
        lv_obj_is_valid(bk_lv_tool_ui.page_11)) {
        destroy_page_page_11(&bk_lv_tool_ui);
    }

    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_11,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_11);
    return 0;
}

#else  /* !ROBOT_TEST */

void page_robot_video_init_hooks(void) {}
int  page_robot_video_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
