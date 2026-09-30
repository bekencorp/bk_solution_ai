/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 * 
 * This software is proprietary and confidential. No part of this software may be
 * reproduced, distributed, or transmitted in any form or by any means, including
 * photocopying, recording, or other electronic or mechanical methods, without the
 * prior written permission of BekenCorp, except in the case of brief quotations
 * embodied in critical reviews and certain other noncommercial uses permitted
 * by copyright law.
 * 
 * For permission requests, write to BekenCorp at armino_support@bekencorp.com.

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
 * @brief: init page page_5
 */
void init_page_page_5(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_5 != NULL && lv_obj_is_valid(bk_ui->page_5)) {
        destroy_page_page_5(bk_ui);
    }
    

    bk_ui->page_5 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_5, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_5, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(bk_ui->page_5, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_5, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_5, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_5_arc_1 = lv_arc_create(bk_ui->page_5);
    lv_arc_set_range(bk_ui->page_5_arc_1, 0, 103);
    lv_arc_set_value(bk_ui->page_5_arc_1, 78);
    lv_arc_set_bg_angles(bk_ui->page_5_arc_1, 0, 360);
    lv_arc_set_rotation(bk_ui->page_5_arc_1, 0);
    lv_arc_set_mode(bk_ui->page_5_arc_1, LV_ARC_MODE_NORMAL);
    lv_obj_set_x(bk_ui->page_5_arc_1, 56);
    lv_obj_set_y(bk_ui->page_5_arc_1, 63);
    lv_obj_set_width(bk_ui->page_5_arc_1, 248);
    lv_obj_set_height(bk_ui->page_5_arc_1, 220);
    lv_obj_set_style_bg_color(bk_ui->page_5_arc_1, lv_color_hex(0xf6f6f6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_5_arc_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_5_arc_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_5_arc_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_5_arc_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_5_arc_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_5_arc_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_5_arc_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(bk_ui->page_5_arc_1, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(bk_ui->page_5_arc_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(bk_ui->page_5_arc_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(bk_ui->page_5_arc_1, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(bk_ui->page_5_arc_1, 12, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(bk_ui->page_5_arc_1, lv_color_hex(0x2195f6), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(bk_ui->page_5_arc_1, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(bk_ui->page_5_arc_1, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_5_arc_1, lv_color_hex(0x00b8ff), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_5_arc_1, 255, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_5_arc_1, LV_GRAD_DIR_NONE, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(bk_ui->page_5_arc_1, 13, LV_PART_KNOB | LV_STATE_DEFAULT);


    lv_obj_update_layout(bk_ui->page_5);

    bk_page_fire_init(5, bk_ui);
}

/*
 * @brief: destroy page page_5
 */
void destroy_page_page_5(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(5, bk_ui);

    if (bk_ui->page_5 != NULL) {
        lv_obj_del(bk_ui->page_5);
        bk_ui->page_5 = NULL;
    }
}