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
#include "ui_i18n.h"
#include "demo/provisioning.h"
#include <stdio.h>
#include <string.h>


/*
 * @brief: init page page_4
 */
void init_page_page_4(bk_lv_ui_t *bk_ui)
{
    if (bk_ui->page_4 != NULL && lv_obj_is_valid(bk_ui->page_4)) {
        destroy_page_page_4(bk_ui);
    }
    

    bk_ui->page_4 = lv_obj_create(NULL);
    ui_theme_apply_screen(bk_ui->page_4);

    bk_ui->page_4_label_title = ui_theme_create_title(bk_ui->page_4, ui_tr(STR_PROV_TITLE));
    bk_ui->page_4_label_desc = ui_theme_create_subtitle(bk_ui->page_4, ui_tr(STR_PROV_SUBTITLE));

    bk_ui->page_4_status_card = ui_theme_create_card(bk_ui->page_4, 22, 74, 341, 124, 26, true);

    /* Caption line shows the device name inline after the "Device Name" label,
     * e.g. "当前设备名 bk_robot_80D6D6", so the phone app can find the device as
     * soon as this page opens. The name is derived from the BT MAC and is
     * constant. The label part stays muted; the device name is recolored to the
     * theme primary so it stands out from the caption. */
    char prov_devname[24] = {0};
    char prov_caption[80];
    lv_obj_t *prov_cap_label = ui_theme_create_text(bk_ui->page_4_status_card, "",
                                                    19, 18, 303,
                                                    &lv_font_ali_16, UI_THEME_COLOR_MUTED,
                                                    LV_TEXT_ALIGN_LEFT);
    lv_label_set_recolor(prov_cap_label, true);
    (void)provisioning_get_ble_name(prov_devname, sizeof(prov_devname));
    if (prov_devname[0] != '\0') {
        snprintf(prov_caption, sizeof(prov_caption), "%s #%06x %s#",
                 ui_tr(STR_PROV_DEVICE_NAME),
                 (unsigned)(UI_THEME_COLOR_PRIMARY & 0xFFFFFFu), prov_devname);
    } else {
        snprintf(prov_caption, sizeof(prov_caption), "%s", ui_tr(STR_PROV_DEVICE_NAME));
    }
    lv_label_set_text(prov_cap_label, prov_caption);

    bk_ui->page_4_label_status = ui_theme_create_text(bk_ui->page_4_status_card,
                                                      "",
                                                      19, 50, 303,
                                                      &lv_font_ali_25,
                                                      UI_THEME_COLOR_PRIMARY,
                                                      LV_TEXT_ALIGN_LEFT);
    bk_ui->page_4_label_hint = ui_theme_create_text(bk_ui->page_4_status_card,
                                                    "State: WAIT_PROVISIONING",
                                                    19, 92, 303,
                                                    &lv_font_ali_16,
                                                    UI_THEME_COLOR_MUTED,
                                                    LV_TEXT_ALIGN_LEFT);

    bk_ui->page_4_button_1 = ui_theme_create_action_button(bk_ui->page_4,
                                                           ui_tr(STR_PROV_BTN_START),
                                                           22, 216, 160, 52,
                                                           UI_THEME_BUTTON_PRIMARY);
    bk_ui->page_4_button_1_label = lv_obj_get_child(bk_ui->page_4_button_1, 0);

    bk_ui->page_4_button_2 = ui_theme_create_action_button(bk_ui->page_4,
                                                           ui_tr(STR_PROV_BTN_DELETE),
                                                           203, 216, 160, 52,
                                                           UI_THEME_BUTTON_DANGER);
    bk_ui->page_4_button_2_label = lv_obj_get_child(bk_ui->page_4_button_2, 0);

    /* Factory reset moved to the device-settings menu (see demo_catalog). */
    bk_ui->page_4_button_3 = NULL;
    bk_ui->page_4_button_3_label = NULL;


    lv_obj_update_layout(bk_ui->page_4);

    bk_page_fire_init(4, bk_ui);
}

/*
 * @brief: destroy page page_4
 */
void destroy_page_page_4(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

    bk_page_fire_destroy(4, bk_ui);

    if (bk_ui->page_4 != NULL) {
        lv_obj_del(bk_ui->page_4);
        bk_ui->page_4 = NULL;
        bk_ui->page_4_label_title = NULL;
        bk_ui->page_4_label_desc = NULL;
        bk_ui->page_4_status_card = NULL;
        bk_ui->page_4_label_status = NULL;
        bk_ui->page_4_label_hint = NULL;
        bk_ui->page_4_button_1 = NULL;
        bk_ui->page_4_button_1_label = NULL;
        bk_ui->page_4_button_2 = NULL;
        bk_ui->page_4_button_2_label = NULL;
        bk_ui->page_4_button_3 = NULL;
        bk_ui->page_4_button_3_label = NULL;
    }
}
