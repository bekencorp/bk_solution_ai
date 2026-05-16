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
#include <stdio.h>
#include <string.h>
// custom page code
#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "lv_vendor.h"
#include "page_5_api.h"
#include "page_5_eyes.h"
#include "components/log.h"
#include "bk_cli.h"
#include "cli.h"
#include "os/os.h"
#include "os/str.h"
#include "audio_engine.h"

#define TAG "page5"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

/* ------------------------------------------------------------------------- */
/*  Sound source localization arc control                                    */
/* ------------------------------------------------------------------------- */

/*
 * Designer 把 page_5 的指示控件从"旋转图像"改成了 lv_arc：
 *   - 白色完整圆环作为背景（bg_angles 0~360）
 *   - 蓝色 KNOB 沿圆环移动表示声源方位
 *   - INDICATOR（中间填充弧）opa=0 隐藏，只露 KNOB
 *
 * 因此 page_5_set_arrow_angle(deg) 的语义变成：
 *   "把 KNOB 移到圆环上对应 deg 度的位置"
 *
 * 实现做法：
 *   - init 时把 arc range 强制改为 [0, 360]，让 1 个 value 单位 = 1°
 *   - set_arrow_angle 内部直接 lv_arc_set_value(arc, deg_normalized)
 *
 * LVGL arc 的 0° 起点是 3 点钟方向（屏幕右），顺时针递增：
 *      0° = 右       90° = 下       180° = 左       270° = 上
 * 如果业务习惯"0° = 正北/屏幕上"，外部传值前自己做 (deg + 270) % 360
 * 即可，本接口不强加偏移，保持与 LVGL 一致。
 */
#define PAGE5_ARC_RANGE_MIN    0
#define PAGE5_ARC_RANGE_MAX    360

/* 默认向下(90°)：蓝色小球位于圆框底部，对齐嘴巴正下方；
 * 同时瞳孔默认落在上/下区，颜色为棕色。 */
static volatile int s_arrow_deg = 90;

static void page_5_apply_arrow_state_locked(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    if (ui->page_5_arc_1 == NULL || !lv_obj_is_valid(ui->page_5_arc_1)) {
        return;
    }
    lv_arc_set_value(ui->page_5_arc_1, s_arrow_deg);

    /* KNOB 角度同步给中心双眼，瞳孔朝同方向偏移 + 颜色变化，
     * 形成"看向你"的凝视错觉。eyes 还未创建时函数内自检后直接返回。
     * 已经在 lv_vendor_disp_lock 内，page_5_eyes_set_gaze 不再加锁。 */
    page_5_eyes_set_gaze(s_arrow_deg);
}

void page_5_set_arrow_angle(int degrees)
{
    /* 归一化到 [0, 360)，含负数处理 */
    int d = degrees % 360;
    if (d < 0) {
        d += 360;
    }
    s_arrow_deg = d;

    lv_vendor_disp_lock();
    page_5_apply_arrow_state_locked(&bk_lv_tool_ui);
    lv_vendor_disp_unlock();
}

int page_5_get_arrow_angle(void)
{
    return s_arrow_deg;
}

void page_5_set_arrow_pivot(int x, int y)
{
    (void)x;
    (void)y;
    LOGW("page_5_set_arrow_pivot ignored: page_5 now uses lv_arc, no pivot\r\n");
}

/* ------------------------------------------------------------------------- */
/*  CLI: arrow ...                                                           */
/* ------------------------------------------------------------------------- */
/*
 * 串口命令，用于人工调试箭头旋转：
 *
 *   arrow                       打印用法
 *   arrow get                   读回当前角度
 *   arrow set <deg>             设置角度（也可写成 arrow <deg>）
 *   arrow sweep <step> <ms>     周期性自动旋转（每 ms 毫秒 +step 度）
 *   arrow stop                  停止 sweep
 *
 * 注：page_5 现在用 lv_arc，"角度"语义是 KNOB 在圆环上的位置（0~360）。
 * 旧版 image 旋转时代的 `arrow pivot` 子命令已经无意义，被移除。
 *
 * sweep 用 rtos 周期定时器实现，timer 回调里只调
 * page_5_set_arrow_angle()，已经是线程安全的。
 */

static beken_timer_t s_sweep_timer;
static bool          s_sweep_timer_inited;
static int           s_sweep_step;       /* 每 tick 增量，单位"度" */

static void page_5_sweep_tick(void *arg)
{
    (void)arg;
    page_5_set_arrow_angle(s_arrow_deg + s_sweep_step);
}

static void page_5_sweep_stop(void)
{
    if (!s_sweep_timer_inited) {
        return;
    }
    if (rtos_is_timer_running(&s_sweep_timer)) {
        rtos_stop_timer(&s_sweep_timer);
    }
    rtos_deinit_timer(&s_sweep_timer);
    s_sweep_timer_inited = false;
}

static int page_5_sweep_start(int step_deg, int interval_ms)
{
    if (interval_ms < 10) {
        /* 太快没意义且会阻塞 LVGL */
        return -1;
    }
    page_5_sweep_stop();

    s_sweep_step = step_deg;
    if (rtos_init_timer(&s_sweep_timer,
                        (uint32_t)interval_ms,
                        page_5_sweep_tick,
                        NULL) != BK_OK) {
        return -2;
    }
    s_sweep_timer_inited = true;
    if (rtos_start_timer(&s_sweep_timer) != BK_OK) {
        page_5_sweep_stop();
        return -3;
    }
    return 0;
}

static void arrow_cli_usage(void)
{
    LOGI("Usage (page_5 uses lv_arc; angle = KNOB position on the ring):\r\n"
         "  arrow                       show this help\r\n"
         "  arrow get                   print current angle\r\n"
         "  arrow set <deg>             set angle 0~360 (or: arrow <deg>)\r\n"
         "  arrow sweep <step> <ms>     auto-rotate every <ms> by <step> deg\r\n"
         "  arrow stop                  stop sweep\r\n");
}

static void arrow_cli_handler(char *out, int out_len, int argc, char **argv)
{
    (void)out;
    (void)out_len;

    if (argc < 2) {
        arrow_cli_usage();
        return;
    }

    /* arrow <deg>  — 直接传一个数字，等价于 set */
    if (argc == 2 && argv[1][0] != '\0' &&
        (argv[1][0] == '-' ||
         (argv[1][0] >= '0' && argv[1][0] <= '9'))) {
        int deg = atoi(argv[1]);
        page_5_set_arrow_angle(deg);
        LOGI("arrow set deg=%d (now=%d)\r\n", deg, page_5_get_arrow_angle());
        return;
    }

    if (os_strcmp(argv[1], "get") == 0) {
        LOGI("arrow angle=%d\r\n", page_5_get_arrow_angle());
        return;
    }

    if (os_strcmp(argv[1], "set") == 0 && argc >= 3) {
        int deg = atoi(argv[2]);
        page_5_set_arrow_angle(deg);
        LOGI("arrow set deg=%d (now=%d)\r\n", deg, page_5_get_arrow_angle());
        return;
    }

    if (os_strcmp(argv[1], "sweep") == 0 && argc >= 4) {
        int step = atoi(argv[2]);
        int ms   = atoi(argv[3]);
        int ret  = page_5_sweep_start(step, ms);
        if (ret == 0) {
            LOGI("arrow sweep started: step=%d deg, interval=%d ms\r\n",
                 step, ms);
        } else {
            LOGW("arrow sweep start failed: ret=%d (interval must be >= 10)\r\n",
                 ret);
        }
        return;
    }

    if (os_strcmp(argv[1], "stop") == 0) {
        page_5_sweep_stop();
        LOGI("arrow sweep stopped\r\n");
        return;
    }

    arrow_cli_usage();
}

static const struct cli_command s_arrow_cli_cmd[] = {
    {
        "arrow",
        "arrow [get|set <deg>|sweep <step> <ms>|stop]",
        arrow_cli_handler,
    },
};

int page_5_cli_init(void)
{
    static bool s_inited;
    if (s_inited) {
        return 0;
    }
    int ret = cli_register_commands(s_arrow_cli_cmd,
                                    sizeof(s_arrow_cli_cmd) / sizeof(s_arrow_cli_cmd[0]));
    if (ret == 0) {
        s_inited = true;
        LOGI("arrow CLI registered\r\n");
    } else {
        LOGW("arrow CLI register failed: ret=%d\r\n", ret);
    }
    return ret;
}

/* ------------------------------------------------------------------------- */
/*  Navigation hook                                                          */
/* ------------------------------------------------------------------------- */

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page5 back -> page_3\r\n");
    #if (CONFIG_ASR_SERVICE)
    if (AUDIO_ENGINE_SUCCESS != audio_engine_asr_stop()) {
        LOGI("page5 stop asr failed\r\n");
    } else
    {
        LOGI("page5 stop asr\r\n");
    }
    #endif
    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    LOGI("page5 enter (no submenu)\r\n");
}

const ui_page_nav_ops_t page_5_nav_ops = {
    .on_focus_prev  = NULL,
    .on_focus_next  = NULL,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

#endif /* ROBOT_TEST */

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
    lv_obj_set_size(bk_ui->page_5, 390, 360);
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


    // custom code implementation
                #ifdef ROBOT_TEST
                    /* 页面首帧强制置为向下(90°)：避免进入 page 前被别的流程
                     * 写成 0° 导致小球跑到右侧、瞳孔变琥珀。 */
                    s_arrow_deg = 90;

                    lv_arc_set_range(bk_ui->page_5_arc_1,
                                     PAGE5_ARC_RANGE_MIN, PAGE5_ARC_RANGE_MAX);

                    /* Page 大小覆盖：Designer 默认 lv_obj_set_size(page_5, 360, 390)
                     * 是按"原始物理屏 360x390"算的；但 ap_main.c 设了 ROTATE_90，
                     * 应用层逻辑屏实际是 390x360（横屏），原 page 会有：
                     *   - 右边 30 px 是 page 之外（黑边）
                     *   - 下面 30 px 在 page 之内但超出应用层屏底，被裁掉
                     * 直接覆盖成 390x360 让 page 和应用层屏对齐，page 局部坐标
                     * = 应用层坐标，后续元素位置才好算。 */
                    lv_obj_set_size(bk_ui->page_5, 390, 360);

                    /* 居中修正 + 尺寸覆盖：Designer 默认把 KNOB 圆环放在
                     * (56, 63) 248x220，跟 ROTATE_90 后的逻辑屏中心对不上。
                     * 圆环外接矩形和经验补偿都集中在 page_5_eyes.h 里：
                     *   - 圆环大小：PAGE_5_RING_W x PAGE_5_RING_H
                     *   - 整组偏移：PAGE_5_FACE_NUDGE_X / _Y
                     * 圆环和 page_5_eyes 双眼用同一个 FACE_CENTER（=逻辑屏中
                     * 心 + NUDGE）同心放置，调一处所有元素同步跟随。
                     *
                     * 写在 #ifdef ROBOT_TEST 的 custom code 块里而不是改前面
                     * Designer 生成的 lv_obj_set_x/y/width/height(arc, ...) ：
                     *   - 那几行 Designer 重生成时会被还原成默认值（OK，本块
                     *     在后面再覆盖一次）
                     *   - #ifdef ROBOT_TEST 自定义代码块按惯例会被工具保留，
                     *     所以本补丁不会随重生成丢失 */
                    lv_obj_set_width(bk_ui->page_5_arc_1, PAGE_5_RING_W);
                    lv_obj_set_height(bk_ui->page_5_arc_1, PAGE_5_RING_H);
                    lv_obj_set_x(bk_ui->page_5_arc_1,
                                 (195 - PAGE_5_RING_W / 2) + PAGE_5_FACE_NUDGE_X);
                    lv_obj_set_y(bk_ui->page_5_arc_1,
                                 (180 - PAGE_5_RING_H / 2) + PAGE_5_FACE_NUDGE_Y);

                    /* 创建中心双眼 + 笑嘴。必须在 apply_arrow_state_locked
                     * 之前，否则首帧没有凝视方向。详见 page_5_eyes.h。 */
                    page_5_eyes_create(bk_ui->page_5);
        
                    page_5_apply_arrow_state_locked(bk_ui);
        
                    (void)ui_nav_register_screen(bk_ui->page_5, &page_5_nav_ops);
                #endif
    
    lv_obj_update_layout(bk_ui->page_5);
}

/*
 * @brief: destroy page page_5
 */
void destroy_page_page_5(bk_lv_ui_t *bk_ui)
{
    if (bk_ui == NULL) {
        return;
    }

#ifdef ROBOT_TEST
    /* 必须在 lv_obj_del(page_5) 之前先停眨眼动画并释放眼睛对象，
     * 否则 anim 回调还在跑，会写到已释放的 lv_obj 上。 */
    page_5_eyes_destroy();
#endif
    
    if (bk_ui->page_5 != NULL) {
        lv_obj_del(bk_ui->page_5);
        bk_ui->page_5 = NULL;
    }
}