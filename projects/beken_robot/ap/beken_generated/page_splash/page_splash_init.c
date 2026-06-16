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
    lv_obj_set_scrollbar_mode(bk_ui->page_1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_1, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(bk_ui->page_1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_label_1 = lv_label_create(bk_ui->page_1);
    lv_label_set_text(bk_ui->page_1_label_1, "BK7259机器人方案");
    lv_label_set_long_mode(bk_ui->page_1_label_1, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_x(bk_ui->page_1_label_1, 21);
    lv_obj_set_y(bk_ui->page_1_label_1, 225 + UI_SHIFT_Y_2MM);
    lv_obj_set_width(bk_ui->page_1_label_1, 319);
    lv_obj_set_height(bk_ui->page_1_label_1, 47);
    lv_obj_set_style_bg_color(bk_ui->page_1_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_label_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_label_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_1_label_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_1_label_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_1_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_1_label_1, &lv_font_ali_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_1_label_1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_label_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_label_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_label_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_1_image_3 = lv_image_create(bk_ui->page_1);
    lv_image_set_src(bk_ui->page_1_image_3, &beken_logo_blue_321x147_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->page_1_image_3, 50, 50);
    lv_image_set_rotation(bk_ui->page_1_image_3, 0);
    lv_obj_set_x(bk_ui->page_1_image_3, 32);
    lv_obj_set_y(bk_ui->page_1_image_3, 71);
    lv_obj_set_width(bk_ui->page_1_image_3, 321);
    lv_obj_set_height(bk_ui->page_1_image_3, 147);
    lv_obj_set_style_bg_color(bk_ui->page_1_image_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_1_image_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_1_image_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_1_image_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_1_image_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_1_image_3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_1_image_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_1_image_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_1_image_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_1_image_3, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_1_image_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

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
