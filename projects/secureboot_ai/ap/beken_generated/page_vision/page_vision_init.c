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


void init_page_page_7(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_7 != NULL && lv_obj_is_valid(bk_ui->page_7)) {
        destroy_page_page_7(bk_ui);
    }

    bk_ui->page_7 = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(bk_ui->page_7, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(bk_ui->page_7, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(bk_ui->page_7, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bk_ui->page_7, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(bk_ui->page_7, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* The vision-recognition animation attach/detach and the
     * bk_sconf_enter_vision_mode service start/stop are wired up by
     * the page hook in beken_generated/page_vision/page_vision_hooks.c. */
    bk_ui->page_7_label_1 = NULL;

    lv_obj_update_layout(bk_ui->page_7);

    bk_page_fire_init(7, bk_ui);
}

void destroy_page_page_7(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(7, bk_ui);

    if (bk_ui->page_7 != NULL) {
        lv_obj_del(bk_ui->page_7);
        bk_ui->page_7 = NULL;
        bk_ui->page_7_label_1 = NULL;
    }
}
