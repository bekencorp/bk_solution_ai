/**
 * @file muse_status.c
 * @brief Muse status / pairing page implementation.
 *
 * Dynamic screen (same lifetime model as ui_list_menu): created on
 * muse_status_enter(), torn down eagerly on back so the 500 ms refresh
 * timer never outlives the screen.
 *
 * Layout (385x320 logical canvas):
 *   - title "Muse 助手"
 *   - status card: state / pairing / network+cloud / node id
 *   - context hint line (what the user should do next)
 *   - danger button "清除配对" -> muse_service_clear_pairing()
 */
#include "ui/muse_status.h"

#if CONFIG_MUSE_GADGET

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <components/log.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "page_hooks.h"       /* bk_page_attach_right_swipe_gesture */
#include "ui_theme.h"
#include "ui_i18n.h"
#include "ui_nav_router.h"
#include "muse_service.h"
#include "muse_display.h"     /* muse_cjk_font: full-CJK text rendering */

#define TAG "muse_status"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

extern int page_top_enter(void);

static lv_obj_t  *s_screen;
static lv_obj_t  *s_state_value;
static lv_obj_t  *s_pair_value;
static lv_obj_t  *s_net_value;
static lv_obj_t  *s_node_value;
static lv_obj_t  *s_hint_label;
static lv_timer_t *s_refresh_timer;

static const char *const s_title_txt[UI_LANG_COUNT]  = { "Muse 助手", "Muse" };
static const char *const s_lbl_state[UI_LANG_COUNT]  = { "状态", "State" };
static const char *const s_lbl_pair[UI_LANG_COUNT]   = { "配对", "Pairing" };
static const char *const s_lbl_net[UI_LANG_COUNT]    = { "网络/云端", "Net/Cloud" };
static const char *const s_lbl_node[UI_LANG_COUNT]   = { "设备", "Node" };
static const char *const s_paired_yes[UI_LANG_COUNT] = { "已配对", "Paired" };
static const char *const s_paired_no[UI_LANG_COUNT]  = { "未配对", "Not paired" };
static const char *const s_ready_yes[UI_LANG_COUNT]  = { "正常", "Ready" };
static const char *const s_ready_no[UI_LANG_COUNT]   = { "断开", "Down" };
static const char *const s_clear_txt[UI_LANG_COUNT]  = { "清除配对", "Clear pairing" };

static const char *const s_state_txt[UI_LANG_COUNT][8] = {
    { "未启动", "等待配对", "按 S4 确认配对", "等待网络", "连接中", "已连接", "出错了", "未知" },
    { "Stopped", "Waiting pairing", "Press S4 to confirm", "Waiting network",
      "Connecting", "Connected", "Error", "Unknown" },
};

static const char *const s_hint_txt[UI_LANG_COUNT][8] = {
    { "服务未启动", "打开 Muse App 搜索本设备配对", "在 App 确认后按 S4 完成配对",
      "等待 WiFi 连接…", "正在连接云端…", "长按 S4 说话; AI相机页 S5 拍照",
      "请清除配对后重试", "" },
    { "Service stopped", "Open the Muse App and pair this device",
      "Confirm in the App, then press S4", "Waiting for WiFi...",
      "Connecting...", "Hold S4 to talk; S5 shoots in AI Camera",
      "Clear pairing and retry", "" },
};

static inline ui_lang_t ms_lang(void)
{
    return ui_i18n_get_lang();
}

static int state_idx(muse_service_state_t state)
{
    switch (state) {
    case MUSE_SERVICE_STOPPED:                 return 0;
    case MUSE_SERVICE_WAITING_FOR_PAIRING:     return 1;
    case MUSE_SERVICE_WAITING_FOR_CONFIRMATION: return 2;
    case MUSE_SERVICE_WAITING_FOR_NETWORK:     return 3;
    case MUSE_SERVICE_CONNECTING:              return 4;
    case MUSE_SERVICE_CONNECTED:               return 5;
    case MUSE_SERVICE_ERROR:                   return 6;
    default:                                   return 7;
    }
}

static void muse_status_refresh(void)
{
    if (s_screen == NULL || !lv_obj_is_valid(s_screen)) {
        return;
    }
    muse_service_status_t st;
    muse_service_get_status(&st);
    ui_lang_t lang = ms_lang();
    int idx = state_idx(st.state);

    lv_label_set_text(s_state_value, s_state_txt[lang][idx]);
    lv_label_set_text(s_pair_value, st.paired ? s_paired_yes[lang] : s_paired_no[lang]);

    char net_buf[40];
    snprintf(net_buf, sizeof(net_buf), "%s / %s",
             st.network_ready ? s_ready_yes[lang] : s_ready_no[lang],
             st.noise_connected ? s_ready_yes[lang] : s_ready_no[lang]);
    lv_label_set_text(s_net_value, net_buf);

    const char *node = muse_service_node_id();
    lv_label_set_text(s_node_value, (node != NULL && node[0] != '\0') ? node : "-");

    lv_label_set_text(s_hint_label, s_hint_txt[lang][idx]);
}

static void muse_status_refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    muse_status_refresh();
}

static void muse_status_teardown(void)
{
    lv_obj_t *scr = s_screen;
    s_screen = NULL;
    s_state_value = NULL;
    s_pair_value = NULL;
    s_net_value = NULL;
    s_node_value = NULL;
    s_hint_label = NULL;
    if (s_refresh_timer != NULL) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    if (scr != NULL && lv_obj_is_valid(scr)) {
        ui_nav_unregister_screen(scr);
    }
    (void)page_top_enter();  /* switch active screen first */
    if (scr != NULL && lv_obj_is_valid(scr)) {
        lv_obj_del(scr);     /* safe: no longer the active screen */
    }
}

static void muse_status_clear_pairing(void)
{
    LOGI("clear pairing requested from status page\r\n");
    int rc = muse_service_clear_pairing();
    if (rc != MUSE_SERVICE_OK) {
        LOGW("muse_service_clear_pairing -> %d\r\n", rc);
    }
    /* The service worker drops the link and re-enters BLE pairing; the
     * refresh timer picks the new state up within 500 ms. */
    muse_status_refresh();
}

static lv_obj_t *create_status_row(lv_obj_t *parent, const char *label, int y,
                                   lv_obj_t **value_out)
{
    (void)ui_theme_create_text(parent, label, 14, y, 110,
                               muse_cjk_font(), UI_THEME_COLOR_MUTED,
                               LV_TEXT_ALIGN_LEFT);
    lv_obj_t *value = ui_theme_create_text(parent, "", 120, y, 210,
                                           muse_cjk_font(), UI_THEME_COLOR_INK,
                                           LV_TEXT_ALIGN_RIGHT);
    *value_out = value;
    return value;
}

static void clear_btn_release_cb(lv_event_t *e)
{
    (void)e;
    muse_status_clear_pairing();
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    muse_status_teardown();
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    muse_status_clear_pairing();
}

static const ui_page_nav_ops_t s_muse_status_nav_ops = {
    .on_focus_prev = NULL,
    .on_focus_next = NULL,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
    .on_confirm_long = NULL,
};

int muse_status_enter(void)
{
    if (s_screen != NULL && lv_obj_is_valid(s_screen)) {
        lv_screen_load(s_screen);
        return 0;
    }

    ui_lang_t lang = ms_lang();
    lv_obj_t *screen = lv_obj_create(NULL);
    ui_theme_apply_screen(screen);

    (void)ui_theme_create_title(screen, s_title_txt[lang]);
    /* Subtitle rendered with the full-CJK font (ali_16 subset lacks 与). */
    (void)ui_theme_create_text(screen,
                               lang == UI_LANG_ZH ? "配对与连接状态" : "Pairing & link",
                               UI_THEME_SAFE_LEFT, 52, 333,
                               muse_cjk_font(), UI_THEME_COLOR_DESC,
                               LV_TEXT_ALIGN_LEFT);

    lv_obj_t *card = ui_theme_create_card(screen, 19, 76, 347, 132, 22, true);
    create_status_row(card, s_lbl_state[lang], 14, &s_state_value);
    create_status_row(card, s_lbl_pair[lang], 44, &s_pair_value);
    create_status_row(card, s_lbl_net[lang], 74, &s_net_value);
    create_status_row(card, s_lbl_node[lang], 104, &s_node_value);

    s_hint_label = ui_theme_create_text(screen, "", 26, 218, 333,
                                        muse_cjk_font(), UI_THEME_COLOR_DESC,
                                        LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *btn = ui_theme_create_action_button(screen, s_clear_txt[lang],
                                                  112, 258, 160, 38,
                                                  UI_THEME_BUTTON_DANGER);
    ui_theme_set_button_focus(btn, UI_THEME_BUTTON_DANGER, true);
    lv_obj_add_event_cb(btn, clear_btn_release_cb, LV_EVENT_RELEASED, NULL);

    s_screen = screen;
    bk_page_attach_right_swipe_gesture(screen);
    lv_screen_load(screen);
    (void)ui_nav_register_screen(screen, &s_muse_status_nav_ops);

    s_refresh_timer = lv_timer_create(muse_status_refresh_timer_cb, 500, NULL);
    muse_status_refresh();
    LOGI("muse status page entered\r\n");
    return 0;
}

#else  /* !CONFIG_MUSE_GADGET */

int muse_status_enter(void) { return 0; }

#endif /* CONFIG_MUSE_GADGET */
