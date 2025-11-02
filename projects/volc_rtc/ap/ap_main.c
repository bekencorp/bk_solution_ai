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

#if CONFIG_NET_PAN
#include "bluetooth_storage.h"
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

extern uint32_t bk_misc_get_ap_reset_reason(void);
extern uint32_t bk_misc_get_cp_reset_reason(void);

static const uint32_t s_user_value2 = 10;

const struct factory_config_t s_user_config[] = {
    {"user_key1", (void *)"user_value1", 11, BK_FALSE, 0},
    {"user_key2", (void *)&s_user_value2, 4, BK_TRUE, 4},
};

/*Different hardware designs, different GPIOs control the LDO. Customers can adjust the following GPIO unmapping code */
static bk_err_t app_force_ldo_gpio_close(void)
{
    /*usb/psram ldo ctrl at deepsleep last location*/
    /*ldo*/
    gpio_dev_unmap(GPIO_50);
    gpio_dev_unmap(GPIO_52);

    /*UART*/
    gpio_dev_unmap(GPIO_10);
    gpio_dev_unmap(GPIO_11);

    /*I2C*/
    gpio_dev_unmap(GPIO_0);
    gpio_dev_unmap(GPIO_1);

    /*MOTO*/
    gpio_dev_unmap(GPIO_9);

    return 0;
}
static void bk_enter_deepsleep(void)
{
#if CONFIG_GSENSOR_ENABLE
    extern int gsensor_enter_sleep_config();
    gsensor_enter_sleep_config();
    rtos_delay_milliseconds(10);
#endif

    BK_LOGI(TAG,"RESET_SOURCE_FORCE_DEEPSLEEP\r\n");
    bk_key_register_wakeup_source();
    app_force_ldo_gpio_close();
    rtos_delay_milliseconds(100);
    bk_pm_ap_sleep_mode_set(PM_MODE_FORCE_DEEP_SLEEP);
    rtos_delay_milliseconds(10);
}

static void bk_wait_power_on(void)
{
    uint32_t press_time = 0;

    GLOBAL_INT_DECLARATION();
    GLOBAL_INT_DISABLE();
    do {
        if (bk_gpio_get_input(KEY_GPIO_12) == 0) {
            extern void delay_ms(uint32 num);
            delay_ms(500);
            press_time += 500;

            if (bk_gpio_get_input(KEY_GPIO_12) != 0) {
                break;
            }
        } else {

            break;
        }
    } while (press_time < LONG_RRESS_TIMR);
    GLOBAL_INT_RESTORE();

    if (press_time < LONG_RRESS_TIMR)
    {
        bk_enter_deepsleep();
    }
}

int main(void)
{

    if (bk_misc_get_ap_reset_reason() != RESET_SOURCE_FORCE_DEEPSLEEP)
    {
        bk_init();

        media_service_init();

    #ifdef CONFIG_LDO3V3_ENABLE
        BK_LOG_ON_ERR(gpio_dev_unmap(LDO3V3_CTRL_GPIO));
        bk_gpio_disable_pull(LDO3V3_CTRL_GPIO);
        bk_gpio_enable_output(LDO3V3_CTRL_GPIO);
        bk_gpio_set_output_high(LDO3V3_CTRL_GPIO);
    #endif

    if(bk_misc_get_cp_reset_reason() == RESET_SOURCE_DEEPPS_GPIO && (bk_gpio_get_wakeup_gpio_id() == KEY_GPIO_12))
    {
        //motor vibration
    #if CONFIG_MOTOR
        motor_open(PWM_MOTOR_CH_3);
    #endif

        bk_wait_power_on();

    #if CONFIG_MOTOR
        motor_close(PWM_MOTOR_CH_3);
    #endif
    }

    bk_regist_factory_user_config((const struct factory_config_t *)&s_user_config,
                                    sizeof(s_user_config)/sizeof(s_user_config[0]));
    bk_factory_init();

    #if CONFIG_LED_BLINK
        led_driver_init();
        led_app_set(LED_ON_GREEN,LED_LAST_FOREVER);
    #endif

    #if (CONFIG_NFC_ENABLE)
        void nfc_get_id_task(void);
        nfc_get_id_task();
    #endif

    #if CONFIG_BK_AUDIO_ENGINE
        audio_engine_init();
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

    //Notice need to wait other services to initialize
    #if CONFIG_BUTTON
        bk_key_service_init();
    #endif

    #if CONFIG_BAT_MONITOR
        extern void battery_monitor_init(void);
        battery_monitor_init();
    #endif
    }
    else
    {
        bk_init();
        bk_enter_deepsleep();
    }

        return 0;

}
