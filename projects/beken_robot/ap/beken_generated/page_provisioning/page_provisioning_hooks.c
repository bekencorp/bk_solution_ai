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

#if CONFIG_APP_EVT
#include "lv_vendor.h"
#include "app_event.h"
#endif

#define TAG "page_provisioning"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define PAGE4_MENU_COUNT 2

static int s_menu_idx;
static ui_touch_tap_state_t s_button_tap_state;

/* Latched state text for the non-connected case. Updated by the network
 * app_event callback (provisioning / reconnect progress and failure); the
 * connected case is derived live from the Wi-Fi link when a *_SUCCESS event
 * arrives. */
static const char *s_state_text = "State: WAIT_PROVISIONING";

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
 * Fill the connection/name value line and the State line. ASCII only so it
 * never depends on CJK glyph coverage.
 *
 * The value line is the connection (bearer) line. The device can be
 * provisioned/connected over several bearers (Wi-Fi / BT PAN / 4G LTE); they
 * all render in this same slot with a bearer prefix:
 *   - Wi-Fi connected  -> "Wi-Fi: <ssid>"          (wired today)
 *   - BT PAN connected -> "PAN: <name>"            (reserved, see TODO)
 *   - 4G LTE connected -> "LTE: <operator/apn>"    (reserved, see TODO)
 * When no bearer is up yet, this line stays empty. The BLE device name the
 * phone app scans for is shown on the caption line instead (built once in
 * init_page_page_4() from provisioning_get_ble_name()), not here.
 *
 * This performs the wifi link-status query (an IPC round-trip to the CP core)
 * and touches NO LVGL objects, so it is safe to call without the display lock.
 */
static void compute_status(char *status_line, size_t status_sz,
                           char *hint_line, size_t hint_sz)
{
    char ssid[33];

    if (provisioning_get_ssid(ssid, sizeof(ssid)) == 0) {
        snprintf(status_line, status_sz, "Wi-Fi: %s", ssid);
        snprintf(hint_line, hint_sz, "%s", "State: CONNECTED");
    /*
     * TODO: BT PAN / 4G LTE bearers render in this same slot once their
     * link-status getters are available, e.g.:
     *
     * } else if (provisioning_get_pan_name(pan, sizeof(pan)) == 0) {
     *     snprintf(status_line, status_sz, "PAN: %s", pan);
     *     snprintf(hint_line, hint_sz, "%s", "State: CONNECTED");
     * } else if (provisioning_get_lte_info(lte, sizeof(lte)) == 0) {
     *     snprintf(status_line, status_sz, "LTE: %s", lte);
     *     snprintf(hint_line, hint_sz, "%s", "State: CONNECTED");
     */
    } else {
        status_line[0] = '\0';
        snprintf(hint_line, hint_sz, "%s", s_state_text);
    }
}

/* Write the pre-computed lines onto the page_4 labels. The caller MUST already
 * hold lv_vendor_disp_lock() (both the page init hook and the button-click
 * callback run under it; the app_event path takes it explicitly). */
static void apply_status_locked(bk_lv_ui_t *ui,
                                const char *status_line, const char *hint_line)
{
    if (ui == NULL) {
        return;
    }
    if (ui->page_4_label_status != NULL && lv_obj_is_valid(ui->page_4_label_status)) {
        lv_label_set_text(ui->page_4_label_status, status_line);
    }
    if (ui->page_4_label_hint != NULL && lv_obj_is_valid(ui->page_4_label_hint)) {
        lv_label_set_text(ui->page_4_label_hint, hint_line);
    }
}

/* One-shot refresh used from contexts that already hold the display lock
 * (page init hook, button-click callback). */
static void update_status(bk_lv_ui_t *ui)
{
    char status_line[48];
    char hint_line[32];

    if (ui == NULL) {
        return;
    }
    compute_status(status_line, sizeof(status_line), hint_line, sizeof(hint_line));
    apply_status_locked(ui, status_line, hint_line);
}

#if CONFIG_APP_EVT
/*
 * Event-driven status refresh: replaces the old 1 Hz lv_timer poll (which
 * issued a STA_GET_LINK_STATUS IPC every second and flooded the WDRV log).
 * The smart-config core emits these events across the provisioning / reconnect
 * lifecycle, so the page updates only when the link state actually changes.
 *
 * Runs on the app_event task (NOT the LVGL task), so we query the link status
 * first and then take the display lock only to write the labels.
 */
static void net_status_evt_cb(app_evt_msg_t *msg, void *user_data)
{
    char status_line[48];
    char hint_line[32];

    (void)user_data;
    if (msg == NULL) {
        return;
    }

    switch (msg->event) {
    case APP_EVT_NETWORK_PROVISIONING:
        s_state_text = "State: PROVISIONING";
        break;
    case APP_EVT_RECONNECT_NETWORK:
        s_state_text = "State: RECONNECTING";
        break;
    case APP_EVT_NETWORK_PROVISIONING_FAIL:
    case APP_EVT_RECONNECT_NETWORK_FAIL:
        s_state_text = "State: FAILED";
        break;
    case APP_EVT_NETWORK_PROVISIONING_SUCCESS:
    case APP_EVT_RECONNECT_NETWORK_SUCCESS:
        /* Connected: the SSID branch in compute_status() overrides the hint. */
        break;
    default:
        return;
    }

    compute_status(status_line, sizeof(status_line), hint_line, sizeof(hint_line));

    lv_vendor_disp_lock();
    apply_status_locked(&bk_lv_tool_ui, status_line, hint_line);
    lv_vendor_disp_unlock();
}

static void register_net_status_events(void)
{
    static bool registered;

    if (registered) {
        return;
    }
    (void)app_event_register_handler(APP_EVT_NETWORK_PROVISIONING, net_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_NETWORK_PROVISIONING_SUCCESS, net_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_NETWORK_PROVISIONING_FAIL, net_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_RECONNECT_NETWORK, net_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_RECONNECT_NETWORK_SUCCESS, net_status_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_RECONNECT_NETWORK_FAIL, net_status_evt_cb, NULL);
    registered = true;
}
#endif /* CONFIG_APP_EVT */

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
}

static void page_provisioning_on_destroy(bk_lv_ui_t *ui)
{
    (void)provisioning_stop();
    ui_nav_unregister_screen(ui->page_4);
}

void page_provisioning_init_hooks(void)
{
    (void)bk_page_set_init_hook(4, page_provisioning_on_init);
    (void)bk_page_set_destroy_hook(4, page_provisioning_on_destroy);
#if CONFIG_APP_EVT
    /* Called once at startup, after app_event_init(): subscribe to the network
     * lifecycle events so the page refreshes on change instead of polling. */
    register_net_status_events();
#endif
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
