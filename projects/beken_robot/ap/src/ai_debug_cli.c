/**
 * @file ai_debug_cli.c
 * @brief CLI helpers for verifying the AI dialogue / vision page state
 *        machine and animations without depending on a real conversation.
 */
#include <common/sys_config.h>

#include "ai_debug_cli.h"

#include <components/log.h>

#define TAG "ai_dbg"

#if CONFIG_CLI && CONFIG_LVGL

#include <stdio.h>
#include <string.h>

#include "cli.h"

#include "lvgl.h"
#include "lv_vendor.h"

#include "beken_ui.h"
#include "event_runtime.h"

#include "audio_engine.h"

#include "ui_nav_router.h"
#include "ui_nav_events.h"

#if CONFIG_APP_EVT
#include "app_event.h"
#endif

#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

/* ---------------------------------------------------------------------- */
/*  Helpers                                                                */
/* ---------------------------------------------------------------------- */

static const char *ai_evt_name(int evt)
{
#if CONFIG_APP_EVT
    switch (evt) {
    case APP_EVT_AI_LISTENING: return "LISTENING";
    case APP_EVT_AI_THINKING:  return "THINKING";
    case APP_EVT_AI_SPEAKING:  return "SPEAKING";
    case APP_EVT_AI_IDLE:      return "IDLE";
    default:                   return "?";
    }
#else
    (void)evt;
    return "?";
#endif
}

static void navigate_locked(lv_obj_t **target, void (*init)(bk_lv_ui_t *))
{
    lv_vendor_disp_lock();
    navigate_to_screen(target, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, init);
    lv_vendor_disp_unlock();
}

/* ---------------------------------------------------------------------- */
/*  Subcommand handlers                                                    */
/* ---------------------------------------------------------------------- */

static void cmd_chat(void)
{
    BK_LOGI(TAG, "aiui chat -> page_6\n");
#if CONFIG_BK_SMART_CONFIG
    /* page_6_init.c (ROBOT_TEST build) already calls enter_text_mode, but
     * the menu uses bk_sconf for state tracking. Trying to enter while
     * already in another mode is benign; we just let the page init path
     * handle it. */
#endif
    navigate_locked((lv_obj_t **)&bk_lv_tool_ui.page_6, init_page_page_6);
}

static void cmd_vision(void)
{
    BK_LOGI(TAG, "aiui vision -> page_7\n");
    navigate_locked((lv_obj_t **)&bk_lv_tool_ui.page_7, init_page_page_7);
}

static void cmd_back(void)
{
    BK_LOGI(TAG, "aiui back -> page_3\n");

    lv_vendor_disp_lock();
    lv_obj_t *active = lv_screen_active();
#if CONFIG_BK_SMART_CONFIG
    if (active == bk_lv_tool_ui.page_6) {
        (void)bk_sconf_exit_ai_mode_async(0);
    } else if (active == bk_lv_tool_ui.page_7) {
        (void)bk_sconf_exit_ai_mode_async(1);
    }
#else
    (void)active;
#endif
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
    lv_vendor_disp_unlock();
}

static void cmd_state(void)
{
    uint8_t  mic_active = audio_engine_get_mic_active();
    uint8_t  spk_active = audio_engine_get_spk_active();
    uint8_t  mic_level  = audio_engine_get_mic_level();
    uint8_t  spk_level  = audio_engine_get_spk_level();
    uint32_t pending    = audio_engine_get_spk_off_pending_ms();
    int      last_evt   = audio_engine_get_last_ai_evt();
    BK_LOGI(TAG,
            "state: mic_active=%u mic_level=%u  spk_active=%u spk_level=%u  "
            "spk_off_pending=%ums  last_ai_evt=%d(%s)\n",
            (unsigned)mic_active, (unsigned)mic_level,
            (unsigned)spk_active, (unsigned)spk_level,
            (unsigned)pending,
            last_evt, ai_evt_name(last_evt));
}

static void cmd_prev(void)
{
    BK_LOGI(TAG, "aiui prev -> simulate ADC_KEY_S4_DOUBLE / SCREEN_PREV\n");
    ui_nav_dispatch_event(UI_NAV_EVENT_SCREEN_PREV);
}

static int cmd_evt(const char *which)
{
#if CONFIG_APP_EVT
    int evt;
    if      (!strcmp(which, "listen")) evt = APP_EVT_AI_LISTENING;
    else if (!strcmp(which, "think"))  evt = APP_EVT_AI_THINKING;
    else if (!strcmp(which, "speak"))  evt = APP_EVT_AI_SPEAKING;
    else if (!strcmp(which, "idle"))   evt = APP_EVT_AI_IDLE;
    else                                return -1;

    BK_LOGI(TAG, "aiui evt -> %s (evt=%d)\n", ai_evt_name(evt), evt);
    (void)app_event_send_msg((uint32_t)evt, 0);
    return 0;
#else
    (void)which;
    BK_LOGW(TAG, "APP_EVT not enabled in build; cannot inject events\n");
    return -1;
#endif
}

/* ---------------------------------------------------------------------- */
/*  CLI dispatcher                                                         */
/* ---------------------------------------------------------------------- */

static void cli_aiui_fn(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;
    if (argc < 2) {
        BK_LOGI(TAG, "usage: aiui chat | vision | back | prev | state | evt <listen|think|speak|idle>\n");
        return;
    }

    const char *sub = argv[1];
    if      (!strcmp(sub, "chat"))   cmd_chat();
    else if (!strcmp(sub, "vision")) cmd_vision();
    else if (!strcmp(sub, "back"))   cmd_back();
    else if (!strcmp(sub, "prev"))   cmd_prev();
    else if (!strcmp(sub, "state"))  cmd_state();
    else if (!strcmp(sub, "evt")) {
        if (argc < 3 || cmd_evt(argv[2]) != 0) {
            BK_LOGI(TAG, "usage: aiui evt <listen|think|speak|idle>\n");
        }
    } else {
        BK_LOGI(TAG, "unknown subcommand '%s'\n", sub);
        BK_LOGI(TAG, "usage: aiui chat | vision | back | prev | state | evt <listen|think|speak|idle>\n");
    }
}

static const struct cli_command s_aiui_cmds[] = {
    {"aiui",
     "aiui chat|vision|back|prev|state|evt <listen|think|speak|idle>",
     cli_aiui_fn},
};

void ai_debug_cli_init(void)
{
    static bool inited;
    if (inited) {
        return;
    }
    inited = true;

    int rc = cli_register_commands(s_aiui_cmds,
                                   sizeof(s_aiui_cmds) / sizeof(s_aiui_cmds[0]));
    BK_LOGI(TAG, "aiui CLI %s\n", rc == 0 ? "registered" : "register failed");
}

#else /* !(CONFIG_CLI && CONFIG_LVGL) */

void ai_debug_cli_init(void)
{
    BK_LOGI(TAG, "aiui CLI skipped (CLI or LVGL disabled)\n");
}

#endif /* CONFIG_CLI && CONFIG_LVGL */
