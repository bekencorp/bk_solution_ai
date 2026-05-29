/**
 * @file page_vision_hooks.c
 * @brief page_7 vision recognition UI hooks. Reuses the page_chat_anim
 *        widget in VISION mode; backend service control lives in
 *        src/demo/vision.c.
 */
#include <stddef.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "page_chat_anim.h"
#include "demo/vision.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include <components/log.h>

#define TAG "page_vision"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("Vision recognition back -> page_3\r\n");
    /* Heavy teardown (Agora destroy + video_engine_deinit + MIPI camera
     * close) runs on a worker so the LVGL lock held by ui_nav_dispatch
     * is not blocked for ~250 ms - 1 s. */
    if (vision_request_exit() != 0) {
        LOGW("Vision recognition exit dispatch failed (worker busy?)\r\n");
    }
    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
    destroy_page_page_7(ui);
}

static const ui_page_nav_ops_t page_7_nav_ops = {
    .on_focus_prev  = NULL,
    .on_focus_next  = NULL,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = NULL,
};

static void page_vision_on_init(bk_lv_ui_t *ui)
{
    page_chat_anim_attach(ui->page_7, PAGE_CHAT_ANIM_MODE_VISION);
    (void)ui_nav_register_screen(ui->page_7, &page_7_nav_ops);
    if (vision_start_service() != 0) {
        LOGW("Vision mode start failed\r\n");
    } else {
        LOGI("Vision mode started\r\n");
    }
}

static void page_vision_on_destroy(bk_lv_ui_t *ui)
{
    ui_nav_unregister_screen(ui->page_7);
    page_chat_anim_detach();
}

void page_vision_init_hooks(void)
{
    (void)bk_page_set_init_hook(7, page_vision_on_init);
    (void)bk_page_set_destroy_hook(7, page_vision_on_destroy);
}

int page_vision_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_7,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_7);
    return 0;
}

#else  /* !ROBOT_TEST */

void page_vision_init_hooks(void) {}
int  page_vision_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
