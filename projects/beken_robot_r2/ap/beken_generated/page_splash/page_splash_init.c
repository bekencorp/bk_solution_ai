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

    (void)ui_theme_create_deco_ellipse(bk_ui->page_1, -62, -90, 310, 240, 0xdcecff);
    (void)ui_theme_create_deco_ellipse(bk_ui->page_1, 155, 180, 320, 250, 0xdfeeff);

    bk_ui->page_1_image_3 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_3, &beken_logo_blue_321x147_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->page_1_image_3, 50, 50);
    lv_image_set_rotation(bk_ui->page_1_image_3, 0);
    lv_image_set_scale(bk_ui->page_1_image_3, 210);
    lv_obj_set_x(bk_ui->page_1_image_3, 60);
    lv_obj_set_y(bk_ui->page_1_image_3, 57);
    lv_obj_set_width(bk_ui->page_1_image_3, 264);
    lv_obj_set_height(bk_ui->page_1_image_3, 121);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_3, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_3, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_label_1 = lv_label_create(bk_ui->page_1);
    lv_label_set_text(bk_ui->page_1_label_1, ui_tr(STR_SPLASH_TITLE));
    lv_label_set_long_mode(bk_ui->page_1_label_1, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_x(bk_ui->page_1_label_1, 20);
    lv_obj_set_y(bk_ui->page_1_label_1, 200);
    lv_obj_set_width(bk_ui->page_1_label_1, 345);
    lv_obj_set_style_bg_opa(bk_ui->page_1_label_1, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_1_label_1, lv_color_hex(UI_THEME_COLOR_INK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_1_label_1, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_1_label_1, &lv_font_ali_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_1_label_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    (void)ui_theme_create_text(bk_ui->page_1, ui_tr(STR_SPLASH_FEATURES),
                               32, 244, 321, &lv_font_ali_16,
                               UI_THEME_COLOR_DESC, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *hint = lv_label_create(bk_ui->page_1);
    lv_label_set_text(hint, ui_tr(STR_SPLASH_HINT));
    lv_label_set_long_mode(hint, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_x(hint, 32);
    lv_obj_set_y(hint, 274);
    lv_obj_set_width(hint, 321);
    lv_obj_set_style_bg_opa(hint, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_THEME_COLOR_HINT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(hint, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(hint, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
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
