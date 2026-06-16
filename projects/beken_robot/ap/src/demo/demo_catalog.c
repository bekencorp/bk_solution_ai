/**
 * @file demo_catalog.c
 * @brief Demo-center category / device-settings catalog implementation.
 *
 * Demo center renders the category tabs directly on page_3, while device
 * settings still uses the shared ui_list_menu component. Demos are launched
 * through ui_demo_set_return_menu() so their "back" gesture returns to the
 * active tab.
 */
#include "demo/demo_catalog.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ui_list_menu.h"
#include "ui_nav_router.h"
#include "ui_theme.h"
#include "ui_touch_gesture.h"
#include "event_runtime.h"

#ifdef ROBOT_TEST

#include "demo/ai_chat.h"
#include "demo/vision.h"
#include "demo/asr.h"
#include "demo/music.h"
#include "demo/volume.h"
#include "demo/sound_localization.h"
#include "demo/palm_tracking.h"
#include "demo/yoloface_tracking.h"
#include "demo/hand_gesture.h"
#include "demo/camera_preview_demo.h"
#include "demo/udisk.h"
#include "demo/robot_video.h"
#include "demo/provisioning.h"
#include "camera_preview.h"

/* ------------------------------------------------------------------ */
/* Demo center: top-level categories.                                  */
/* ------------------------------------------------------------------ */
enum {
    DEMO_CAT_EDGE = 0,
    DEMO_CAT_CLOUD,
    DEMO_CAT_FUN,
    DEMO_CAT_COUNT,
};

static const char *const s_category_items[DEMO_CAT_COUNT] = {
    "端侧AI",
    "云端AI",
    "娱乐互动",
};

static const char *const s_category_descs[DEMO_CAT_COUNT] = {
    "本地推理",
    "大模型",
    "影音互动",
};

static int home_enter(void)
{
    /* Home / top-level menu lives on the generated page_2 slot. */
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_2,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_2);
    return 0;
}

#define DEMO_CENTER_MAX_ITEMS 5

static lv_obj_t *s_demo_center_screen;
static lv_obj_t *s_demo_center_panel;
static lv_obj_t *s_demo_center_tabs[DEMO_CAT_COUNT];
static lv_obj_t *s_demo_center_rows[DEMO_CENTER_MAX_ITEMS];
static int s_demo_center_active_cat = DEMO_CAT_EDGE;
static int s_demo_center_focus;
static ui_touch_tap_state_t s_demo_center_tap_state;

static void demo_center_render(void);
static int demo_center_show_category(int category);

/* ------------------------------------------------------------------ */
/* End-side AI sub-menu.                                               */
/* ------------------------------------------------------------------ */
static const char *const s_edge_items[] = {
    "命令词识别",
    "声源定位",
    "手掌跟随",
    "人脸检测",
    "手势识别",
};
static const char *const s_edge_descs[] = {
    "语音控制",
    "方向定位",
    "摄像头跟随",
    "人脸检测",
    "手势识别",
};
static const ui_theme_icon_kind_t s_edge_icons[] = {
    UI_THEME_ICON_ASR,
    UI_THEME_ICON_DOA,
    UI_THEME_ICON_PALM,
    UI_THEME_ICON_FACE,
    UI_THEME_ICON_GESTURE,
};
#define EDGE_ITEM_COUNT ((int)(sizeof(s_edge_items) / sizeof(s_edge_items[0])))

static void edge_on_select(int index, void *user_data)
{
    (void)user_data;
    /* Page-bound demos (ASR / DOA) return through the generic indirection;
     * the overlay demos (palm / face / gesture) navigate back via their own
     * set_return_to_edge_ai() flag, which routes to page_edge_ai_enter(). */
    ui_demo_set_return_menu(demo_category_edge_enter);
    switch (index) {
    case 0:
        (void)asr_start();
        break;
    case 1:
        (void)sound_localization_start();
        break;
    case 2:
        palm_tracking_set_return_to_edge_ai(true);
        (void)palm_tracking_start();
        break;
    case 3:
        yoloface_tracking_set_return_to_edge_ai(true);
        (void)yoloface_tracking_start();
        break;
    case 4:
        hand_gesture_set_return_to_edge_ai(true);
        (void)hand_gesture_start();
        break;
    default:
        break;
    }
}

static const ui_list_menu_config_t s_edge_cfg = {
    .title = "端侧AI",
    .subtitle = "本地神经网络示例",
    .items = s_edge_items,
    .descriptions = s_edge_descs,
    .icons = s_edge_icons,
    .item_count = EDGE_ITEM_COUNT,
    .tabs = s_category_items,
    .tab_count = DEMO_CAT_COUNT,
    .active_tab = DEMO_CAT_EDGE,
    .on_select = edge_on_select,
    .on_back = demo_center_enter,
    .on_nav_intercept = NULL,
    .user_data = NULL,
};

int demo_category_edge_enter(void)
{
    return demo_center_show_category(DEMO_CAT_EDGE);
}

/* ------------------------------------------------------------------ */
/* Cloud AI sub-menu (AI camera is an overlay demo).                   */
/* ------------------------------------------------------------------ */
static const char *const s_cloud_items[] = {
    "AI对话",
    "视觉识别",
    "AI相机",
};
static const char *const s_cloud_descs[] = {
    "大模型对话",
    "预览/识图",
    "拍照预览",
};
static const ui_theme_icon_kind_t s_cloud_icons[] = {
    UI_THEME_ICON_CHAT,
    UI_THEME_ICON_VISION,
    UI_THEME_ICON_CAMERA,
};
#define CLOUD_ITEM_COUNT ((int)(sizeof(s_cloud_items) / sizeof(s_cloud_items[0])))

/*
 * AI camera takes the framebuffer away from LVGL while the sub-menu screen
 * stays active underneath; on exit lv_vendor resumes and shows this menu
 * again. While the preview is active, navigation keys are routed to the
 * preview state machine (take photo / resume live / stop) instead of moving
 * the menu focus, and re-entry is blocked.
 */
static bool cloud_nav_intercept(ui_nav_event_t ev, void *user_data)
{
    (void)user_data;
    if (!camera_preview_is_active()) {
        return false;
    }
    switch (ev) {
    case UI_NAV_EVENT_FOCUS_PREV:
        if (camera_preview_is_running()) {
            (void)camera_preview_take_photo();
        }
        return true;
    case UI_NAV_EVENT_FOCUS_NEXT:
        if (camera_preview_is_frozen()) {
            (void)camera_preview_resume_live();
        }
        return true;
    case UI_NAV_EVENT_SCREEN_PREV:
        if (camera_preview_is_running() || camera_preview_is_frozen()) {
            (void)camera_preview_stop();
        }
        return true;
    default:
        return true;
    }
}

static void cloud_on_select(int index, void *user_data)
{
    (void)user_data;
    ui_demo_set_return_menu(demo_category_cloud_enter);
    switch (index) {
    case 0: (void)ai_chat_start();             break;
    case 1: (void)vision_start();              break;
    case 2: (void)camera_preview_demo_start(); break;
    default: break;
    }
}

static const ui_list_menu_config_t s_cloud_cfg = {
    .title = "云端AI",
    .subtitle = "云端大模型示例",
    .items = s_cloud_items,
    .descriptions = s_cloud_descs,
    .icons = s_cloud_icons,
    .item_count = CLOUD_ITEM_COUNT,
    .tabs = s_category_items,
    .tab_count = DEMO_CAT_COUNT,
    .active_tab = DEMO_CAT_CLOUD,
    .on_select = cloud_on_select,
    .on_back = demo_center_enter,
    .on_nav_intercept = cloud_nav_intercept,
    .user_data = NULL,
};

int demo_category_cloud_enter(void)
{
    return demo_center_show_category(DEMO_CAT_CLOUD);
}

/* ------------------------------------------------------------------ */
/* Entertainment sub-menu.                                             */
/* ------------------------------------------------------------------ */
static const char *const s_fun_items[] = {
    "音乐播放",
    "图传播放",
};
static const char *const s_fun_descs[] = {
    "音乐控制",
    "实时图传",
};
static const ui_theme_icon_kind_t s_fun_icons[] = {
    UI_THEME_ICON_MUSIC,
    UI_THEME_ICON_VIDEO,
};
#define FUN_ITEM_COUNT ((int)(sizeof(s_fun_items) / sizeof(s_fun_items[0])))

static void fun_on_select(int index, void *user_data)
{
    (void)user_data;
    ui_demo_set_return_menu(demo_category_fun_enter);
    switch (index) {
    case 0: (void)music_start();       break;
    case 1: (void)robot_video_start(); break;
    default: break;
    }
}

static const ui_list_menu_config_t s_fun_cfg = {
    .title = "娱乐互动",
    .subtitle = "影音娱乐示例",
    .items = s_fun_items,
    .descriptions = s_fun_descs,
    .icons = s_fun_icons,
    .item_count = FUN_ITEM_COUNT,
    .tabs = s_category_items,
    .tab_count = DEMO_CAT_COUNT,
    .active_tab = DEMO_CAT_FUN,
    .on_select = fun_on_select,
    .on_back = demo_center_enter,
    .on_nav_intercept = NULL,
    .user_data = NULL,
};

int demo_category_fun_enter(void)
{
    return demo_center_show_category(DEMO_CAT_FUN);
}

static const ui_list_menu_config_t *demo_center_cfg_for_category(int category)
{
    switch (category) {
    case DEMO_CAT_EDGE:
        return &s_edge_cfg;
    case DEMO_CAT_CLOUD:
        return &s_cloud_cfg;
    case DEMO_CAT_FUN:
        return &s_fun_cfg;
    default:
        return &s_edge_cfg;
    }
}

static bool demo_center_nav_intercepted(ui_nav_event_t ev)
{
    const ui_list_menu_config_t *cfg = demo_center_cfg_for_category(s_demo_center_active_cat);
    if (cfg != NULL && cfg->on_nav_intercept != NULL) {
        return cfg->on_nav_intercept(ev, cfg->user_data);
    }
    return false;
}

static void demo_center_apply_focus(void)
{
    const ui_list_menu_config_t *cfg = demo_center_cfg_for_category(s_demo_center_active_cat);
    if (cfg == NULL) {
        return;
    }

    for (int i = 0; i < cfg->item_count && i < DEMO_CENTER_MAX_ITEMS; i++) {
        lv_obj_t *row = s_demo_center_rows[i];
        if (row == NULL || !lv_obj_is_valid(row)) {
            continue;
        }

        bool focused = (i == s_demo_center_focus);
        const char *desc = cfg->descriptions != NULL ? cfg->descriptions[i] : NULL;
        ui_theme_icon_kind_t icon = cfg->icons != NULL ? cfg->icons[i] : UI_THEME_ICON_DEMO;
        ui_theme_set_row_focus(row, cfg->items[i], desc, icon, focused);
        if (focused) {
            lv_obj_scroll_to_view(row, LV_ANIM_ON);
        }
    }
}

static void demo_center_select_focused(void)
{
    const ui_list_menu_config_t *cfg = demo_center_cfg_for_category(s_demo_center_active_cat);
    if (cfg == NULL || cfg->on_select == NULL || cfg->item_count <= 0) {
        return;
    }
    if (demo_center_nav_intercepted(UI_NAV_EVENT_SCREEN_NEXT)) {
        return;
    }
    if (s_demo_center_focus >= 0 && s_demo_center_focus < cfg->item_count) {
        cfg->on_select(s_demo_center_focus, cfg->user_data);
    }
}

static void demo_center_row_press_cb(lv_event_t *e)
{
    ui_touch_tap_press(e, &s_demo_center_tap_state);
}

static void demo_center_row_release_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const ui_list_menu_config_t *cfg = demo_center_cfg_for_category(s_demo_center_active_cat);

    if (cfg == NULL || idx < 0 || idx >= cfg->item_count) {
        return;
    }

    if (!ui_touch_tap_release(e, &s_demo_center_tap_state,
                              UI_TOUCH_TAP_MOVE_LIMIT_DEFAULT)) {
        return;
    }

    s_demo_center_focus = idx;
    demo_center_apply_focus();
    demo_center_select_focused();
}

static void demo_center_tab_press_cb(lv_event_t *e)
{
    ui_touch_tap_press(e, &s_demo_center_tap_state);
}

static void demo_center_tab_release_cb(lv_event_t *e)
{
    int category = (int)(intptr_t)lv_event_get_user_data(e);

    if (category < 0 || category >= DEMO_CAT_COUNT) {
        ui_touch_tap_reset(&s_demo_center_tap_state);
        return;
    }
    if (!ui_touch_tap_release(e, &s_demo_center_tap_state,
                              UI_TOUCH_TAP_MOVE_LIMIT_DEFAULT)) {
        return;
    }
    (void)demo_center_show_category(category);
}

static void demo_center_tab_press_lost_cb(lv_event_t *e)
{
    ui_touch_tap_cancel(e, &s_demo_center_tap_state);
}

static lv_obj_t *demo_center_create_segment(lv_obj_t *parent, const char *text,
                                            int x, int w, bool active)
{
    lv_obj_t *segment = lv_obj_create(parent);
    lv_obj_remove_style_all(segment);
    lv_obj_set_pos(segment, x, 6);
    lv_obj_set_size(segment, w, 34);
    lv_obj_set_style_radius(segment, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(segment,
                              lv_color_hex(active ? UI_THEME_COLOR_TAB_SEL : UI_THEME_COLOR_CARD),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(segment,
                                   lv_color_hex(active ? UI_THEME_COLOR_TAB_SEL_2 : 0xf0f7ff),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(segment, LV_GRAD_DIR_VER,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(segment,
                                  lv_color_hex(active ? 0x15c2a4 : UI_THEME_COLOR_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(segment, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(segment, lv_color_hex(UI_THEME_COLOR_TAB_SEL),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(segment, active ? 8 : 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(segment, active ? LV_OPA_30 : LV_OPA_TRANSP,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(segment, active ? 3 : 0,
                                     LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(segment, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(segment);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, w);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(active ? 0xffffff : UI_THEME_COLOR_DESC),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, &lv_font_ali_25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    return segment;
}

static lv_obj_t *demo_center_create_row(lv_obj_t *parent, const char *title,
                                        const char *desc, int y, bool active,
                                        ui_theme_icon_kind_t icon)
{
    lv_obj_t *row = ui_theme_create_card(parent, 10, y, 316, 56, 18, false);
    ui_theme_set_row_focus(row, title, desc, icon, active);
    return row;
}

static void demo_center_focus_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    const ui_list_menu_config_t *cfg = demo_center_cfg_for_category(s_demo_center_active_cat);
    if (cfg == NULL || cfg->item_count <= 0) {
        return;
    }
    if (demo_center_nav_intercepted(UI_NAV_EVENT_FOCUS_PREV)) {
        return;
    }
    s_demo_center_focus = (s_demo_center_focus + cfg->item_count - 1) % cfg->item_count;
    demo_center_apply_focus();
}

static void demo_center_focus_next(bk_lv_ui_t *ui)
{
    (void)ui;
    const ui_list_menu_config_t *cfg = demo_center_cfg_for_category(s_demo_center_active_cat);
    if (cfg == NULL || cfg->item_count <= 0) {
        return;
    }
    if (demo_center_nav_intercepted(UI_NAV_EVENT_FOCUS_NEXT)) {
        return;
    }
    s_demo_center_focus = (s_demo_center_focus + 1) % cfg->item_count;
    demo_center_apply_focus();
}

static void demo_center_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    if (demo_center_nav_intercepted(UI_NAV_EVENT_SCREEN_PREV)) {
        return;
    }
    (void)home_enter();
}

static void demo_center_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    demo_center_select_focused();
}

static const ui_page_nav_ops_t s_demo_center_nav_ops = {
    .on_focus_prev = demo_center_focus_prev,
    .on_focus_next = demo_center_focus_next,
    .on_screen_prev = demo_center_screen_prev,
    .on_screen_next = demo_center_screen_next,
    .on_confirm_long = NULL,
};

static void demo_center_render(void)
{
    if (s_demo_center_screen == NULL || !lv_obj_is_valid(s_demo_center_screen)) {
        return;
    }

    const ui_list_menu_config_t *cfg = demo_center_cfg_for_category(s_demo_center_active_cat);
    if (cfg == NULL) {
        return;
    }

    lv_obj_clean(s_demo_center_screen);
    ui_theme_apply_screen(s_demo_center_screen);

    char subtitle[48];
    snprintf(subtitle, sizeof(subtitle), "%s · %s",
             s_category_descs[s_demo_center_active_cat], cfg->subtitle);
    (void)ui_theme_create_title(s_demo_center_screen, "Demo中心");
    (void)ui_theme_create_subtitle(s_demo_center_screen, subtitle);

    lv_obj_t *demo_panel = ui_theme_create_card(s_demo_center_screen,
                                                19, 76, 347, 236, 22, true);
    lv_obj_set_style_bg_color(demo_panel, lv_color_hex(0xf8fbff),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(demo_panel, lv_color_hex(UI_THEME_COLOR_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *tabs_bg = lv_obj_create(demo_panel);
    lv_obj_remove_style_all(tabs_bg);
    lv_obj_set_pos(tabs_bg, 8, 8);
    lv_obj_set_size(tabs_bg, 331, 46);
    lv_obj_set_style_bg_opa(tabs_bg, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);

    int segment_w = 103;
    int segment_gap = 8;
    for (int i = 0; i < DEMO_CAT_COUNT; i++) {
        int x = 3 + i * (segment_w + segment_gap);
        s_demo_center_tabs[i] = demo_center_create_segment(tabs_bg,
                                                          s_category_items[i],
                                                          x, segment_w,
                                                          i == s_demo_center_active_cat);
        lv_obj_add_flag(s_demo_center_tabs[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_demo_center_tabs[i], demo_center_tab_press_cb,
                            LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_demo_center_tabs[i], demo_center_tab_release_cb,
                            LV_EVENT_RELEASED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_demo_center_tabs[i], demo_center_tab_press_lost_cb,
                            LV_EVENT_PRESS_LOST, (void *)(intptr_t)i);
    }

    s_demo_center_panel = lv_obj_create(demo_panel);
    lv_obj_remove_style_all(s_demo_center_panel);
    lv_obj_set_pos(s_demo_center_panel, 0, 66);
    lv_obj_set_size(s_demo_center_panel, 347, 162);
    lv_obj_set_scroll_dir(s_demo_center_panel, LV_DIR_VER);
    int content_h = cfg->item_count * 58 + (cfg->item_count > 0 ? (cfg->item_count - 1) * 10 : 0);
    lv_obj_set_scrollbar_mode(s_demo_center_panel,
                              content_h > 162 ? LV_SCROLLBAR_MODE_ON : LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(s_demo_center_panel, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_demo_center_panel, 0,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_width(s_demo_center_panel, 7, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_demo_center_panel, lv_color_hex(UI_THEME_COLOR_PRIMARY),
                              LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_demo_center_panel, LV_OPA_COVER,
                            LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_demo_center_panel, 4, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    for (int i = 0; i < cfg->item_count && i < DEMO_CENTER_MAX_ITEMS; i++) {
        s_demo_center_rows[i] = demo_center_create_row(s_demo_center_panel,
                                                       cfg->items[i],
                                                       cfg->descriptions != NULL ? cfg->descriptions[i] : NULL,
                                                       i * 68, i == s_demo_center_focus,
                                                       cfg->icons != NULL ? cfg->icons[i] : UI_THEME_ICON_DEMO);
        lv_obj_add_flag(s_demo_center_rows[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_demo_center_rows[i], demo_center_row_press_cb,
                            LV_EVENT_PRESSED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(s_demo_center_rows[i], demo_center_row_release_cb,
                            LV_EVENT_RELEASED, (void *)(intptr_t)i);
    }

    demo_center_apply_focus();
}

static int demo_center_show_category(int category)
{
    if (category < 0 || category >= DEMO_CAT_COUNT) {
        category = DEMO_CAT_EDGE;
    }

    s_demo_center_active_cat = category;
    s_demo_center_focus = 0;
    ui_touch_tap_reset(&s_demo_center_tap_state);

    if (s_demo_center_screen == NULL || !lv_obj_is_valid(s_demo_center_screen)) {
        return demo_center_enter();
    }

    demo_center_render();

    /* When returning from a page-bound demo (e.g. ASR/DOA) the demo-center
     * screen still exists but is no longer the active screen, because the demo
     * was loaded with auto_del=false. Re-render alone keeps us on the demo
     * page, so bring the panel back to the foreground here. */
    if (lv_screen_active() != s_demo_center_screen) {
        lv_screen_load_anim(s_demo_center_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0,
                            false);
    }
    return 0;
}

void demo_center_build(lv_obj_t *screen)
{
    s_demo_center_screen = screen;
    s_demo_center_focus = 0;
    ui_touch_tap_reset(&s_demo_center_tap_state);
    demo_center_render();
    (void)ui_nav_register_screen(screen, &s_demo_center_nav_ops);
}

void demo_center_unbuild(lv_obj_t *screen)
{
    if (screen != NULL) {
        ui_nav_unregister_screen(screen);
    }
    s_demo_center_screen = NULL;
    s_demo_center_panel = NULL;
    for (int i = 0; i < DEMO_CAT_COUNT; i++) {
        s_demo_center_tabs[i] = NULL;
    }
    for (int i = 0; i < DEMO_CENTER_MAX_ITEMS; i++) {
        s_demo_center_rows[i] = NULL;
    }
}

int demo_center_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Device settings.                                                    */
/* ------------------------------------------------------------------ */
static const char *const s_settings_items[] = {
    "音量设置",
    "U盘",
    "恢复出厂设置",
};
static const char *const s_settings_descs[] = {
    "系统音量",
    "USB存储",
    "清除配置",
};
static const ui_theme_icon_kind_t s_settings_icons[] = {
    UI_THEME_ICON_VOLUME,
    UI_THEME_ICON_USB,
    UI_THEME_ICON_RESET,
};
#define SETTINGS_ITEM_COUNT ((int)(sizeof(s_settings_items) / sizeof(s_settings_items[0])))

static void settings_on_select(int index, void *user_data)
{
    (void)user_data;
    ui_demo_set_return_menu(device_settings_enter);
    switch (index) {
    case 0:
        (void)volume_start();
        break;
    case 1:
        (void)udisk_start();
        break;
    case 2:
        /* Factory reset clears provisioning state and reboots the board. */
        provisioning_factory_reset();
        break;
    default:
        break;
    }
}

static const ui_list_menu_config_t s_settings_cfg = {
    .title = "设备设置",
    .subtitle = "音量 · U盘 · 恢复出厂",
    .items = s_settings_items,
    .descriptions = s_settings_descs,
    .icons = s_settings_icons,
    .item_count = SETTINGS_ITEM_COUNT,
    .on_select = settings_on_select,
    .on_back = home_enter,
    .on_nav_intercept = NULL,
    .user_data = NULL,
};

int device_settings_enter(void)
{
    return ui_list_menu_create(&s_settings_cfg) != NULL ? 0 : -1;
}

#else /* !ROBOT_TEST */

void demo_center_build(lv_obj_t *screen) { (void)screen; }
void demo_center_unbuild(lv_obj_t *screen) { (void)screen; }
int  demo_center_enter(void) { return 0; }
int  demo_category_edge_enter(void) { return 0; }
int  demo_category_cloud_enter(void) { return 0; }
int  demo_category_fun_enter(void) { return 0; }
int  device_settings_enter(void) { return 0; }

#endif /* ROBOT_TEST */
