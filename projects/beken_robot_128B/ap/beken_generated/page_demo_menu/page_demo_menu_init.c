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
#include <stdio.h>
#include <string.h>

/*
 * @brief: init page page_3 (demo center)
 *
 * page_3 is now just an empty themed screen; the category bar list
 * (End-side AI / Cloud AI / Entertainment) is rendered onto it by the
 * shared ui_list_menu component from page_demo_menu_hooks.c. Keeping the
 * generated page_3 slot stable preserves nav-router page ids and the
 * navigate_to_screen(page_3) call sites used as the demo-center entry.
 */
void init_page_page_3(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_3 != NULL && lv_obj_is_valid(bk_ui->page_3)) {
        destroy_page_page_3(bk_ui);
    }

    bk_ui->page_3 = lv_obj_create(NULL);
    ui_theme_apply_screen(bk_ui->page_3);

    lv_obj_update_layout(bk_ui->page_3);

    bk_page_fire_init(3, bk_ui);
}

/*
 * @brief: destroy page page_3
 */
void destroy_page_page_3(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(3, bk_ui);

    if (bk_ui->page_3 != NULL) {
        lv_obj_del(bk_ui->page_3);
        bk_ui->page_3 = NULL;
    }
}
