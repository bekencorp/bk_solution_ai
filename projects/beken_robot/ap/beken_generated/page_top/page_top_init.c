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
#include "ui_theme.h"
#include <stdio.h>
#include <string.h>


/*
 * Vector Wi-Fi status icon. Drawn with LVGL primitives instead of a bitmap so
 * it has crisp proportions, a solid centre dot and shares the theme primary
 * colour with the battery indicator (keeps both status icons unified). The host
 * object is page_2_image_2 so the wifi_status_ui bridge can still toggle it.
 */
static void wifi_icon_draw_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    const lv_color_t color = lv_color_hex(UI_THEME_COLOR_PRIMARY);
    const int32_t cx = (coords.x1 + coords.x2) / 2;
    const int32_t cy = coords.y2 - 2;               /* source dot near bottom */
    static const uint16_t radii[3] = {4, 8, 12};    /* nested upward waves */

    for (int i = 0; i < 3; i++) {
        lv_draw_arc_dsc_t adsc;
        lv_draw_arc_dsc_init(&adsc);
        adsc.base.layer = layer;
        adsc.color = color;
        adsc.opa = LV_OPA_COVER;
        adsc.width = 2;
        adsc.rounded = 1;
        adsc.center.x = cx;
        adsc.center.y = cy;
        adsc.radius = radii[i];
        adsc.start_angle = 225;                     /* upper-left ... */
        adsc.end_angle = 315;                       /* ... top ... upper-right */
        lv_draw_arc(layer, &adsc);
    }

    lv_draw_rect_dsc_t ddsc;
    lv_draw_rect_dsc_init(&ddsc);
    ddsc.base.layer = layer;
    ddsc.bg_color = color;
    ddsc.bg_opa = LV_OPA_COVER;
    ddsc.radius = LV_RADIUS_CIRCLE;

    const int32_t rdot = 2;
    lv_area_t dot = { cx - rdot, cy - rdot, cx + rdot, cy + rdot };
    lv_draw_rect(layer, &ddsc, &dot);
}


/*
 * @brief: init page page_2
 */
void init_page_page_2(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_2 != NULL && lv_obj_is_valid(bk_ui->page_2)) {
        destroy_page_page_2(bk_ui);
    }
    

    bk_ui->page_2 = lv_obj_create(NULL);
    ui_theme_apply_screen(bk_ui->page_2);

    /* The console title + the three navigation bars
     * (Connection / Demo center / Device settings) are rendered by the
     * shared ui_list_menu component from page_top_hooks.c. This init only
     * keeps the top-right status indicators (wifi icon + battery bar) so the
     * wifi_status_ui bridge (which toggles page_2_image_2) keeps working. */
    bk_ui->page_2_button_1 = NULL;
    bk_ui->page_2_button_1_label = NULL;
    bk_ui->page_2_button_3 = NULL;
    bk_ui->page_2_button_3_label = NULL;
    bk_ui->page_2_image_1 = NULL;

    /* Status indicators (top-right corner): wifi icon + battery bar. The wifi
     * icon is a transparent container rendered by wifi_icon_draw_event_cb(). */
    bk_ui->page_2_image_2 = lv_obj_create(bk_ui->page_2);
    lv_obj_remove_style_all(bk_ui->page_2_image_2);
    lv_obj_clear_flag(bk_ui->page_2_image_2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_x(bk_ui->page_2_image_2, 330);
    lv_obj_set_y(bk_ui->page_2_image_2, 24);
    lv_obj_set_width(bk_ui->page_2_image_2, 28);
    lv_obj_set_height(bk_ui->page_2_image_2, 18);
    lv_obj_add_event_cb(bk_ui->page_2_image_2, wifi_icon_draw_event_cb,
                        LV_EVENT_DRAW_MAIN, NULL);
    /* Initial WiFi icon visibility is set by the page_2 init hook in
     * src/common/wifi_status_ui.c based on
     * bk_sconf_is_network_provisioned(); that dependency is kept out
     * of this generated file. */

    bk_ui->page_2_bar_1 = lv_bar_create(bk_ui->page_2);
    lv_bar_set_range(bk_ui->page_2_bar_1, 0, 100);
    lv_obj_set_style_anim_duration(bk_ui->page_2_bar_1, 1000, 0);
    lv_bar_set_start_value(bk_ui->page_2_bar_1, 0, LV_ANIM_ON);
    lv_bar_set_value(bk_ui->page_2_bar_1, 82, LV_ANIM_ON);
    lv_bar_set_mode(bk_ui->page_2_bar_1, LV_BAR_MODE_NORMAL);
    lv_obj_set_x(bk_ui->page_2_bar_1, 286);
    lv_obj_set_y(bk_ui->page_2_bar_1, 28);
    lv_obj_set_width(bk_ui->page_2_bar_1, 36);
    lv_obj_set_height(bk_ui->page_2_bar_1, 12);
    lv_obj_set_style_anim_duration(bk_ui->page_2_bar_1, 1000, 0);
    lv_obj_set_style_bg_color(bk_ui->page_2_bar_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_bar_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_bar_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_bar_1, lv_color_hex(UI_THEME_COLOR_PRIMARY), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_bar_1, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_bar_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_bar_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_bar_1, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_2_bar_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_bar_1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_bar_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_bar_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_2_bar_1, lv_color_hex(UI_THEME_COLOR_PRIMARY), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_bar_1, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_bar_1, 3, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_t *battery_tip = lv_obj_create(bk_ui->page_2);
    lv_obj_remove_style_all(battery_tip);
    lv_obj_set_pos(battery_tip, 323, 31);
    lv_obj_set_size(battery_tip, 4, 6);
    lv_obj_set_style_radius(battery_tip, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(battery_tip, lv_color_hex(UI_THEME_COLOR_PRIMARY), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(battery_tip, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

#if 0 /* Legacy top-page layout (logo + provisioning/demo buttons) replaced
       * by the three-bar ui_list_menu built in page_top_hooks.c. */
    bk_ui->page_2_button_1 = lv_btn_create(bk_ui->page_2);
    bk_ui->page_2_button_1_label = lv_label_create(bk_ui->page_2_button_1);
    lv_label_set_text(bk_ui->page_2_button_1_label, "配    网");
    lv_label_set_long_mode(bk_ui->page_2_button_1_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_2_button_1_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_2_button_1, 25);
    lv_obj_set_y(bk_ui->page_2_button_1, 218 + UI_SHIFT_Y_2MM);
    lv_obj_set_width(bk_ui->page_2_button_1, 100);
    lv_obj_set_height(bk_ui->page_2_button_1, 40);
    lv_obj_set_style_bg_color(bk_ui->page_2_button_1, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_button_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_button_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_button_1, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_2_button_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_2_button_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_button_1, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_button_1, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_button_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_button_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_2_button_1, lv_color_hex(0xc0c0c0), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(bk_ui->page_2_button_1, 255, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_button_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DISABLED);

    bk_ui->page_2_button_3 = lv_btn_create(bk_ui->page_2);
    bk_ui->page_2_button_3_label = lv_label_create(bk_ui->page_2_button_3);
    lv_label_set_text(bk_ui->page_2_button_3_label, "示例模式");
    lv_label_set_long_mode(bk_ui->page_2_button_3_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_align(bk_ui->page_2_button_3_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_x(bk_ui->page_2_button_3, 243);
    lv_obj_set_y(bk_ui->page_2_button_3, 218 + UI_SHIFT_Y_2MM);
    lv_obj_set_width(bk_ui->page_2_button_3, 100);
    lv_obj_set_height(bk_ui->page_2_button_3, 40);
    lv_obj_add_state(bk_ui->page_2_button_3, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(bk_ui->page_2_button_3, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_button_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_button_3, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_button_3, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_2_button_3, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bk_ui->page_2_button_3, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_2_button_3, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bk_ui->page_2_button_3, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_button_3, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_button_3, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bk_ui->page_2_button_3, lv_color_hex(0xc0c0c0), LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_opa(bk_ui->page_2_button_3, 255, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_button_3, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DISABLED);

    bk_ui->page_2_image_1 = lv_image_create(bk_ui->page_2);
    lv_image_set_src(bk_ui->page_2_image_1, &beken_logo_blue_321x147_RGB565A8_NONE);
    lv_image_set_pivot(bk_ui->page_2_image_1, 50, 50);
    lv_image_set_rotation(bk_ui->page_2_image_1, 0);
    lv_obj_set_x(bk_ui->page_2_image_1, 32);
    lv_obj_set_y(bk_ui->page_2_image_1, 71);
    lv_obj_set_width(bk_ui->page_2_image_1, 321);
    lv_obj_set_height(bk_ui->page_2_image_1, 147);
    lv_obj_set_style_bg_color(bk_ui->page_2_image_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_2_image_1, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(bk_ui->page_2_image_1, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(bk_ui->page_2_image_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(bk_ui->page_2_image_1, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(bk_ui->page_2_image_1, false, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(bk_ui->page_2_image_1, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(bk_ui->page_2_image_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(bk_ui->page_2_image_1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor(bk_ui->page_2_image_1, lv_color_hex(0x00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(bk_ui->page_2_image_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
#endif /* legacy top-page layout */


    lv_obj_update_layout(bk_ui->page_2);

    bk_page_fire_init(2, bk_ui);
}

/*
 * @brief: destroy page page_2
 */
void destroy_page_page_2(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(2, bk_ui);

    if (bk_ui->page_2 != NULL) {
        lv_obj_del(bk_ui->page_2);
        bk_ui->page_2 = NULL;
    }
}