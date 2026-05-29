/**
 * @file page_music_hooks.c
 * @brief page_9 music demo UI hooks (3-button menu: play / stop / next).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "demo/music.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include <components/log.h>

#define TAG "page_music"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define MUSIC_MENU_COUNT 3

static int s_menu_idx;

static lv_obj_t *menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_9_button_play;
    case 1: return ui->page_9_button_stop;
    case 2: return ui->page_9_button_next;
    default: return NULL;
    }
}

static void apply_menu_focus(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < MUSIC_MENU_COUNT; i++) {
        lv_obj_t *b = menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_remove_state(b, LV_STATE_DISABLED);
        uint32_t color = (i == s_menu_idx) ? 0xc0c0c0 : 0x2d75b9;
        lv_obj_set_style_bg_color(b, lv_color_hex(color),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void refresh_text(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    if (ui->page_9_label_state) {
        lv_label_set_text_fmt(ui->page_9_label_state, "状态: %s",
                              music_is_playing() ? "播放中" : "已停止");
    }
    if (ui->page_9_label_track) {
        lv_label_set_text_fmt(ui->page_9_label_track, "曲目: %s",
                              music_get_track_name());
    }
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_menu_idx = (s_menu_idx + MUSIC_MENU_COUNT - 1) % MUSIC_MENU_COUNT;
    apply_menu_focus(ui);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_menu_idx = (s_menu_idx + 1) % MUSIC_MENU_COUNT;
    apply_menu_focus(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    (void)music_pause();
    LOGI("page9 back -> page_3\r\n");
    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page9 short press idx=%d\r\n", s_menu_idx);
    switch (s_menu_idx) {
    case 0: (void)music_play();  break;
    case 1: (void)music_pause(); break;
    case 2: (void)music_next();  break;
    default: break;
    }
    refresh_text(ui);
}

static const ui_page_nav_ops_t page_9_nav_ops = {
    .on_focus_prev  = on_focus_prev,
    .on_focus_next  = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

static void button_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= MUSIC_MENU_COUNT) {
        return;
    }
    s_menu_idx = idx;
    apply_menu_focus(&bk_lv_tool_ui);
    on_screen_next(&bk_lv_tool_ui);
}

static void register_button_clicks(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < MUSIC_MENU_COUNT; i++) {
        lv_obj_t *b = menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_add_event_cb(b, button_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
}

static void page_music_on_init(bk_lv_ui_t *ui)
{
    s_menu_idx = 0;
    apply_menu_focus(ui);
    refresh_text(ui);
    register_button_clicks(ui);
    (void)ui_nav_register_screen(ui->page_9, &page_9_nav_ops);
}

static void page_music_on_destroy(bk_lv_ui_t *ui)
{
    ui_nav_unregister_screen(ui->page_9);
}

void page_music_init_hooks(void)
{
    (void)bk_page_set_init_hook(9, page_music_on_init);
    (void)bk_page_set_destroy_hook(9, page_music_on_destroy);
}

int page_music_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_9,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_9);
    return 0;
}

#else  /* !ROBOT_TEST */

void page_music_init_hooks(void) {}
int  page_music_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
