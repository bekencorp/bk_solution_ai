/**
 * @file page_top_hooks.c
 * @brief page_2 home / top-level menu.
 *
 * The page is rendered as a visual home card:
 *   - Connection settings -> provisioning page (page_4)
 *   - Demo center         -> page_3 category list
 *   - Device settings     -> volume / U-disk / factory reset list
 *
 * The WiFi status icon visibility is driven separately by
 * common/wifi_status_ui.c via its own page_2 init hook.
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

#ifdef ROBOT_TEST

#include "demo/demo_catalog.h"
#include "ui_nav_router.h"

/* Entry points implemented in sibling page hook files. */
extern int page_provisioning_enter(void);
extern int page_splash_enter(void);

enum {
    HOME_ITEM_PROVISIONING = 0,
    HOME_ITEM_DEMO_CENTER,
    HOME_ITEM_DEVICE_SETTINGS,
    HOME_ITEM_COUNT,
};

static const char *const s_home_items[HOME_ITEM_COUNT] = {
    "连接设置",
    "Demo中心",
    "设备设置",
};

static const char *const s_home_tags[HOME_ITEM_COUNT] = {
    "WiFi/BLE",
    "AI/娱乐",
    "音量/系统",
};

static const ui_theme_icon_kind_t s_home_icons[HOME_ITEM_COUNT] = {
    UI_THEME_ICON_CONNECT,
    UI_THEME_ICON_DEMO,
    UI_THEME_ICON_SETTINGS,
};

static lv_obj_t *s_home_rows[HOME_ITEM_COUNT];
static int s_home_focus;
static ui_touch_tap_state_t s_home_tap_state;

static void home_on_select(int index, void *user_data)
{
    (void)user_data;
    switch (index) {
    case HOME_ITEM_PROVISIONING:    (void)page_provisioning_enter(); break;
    case HOME_ITEM_DEMO_CENTER:     (void)demo_center_enter();       break;
    case HOME_ITEM_DEVICE_SETTINGS: (void)device_settings_enter();   break;
    default: break;
    }
}

static int home_on_back(void)
{
    return page_splash_enter();
}

static void apply_home_focus(void)
{
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        lv_obj_t *row = s_home_rows[i];
        if (row == NULL || !lv_obj_is_valid(row)) {
            continue;
        }
        bool focused = (i == s_home_focus);
        ui_theme_set_row_focus(row, s_home_items[i], s_home_tags[i],
                               s_home_icons[i], focused);
    }
}

static lv_obj_t *create_home_row(lv_obj_t *parent, int index, int y)
{
    lv_obj_t *row = ui_theme_create_card(parent, 18, y, 311, 48, 18, false);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    ui_theme_set_row_focus(row, s_home_items[index], s_home_tags[index],
                           s_home_icons[index], false);
    return row;
}

static void home_row_press_cb(lv_event_t *e)
{
    ui_touch_tap_press(e, &s_home_tap_state);
}

static void home_row_release_cb(lv_event_t *e)
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

static void home_row_press_lost_cb(lv_event_t *e)
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

    (void)ui_theme_create_title(ui->page_2, "机器人控制台");
    ui_touch_tap_reset(&s_home_tap_state);

    lv_obj_t *card = ui_theme_create_card(ui->page_2, 19, 70, 347, 223, 30, true);
    (void)ui_theme_create_text(card, "请选择功能模块", 22, 18, 170,
                               &lv_font_ali_16, UI_THEME_COLOR_MUTED,
                               LV_TEXT_ALIGN_LEFT);

    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        s_home_rows[i] = create_home_row(card, i, 56 + i * 52);
        lv_obj_add_event_cb(s_home_rows[i], home_row_press_cb, LV_EVENT_PRESSED,
                            (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_home_rows[i], home_row_release_cb, LV_EVENT_RELEASED,
                            (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_home_rows[i], home_row_press_lost_cb, LV_EVENT_PRESS_LOST,
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
        s_home_rows[i] = NULL;
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
