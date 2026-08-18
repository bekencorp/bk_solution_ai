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

/* Tracks whether a chat session was entered, so teardown runs exactly once.
 * destroy_page_page_6() calls bk_page_fire_destroy() unconditionally and the
 * back-key path destroys the page twice (return_to_menu release + explicit
 * destroy), so on_destroy can fire more than once per exit.
 *
 * SMP safety: on_init/on_destroy are the ONLY accessors and both always run
 * while holding the LVGL display lock (lv_vendor_disp_lock) - the CLI/gesture
 * path locks in nav_cli_goto()/ui_nav_dispatch_event() and navigate_to_screen()
 * fires the hooks inside that lock. That lock serializes the two accessors
 * across cores (so the test-and-clear below is never concurrent) and its
 * acquire/release barriers make the write visible to the other core, so a plain
 * bool is sufficient. Do NOT read/write this flag from any context that does
 * not hold the LVGL lock (e.g. the async sconf worker, timers, ISRs). */
static bool s_ai_chat_session_active = false;

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("AI chat back -> page_3\r\n");
    /* Backend teardown (RTC/Agora stop + audio stop) lives in
     * page_ai_chat_on_destroy so it runs on EVERY exit path, not just the back
     * key. destroy_page_page_6() below fires that hook (via bk_page_fire_destroy),
     * so we must NOT call ai_chat_request_exit() here as well, or the exit op
     * would be enqueued twice. */
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
    /* Arm teardown for this entry before starting: even a partial/failed
     * bring-up must be cleaned up on destroy (ai_chat_request_exit is a no-op
     * when nothing started). */
    s_ai_chat_session_active = true;
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
    /* Tear the AI session (RTC/Agora + audio) down here so it happens no matter
     * how the page was left - back key, right-swipe gesture, jumping to another
     * demo, or nav goto. Previously teardown only lived in on_screen_prev, so
     * any other exit left the RTC channel joined and the AI kept talking after
     * the chat UI was gone. Guarded so the double destroy on the back-key path
     * only tears down once. */
    if (s_ai_chat_session_active) {
        s_ai_chat_session_active = false;
        if (ai_chat_request_exit() != 0) {
            LOGW("AI chat exit dispatch failed (worker busy?)\r\n");
        }
    }
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
