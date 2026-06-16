/**
 * @file page_ai_chat_hooks.c
 * @brief page_6 AI chat UI hooks: chat-animation widget lifecycle +
 *        navigation. Backend service control (bk_smart_config text mode)
 *        lives in src/demo/ai_chat.c.
 */
#include <stddef.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "page_chat_anim.h"
#include "demo/ai_chat.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "ui_list_menu.h"
#include <components/log.h>

#define TAG "page_ai_chat"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("AI chat back -> page_3\r\n");
    /* Async exit (mirrors vision page): avoids holding the LVGL lock
     * while the synchronous AI teardown path runs. */
    if (ai_chat_request_exit() != 0) {
        LOGW("AI chat exit dispatch failed (worker busy?)\r\n");
    }
    (void)ui_demo_return_to_menu();
    destroy_page_page_6(ui);
}

static const ui_page_nav_ops_t page_6_nav_ops = {
    .on_focus_prev  = NULL,
    .on_focus_next  = NULL,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = NULL,
};

static void page_ai_chat_on_init(bk_lv_ui_t *ui)
{
    page_chat_anim_attach(ui->page_6, PAGE_CHAT_ANIM_MODE_VOICE);
    (void)ui_nav_register_screen(ui->page_6, &page_6_nav_ops);
    if (ai_chat_start_service() != 0) {
        LOGW("AI chat text mode start failed\r\n");
    } else {
        LOGI("AI chat text mode started\r\n");
    }
}

static void page_ai_chat_on_destroy(bk_lv_ui_t *ui)
{
    ui_nav_unregister_screen(ui->page_6);
    page_chat_anim_detach();
}

void page_ai_chat_init_hooks(void)
{
    (void)bk_page_set_init_hook(6, page_ai_chat_on_init);
    (void)bk_page_set_destroy_hook(6, page_ai_chat_on_destroy);
}

int page_ai_chat_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_6,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_6);
    return 0;
}

#else  /* !ROBOT_TEST */

void page_ai_chat_init_hooks(void) {}
int  page_ai_chat_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
