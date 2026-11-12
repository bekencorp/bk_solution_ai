/**
 * @file voice_history_page.c
 * @brief Conversation history page: lists the voice_history ring (newest
 *        on top) as small cards in a scrollable column. S5/S2 scroll, S4
 *        clears the history, S3 / right-swipe returns home.
 */
#include "ui/voice_history_page.h"

#if CONFIG_MUSE_GADGET

#include <stdbool.h>

#include <components/log.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "page_hooks.h"       /* bk_page_attach_right_swipe_gesture */
#include "ui_theme.h"
#include "ui_i18n.h"
#include "ui_nav_router.h"
#include "muse_display.h"     /* muse_cjk_font: full-CJK text rendering */
#include "voice_history.h"

#define TAG "voice_hist"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

extern int page_top_enter(void);

static lv_obj_t *s_screen;
static lv_obj_t *s_list;

static const char *const s_title_txt[UI_LANG_COUNT] = { "对话记录", "History" };
static const char *const s_empty_txt[UI_LANG_COUNT] = {
    "暂无对话记录", "No conversations yet",
};
static const char *const s_clear_hint[UI_LANG_COUNT] = {
    "按 S4 清空记录", "Press S4 to clear",
};

static void history_page_rebuild(void)
{
    if (s_list == NULL || !lv_obj_is_valid(s_list)) {
        return;
    }
    lv_obj_clean(s_list);

    ui_lang_t lang = ui_i18n_get_lang();
    int n = voice_history_count();
    if (n == 0) {
        lv_obj_t *empty = ui_theme_create_text(s_list, s_empty_txt[lang],
                                               10, 30, 313, muse_cjk_font(),
                                               UI_THEME_COLOR_HINT,
                                               LV_TEXT_ALIGN_CENTER);
        (void)empty;
        return;
    }

    char title[VOICE_HISTORY_TITLE_MAX];
    char body[VOICE_HISTORY_BODY_MAX];
    for (int i = 0; i < n; i++) {
        if (!voice_history_get(i, title, sizeof(title), body, sizeof(body))) {
            break;
        }
        lv_obj_t *card = ui_theme_create_card(s_list, 6, i * 78, 321, 70, 14, false);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        /* Turn kind ("Muse voice" / "Muse photo") as the card header. */
        (void)ui_theme_create_text(card, title, 12, 6, 297,
                                   muse_cjk_font(), UI_THEME_COLOR_PRIMARY,
                                   LV_TEXT_ALIGN_LEFT);
        lv_obj_t *b = ui_theme_create_text(card, body, 12, 28, 297,
                                           muse_cjk_font(), UI_THEME_COLOR_INK,
                                           LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(b, LV_LABEL_LONG_MODE_DOTS);
    }
}

static void history_page_teardown(void)
{
    lv_obj_t *scr = s_screen;
    s_screen = NULL;
    s_list = NULL;
    if (scr != NULL && lv_obj_is_valid(scr)) {
        ui_nav_unregister_screen(scr);
    }
    (void)page_top_enter();  /* switch active screen first */
    if (scr != NULL && lv_obj_is_valid(scr)) {
        lv_obj_del(scr);
    }
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    history_page_teardown();
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    voice_history_clear();
    history_page_rebuild();
    LOGI("voice history cleared\r\n");
}

/* S5/S2 scroll the list by one card height. */
static void on_focus_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    if (s_list != NULL && lv_obj_is_valid(s_list)) {
        lv_obj_scroll_by(s_list, 0, 78, LV_ANIM_ON);
    }
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    (void)ui;
    if (s_list != NULL && lv_obj_is_valid(s_list)) {
        lv_obj_scroll_by(s_list, 0, -78, LV_ANIM_ON);
    }
}

static const ui_page_nav_ops_t s_history_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
    .on_confirm_long = NULL,
};

int voice_history_page_enter(void)
{
    if (s_screen != NULL && lv_obj_is_valid(s_screen)) {
        lv_screen_load(s_screen);
        return 0;
    }

    ui_lang_t lang = ui_i18n_get_lang();
    lv_obj_t *screen = lv_obj_create(NULL);
    ui_theme_apply_screen(screen);

    (void)ui_theme_create_title(screen, s_title_txt[lang]);
    (void)ui_theme_create_text(screen, s_clear_hint[lang],
                               UI_THEME_SAFE_LEFT, 52, 333,
                               muse_cjk_font(), UI_THEME_COLOR_DESC,
                               LV_TEXT_ALIGN_LEFT);

    s_list = lv_obj_create(screen);
    lv_obj_remove_style_all(s_list);
    lv_obj_set_pos(s_list, 19, 76);
    lv_obj_set_size(s_list, 347, 224);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list, LV_SCROLLBAR_MODE_AUTO);

    s_screen = screen;
    bk_page_attach_right_swipe_gesture(screen);
    lv_screen_load(screen);
    (void)ui_nav_register_screen(screen, &s_history_nav_ops);

    history_page_rebuild();
    LOGI("voice history page entered\r\n");
    return 0;
}

#else  /* !CONFIG_MUSE_GADGET */

int voice_history_page_enter(void) { return 0; }

#endif /* CONFIG_MUSE_GADGET */
