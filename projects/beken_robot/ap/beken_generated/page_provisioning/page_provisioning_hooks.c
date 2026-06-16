/**
 * @file page_provisioning_hooks.c
 * @brief page_4 provisioning / delete / factory-reset menu UI hooks.
 *
 * All LVGL bits (nav ops, focus management, button click dispatch) live
 * here; the matching src/demo/provisioning.c contains only the backend
 * "trigger smart-config" service call.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "demo/provisioning.h"
#include "wifi_status_ui.h"
#include "ui_theme.h"
#include "ui_touch_gesture.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include <components/log.h>

#define TAG "page_provisioning"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define PAGE4_MENU_COUNT 2

static int s_menu_idx;
static ui_touch_tap_state_t s_button_tap_state;

/* Latched state text for the non-connected case. The connected case is
 * derived live from the Wi-Fi link, so provisioning success (which arrives
 * asynchronously on another task) is reflected without an explicit callback. */
static const char *s_state_text = "State: WAIT_PROVISIONING";
static lv_timer_t *s_status_timer;

static lv_obj_t *menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_4_button_1;
    case 1: return ui->page_4_button_2;
    default: return NULL;
    }
}

static void apply_menu_focus(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE4_MENU_COUNT; i++) {
        lv_obj_t *b = menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_remove_state(b, LV_STATE_DISABLED);
        ui_theme_set_button_focus(b,
                                  i == 0 ? UI_THEME_BUTTON_PRIMARY : UI_THEME_BUTTON_DANGER,
                                  i == s_menu_idx);
    }
}

/*
 * Refresh both the SSID and State lines from the live Wi-Fi link status.
 * ASCII only so it never depends on CJK glyph coverage. When the STA is
 * connected we show the SSID and "CONNECTED"; otherwise we fall back to the
 * latched s_state_text (READY / PROVISIONING / DELETED / RESETTING).
 */
static void update_status(bk_lv_ui_t *ui)
{
    char ssid[33];
    char ble_name[32];
    char line[48];

    if (ui == NULL) {
        return;
    }
    if (provisioning_get_ssid(ssid, sizeof(ssid)) == 0) {
        if (ui->page_4_label_status != NULL) {
            snprintf(line, sizeof(line), "Wi-Fi: %s", ssid);
            lv_label_set_text(ui->page_4_label_status, line);
        }
        if (ui->page_4_label_hint != NULL) {
            lv_label_set_text(ui->page_4_label_hint, "State: CONNECTED");
        }
    } else if (provisioning_get_ble_name(ble_name, sizeof(ble_name)) == 0) {
        if (ui->page_4_label_status != NULL) {
            snprintf(line, sizeof(line), "BLE: %s", ble_name);
            lv_label_set_text(ui->page_4_label_status, line);
        }
        if (ui->page_4_label_hint != NULL) {
            lv_label_set_text(ui->page_4_label_hint, s_state_text);
        }
    } else {
        if (ui->page_4_label_status != NULL) {
            lv_label_set_text(ui->page_4_label_status, "Wi-Fi: BK-Robot");
        }
        if (ui->page_4_label_hint != NULL) {
            lv_label_set_text(ui->page_4_label_hint, s_state_text);
        }
    }
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_status(&bk_lv_tool_ui);
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_menu_idx = (s_menu_idx + PAGE4_MENU_COUNT - 1) % PAGE4_MENU_COUNT;
    apply_menu_focus(ui);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_menu_idx = (s_menu_idx + 1) % PAGE4_MENU_COUNT;
    apply_menu_focus(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    /* page_4 is entered from page_2's "Provisioning" button -> back to page_2. */
    navigate_to_screen((lv_obj_t **)&ui->page_2,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_2);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page_provisioning short press idx=%d\r\n", s_menu_idx);
    switch (s_menu_idx) {
    case 0:
        LOGI("Start provisioning\r\n");
        s_state_text = "State: PROVISIONING";
        provisioning_trigger_smart_config();
        break;
    case 1:
        LOGI("Delete provisioning\r\n");
        s_state_text = "State: DELETED";
        provisioning_delete_smart_config();
        /* bk_sconf_erase_smart_config() clears the provisioned flag but
         * emits no app_event, and page_2 is cached across navigation so
         * its init hook will not re-run on return. Hide the wifi icon
         * directly. We are already inside the LVGL event-callback chain
         * (button_click_cb -> on_screen_next) running under lv_task_handler,
         * which already holds lv_vendor_disp_lock(); use the _locked
         * variant to avoid deadlocking the non-recursive disp mutex. */
        wifi_status_ui_set_provisioned_locked(false);
        break;
    default: break;
    }
    update_status(ui);
}

static void on_confirm_long(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page_provisioning long press idx=%d\r\n", s_menu_idx);
    if (s_menu_idx == 0) {
        LOGI("Provisioning is triggered by short press S4\r\n");
    }
}

static const ui_page_nav_ops_t page_4_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
    .on_confirm_long = on_confirm_long,
};

static void button_press_cb(lv_event_t *e)
{
    ui_touch_tap_press(e, &s_button_tap_state);
}

static void button_release_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (idx < 0 || idx >= PAGE4_MENU_COUNT) {
        return;
    }
    if (!ui_touch_tap_release(e, &s_button_tap_state,
                              UI_TOUCH_TAP_MOVE_LIMIT_DEFAULT)) {
        return;
    }
    s_menu_idx = idx;
    apply_menu_focus(&bk_lv_tool_ui);
    on_screen_next(&bk_lv_tool_ui);
}

static void button_press_lost_cb(lv_event_t *e)
{
    ui_touch_tap_cancel(e, &s_button_tap_state);
}

static void register_button_clicks(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE4_MENU_COUNT; i++) {
        lv_obj_t *b = menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_add_event_cb(b, button_press_cb, LV_EVENT_PRESSED,
                            (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, button_release_cb, LV_EVENT_RELEASED,
                            (void *)(intptr_t)i);
        lv_obj_add_event_cb(b, button_press_lost_cb, LV_EVENT_PRESS_LOST,
                            (void *)(intptr_t)i);
    }
}

static void page_provisioning_on_init(bk_lv_ui_t *ui)
{
    s_menu_idx = 0;
    ui_touch_tap_reset(&s_button_tap_state);
    s_state_text = "State: WAIT_PROVISIONING";
    update_status(ui);
    apply_menu_focus(ui);
    register_button_clicks(ui);
    (void)ui_nav_register_screen(ui->page_4, &page_4_nav_ops);

    /* Poll the live Wi-Fi link so asynchronous provisioning success (which is
     * reported on other tasks) is reflected on the page without a callback. */
    if (s_status_timer == NULL) {
        s_status_timer = lv_timer_create(status_timer_cb, 1000, NULL);
    }
}

static void page_provisioning_on_destroy(bk_lv_ui_t *ui)
{
    if (s_status_timer != NULL) {
        lv_timer_del(s_status_timer);
        s_status_timer = NULL;
    }
    (void)provisioning_stop();
    ui_nav_unregister_screen(ui->page_4);
}

void page_provisioning_init_hooks(void)
{
    (void)bk_page_set_init_hook(4, page_provisioning_on_init);
    (void)bk_page_set_destroy_hook(4, page_provisioning_on_destroy);
}

int page_provisioning_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_4,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_4);
    return 0;
}

#else  /* !ROBOT_TEST */

void page_provisioning_init_hooks(void) {}
int  page_provisioning_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
