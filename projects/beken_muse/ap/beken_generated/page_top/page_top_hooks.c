/**
 * @file page_top_hooks.c
 * @brief page_2 home / top-level menu.
 *
 * The home screen is a 2x3 icon grid of the six Muse features:
 *   Voice chat (PTT page) / AI camera (photo recognition) /
 *   History (reply log) / Link status (pairing page) /
 *   Volume / Device settings
 *
 * S5/S2 move the focus, S4 opens the focused tile; tiles are also
 * directly tappable. The WiFi status icon visibility is driven separately
 * by common/wifi_status_ui.c via its own page_2 init hook.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "ui_theme.h"
#include "ui_touch_gesture.h"
#include "ui_i18n.h"

#ifdef ROBOT_TEST

#include "ui/demo_catalog.h"
#include "ui/muse_status.h"
#include "ui/voice_chat_page.h"
#include "ui/voice_history_page.h"
#include "ui/volume.h"
#include "ui_list_menu.h"     /* ui_demo_set_return_menu */
#include "ui_nav_router.h"

/* Grid labels need the full-CJK font (ali_16 subset lacks 记 etc.). */
#if CONFIG_MUSE_GADGET
#include "muse_display.h"
#define HOME_LABEL_FONT   (muse_cjk_font())
#else
#define HOME_LABEL_FONT   (&lv_font_ali_16)
#endif

/* Entry points implemented in sibling page hook files. */
extern int page_splash_enter(void);
int page_top_enter(void);   /* defined at the bottom of this file */

enum {
    HOME_ITEM_VOICE_CHAT = 0,
    HOME_ITEM_AI_CAMERA,
    HOME_ITEM_HISTORY,
    HOME_ITEM_MUSE_STATUS,
    HOME_ITEM_VOLUME,
    HOME_ITEM_SETTINGS,
    HOME_ITEM_COUNT,
};

static const char *const s_home_items[UI_LANG_COUNT][HOME_ITEM_COUNT] = {
    { "语音对话", "拍照识图", "对话记录", "连接状态", "音量", "设备设置" },
    { "Voice",   "Camera", "History", "Status",  "Volume", "Settings" },
};

static const ui_theme_icon_kind_t s_home_icons[HOME_ITEM_COUNT] = {
    UI_THEME_ICON_MUSIC,    /* LV_SYMBOL_AUDIO waveform: voice chat */
    UI_THEME_ICON_CAMERA,
    UI_THEME_ICON_DEMO,     /* LV_SYMBOL_LIST: history */
    UI_THEME_ICON_CONNECT,
    UI_THEME_ICON_VOLUME,
    UI_THEME_ICON_SETTINGS,
};

/* 2 columns x 3 rows inside the 347x223 card. */
#define GRID_CELL_W   160
#define GRID_CELL_H   62
#define GRID_COL_X(c) (8 + (c) * 171)
#define GRID_ROW_Y(r) (6 + (r) * 72)

static lv_obj_t *s_home_cells[HOME_ITEM_COUNT];
static int s_home_focus;
static ui_touch_tap_state_t s_home_tap_state;

static void home_on_select(int index, void *user_data)
{
    (void)user_data;
    switch (index) {
    case HOME_ITEM_VOICE_CHAT:  (void)voice_chat_page_enter();      break;
    case HOME_ITEM_AI_CAMERA:   (void)demo_category_cloud_enter();  break;
    case HOME_ITEM_HISTORY:     (void)voice_history_page_enter();   break;
    case HOME_ITEM_MUSE_STATUS: (void)muse_status_enter();          break;
    case HOME_ITEM_VOLUME:
        /* Back from the volume page returns straight home. */
        ui_demo_set_return_menu(page_top_enter);
        (void)volume_start();
        break;
    case HOME_ITEM_SETTINGS:    (void)device_settings_enter();      break;
    default: break;
    }
}

static int home_on_back(void)
{
    return page_splash_enter();
}

static void cell_set_focus(lv_obj_t *cell, bool focused)
{
    lv_obj_set_style_border_width(cell, focused ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(cell,
        lv_color_hex(focused ? UI_THEME_COLOR_SELECTED_2 : 0xdde9fb),
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(cell,
        lv_color_hex(focused ? UI_THEME_COLOR_CARD_ALT : UI_THEME_COLOR_CARD),
        LV_PART_MAIN);
    lv_obj_set_style_shadow_width(cell, focused ? 10 : 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(cell, lv_color_hex(UI_THEME_COLOR_PRIMARY_2),
                                  LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(cell, LV_OPA_20, LV_PART_MAIN);
}

static void apply_home_focus(void)
{
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        lv_obj_t *cell = s_home_cells[i];
        if (cell == NULL || !lv_obj_is_valid(cell)) {
            continue;
        }
        cell_set_focus(cell, i == s_home_focus);
    }
}

static lv_obj_t *create_home_cell(lv_obj_t *parent, int index)
{
    int col = index % 2;
    int row = index / 2;
    lv_obj_t *cell = ui_theme_create_card(parent, GRID_COL_X(col), GRID_ROW_Y(row),
                                          GRID_CELL_W, GRID_CELL_H, 16, false);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);

    (void)ui_theme_create_icon_badge(cell, s_home_icons[index], 10, 16, false);

    lv_obj_t *label = lv_label_create(cell);
    lv_label_set_text(label, s_home_items[ui_i18n_get_lang()][index]);
    lv_obj_set_style_text_font(label, HOME_LABEL_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_THEME_COLOR_INK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 46, 0);

    cell_set_focus(cell, false);
    return cell;
}

static void home_cell_press_cb(lv_event_t *e)
{
    ui_touch_tap_press(e, &s_home_tap_state);
}

static void home_cell_release_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (idx < 0 || idx >= HOME_ITEM_COUNT) {
        return;
    }

    if (!ui_touch_tap_release(e, &s_home_tap_state,
                              UI_TOUCH_TAP_MOVE_LIMIT_DEFAULT)) {
        return;
    }

    s_home_focus = idx;
    apply_home_focus();
    home_on_select(idx, NULL);
}

static void home_cell_press_lost_cb(lv_event_t *e)
{
    ui_touch_tap_cancel(e, &s_home_tap_state);
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    s_home_focus = (s_home_focus + HOME_ITEM_COUNT - 1) % HOME_ITEM_COUNT;
    apply_home_focus();
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    (void)ui;
    s_home_focus = (s_home_focus + 1) % HOME_ITEM_COUNT;
    apply_home_focus();
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    (void)home_on_back();
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    home_on_select(s_home_focus, NULL);
}

static const ui_page_nav_ops_t s_home_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
    .on_confirm_long = NULL,
};

static void page_top_on_init(bk_lv_ui_t *ui)
{
    if (ui == NULL || ui->page_2 == NULL) {
        return;
    }

    (void)ui_theme_create_title(ui->page_2, ui_tr(STR_HOME_TITLE));
    ui_touch_tap_reset(&s_home_tap_state);

    lv_obj_t *card = ui_theme_create_card(ui->page_2, 19, 70, 347, 223, 30, true);

    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        s_home_cells[i] = create_home_cell(card, i);
        lv_obj_add_event_cb(s_home_cells[i], home_cell_press_cb, LV_EVENT_PRESSED,
                            (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_home_cells[i], home_cell_release_cb, LV_EVENT_RELEASED,
                            (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_home_cells[i], home_cell_press_lost_cb, LV_EVENT_PRESS_LOST,
                            (void *)(intptr_t)i);
    }
    s_home_focus = 0;
    apply_home_focus();
    (void)ui_nav_register_screen(ui->page_2, &s_home_nav_ops);
}

static void page_top_on_destroy(bk_lv_ui_t *ui)
{
    if (ui != NULL && ui->page_2 != NULL) {
        ui_nav_unregister_screen(ui->page_2);
    }
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        s_home_cells[i] = NULL;
    }
}

void page_top_init_hooks(void)
{
    (void)bk_page_set_init_hook(2, page_top_on_init);
    (void)bk_page_set_destroy_hook(2, page_top_on_destroy);
}

int page_top_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_2,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_2);
    return 0;
}

#else  /* !ROBOT_TEST */

void page_top_init_hooks(void) {}
int  page_top_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
