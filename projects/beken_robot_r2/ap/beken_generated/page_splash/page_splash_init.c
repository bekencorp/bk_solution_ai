/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 *
 * For permission requests, write to BekenCorp at armino_support@bekencorp.com.
 *
 * Author: Beken LVGL Designer Tool
 */
#include "lvgl.h"
#include "beken_ui.h"
#include "custom_func.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "ui_theme.h"
#include "ui_i18n.h"
#include <stdio.h>
#include <string.h>

/*
 * @brief: init page page_1
 */
void init_page_page_1(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_1 != NULL && lv_obj_is_valid(bk_ui->page_1)) {
        destroy_page_page_1(bk_ui);
    }

    bk_ui->page_1 = lv_obj_create(NULL);
    ui_theme_apply_screen(bk_ui->page_1);

    (void)ui_theme_create_deco_ellipse(bk_ui->page_1, UI_LX(-62), UI_LY(-90),
                                       UI_LX(310), UI_LY(240), 0xdcecff);
    (void)ui_theme_create_deco_ellipse(bk_ui->page_1, UI_LX(155), UI_LY(180),
                                       UI_LX(320), UI_LY(250), 0xdfeeff);

    bk_ui->page_1_image_3 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_3, &beken_logo_blue_321x147_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->page_1_image_3, 50, 50);
    lv_image_set_rotation(bk_ui->page_1_image_3, 0);
    /* The logo is scaled uniformly by the Y factor and re-centred rather than
     * stretched with UI_LX: the canvas grows 1.66x on X but only 1.50x on Y,
     * and that 11% difference is visible on a wordmark. */
    lv_image_set_scale(bk_ui->page_1_image_3, UI_LY(210));
    lv_obj_set_width(bk_ui->page_1_image_3, UI_LY(264));
    lv_obj_set_height(bk_ui->page_1_image_3, UI_LY(121));
    lv_obj_set_x(bk_ui->page_1_image_3, (LOGICAL_SCREEN_WIDTH - UI_LY(264)) / 2);
    lv_obj_set_y(bk_ui->page_1_image_3, UI_LY(57));
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_3, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_3, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_label_1 = lv_label_create(bk_ui->page_1);
    lv_label_set_text(bk_ui->page_1_label_1, ui_tr(STR_SPLASH_TITLE));
    lv_label_set_long_mode(bk_ui->page_1_label_1, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_x(bk_ui->page_1_label_1, UI_LX(20));
    lv_obj_set_y(bk_ui->page_1_label_1, UI_LY(200));
    lv_obj_set_width(bk_ui->page_1_label_1, UI_LX(345));
    lv_obj_set_style_bg_opa(bk_ui->page_1_label_1, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_1_label_1, lv_color_hex(UI_THEME_COLOR_INK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_1_label_1, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_1_label_1, &lv_font_ali_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_1_label_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    (void)ui_theme_create_text(bk_ui->page_1, ui_tr(STR_SPLASH_FEATURES),
                               UI_LX(32), UI_LY(244), UI_LX(321), &lv_font_ali_25,
                               UI_THEME_COLOR_DESC, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *hint = lv_label_create(bk_ui->page_1);
    lv_label_set_text(hint, ui_tr(STR_SPLASH_HINT));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_x(hint, UI_LX(32));
    lv_obj_set_y(hint, UI_LY(274));
    lv_obj_set_width(hint, UI_LX(321));
    lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_THEME_COLOR_HINT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(hint, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(hint, &lv_font_ali_25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_update_layout(bk_ui->page_1);

    bk_page_fire_init(1, bk_ui);
}

/*
 * @brief: destroy page page_1
 */
void destroy_page_page_1(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(1, bk_ui);

    if (bk_ui->page_1 != NULL) {
        lv_obj_del(bk_ui->page_1);
        bk_ui->page_1 = NULL;
    }
}
