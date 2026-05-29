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

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "demo/provisioning.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include <components/log.h>

#define TAG "page_provisioning"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define PAGE4_MENU_COUNT 3

static int s_menu_idx;

static lv_obj_t *menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_4_button_1;
    case 1: return ui->page_4_button_2;
    case 2: return ui->page_4_button_3;
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
        uint32_t color = (i == s_menu_idx) ? 0xc0c0c0 : 0x2d75b9;
        lv_obj_set_style_bg_color(b, lv_color_hex(color),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
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
        LOGI("Start provisioning (short press S4 to trigger)\r\n");
        provisioning_trigger_smart_config();
        break;
    case 1: LOGI("Delete\r\n"); break;
    case 2: LOGI("Factory reset\r\n"); break;
    default: break;
    }
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

static void button_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= PAGE4_MENU_COUNT) {
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
    for (int i = 0; i < PAGE4_MENU_COUNT; i++) {
        lv_obj_t *b = menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_add_event_cb(b, button_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
}

static void page_provisioning_on_init(bk_lv_ui_t *ui)
{
    s_menu_idx = 0;
    apply_menu_focus(ui);
    register_button_clicks(ui);
    (void)ui_nav_register_screen(ui->page_4, &page_4_nav_ops);
}

static void page_provisioning_on_destroy(bk_lv_ui_t *ui)
{
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
