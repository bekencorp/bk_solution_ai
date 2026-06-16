#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#include <media_service.h>
#include <components/bk_frame_buffer.h>
#include "media_devices.h"
#include <avdk_check.h>
#include <avdk_error.h>
#include <common/avdk_pixel_types.h>
#if CONFIG_LVGL
#include "lvgl.h"
#include "lv_vendor.h"
#include "beken_ui.h"
#include "ui_nav_router.h"
#include "ui_overlay_swipe.h"
#include "custom_func.h"
#include "demo/demo_registry.h"
#include "page_hooks.h"
#include <common/avdk_pixel_types.h>
#include <components/bk_display.h>
#endif
#include "driver/drv_tp.h"

#if CONFIG_BK_AUDIO_ENGINE
#include "audio_engine.h"
#endif

#if CONFIG_BK_VIDEO_ENGINE
#include "video_engine.h"
#if CONFIG_USB_CAMERA && CONFIG_VIDEO_ENGINE_USE_UVC_CAMERA
#include "devices_mgmt.h"
#endif
#if CONFIG_VIDEO_ENGINE_USE_DVP_CAMERA
#include "app_camera.h"
#include "app_camera_types.h"
#endif
#endif

#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif

#if CONFIG_BK_ROBOT_LAN_NET
#include "robot_lan_net.h"
#endif

#if CONFIG_BK_ROBOT_VIDEO_SERVICE
#include "robot_video_service.h"
#endif

#if CONFIG_BK_ROBOT_CTRL_SERVICE
#include "robot_ctrl_service.h"
#endif

#if CONFIG_APP_EVT
#include "app_event.h"
#endif

#if CONFIG_BUTTON
#include <key_app_service.h>
#endif

#include "bk_factory_config.h"

#include "board_usb_switch.h"
#include "camera_preview.h"

#if CONFIG_LVGL
#include "wifi_status_ui.h"
#endif

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

extern const bk_display_dsi_panel_t lcd_device_jd9855_mipi_360x390;
#define DEFAULT_MIPI_PANEL (&lcd_device_jd9855_mipi_360x390)

#if CONFIG_LVGL
#define LVGL_DISP_WIDTH  360
#define LVGL_DISP_HEIGHT 390

static bk_display_ctlr_handle_t s_lvgl_dpu_handle = NULL;

static void bk_robot_lvgl_flush_cb(void *args, void *frame_buffer, int (*cb)(void *args))
{
    bk_display_ctlr_handle_t *slot = (bk_display_ctlr_handle_t *)args;

    /* Panel resume/reopen replaces the DPU controller, so refresh per flush. */
    bk_display_ctlr_handle_t dpu = media_panel_get_dpu_handle();
    if (slot != NULL) {
        *slot = dpu;
    }

    if (dpu == NULL) {
        if (cb != NULL) {
            cb(frame_buffer);
        }
        return;
    }
    bk_display_flush(dpu, frame_buffer, cb);
}

static bk_err_t bk_robot_lvgl_init(bk_display_ctlr_handle_t dpu_handle)
{
    lv_vnd_config_t cfg = {0};

    cfg.width = LVGL_DISP_WIDTH;
    cfg.height = LVGL_DISP_HEIGHT;
    cfg.render_mode = RENDER_PARTIAL_MODE;
    cfg.rotation = ROTATE_90;
    cfg.frame_buffer[0] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED,
                              LVGL_DISP_WIDTH * LVGL_DISP_HEIGHT * sizeof(lv_color_t));
    cfg.frame_buffer[1] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                              LVGL_DISP_WIDTH * LVGL_DISP_HEIGHT * sizeof(lv_color_t));
    if (!cfg.frame_buffer[0] || !cfg.frame_buffer[1]) {
        LOGE("LVGL frame buffer alloc failed\n");
        return BK_FAIL;
    }
    s_lvgl_dpu_handle = dpu_handle;
    cfg.args = &s_lvgl_dpu_handle;
    cfg.flush_cb = bk_robot_lvgl_flush_cb;

    ui_overlay_swipe_set_display_transform(cfg.width, cfg.height, cfg.rotation);
    lv_vendor_init(&cfg);

#if (CONFIG_TP)
    drv_tp_open(LVGL_DISP_WIDTH, LVGL_DISP_HEIGHT, TP_MIRROR_NONE);
#endif

    lv_vendor_disp_lock();
    ui_nav_router_init();
    lv_vendor_disp_unlock();

    /* NOTE: do NOT call beken_ui_init() here. beken_ui_init() creates
     * page_1 and fires its on_page_init hook, which must be registered
     * first. We defer it to bk_robot_lvgl_load_first_page() (called
     * later in main() after bk_pages_init_all_hooks() + wifi_status_ui_init()). */

    LOGI("LVGL ready on %dx%d MIPI (first page pending)\n",
         LVGL_DISP_WIDTH, LVGL_DISP_HEIGHT);
    return BK_OK;
}

static void bk_robot_lvgl_load_first_page(void)
{
    lv_vendor_disp_lock();
    beken_ui_init();
    lv_vendor_disp_unlock();

    lv_vendor_start();

    LOGI("LVGL started, page_1 loaded\n");
}

bk_err_t bk_robot_lvgl_resume_display(void)
{
    (void)media_lcd_panel_close();

    avdk_err_t ret = media_lcd_panel_open(DEFAULT_MIPI_PANEL, BK_PIXEL_FORMAT_RGB565, false);
    if (ret != AVDK_ERR_OK) {
        LOGE("bk_robot_lvgl_resume_display: media_lcd_panel_open failed %d\n", ret);
        return BK_FAIL;
    }

    bk_display_ctlr_handle_t new_handle = media_panel_get_dpu_handle();
    if (new_handle == NULL) {
        LOGE("bk_robot_lvgl_resume_display: dpu handle NULL after open\n");
        return BK_FAIL;
    }

    s_lvgl_dpu_handle = new_handle;
    LOGI("bk_robot_lvgl_resume_display: dpu handle refreshed %p\n", new_handle);

    return BK_OK;
}
#endif

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
#if 0
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
#endif
int main(void)
{

    //if (bk_misc_get_ap_reset_reason() != RESET_SOURCE_FORCE_DEEPSLEEP)
    {
        bk_init();

        media_service_init();
        bk_frame_buffer_init();
        media_board_power_on(); //power on ai board peripherals
#if CONFIG_LVGL
        AVDK_RETURN_ON_ERROR(media_lcd_panel_open(DEFAULT_MIPI_PANEL, BK_PIXEL_FORMAT_RGB565, false), TAG, "media lcd panel open error");
        AVDK_RETURN_ON_ERROR(bk_robot_lvgl_init(media_panel_get_dpu_handle()), TAG, "bk robot lvgl init error");
#else
        //AVDK_RETURN_ON_ERROR(media_lcd_panel_open(DEFAULT_MIPI_PANEL, BK_PIXEL_FORMAT_ARGB8888, true), TAG, "media lcd panel open error");
        AVDK_RETURN_ON_ERROR(media_camera_open(1280, 720, 25, 400, 368), TAG, "media camera open error");
        //AVDK_RETURN_ON_ERROR(media_gpu_open(400, 368, 90), TAG, "media gpu open error");  //open gpu will display camera image on lcd
        AVDK_RETURN_ON_ERROR(media_h264_encoder_start(), TAG, "media h264 encoder start error");
        AVDK_RETURN_ON_ERROR(media_test_thread_start(MEDIA_TEST_MODE_H264_WIFI_TX), TAG, "media test thread start error");
#endif

    #ifdef CONFIG_LDO3V3_ENABLE
        BK_LOG_ON_ERR(gpio_dev_unmap(LDO3V3_CTRL_GPIO));
        bk_gpio_disable_pull(LDO3V3_CTRL_GPIO); 
        bk_gpio_enable_output(LDO3V3_CTRL_GPIO);
        bk_gpio_set_output_high(LDO3V3_CTRL_GPIO);
    #endif

    // if(bk_misc_get_cp_reset_reason() == RESET_SOURCE_DEEPPS_GPIO && (bk_gpio_get_wakeup_gpio_id() == KEY_GPIO_12))
    // {
    //     //motor vibration
    // #if CONFIG_MOTOR
    //     motor_open(PWM_MOTOR_CH_3);
    // #endif

    //     bk_wait_power_on();

    // #if CONFIG_MOTOR
    //     motor_close(PWM_MOTOR_CH_3);
    // #endif
    // }

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

    #if CONFIG_LVGL
        wifi_status_ui_init();
        /* Order matters:
         *   1) wifi_status_ui_init() installs the WiFi icon page_top
         *      init hook first, so it always runs ahead of the menu
         *      hook (which would otherwise overwrite the icon state).
         *   2) bk_pages_init_all_hooks() registers all per-page UI
         *      callbacks defined under beken_generated/page_<feature>/.
         *   3) bk_robot_lvgl_load_first_page() materializes page_1
         *      (splash) AFTER hooks are in place, so the splash hook
         *      actually fires and its nav_ops gets registered.
         *   4) bk_demos_init_all() wires the LVGL-free demo backends
         *      (CLI registration, app_event subscriptions, etc.). */
        bk_pages_init_all_hooks();
        bk_robot_lvgl_load_first_page();
        ui_nav_router_cli_init();
        (void)bk_demos_init_all();
    #endif

        (void)board_usb_switch_init();

        /* Debug-only: power on SD-NAND and mount the FATFS volume once
         * for the whole app lifetime. Each camera_preview_take_photo()
         * then persists its HW-encoded JPEG with just mkdir + write
         * (no per-shot power_on / SDIO enum / FAT scan tax). The
         * matching unmount + power_off is intentionally omitted -- see
         * camera_preview_sdnand_debug_init() doc for the LCD-blacks-out
         * regression that drove this decision. Toggle off in defconfig
         * (CONFIG_CAM_PREVIEW_SDNAND_DEBUG=0) for production builds. */
        (void)camera_preview_sdnand_debug_init();

    #if CONFIG_BK_SMART_CONFIG
        bk_sconf_init();
    #endif

    #if CONFIG_BK_ROBOT_LAN_NET
        robot_lan_net_init();
    #endif

    #if CONFIG_BK_ROBOT_VIDEO_SERVICE
        robot_video_service_init();
    #endif

    #if CONFIG_BK_ROBOT_CTRL_SERVICE
        robot_ctrl_service_init();
    #endif

    //Notice need to wait other services to initialize
    #if CONFIG_BUTTON
        bk_key_service_init();
    #endif

    /* The page_5 `arrow` / `eyes` debug CLIs are registered on demand
     * by sound_localization_init() / page_doa_init_hooks(); ap_main
     * no longer registers them directly. */

    #if CONFIG_BAT_MONITOR
        extern void battery_monitor_init(void);
        battery_monitor_init();
    #endif

    #if CONFIG_USBD_MSC
        extern void msc_storage_init(void);
        msc_storage_init();
    #endif
    }
    // else
    // {
    //     bk_init();
    //     bk_enter_deepsleep();
    // }
    return 0;
}
