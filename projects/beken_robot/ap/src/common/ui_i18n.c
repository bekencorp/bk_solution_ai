/**
 * @file ui_i18n.c
 * @brief Bilingual string table + language persistence/runtime switching.
 */
#include "ui_i18n.h"

#include <stdint.h>
#include <stddef.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "bk_factory_config.h"

#define UI_LOCALE_KEY "ui_locale"

static ui_lang_t s_lang = UI_LANG_ZH;

static const char *const s_tr[STR_ID_COUNT][UI_LANG_COUNT] = {
    /*                           [ZH]                  [EN]                  */
    [STR_HOME_TITLE]       = { "机器人控制台",          "Robot Console" },
    [STR_HOME_HINT]        = { "请选择功能模块",          "Select a module" },

    [STR_SPLASH_TITLE]     = { "BK7259机器人方案",       "BK7259 Robot" },
    [STR_SPLASH_FEATURES]  = { "AI · 视觉 · 图传 · 设备设置",
                               "AI · Vision · Video · Settings" },
    [STR_SPLASH_HINT]      = { "轻触屏幕进入系统",        "Tap screen to start" },

    [STR_PROV_TITLE]       = { "连接设置",              "Connection" },
    [STR_PROV_SUBTITLE]    = { "BLE配网 · WiFi连接",     "BLE Setup · WiFi" },
    [STR_PROV_DEVICE_NAME] = { "当前设备名",            "Device Name" },
    [STR_PROV_BTN_START]   = { "开始配网",              "Start Setup" },
    [STR_PROV_BTN_DELETE]  = { "删除配网",              "Delete" },

    [STR_ASR_LISTENING]    = { "聆听中...",             "Listening..." },
    [STR_ASR_HINT]         = { "你可以说 前进 后退 左转弯 右转弯",
                               "Say Forward Back Left Right" },
    [STR_ASR_RECOGNIZED]   = { "已识别",                "Recognized" },
    [STR_ASR_HELLO]        = { "你好博通",              "Hello Beken" },
    [STR_ASR_BYE]          = { "再见博通",              "Bye Beken" },

    [STR_VOLUME_TITLE]     = { "音量",                  "Volume" },

    [STR_DEMO_CENTER_TITLE]= { "Demo中心",              "Demo Center" },

    [STR_SETTINGS_TITLE]   = { "设备设置",              "Settings" },
    [STR_SETTINGS_SUBTITLE]= { "音量 · U盘 · 恢复出厂",  "Volume · USB · Reset" },
    [STR_SETTINGS_LANG]    = { "语言",                  "Language" },

    [STR_LANG_NATIVE_ZH]   = { "中文",                  "中文" },
    [STR_LANG_NATIVE_EN]   = { "English",               "English" },
};

const char *ui_tr(ui_str_id_t id)
{
    if (id < 0 || id >= STR_ID_COUNT) {
        return "";
    }
    const char *s = s_tr[id][s_lang];
    return s != NULL ? s : "";
}

ui_lang_t ui_i18n_get_lang(void)
{
    return s_lang;
}

void ui_i18n_init(void)
{
#if CONFIG_BK_FACTORY_CONFIG
    uint32_t v = UI_LANG_ZH;
    if (bk_config_read(UI_LOCALE_KEY, (void *)&v, sizeof(v)) == (int)sizeof(v)) {
        if (v < UI_LANG_COUNT) {
            s_lang = (ui_lang_t)v;
        }
    }
#endif
}

/* Top-level generated screens that cache their objects (auto_del=false) and
 * carry translated text. Destroying the non-active ones forces a rebuild in
 * the new language on the next navigate_to_screen() call (which re-inits when
 * the slot is no longer a valid object). Dynamic menus (settings / demo
 * center) rebuild via their on_back/enter handlers, so they need no entry here.
 *
 * IMPORTANT: pages must be torn down through their generated destroy_page_*()
 * helper, NOT a raw lv_obj_del(). The generated helper fires bk_page_fire_
 * destroy() first (which deletes per-page lv_timers such as provisioning's
 * status poll / ASR's animation timer and unregisters the nav-router entry)
 * and then NULLs every cached child-object pointer in bk_lv_tool_ui. A bare
 * lv_obj_del() leaves those timers running and the child pointers dangling, so
 * the next timer tick dereferences freed objects -> CPU MemFault. */
static void invalidate_cached_pages(void)
{
    lv_obj_t *active = lv_screen_active();
    const struct {
        lv_obj_t **slot;
        void (*destroy)(bk_lv_ui_t *);
    } pages[] = {
        { &bk_lv_tool_ui.page_1,  destroy_page_page_1  },  /* splash / welcome */
        { &bk_lv_tool_ui.page_2,  destroy_page_page_2  },  /* home */
        { &bk_lv_tool_ui.page_4,  destroy_page_page_4  },  /* provisioning */
        { &bk_lv_tool_ui.page_8,  destroy_page_page_8  },  /* asr */
        { &bk_lv_tool_ui.page_10, destroy_page_page_10 },  /* volume */
    };
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); i++) {
        lv_obj_t *obj = *pages[i].slot;
        if (obj != NULL && lv_obj_is_valid(obj) && obj != active) {
            pages[i].destroy(&bk_lv_tool_ui);
        }
    }
}

void ui_i18n_set_lang(ui_lang_t lang)
{
    if (lang >= UI_LANG_COUNT || lang == s_lang) {
        return;
    }
    s_lang = lang;

#if CONFIG_BK_FACTORY_CONFIG
    uint32_t v = (uint32_t)lang;
    if (bk_config_write(UI_LOCALE_KEY, (void *)&v, sizeof(v)) == 0) {
        (void)bk_config_sync_flash_safely();
    }
#endif

    invalidate_cached_pages();
}
