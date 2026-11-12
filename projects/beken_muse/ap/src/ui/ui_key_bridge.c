/**
 * @file ui_key_bridge.c
 * @brief Map physical key_event_t -> ui_nav_event_t and invoke
 *        ui_nav_dispatch_event.
 *
 * ## Physical key -> navigation semantics (Robot V2 dual ADC channels)
 *
 * | key_adapter event   | Mapped ui_nav_event_t     | Notes                              |
 * |---------------------|---------------------------|------------------------------------|
 * | ROBOT_ADC_KEY_S2_SHORT  | UI_NAV_EVENT_FOCUS_NEXT   | Next focus item                |
 * | ROBOT_ADC_KEY_S3_SHORT  | UI_NAV_EVENT_SCREEN_PREV  | Return / previous screen       |
 * | ROBOT_ADC_KEY_S4_SHORT  | UI_NAV_EVENT_SCREEN_NEXT  | Next screen                    |
 * | ROBOT_ADC_KEY_S4_LONG   | UI_NAV_EVENT_CONFIRM_LONG | Long-press confirm             |
 * | ROBOT_ADC_KEY_S5_SHORT  | UI_NAV_EVENT_FOCUS_PREV   | Previous focus item            |
 *
 * Unmapped events (double-click, etc.) are ignored for now -- extend
 * the switch below as needed.
 *
 * Overlay demos (camera preview) intercept nav events through their menu
 * on_nav_intercept hook instead of this bridge; see demo_catalog.c.
 */
#include <common/sys_config.h>

#if CONFIG_LVGL && CONFIG_BUTTON

#include "ui_nav_router.h"
#include "ui_nav_events.h"

#include <key_app_service.h>
#include <components/log.h>
#if CONFIG_MUSE_GADGET
#include "muse_service.h"
#endif
#if CONFIG_MUSE_GADGET && CONFIG_MUSE_GADGET_VOICE
#include "muse_voice.h"
#include "ui_theme.h"
#include "ui_i18n.h"
#include "camera_preview.h"
#endif

#define TAG "ui_key_bridge"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

static ui_nav_event_t ui_key_to_nav_event(uint8_t key)
{
    ui_nav_event_t nav = UI_NAV_EVENT_COUNT;

    switch (key) {
#if CONFIG_ROBOT_V2_ADC_KEYS
    case ROBOT_ADC_KEY_S2_SHORT:
        nav = UI_NAV_EVENT_FOCUS_NEXT;
        break;
    case ROBOT_ADC_KEY_S3_SHORT:
        nav = UI_NAV_EVENT_SCREEN_PREV;
        break;
    case ROBOT_ADC_KEY_S5_SHORT:
        nav = UI_NAV_EVENT_FOCUS_PREV;
        break;
    case ROBOT_ADC_KEY_S4_SHORT:
        nav = UI_NAV_EVENT_SCREEN_NEXT;
        break;
    case ROBOT_ADC_KEY_S4_LONG:
        nav = UI_NAV_EVENT_CONFIRM_LONG;
        break;
#elif CONFIG_ADC_KEY
    case ADC_KEY_S5_SHORT:
        nav = UI_NAV_EVENT_FOCUS_PREV;
        break;
    case GPIO_KEY1_ANY_SHORT:
        nav = UI_NAV_EVENT_FOCUS_NEXT;
        break;
    case ADC_KEY_S4_SHORT:
        nav = UI_NAV_EVENT_SCREEN_NEXT;
        break;
    case ADC_KEY_S4_LONG:
        nav = UI_NAV_EVENT_CONFIRM_LONG;
        break;
#endif
    default:
        break;
    }

    return nav;
}

void bk_key_app_notify_ui_nav(uint8_t event)
{
#if CONFIG_MUSE_GADGET && CONFIG_ROBOT_V2_ADC_KEYS
    muse_service_status_t muse_status;
    muse_service_get_status(&muse_status);
    if (event == ROBOT_ADC_KEY_S4_SHORT &&
        muse_status.state == MUSE_SERVICE_WAITING_FOR_CONFIRMATION) {
        LOGI("S4 confirms pending Muse pairing\r\n");
        (void)muse_service_confirm_pairing();
        return;
    }
#endif
#if CONFIG_MUSE_GADGET && CONFIG_MUSE_GADGET_VOICE && CONFIG_ROBOT_V2_ADC_KEYS
    /* Push-to-talk: S4 long-press toggles record on/off (the ADC key model
     * has no release event, so hold-to-talk maps poorly; toggle it is).
     * The key is always consumed here: connected -> record toggle; offline
     * -> a transient popup tells the user why nothing happens (previously
     * it silently fell through to the long-confirm navigation, looking
     * like a dead key). */
    if (event == ROBOT_ADC_KEY_S4_LONG) {
        if (muse_status.state == MUSE_SERVICE_CONNECTED) {
            int rc = muse_voice_ptt_toggle();
            LOGI("S4 PTT toggle -> %d (recording=%u)\r\n", rc,
                 (unsigned)muse_voice_recording());
        } else if (!camera_preview_is_active()) {
            /* LVGL is stopped while the camera preview owns the panel;
             * a popup there would never render. */
            LOGI("S4 PTT ignored: muse state=%s\r\n",
                 muse_service_state_name(muse_status.state));
            ui_theme_create_popup(ui_i18n_get_lang() == UI_LANG_ZH ?
                                  "Muse 未连接" : "Muse not connected");
        }
        return;
    }
#endif
    ui_nav_event_t nav = ui_key_to_nav_event(event);

    if (nav >= UI_NAV_EVENT_COUNT) {
        return;
    }

    LOGI("key %u -> nav %d\r\n", (unsigned)event, (int)nav);
    ui_nav_dispatch_event(nav);
}

#endif /* CONFIG_LVGL && CONFIG_BUTTON */
