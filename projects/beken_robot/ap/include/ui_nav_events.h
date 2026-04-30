/**
 * @file ui_nav_events.h
 * @brief 物理按键到 LVGL 页面之间传递的「导航语义」事件定义。
 *
 * 按键层只产生 ui_nav_event_t；各页面在 ui_page_nav_ops_t 中实现具体行为。
 */
#pragma once

#include <stdint.h>

#include "beken_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 导航语义事件（与物理键的映射在 ui_key_bridge.c 中配置，可自行修改）
 *
 * | 枚举值 | 建议语义 | 页面侧典型用途（由你在各页 ops 中实现） |
 * |--------|----------|------------------------------------------|
 * | UI_NAV_EVENT_FOCUS_PREV | 焦点/选中项「上一项」或向左 | 列表/宫格高亮左移、环形焦点 |
 * | UI_NAV_EVENT_FOCUS_NEXT | 焦点/选中项「下一项」或向右 | 列表/宫格高亮右移 |
 * | UI_NAV_EVENT_SCREEN_PREV | 上一屏 / 返回 | lv_screen_load 上一页、或页内子页后退 |
 * | UI_NAV_EVENT_SCREEN_NEXT | 下一屏 / 前进 | lv_screen_load 下一页、或页内子页前进 |
 */
typedef enum {
    UI_NAV_EVENT_FOCUS_PREV = 0,
    UI_NAV_EVENT_FOCUS_NEXT,
    UI_NAV_EVENT_SCREEN_PREV,
    UI_NAV_EVENT_SCREEN_NEXT,
    UI_NAV_EVENT_CONFIRM_LONG,
    UI_NAV_EVENT_COUNT
} ui_nav_event_t;

/**
 * @brief 单页导航语义的处理入口（每页实现回调，可为 NULL 表示忽略该事件）
 */
typedef struct ui_page_nav_ops {
    void (*on_focus_prev)(bk_lv_ui_t *ui);
    void (*on_focus_next)(bk_lv_ui_t *ui);
    void (*on_screen_prev)(bk_lv_ui_t *ui);
    void (*on_screen_next)(bk_lv_ui_t *ui);
    void (*on_confirm_long)(bk_lv_ui_t *ui);
} ui_page_nav_ops_t;

#ifdef __cplusplus
}
#endif
