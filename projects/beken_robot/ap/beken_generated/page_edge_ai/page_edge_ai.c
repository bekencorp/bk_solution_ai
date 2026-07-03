/**
 * @file page_edge_ai.c
 * @brief End-side AI entry shim and solution-example page.
 *
 * The End-side AI menu is rendered by demo_catalog. This file keeps the
 * legacy page_edge_ai entry points while hosting the LVGL overlay solution
 * page introduced by the camera/LVGL blending flow.
 */
#include "page_edge_ai.h"

#ifdef ROBOT_TEST

#include <stdint.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "demo/demo_catalog.h"
#include "demo/yoloface_tracking.h"
#include "page_hooks.h"
#include "ui_nav_router.h"

#define SOL_SCREEN_W           LOGICAL_SCREEN_WIDTH
#define SOL_SCREEN_H           LOGICAL_SCREEN_HEIGHT
#define SOL_CAMERA_VIEW_X      25
#define SOL_CAMERA_VIEW_Y      20
#define SOL_CAMERA_VIEW_W      256
#define SOL_CAMERA_VIEW_H      256
#define SOL_TEXT_H             20
#define SOL_CAMERA_BORDER_W    2
#define SOL_CAMERA_RADIUS      15
#define SOL_CAMERA_PANEL_W     (SOL_CAMERA_VIEW_W + SOL_CAMERA_BORDER_W * 2)
#define SOL_CAMERA_PANEL_H     (SOL_CAMERA_VIEW_H + SOL_TEXT_H + SOL_CAMERA_BORDER_W * 2)
#define SOL_BUTTON_X           292
#define SOL_BUTTON_W           82
#define SOL_BUTTON_H           38
#define SOL_BUTTON_GAP         18
#define SOL_BUTTON_Y0          41

static lv_obj_t *s_solution_screen;

static const char *const s_solution_btn_titles[4] = {
    "录入",
    "验证",
    "查询",
    "重置",
};

static void set_label_font(lv_obj_t *label, const lv_font_t *font)
{
    if (label != NULL) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text,
                              int x, int y, int w, int h,
                              const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_label_font(label, font);
    return label;
}

static lv_obj_t *create_solution_button(lv_obj_t *parent, const char *text,
                                        int x, int y, int w, int h)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_label_font(label, &lv_font_ali_16);
    return btn;
}

int page_edge_ai_enter(void)
{
    return demo_category_edge_enter();
}

static void solution_on_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;

    if (yoloface_detection_is_active()) {
        (void)yoloface_detection_exit_to_menu();
    } else {
        (void)page_edge_ai_enter();
    }
}

static const ui_page_nav_ops_t s_solution_nav_ops = {
    .on_focus_prev = NULL,
    .on_focus_next = NULL,
    .on_screen_prev = solution_on_screen_prev,
    .on_screen_next = NULL,
};

int page_edge_ai_solution_enter(void)
{
    if (s_solution_screen != NULL && lv_obj_is_valid(s_solution_screen)) {
        ui_nav_unregister_screen(s_solution_screen);
        lv_obj_del(s_solution_screen);
    }

    s_solution_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_solution_screen, SOL_SCREEN_W, SOL_SCREEN_H);
    lv_obj_set_scrollbar_mode(s_solution_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_solution_screen, lv_color_hex(0x050910), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_solution_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *cam_panel = lv_obj_create(s_solution_screen);
    lv_obj_remove_style_all(cam_panel);
    lv_obj_set_pos(cam_panel, SOL_CAMERA_VIEW_X, SOL_CAMERA_VIEW_Y);
    lv_obj_set_size(cam_panel, SOL_CAMERA_PANEL_W, SOL_CAMERA_PANEL_H);
    lv_obj_set_style_radius(cam_panel, SOL_CAMERA_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(cam_panel, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cam_panel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cam_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cam_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *preview = lv_obj_create(cam_panel);
    lv_obj_remove_style_all(preview);
    lv_obj_set_pos(preview, SOL_CAMERA_BORDER_W, SOL_CAMERA_BORDER_W);
    lv_obj_set_size(preview, SOL_CAMERA_VIEW_W, SOL_CAMERA_VIEW_H);
    lv_obj_set_style_bg_color(preview, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(preview, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status = create_label(cam_panel, "摄像头预览",
                                    SOL_CAMERA_BORDER_W,
                                    SOL_CAMERA_BORDER_W + SOL_CAMERA_VIEW_H,
                                    SOL_CAMERA_VIEW_W, SOL_TEXT_H,
                                    &lv_font_ali_16, 0xffffff);
    lv_obj_set_style_bg_color(status, lv_color_hex(0x10151f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(status, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *cam_border = lv_obj_create(s_solution_screen);
    lv_obj_remove_style_all(cam_border);
    lv_obj_set_pos(cam_border, SOL_CAMERA_VIEW_X, SOL_CAMERA_VIEW_Y);
    lv_obj_set_size(cam_border, SOL_CAMERA_PANEL_W, SOL_CAMERA_PANEL_H);
    lv_obj_set_style_bg_opa(cam_border, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cam_border, SOL_CAMERA_BORDER_W, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(cam_border, lv_color_hex(0x32d5ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(cam_border, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(cam_border, SOL_CAMERA_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cam_border, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        int y = SOL_BUTTON_Y0 + i * (SOL_BUTTON_H + SOL_BUTTON_GAP);
        (void)create_solution_button(s_solution_screen, s_solution_btn_titles[i],
                                     SOL_BUTTON_X, y,
                                     SOL_BUTTON_W, SOL_BUTTON_H);
    }

    bk_page_attach_right_swipe_gesture(s_solution_screen);
    lv_screen_load(s_solution_screen);
    (void)ui_nav_register_screen(s_solution_screen, &s_solution_nav_ops);
    return 0;
}

#else  /* !ROBOT_TEST */

int page_edge_ai_enter(void) { return 0; }
int page_edge_ai_solution_enter(void) { return 0; }

#endif /* ROBOT_TEST */
