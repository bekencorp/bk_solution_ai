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
#define ACTION_DEBOUNCE_MS     120U

#define SPECTRUM_BIN_COUNT     16
#define SPECTRUM_X_START       20
#define SPECTRUM_BIN_W         14
#define SPECTRUM_BIN_PITCH     20
#define SPECTRUM_BASE_Y        170
#define SPECTRUM_TOP_LIMIT     40
#define SPECTRUM_MIN_H         8
#define SPECTRUM_IDLE_H        24

#define PROGRESS_X             30
#define PROGRESS_Y             205
#define PROGRESS_W             300
#define PROGRESS_H             8
#define TIME_LABEL_X           120
#define TIME_LABEL_Y           220

#define TIMER_PERIOD_MS        40

static int s_menu_idx;
static uint32_t s_last_action_tick_ms;
static uint32_t s_anim_tick;

static lv_obj_t *s_spectrum_bars[SPECTRUM_BIN_COUNT];
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_time_label;
static lv_timer_t *s_timer;
static uint8_t s_spectrum_smooth[SPECTRUM_BIN_COUNT];

static const uint8_t s_spectrum_eq[SPECTRUM_BIN_COUNT] = {
    150, 145, 138, 132, 126, 120, 114, 108,
    104, 100,  96,  92,  88,  84,  80,  76,
};

static const uint32_t s_spectrum_colors[SPECTRUM_BIN_COUNT] = {
    0x3aa0ff, 0x459dff, 0x5098ff, 0x5a93ff,
    0x648dff, 0x6f86ff, 0x7a7fff, 0x8478ff,
    0x8d70ff, 0x9668ff, 0x9f60ff, 0xa758ff,
    0xaf4fff, 0xb746ff, 0xbe3dff, 0xc533ff,
};

static lv_obj_t *menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_9_button_play;  /* PREV */
    case 1: return ui->page_9_button_stop;  /* PLAY / STOP */
    case 2: return ui->page_9_button_next;  /* NEXT */
    default: return NULL;
    }
}

static bool can_accept_action(void)
{
    if (lv_tick_elaps(s_last_action_tick_ms) < ACTION_DEBOUNCE_MS) {
        return false;
    }
    s_last_action_tick_ms = lv_tick_get();
    return true;
}

static void style_btn_normal(lv_obj_t *btn)
{
    if (btn == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x21283f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x2a3458), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x3a4f86), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xeaf4ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void style_btn_focus(lv_obj_t *btn, bool focus)
{
    if (btn == NULL) {
        return;
    }
    if (!focus) {
        style_btn_normal(btn);
        return;
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x5a63f7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x00b8ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x86ffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x55d7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void apply_menu_focus(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < MUSIC_MENU_COUNT; i++) {
        style_btn_focus(menu_btn(ui, i), i == s_menu_idx);
    }
}

static void refresh_text(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    if (ui->page_9_label_track) {
        lv_label_set_text_fmt(ui->page_9_label_track, "%s", music_get_track_name());
    }
    if (ui->page_9_button_stop_label) {
        lv_label_set_text(ui->page_9_button_stop_label,
                          music_is_playing() ? "STOP" : "PLAY");
    }
}

static void build_spectrum(lv_obj_t *page)
{
    for (int i = 0; i < SPECTRUM_BIN_COUNT; i++) {
        lv_obj_t *bar = lv_obj_create(page);
        lv_obj_remove_style_all(bar);
        int h = SPECTRUM_IDLE_H;
        lv_obj_set_size(bar, SPECTRUM_BIN_W, h);
        lv_obj_set_pos(bar, SPECTRUM_X_START + i * SPECTRUM_BIN_PITCH,
                       SPECTRUM_BASE_Y - h);
        lv_obj_set_style_bg_color(bar, lv_color_hex(s_spectrum_colors[i]),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(bar, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
        s_spectrum_bars[i] = bar;
        s_spectrum_smooth[i] = 6;
    }
}

static void refresh_spectrum_idle(void)
{
    for (int i = 0; i < SPECTRUM_BIN_COUNT; i++) {
        if (!s_spectrum_bars[i]) {
            continue;
        }
        s_spectrum_smooth[i] = 6;
        lv_obj_set_height(s_spectrum_bars[i], SPECTRUM_IDLE_H);
        lv_obj_set_y(s_spectrum_bars[i], SPECTRUM_BASE_Y - SPECTRUM_IDLE_H);
    }
}

static void build_progress(lv_obj_t *page)
{
    s_progress_bar = lv_bar_create(page);
    lv_obj_set_size(s_progress_bar, PROGRESS_W, PROGRESS_H);
    lv_obj_set_pos(s_progress_bar, PROGRESS_X, PROGRESS_Y);
    lv_bar_set_range(s_progress_bar, 0, 1000);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x1f263a),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x66ccff),
                              LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(s_progress_bar, lv_color_hex(0x6e72ff),
                                   LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(s_progress_bar, LV_GRAD_DIR_HOR,
                                 LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_progress_bar, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_progress_bar, 5, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    s_time_label = lv_label_create(page);
    lv_label_set_text(s_time_label, "00:00 / 00:00");
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0x8ea4d7),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_time_label, &lv_font_ali_16,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_time_label, TIME_LABEL_X, TIME_LABEL_Y);
}

static void refresh_progress(void)
{
    uint32_t elapsed = music_get_elapsed_sec();
    uint32_t total = music_get_total_sec();

    if (s_progress_bar) {
        int32_t val = (total > 0) ? (int32_t)((uint64_t)elapsed * 1000u / total) : 0;
        if (val > 1000) val = 1000;
        if (val < 0) val = 0;
        lv_bar_set_value(s_progress_bar, val, LV_ANIM_OFF);
    }
    if (s_time_label) {
        if (total > 0) {
            lv_label_set_text_fmt(s_time_label, "%02lu:%02lu / %02lu:%02lu",
                                  (unsigned long)(elapsed / 60u),
                                  (unsigned long)(elapsed % 60u),
                                  (unsigned long)(total / 60u),
                                  (unsigned long)(total % 60u));
        } else {
            lv_label_set_text(s_time_label, "00:00 / 00:00");
        }
    }
}

static void timer_tick(lv_timer_t *t)
{
    (void)t;
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    if (ui->page_9 == NULL || !lv_obj_is_valid(ui->page_9)) {
        return;
    }

    uint8_t bins[SPECTRUM_BIN_COUNT] = {0};
    bool playing = music_is_playing();
    if (playing) {
        music_get_spectrum_bins(bins, SPECTRUM_BIN_COUNT);
    } else {
        refresh_spectrum_idle();
        return;
    }

    s_anim_tick++;
    if (playing && (s_anim_tick % 12U) == 0U) {
        refresh_progress();
        refresh_text(ui);
    }

    for (int i = 0; i < SPECTRUM_BIN_COUNT; i++) {
        if (!s_spectrum_bars[i]) {
            continue;
        }
        uint32_t target = bins[i];
        target = (target * s_spectrum_eq[i]) / 100u;
        if (target > 100u) {
            target = 100u;
        }
        if (target < 40u) {
            target = (target * 18u) / 10u;
        } else {
            target = 40u + ((target - 40u) * 14u) / 10u;
        }
        if (target > 100u) {
            target = 100u;
        }
        if (!playing && target < 6u) {
            target = 6u;
        }

        uint32_t smooth = s_spectrum_smooth[i];
        if (target >= smooth) {
            smooth = (smooth + target * 4u) / 5u;
        } else {
            smooth = (smooth * 6u + target) / 7u;
        }
        s_spectrum_smooth[i] = (uint8_t)smooth;

        lv_coord_t span = SPECTRUM_BASE_Y - SPECTRUM_TOP_LIMIT;
        lv_coord_t h = SPECTRUM_MIN_H + (lv_coord_t)((smooth * (uint32_t)span) / 110u);
        if (h > span) {
            h = span;
        }
        lv_obj_set_height(s_spectrum_bars[i], h);
        lv_obj_set_y(s_spectrum_bars[i], SPECTRUM_BASE_Y - h);
    }
}

static void handle_action(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return;
    }
    switch (idx) {
    case 0:
        (void)music_prev();
        break;
    case 1:
        if (music_is_playing()) {
            (void)music_pause();
        } else {
            (void)music_play();
        }
        break;
    case 2:
        (void)music_next();
        break;
    default:
        break;
    }
    refresh_text(ui);
    refresh_progress();
    if (!music_is_playing()) {
        refresh_spectrum_idle();
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
    if (ui == NULL || !can_accept_action()) {
        return;
    }
    LOGI("page9 short press idx=%d\r\n", s_menu_idx);
    handle_action(ui, s_menu_idx);
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
    if (idx < 0 || idx >= MUSIC_MENU_COUNT || !can_accept_action()) {
        return;
    }
    s_menu_idx = idx;
    apply_menu_focus(&bk_lv_tool_ui);
    handle_action(&bk_lv_tool_ui, idx);
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
        lv_obj_add_event_cb(b, button_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void page_music_on_init(bk_lv_ui_t *ui)
{
    LOGI("page9 hook init: 91554 spectrum layout\r\n");

    s_menu_idx = 1;
    s_last_action_tick_ms = 0;
    s_anim_tick = 0;

    apply_menu_focus(ui);
    refresh_text(ui);
    register_button_clicks(ui);

    build_spectrum(ui->page_9);
    build_progress(ui->page_9);
    refresh_progress();

    if (!s_timer) {
        s_timer = lv_timer_create(timer_tick, TIMER_PERIOD_MS, NULL);
    }

    (void)ui_nav_register_screen(ui->page_9, &page_9_nav_ops);
}

static void page_music_on_destroy(bk_lv_ui_t *ui)
{
    if (s_timer) {
        lv_timer_del(s_timer);
        s_timer = NULL;
    }
    for (int i = 0; i < SPECTRUM_BIN_COUNT; i++) {
        s_spectrum_bars[i] = NULL;
        s_spectrum_smooth[i] = 0;
    }
    s_progress_bar = NULL;
    s_time_label = NULL;

    if (ui != NULL && ui->page_9 != NULL) {
        ui_nav_unregister_screen(ui->page_9);
    }
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
