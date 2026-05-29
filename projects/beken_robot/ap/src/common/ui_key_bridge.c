/**
 * @file ui_key_bridge.c
 * @brief Map physical key_event_t -> ui_nav_event_t and invoke
 *        ui_nav_dispatch_event.
 *
 * ## Physical key -> navigation semantics (default placeholder; adjust
 * ## to the actual hardware layout)
 *
 * | key_adapter event   | Mapped ui_nav_event_t     | Notes                              |
 * |---------------------|---------------------------|------------------------------------|
 * | ADC_KEY_S4_SHORT    | UI_NAV_EVENT_FOCUS_PREV   | Move left / previous focus          |
 * | ADC_KEY_S5_SHORT    | UI_NAV_EVENT_FOCUS_NEXT   | Move right / next focus             |
 * | GPIO_KEY1_ANY_SHORT | UI_NAV_EVENT_SCREEN_NEXT  | Next screen (combined S2/S3 short)  |
 * | GPIO_KEY1_ANY_LONG  | UI_NAV_EVENT_SCREEN_PREV  | Previous screen (long press)        |
 *
 * Unmapped events (double-click, etc.) are ignored for now -- extend
 * the switch below as needed.
 *
 * Special case: palm tracking (page_3 -> palm_detection_start) calls
 * lv_vendor_stop() to pause LVGL and takes over the framebuffer. The
 * normal ui_nav_dispatch_event path stops working because
 * lv_screen_active() no longer updates. Before the switch we
 * therefore intercept "S4 double-click" and call
 * palm_detection_exit_to_menu() directly to stop the NN pipeline,
 * resume LVGL and navigate back to page_3.
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
