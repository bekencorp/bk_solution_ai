#pragma once

#ifdef __cplusplus
extern "C" {
#endif


#include <key_main.h>
#include <key_adapter.h>

#if CONFIG_ROBOT_V2_ADC_KEYS
typedef enum {
    ROBOT_ADC_KEY_S2_SHORT = USER_EVENT_START,
    ROBOT_ADC_KEY_S2_LONG,
    ROBOT_ADC_KEY_S3_SHORT,
    ROBOT_ADC_KEY_S3_LONG,
    ROBOT_ADC_KEY_S4_SHORT,
    ROBOT_ADC_KEY_S4_LONG,
    ROBOT_ADC_KEY_S5_SHORT,
    ROBOT_ADC_KEY_S5_LONG,
} robot_adc_key_event_t;
#endif


void bk_key_register_wakeup_source(void);

void bk_key_service_init(void);

#if CONFIG_LVGL && CONFIG_BUTTON
/**
 * @brief 将物理按键事件转发给产品 UI（弱符号，默认空实现；产品可在 ui_key_bridge 等中提供强符号）
 */
void bk_key_app_notify_ui_nav(uint8_t event);
#endif

void volume_init(void);
#ifdef __cplusplus
}
#endif