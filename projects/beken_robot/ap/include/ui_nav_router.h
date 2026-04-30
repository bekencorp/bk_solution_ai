/**
 * @file ui_nav_router.h
 * @brief 将 ui_nav_event_t 分发给「当前 LVGL 屏」已注册的页面 ops（可扩展多页）
 */
#pragma once

#include "ui_nav_events.h"
#include <common/bk_err.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化路由表（进程内调用一次即可，例如在 beken_ui_init 中）
 */
void ui_nav_router_init(void);

/**
 * @brief 将某块 screen 根对象与页面导航 ops 绑定（新增页面时在创建该屏后调用）
 *
 * 同一 screen 重复注册：更新为新的 ops；线程安全（内部互斥）。
 *
 * @param screen 通常为 bk_lv_ui_t 中的 page_N（与 lv_screen_active() 比较的根对象）
 * @param ops    非空；四个回调可部分为 NULL
 * @return BK_OK / BK_ERR_NO_MEM（注册表满）
 */
bk_err_t ui_nav_register_screen(lv_obj_t *screen, const ui_page_nav_ops_t *ops);

/**
 * @brief 解除绑定（例如 destroy_page_N 之后调用，避免野指针）
 */
void ui_nav_unregister_screen(lv_obj_t *screen);

/**
 * @brief 投递导航事件：内部对 lv_screen_active() 查表并调用当前页 ops（持 lv_vendor_disp_lock）
 *
 * 可从按键线程直接调用（与 LVGL 任务通过 vendor 互斥串行化）。
 */
void ui_nav_dispatch_event(ui_nav_event_t ev);

#ifdef __cplusplus
}
#endif
