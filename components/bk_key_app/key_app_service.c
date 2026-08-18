#include <common/sys_config.h>
#include <components/log.h>
#include <string.h>

#include <key_app_service.h>
#include <key_app_config.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#if CONFIG_ADC_KEY
#include "adc_key_main.h"
#endif
#if (0)
#include "audio_config.h"
#include "aud_intf.h"
#include "bk_factory_config.h"
#if CONFIG_BK_SMART_CONFIG
#include "bk_genie_comm.h"
#include "bk_smart_config.h"
#endif
#endif
#if (CONFIG_USR_KEY_CFG_EN)
#include "usr_key_cfg.h"
#endif

#include "app_event.h"
#include <common/bk_include.h>
#include "components/bluetooth/bk_dm_bluetooth.h"

#if (CONFIG_BK_WSS_TRANS || CONFIG_BK_WSS_TRANS_NOPSRAM)
#include "bk_wss/bk_wss.h"
#endif

#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#if CONFIG_BK_AUDIO_ENGINE
#include "audio_engine.h"
#endif

#include "bk_factory_config.h"

#define TAG "key_service"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#if CONFIG_BUTTON

static void power_off(void)
{
    LOGI(" power_off\r\n");
    LOGW(" ************TODO:Just force deep sleep for Demo!\r\n");
    //extern bk_err_t audio_turn_off(void);
    //extern bk_err_t video_turn_off(void);
    //audio_turn_off();
    //video_turn_off();
    bk_reboot_ex(RESET_SOURCE_FORCE_DEEPSLEEP);
}

static void power_on(void)
{
    LOGI("power_on\r\n");
}

static void ai_agent_config(void)
{
    //BK_LOGW(TAG, " ************TODO:AI Agent doesn't complete!\r\n");
}

#if !CONFIG_ADC_KEY
static KeyConfig_t key_config[] = KEY_DEFAULT_CONFIG_TABLE;
#endif

static void handle_system_event(uint8_t event);

#if CONFIG_ROBOT_V2_ADC_KEYS
typedef struct {
    const char *name;
    uint8_t short_event;
    uint8_t long_event;
} robot_adc_key_context_t;

typedef struct {
    adc_key_id_t key_id;
    gpio_id_t gpio_id;
    adc_chan_t adc_chan;
    uint16_t voltage_low_mv;
    uint16_t voltage_high_mv;
    robot_adc_key_context_t context;
} robot_adc_key_config_t;

static const robot_adc_key_config_t s_robot_adc_keys[] = {
    {
        .key_id = 2U,
        .gpio_id = (gpio_id_t)CONFIG_ROBOT_KEY1_ADC_GPIO,
        .adc_chan = (adc_chan_t)CONFIG_ROBOT_KEY1_ADC_CHAN,
        .voltage_low_mv = CONFIG_ROBOT_KEY_S2_VOLTAGE_LOW,
        .voltage_high_mv = CONFIG_ROBOT_KEY_S2_VOLTAGE_HIGH,
        .context = {
            .name = "S2",
            .short_event = ROBOT_ADC_KEY_S2_SHORT,
            .long_event = ROBOT_ADC_KEY_S2_LONG,
        },
    },
    {
        .key_id = 3U,
        .gpio_id = (gpio_id_t)CONFIG_ROBOT_KEY1_ADC_GPIO,
        .adc_chan = (adc_chan_t)CONFIG_ROBOT_KEY1_ADC_CHAN,
        .voltage_low_mv = CONFIG_ROBOT_KEY_S3_VOLTAGE_LOW,
        .voltage_high_mv = CONFIG_ROBOT_KEY_S3_VOLTAGE_HIGH,
        .context = {
            .name = "S3",
            .short_event = ROBOT_ADC_KEY_S3_SHORT,
            .long_event = ROBOT_ADC_KEY_S3_LONG,
        },
    },
    {
        .key_id = 4U,
        .gpio_id = (gpio_id_t)CONFIG_ROBOT_KEY2_ADC_GPIO,
        .adc_chan = (adc_chan_t)CONFIG_ROBOT_KEY2_ADC_CHAN,
        .voltage_low_mv = CONFIG_ROBOT_KEY_S4_VOLTAGE_LOW,
        .voltage_high_mv = CONFIG_ROBOT_KEY_S4_VOLTAGE_HIGH,
        .context = {
            .name = "S4",
            .short_event = ROBOT_ADC_KEY_S4_SHORT,
            .long_event = ROBOT_ADC_KEY_S4_LONG,
        },
    },
    {
        .key_id = 5U,
        .gpio_id = (gpio_id_t)CONFIG_ROBOT_KEY2_ADC_GPIO,
        .adc_chan = (adc_chan_t)CONFIG_ROBOT_KEY2_ADC_CHAN,
        .voltage_low_mv = CONFIG_ROBOT_KEY_S5_VOLTAGE_LOW,
        .voltage_high_mv = CONFIG_ROBOT_KEY_S5_VOLTAGE_HIGH,
        .context = {
            .name = "S5",
            .short_event = ROBOT_ADC_KEY_S5_SHORT,
            .long_event = ROBOT_ADC_KEY_S5_LONG,
        },
    },
};

static adc_key_handle_t s_robot_adc_key_handles[
    sizeof(s_robot_adc_keys) / sizeof(s_robot_adc_keys[0])];

static void robot_adc_key_short_cb(void *user_data)
{
    const robot_adc_key_context_t *context =
        (const robot_adc_key_context_t *)user_data;
    LOGI("ADC KEY %s short press\r\n", context->name);
    handle_system_event(context->short_event);
}

static void robot_adc_key_long_cb(void *user_data)
{
    const robot_adc_key_context_t *context =
        (const robot_adc_key_context_t *)user_data;
    LOGI("ADC KEY %s long press\r\n", context->name);
    handle_system_event(context->long_event);
}

static bk_err_t robot_adc_keys_init(void)
{
    adc_key_driver_config_t driver_config = {
        .size = sizeof(adc_key_driver_config_t),
        .version = ADC_KEY_CONFIG_VERSION,
        .sample_period_ms = CONFIG_ADC_KEY_SAMPLE_PERIOD_MS,
        .max_channels = 2U,
        .max_items = (uint8_t)(sizeof(s_robot_adc_keys) /
                               sizeof(s_robot_adc_keys[0])),
    };
    bk_err_t ret = bk_adc_key_init_ex(&driver_config);

    if (ret != BK_OK) {
        LOGE("ADC key manager init failed: %d\r\n", ret);
        return ret;
    }

    for (uint8_t i = 0U;
         i < (sizeof(s_robot_adc_keys) / sizeof(s_robot_adc_keys[0]));
         i++) {
        const robot_adc_key_config_t *key = &s_robot_adc_keys[i];
        adc_key_item_config_ex_t item_config = {
            .size = sizeof(adc_key_item_config_ex_t),
            .version = ADC_KEY_CONFIG_VERSION,
            .key_id = key->key_id,
            .gpio_id = key->gpio_id,
            .adc_chan = key->adc_chan,
            .lowest_level = key->voltage_low_mv,
            .highest_level = key->voltage_high_mv,
            .short_press_cb = robot_adc_key_short_cb,
            .double_press_cb = NULL,
            .long_press_cb = robot_adc_key_long_cb,
            .hold_press_cb = NULL,
            .user_data = (void *)&key->context,
        };

        ret = bk_adc_key_item_configure_ex(
            &item_config, &s_robot_adc_key_handles[i]);
        if (ret != BK_OK) {
            LOGE("ADC key %s register failed: %d\r\n",
                 key->context.name, ret);
            (void)bk_adc_key_deinit_ex();
            return ret;
        }
        LOGI("ADC key %s: gpio=%d chan=%d range=%d~%dmV\r\n",
             key->context.name, key->gpio_id, key->adc_chan,
             key->voltage_low_mv, key->voltage_high_mv);
    }

    LOGI("Robot V2 ADC keys initialized: 2 channels, 4 keys\r\n");
    return BK_OK;
}
#endif

/*Do not execute blocking or time-consuming long code in event handler
 functions. The reason is that key_thread processes messages in a
 single task in sequence. If a handler function blocks or takes too
 long to execute, it will cause subsequent key events to be responded to untimely.*/

static void handle_system_event(uint8_t event)
{
    if (IS_INVALID_EVENT(event))
    {
        LOGI("Invalid event: %d\r\n", event);
        return;
    }

    uint32_t time;

    switch (event)
    {
        case VOLUME_UP:
        {
            LOGI("VOLUME_UP\r\n");
            audio_engine_volume_increase();
        }
        break;
        case VOLUME_DOWN:
        {
            LOGI("VOLUME_DOWN\r\n");
            audio_engine_volume_decrease();
        }
        break;
        case SHUT_DOWN:
            time = rtos_get_time(); //long press more than 6s
            if (time < 9000)
            {
                break;
            }
            power_off();
            break;
        case POWER_ON:
            power_on();
            break;
        case AI_AGENT_CONFIG:
            LOGI("AI_AGENT_CONFIG\r\n");
            // ai_agent_config();
            break;
        case CONFIG_NETWORK:
            LOGI("CONFIG_NETWORK\r\n");
            #if CONFIG_BK_SMART_CONFIG
            bk_sconf_prepare_for_smart_config();
            #endif
            break;
        case IR_MODE_SWITCH:	////image recognition mode switch
            LOGI("IR_MODE_SWITCH\r\n");
            app_event_send_msg(APP_EVT_IR_MODE_SWITCH, 0);
            break;
        case FACTORY_RESET:
            LOGI("trigger factory config reset\r\n");
            bk_pm_module_vote_power_ctrl(PM_POWER_MODULE_NAME_BTSP, PM_POWER_MODULE_STATE_OFF);//power off btsp
            bk_factory_reset();
            #if CONFIG_BK_SMART_CONFIG
            bk_sconf_erase_smart_config();
            #endif
            bk_reboot();
            break;
#if CONFIG_BK_WSS_TRANS_NOPSRAM
        case AUDIO_BUF_APPEND:
            LOGI("audio append start!\r\n");
            app_event_send_msg(APP_EVT_ASR_WAKEUP, 0);
            bk_wss_state_event(WSS_EVENT_RECORDING_START, NULL);
            break;
#endif
#if CONFIG_ADC_KEY
#if CONFIG_ROBOT_V2_ADC_KEYS
        case ROBOT_ADC_KEY_S2_SHORT:
        case ROBOT_ADC_KEY_S2_LONG:
        case ROBOT_ADC_KEY_S3_SHORT:
        case ROBOT_ADC_KEY_S3_LONG:
        case ROBOT_ADC_KEY_S4_SHORT:
        case ROBOT_ADC_KEY_S4_LONG:
        case ROBOT_ADC_KEY_S5_SHORT:
        case ROBOT_ADC_KEY_S5_LONG:
            LOGI("Robot V2 ADC key event: %u\r\n", event);
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
#else
        case ADC_KEY_S4_SHORT:
            LOGI("ADC_KEY_S4_SHORT\r\n");
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
        case ADC_KEY_S4_DOUBLE:
            LOGI("ADC_KEY_S4_DOUBLE\r\n");
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
        case ADC_KEY_S4_LONG:
            LOGI("ADC_KEY_S4_LONG\r\n");
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
        case ADC_KEY_S5_SHORT:
            LOGI("ADC_KEY_S5_SHORT\r\n");
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
        case ADC_KEY_S5_DOUBLE:
            LOGI("ADC_KEY_S5_DOUBLE\r\n");
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
        case ADC_KEY_S5_LONG:
            LOGI("ADC_KEY_S5_LONG\r\n");
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
        case GPIO_KEY1_ANY_SHORT:
            LOGI("GPIO_KEY1_ANY_SHORT\r\n");
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
        case GPIO_KEY1_ANY_DOUBLE:
            LOGI("GPIO_KEY1_ANY_DOUBLE\r\n");
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
        case GPIO_KEY1_ANY_LONG:
            LOGI("GPIO_KEY1_ANY_LONG\r\n");
#if CONFIG_LVGL
            bk_key_app_notify_ui_nav(event);
#endif
            break;
#endif
#endif
        default:
            break;
    }
}

#if CONFIG_GPIO_WAKEUP_SOURCE_ENABLE
void bk_key_register_wakeup_source(void)
{
    for (uint8_t i = 0; i < sizeof(key_config) / sizeof(KeyConfig_t); i++)
    {
        if ((key_config[i].short_event == POWER_ON) || (key_config[i].double_event == POWER_ON) || (key_config[i].long_event == POWER_ON))
        {
            if (key_config[i].active_level == LOW_LEVEL_TRIGGER)
            {
                bk_gpio_register_wakeup_source(key_config[i].gpio_id, GPIO_INT_TYPE_FALLING_EDGE);
                LOGI("register wakeup source %d falling edge\r\n", key_config[i].gpio_id);
            }
            else
            {
                bk_gpio_register_wakeup_source(key_config[i].gpio_id, GPIO_INT_TYPE_RISING_EDGE);
                LOGI("register wakeup source %d rising edge\r\n", key_config[i].gpio_id);
            }
        }
    }
}
#endif

void bk_key_service_init(void)
{
#if CONFIG_ADC_KEY
#if CONFIG_ROBOT_V2_ADC_KEYS
    (void)robot_adc_keys_init();
#else
    bk_all_keys_init(handle_system_event);
#endif
#else
    bk_key_register_event_handler(handle_system_event);
    bk_key_driver_init(key_config, sizeof(key_config) / sizeof(KeyConfig_t));
#endif
}

#if CONFIG_LVGL
__attribute__((weak))
void bk_key_app_notify_ui_nav(uint8_t event)
{
    (void)event;
}
#endif

#endif
