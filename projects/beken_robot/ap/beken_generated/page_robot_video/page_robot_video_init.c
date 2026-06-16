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


void init_page_page_11(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_11 != NULL && lv_obj_is_valid(bk_ui->page_11)) {
        destroy_page_page_11(bk_ui);
    }

    bk_ui->page_11 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_11, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_11, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(bk_ui->page_11, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_11, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    bk_ui->page_11_label_title = NULL;
    bk_ui->page_11_spinner = NULL;

    bk_ui->page_11_label_state = lv_label_create(bk_ui->page_11);
    /* Default label is "connecting..."; the page hook in
     * beken_generated/page_robot_video/page_robot_video_hooks.c
     * replaces it with "video connected" on the first frame if
     * robot_ctrl_service is already up. */
    lv_label_set_text(bk_ui->page_11_label_state, "video connecting...");
    lv_obj_set_style_text_color(bk_ui->page_11_label_state, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(bk_ui->page_11_label_state, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(bk_ui->page_11_label_state, LV_ALIGN_CENTER, 0, 0);

    lv_obj_update_layout(bk_ui->page_11);

    bk_page_fire_init(11, bk_ui);
}

void destroy_page_page_11(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(11, bk_ui);

    if (bk_ui->page_11 != NULL) {
        lv_obj_del(bk_ui->page_11);
        bk_ui->page_11 = NULL;
    }

    bk_ui->page_11_label_title = NULL;
    bk_ui->page_11_label_state = NULL;
    bk_ui->page_11_spinner = NULL;
}
