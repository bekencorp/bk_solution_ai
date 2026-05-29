/**
 * @file ui_key_bridge.c
 * @brief 物理 key_event_t → ui_nav_event_t，并调用 ui_nav_dispatch_event
 *
 * ## 物理键 → 导航语义（默认占位，请按硬件修改）
 *
 * | key_adapter 事件 | 当前映射的 ui_nav_event_t | 说明 |
 * |-------------------|---------------------------|------|
 * | ADC_KEY_S4_SHORT  | UI_NAV_EVENT_FOCUS_PREV   | 左移 / 上一焦点 |
 * | ADC_KEY_S5_SHORT  | UI_NAV_EVENT_FOCUS_NEXT   | 右移 / 下一焦点 |
 * | GPIO_KEY1_ANY_SHORT | UI_NAV_EVENT_SCREEN_NEXT | 下一屏（S2/S3 合并键短按） |
 * | GPIO_KEY1_ANY_LONG  | UI_NAV_EVENT_SCREEN_PREV | 上一屏（长按） |
 *
 * 未映射的事件（双击等）当前忽略；需要时在下方 switch 中补充。
 *
 * 特例：手掌跟随（page_3 -> palm_detection_start）会调用 lv_vendor_stop()
 * 暂停 LVGL 并接管显示，此时常规的 ui_nav_dispatch_event 路径无法工作
 * （lv_screen_active() 不会更新）。我们在进入 switch 之前先把
 * "S4 双击" 截走，直接调用 palm_detection_exit_to_menu() 完成停流+恢复
 * LVGL+回到 page_3。
 */
#include <common/sys_config.h>

#if CONFIG_LVGL && CONFIG_BUTTON

#include "ui_nav_router.h"
#include "ui_nav_events.h"
#include "palm_detection.h"

#include <key_adapter.h>
#include <components/log.h>

#define TAG "ui_key_bridge"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

void bk_key_app_notify_ui_nav(uint8_t event)
{
#if CONFIG_ADC_KEY
    if (palm_detection_is_active()) {
        if ((key_event_t)event == ADC_KEY_S4_DOUBLE) {
            LOGI("key S4 double -> exit palm tracking, back to page_3\r\n");
            (void)palm_detection_exit_to_menu();
        }
        return;
    }
#endif

    ui_nav_event_t nav = UI_NAV_EVENT_COUNT;

    switch ((key_event_t)event) {
#if CONFIG_ADC_KEY
    case ADC_KEY_S5_SHORT:
        nav = UI_NAV_EVENT_FOCUS_PREV;
        break;
    case GPIO_KEY1_ANY_SHORT:
        nav = UI_NAV_EVENT_FOCUS_NEXT;
        break;
    case ADC_KEY_S4_SHORT:
        nav = UI_NAV_EVENT_SCREEN_NEXT;
        break;
    case ADC_KEY_S4_DOUBLE:
        nav = UI_NAV_EVENT_SCREEN_PREV;
        break;
    case ADC_KEY_S4_LONG:
        nav = UI_NAV_EVENT_CONFIRM_LONG;
        break;
#endif
    default:
        break;
    }

    if (nav >= UI_NAV_EVENT_COUNT) {
        return;
    }

    LOGI("key %u -> nav %d\r\n", (unsigned)event, (int)nav);
    ui_nav_dispatch_event(nav);
}

#endif /* CONFIG_LVGL && CONFIG_BUTTON */
