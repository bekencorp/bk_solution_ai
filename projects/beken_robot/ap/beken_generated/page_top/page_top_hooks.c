/**
 * @file page_top_hooks.c
 * @brief page_2 top-level menu: "Provisioning" jumps to page_4,
 *        "Demo mode" jumps to page_3.
 *
 * Pure UI: registers nav ops + button click handlers + focus state.
 * The WiFi status icon visibility is driven separately by
 * common/wifi_status_ui.c via its own page_2 init hook.
 */
#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include <components/log.h>

#define TAG "page_top"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define PAGE2_MENU_COUNT 2

static int s_menu_idx;

static lv_obj_t *menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_2_button_1;   /* provisioning */
    case 1: return ui->page_2_button_3;   /* demo mode */
    default: return NULL;
    }
}

static void apply_menu_focus(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE2_MENU_COUNT; i++) {
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
    s_menu_idx = (s_menu_idx + PAGE2_MENU_COUNT - 1) % PAGE2_MENU_COUNT;
    apply_menu_focus(ui);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    s_menu_idx = (s_menu_idx + 1) % PAGE2_MENU_COUNT;
    apply_menu_focus(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    navigate_to_screen((lv_obj_t **)&ui->page_1,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_1);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page_top enter idx=%d\r\n", s_menu_idx);
    switch (s_menu_idx) {
    case 0:
        LOGI("Provisioning -> page_4\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_4,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_4);
        break;
    case 1:
        LOGI("Demo mode -> page_3\r\n");
        navigate_to_screen((lv_obj_t **)&ui->page_3,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_3);
        break;
    default: break;
    }
}

static const ui_page_nav_ops_t page_2_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

static void button_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= PAGE2_MENU_COUNT) {
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
    for (int i = 0; i < PAGE2_MENU_COUNT; i++) {
        lv_obj_t *b = menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_add_event_cb(b, button_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
}

static void page_top_on_init(bk_lv_ui_t *ui)
{
    /* The Designer export ships page_2_button_3 in the DISABLED state.
     * Our focus model never disables buttons (DISABLED silently eats
     * TP CLICKED), so always pull it back to the enabled side here. */
    if (ui->page_2_button_3 != NULL) {
        lv_obj_remove_state(ui->page_2_button_3, LV_STATE_DISABLED);
    }
    s_menu_idx = 0;
    apply_menu_focus(ui);
    register_button_clicks(ui);
    (void)ui_nav_register_screen(ui->page_2, &page_2_nav_ops);
}

static void page_top_on_destroy(bk_lv_ui_t *ui)
{
    ui_nav_unregister_screen(ui->page_2);
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
