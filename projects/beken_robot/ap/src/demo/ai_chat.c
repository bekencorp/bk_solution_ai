/**
 * @file ai_chat.c
 * @brief Backend for the AI chat demo (page_6). LVGL-free: wraps
 *        bk_smart_config "text mode" start/exit. All UI handling lives in
 *        beken_generated/page_ai_chat/page_ai_chat_hooks.c.
 */
#include "demo/ai_chat.h"

#ifdef ROBOT_TEST

#include "bk_smart_config.h"
#if CONFIG_BK_NETWORK_ENGINE
#include "network_engine.h"
#endif
#include <components/log.h>

#define TAG "ai_chat_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

int ai_chat_start_service(void)
{
#if CONFIG_BK_NETWORK_ENGINE
    if (ntwk_eng_init() != 0) {
        LOGW("ntwk_eng_init failed\r\n");
        return -1;
    }
#endif
    return (bk_sconf_enter_text_mode() == BK_OK) ? 0 : -1;
}

int ai_chat_request_exit(void)
{
    return (bk_sconf_exit_ai_mode_async(0) == BK_OK) ? 0 : -1;
}

int ai_chat_init(void)  { return 0; }

extern int page_ai_chat_enter(void);

int ai_chat_start(void)
{
    LOGI("AI chat -> page_6\r\n");
    return page_ai_chat_enter();
}

int ai_chat_stop(void)
{
    (void)ai_chat_request_exit();
    return 0;
}

#else  /* !ROBOT_TEST */

int ai_chat_start_service(void) { return 0; }
int ai_chat_request_exit(void)  { return 0; }
int ai_chat_init(void)  { return 0; }
int ai_chat_start(void) { return 0; }
int ai_chat_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_ai_chat = {
    "ai_chat",
    ai_chat_init,
    ai_chat_start,
    ai_chat_stop,
};
