/**
 * @file page_volume_hooks.c
 * @brief page_10 volume demo UI hooks: slider + +/- buttons +
 *        speaker icon wave/slash visuals + physical-key bindings.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "demo/volume.h"
#include "ui_touch_gesture.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "ui_list_menu.h"
#include <components/log.h>

#define TAG "page_volume"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

static bool s_refreshing;
static ui_touch_tap_state_t s_button_tap_state;

static void set_wave_state(lv_obj_t *wave, bool active)
{
    if (wave == NULL) {
        return;
    }
    lv_obj_set_style_bg_color(wave, lv_color_hex(active ? 0x49a9ff : 0x4a4a4a),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(wave, active ? 255 : 90,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void set_speaker_icon_level(bk_lv_ui_t *ui, uint8_t level)
{
    if (ui == NULL) {
        return;
    }
    set_wave_state(ui->page_10_icon_wave_1, level >= 1);
    set_wave_state(ui->page_10_icon_wave_2, level >= 4);
    set_wave_state(ui->page_10_icon_wave_3, level >= 8);
    if (ui->page_10_icon_slash) {
        if (level == 0) {
            lv_obj_clear_flag(ui->page_10_icon_slash, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ui->page_10_icon_slash, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh_volume(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    uint8_t level = volume_get_level();
    uint8_t max   = volume_get_max();

    s_refreshing = true;
    if (ui->page_10_label_value) {
        lv_label_set_text_fmt(ui->page_10_label_value, "%u / %u", level, max);
    }
    if (ui->page_10_slider_volume) {
        lv_slider_set_range(ui->page_10_slider_volume, 0, max);
        lv_slider_set_value(ui->page_10_slider_volume, level, LV_ANIM_OFF);
    }
    s_refreshing = false;

    set_speaker_icon_level(ui, level);

    if (ui->page_10_button_minus) {
        if (level == 0) {
            lv_obj_add_state(ui->page_10_button_minus, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(ui->page_10_button_minus, LV_STATE_DISABLED);
        }
    }
    if (ui->page_10_button_plus) {
        if (level >= max) {
            lv_obj_add_state(ui->page_10_button_plus, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(ui->page_10_button_plus, LV_STATE_DISABLED);
        }
    }
}

static void volume_down(bk_lv_ui_t *ui)
{
    (void)volume_decrease();
    refresh_volume(ui);
}

static void volume_up(bk_lv_ui_t *ui)
{
    (void)volume_increase();
    refresh_volume(ui);
}

static void slider_event_cb(lv_event_t *e)
{
    if (s_refreshing) {
        return;
    }
    bk_lv_ui_t *ui = (bk_lv_ui_t *)lv_event_get_user_data(e);
    if (ui == NULL || ui->page_10_slider_volume == NULL) {
        return;
    }
    (void)volume_set_level((uint8_t)lv_slider_get_value(ui->page_10_slider_volume));
    refresh_volume(ui);
}

static bool tap_release_accepted(lv_event_t *e)
{
    return ui_touch_tap_release(e, &s_button_tap_state,
                                UI_TOUCH_TAP_MOVE_LIMIT_DEFAULT);
}

static void button_press_cb(lv_event_t *e)
{
    ui_touch_tap_press(e, &s_button_tap_state);
}

static void button_press_lost_cb(lv_event_t *e)
{
    ui_touch_tap_cancel(e, &s_button_tap_state);
}

static void minus_release_cb(lv_event_t *e)
{
    if (tap_release_accepted(e)) {
        volume_down((bk_lv_ui_t *)lv_event_get_user_data(e));
    }
}

static void plus_release_cb(lv_event_t *e)
{
    if (tap_release_accepted(e)) {
        volume_up((bk_lv_ui_t *)lv_event_get_user_data(e));
    }
}

static void on_focus_prev(bk_lv_ui_t *ui) { volume_down(ui); }
static void on_focus_next(bk_lv_ui_t *ui) { volume_up(ui); }

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page10 back -> demo menu\r\n");
    (void)ui_demo_return_to_menu();
    destroy_page_page_10(ui);
}

static const ui_page_nav_ops_t page_10_nav_ops = {
    .on_focus_prev  = on_focus_prev,
    .on_focus_next  = on_focus_next,
    .on_screen_prev = on_screen_prev,
};

static void page_volume_on_init(bk_lv_ui_t *ui)
{
    lv_obj_add_event_cb(ui->page_10_slider_volume, slider_event_cb,
                        LV_EVENT_VALUE_CHANGED, ui);
    ui_touch_tap_reset(&s_button_tap_state);
    lv_obj_add_event_cb(ui->page_10_button_minus, button_press_cb,
                        LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(ui->page_10_button_minus, minus_release_cb,
                        LV_EVENT_RELEASED, ui);
    lv_obj_add_event_cb(ui->page_10_button_minus, button_press_lost_cb,
                        LV_EVENT_PRESS_LOST, ui);
    lv_obj_add_event_cb(ui->page_10_button_plus, button_press_cb,
                        LV_EVENT_PRESSED, ui);
    lv_obj_add_event_cb(ui->page_10_button_plus, plus_release_cb,
                        LV_EVENT_RELEASED, ui);
    lv_obj_add_event_cb(ui->page_10_button_plus, button_press_lost_cb,
                        LV_EVENT_PRESS_LOST, ui);
    refresh_volume(ui);
    (void)ui_nav_register_screen(ui->page_10, &page_10_nav_ops);
}

static void page_volume_on_destroy(bk_lv_ui_t *ui)
{
    (void)ui;
    ui_nav_unregister_screen(ui->page_10);
}

void page_volume_init_hooks(void)
{
    (void)bk_page_set_init_hook(10, page_volume_on_init);
    (void)bk_page_set_destroy_hook(10, page_volume_on_destroy);
}

int page_volume_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_10,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_10);
    return 0;
}

#else  /* !ROBOT_TEST */

void page_volume_init_hooks(void) {}
int  page_volume_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
