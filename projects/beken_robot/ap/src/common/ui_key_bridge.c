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
 * Special case: overlay demos (palm tracking, yoloface detection, hand gesture, car tracking, ...)
 * call lv_vendor_stop() to pause LVGL and take over the framebuffer.
 * The normal ui_nav_dispatch_event path stops working because
 * lv_screen_active() no longer updates. Before the switch we therefore
 * intercept overlay exit keys and call *_detection_exit_to_menu()
 * directly to stop the NN pipeline, resume LVGL and navigate back.
 */
#include <common/sys_config.h>

#if CONFIG_LVGL && CONFIG_BUTTON

#include "ui_nav_router.h"
#include "ui_nav_events.h"
#include "palm_detection.h"
#include "yoloface_detection.h"
#include "hand_gesture_detection.h"
#include "demo/car_tracking.h"

#include <key_adapter.h>
#include <components/log.h>

#define TAG "ui_key_bridge"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

static bool ui_key_overlay_demo_active(void)
{
    return palm_detection_is_active() || yoloface_detection_is_active()
        || hand_gesture_detection_is_active() || car_detection_is_active();
}

static bool ui_key_overlay_exit_event(key_event_t event)
{
    switch (event) {
#if CONFIG_ADC_KEY
    case ADC_KEY_S4_DOUBLE:
        return true;
#endif
    default:
        return false;
    }
}

static void ui_key_overlay_exit(void)
{
    if (palm_detection_is_active()) {
        LOGI("exit palm tracking overlay\r\n");
        (void)palm_detection_exit_to_menu();
        return;
    }
    if (yoloface_detection_is_active()) {
        LOGI("exit yoloface detection overlay\r\n");
        (void)yoloface_detection_exit_to_menu();
        return;
    }
    if (hand_gesture_detection_is_active()) {
        LOGI("exit hand gesture overlay\r\n");
        (void)hand_gesture_detection_exit_to_menu();
        return;
    }
    if (car_detection_is_active()) {
        LOGI("exit car tracking overlay\r\n");
        (void)car_detection_exit_to_menu();
    }
}

void bk_key_app_notify_ui_nav(uint8_t event)
{
    key_event_t key = (key_event_t)event;

    if (ui_key_overlay_demo_active()) {
        if (ui_key_overlay_exit_event(key)) {
            ui_key_overlay_exit();
        }
        return;
    }

    ui_nav_event_t nav = UI_NAV_EVENT_COUNT;

    switch (key) {
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
