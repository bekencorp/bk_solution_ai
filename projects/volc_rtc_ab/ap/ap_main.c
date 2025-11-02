#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#include <media_service.h>
#include "sys_driver.h"
#include "sys_hal.h"

#if CONFIG_BK_NETWORK_TRANSFER
#include "network_transfer.h"
#endif

#if CONFIG_BK_AUDIO_ENGINE
#include "audio_engine.h"
#endif

#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#if CONFIG_APP_EVT
#include "app_event.h"
#endif

#if CONFIG_BUTTON
#include <key_app_service.h>
#endif

#include "bk_factory_config.h"

#if CONFIG_LED_BLINK
#include "led_blink.h"
#endif

#if CONFIG_MOTOR
#include "motor.h"
#endif

#define TAG "ap_main"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#ifdef CONFIG_LDO3V3_ENABLE
#ifndef LDO3V3_CTRL_GPIO
#ifdef CONFIG_LDO3V3_CTRL_GPIO
#define LDO3V3_CTRL_GPIO    CONFIG_LDO3V3_CTRL_GPIO
#else
#define LDO3V3_CTRL_GPIO    GPIO_52
#endif
#endif
#endif

int main(void)
{
    bk_init();

#if CONFIG_GSENSOR_ENABLE
    extern bk_err_t gsensor_demo_open();
    gsensor_demo_open();
    extern bk_err_t gsensor_demo_lowpower_wakeup();
    gsensor_demo_lowpower_wakeup();
#endif

    media_service_init();

#ifdef CONFIG_LDO3V3_ENABLE
    BK_LOG_ON_ERR(gpio_dev_unmap(LDO3V3_CTRL_GPIO));
    bk_gpio_disable_pull(LDO3V3_CTRL_GPIO);
    bk_gpio_enable_output(LDO3V3_CTRL_GPIO);
    bk_gpio_set_output_high(LDO3V3_CTRL_GPIO);
#endif


#if (CONFIG_NFC_ENABLE)
    void nfc_get_id_task(void);
    nfc_get_id_task();
#endif

    bk_factory_init();

#if CONFIG_LED_BLINK
    led_driver_init();
    led_app_set(LED_ON_GREEN,LED_LAST_FOREVER);
#endif
    
#if CONFIG_APP_EVT
    app_event_init();
#endif

#if CONFIG_BK_NETWORK_TRANSFER
    ntwk_trans_init();
#endif

#if CONFIG_BK_SMART_CONFIG
    bk_sconf_init();
#endif

#if CONFIG_BK_AUDIO_ENGINE
    audio_engine_init();
#endif

//Notice need to wait other services to initialize
#if CONFIG_BUTTON
    bk_key_service_init();
#endif

    return 0;
}
