#include <stdio.h>
#include <stdbool.h>
#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>
#include <modules/pm.h>
#include "components/bluetooth/bk_dm_bluetooth.h"
#include "components/bluetooth/bk_dm_a2dp.h"
#include "components/bluetooth/bk_dm_hfp.h"
#include "bt_manager.h"
#include "headset_user_config.h"
#include "media_service.h"
#include <components/bk_frame_buffer.h>
#include "media_devices.h"
#include <avdk_check.h>
#include <avdk_error.h>
#include "a2dp_sink/a2dp_sink_demo.h"
#include "dm_gatt.h"
#include "dm_gatts.h"
#include "dm_gattc.h"
#if CONFIG_LVGL
#include "lvgl.h"
#include "lv_vendor.h"
#include <common/avdk_pixel_types.h>
#include <components/bk_display.h>
#include "page_bt_music.h"
#endif
#include "lcd/lcd_mipi_jd9855_320x385.h"
#include "driver/drv_tp.h"
#include "ap_key.h"
#include "pm_ap_key_sleep.h"

#define AUTO_ENABLE_BLUETOOTH_DEMO 1

#define TAG "ap_main"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

#define DEFAULT_MIPI_PANEL (&lcd_device_jd9855_mipi_320x385)

#if CONFIG_LVGL
extern void lv_gpu_init(uint32_t tess_width, uint32_t tess_height);
extern void lv_gpu_deinit(void);

#define LVGL_DISP_WIDTH  320
#define LVGL_DISP_HEIGHT 385
#define LVGL_COMPRESS_DISP_WIDTH  ((LVGL_DISP_WIDTH + 15) & ~15)
#define LVGL_COMPRESS_DISP_HEIGHT ((LVGL_DISP_HEIGHT + 3) & ~3)
/* Panel open and the first full-screen redraw need more than 4 KB. */
#define DISPLAY_RESUME_TASK_STACK 6144

static bk_display_ctlr_handle_t s_lvgl_dpu_handle = NULL;

/* LVGL does not own these buffers in RENDER_PARTIAL_MODE; retain them together
 * with the LVGL object tree across AP power cycles. */
static void *s_lvgl_frame_buffer[2] = {NULL, NULL};

static beken_thread_t s_display_resume_thread = NULL;
static beken_semaphore_t s_display_resume_sem = NULL;
static volatile bool s_display_active = false;

static void bk_robot_lvgl_flush_to_display(void *args, void *frame_buffer, int (*cb)(void *args))
{
    bk_display_ctlr_handle_t *slot = (bk_display_ctlr_handle_t *)args;

    /* Panel open replaces the DPU controller, so refresh per flush. */
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

static void bk_robot_lvgl_flush_cb(void *args, void *frame_buffer, int (*cb)(void *args))
{
    bk_robot_lvgl_flush_to_display(args, frame_buffer, cb);
}

static bk_err_t bk_robot_lvgl_init(bk_display_ctlr_handle_t dpu_handle)
{
    lv_vnd_config_t cfg = {0};
    const uint32_t frame_buffer_size = LVGL_COMPRESS_DISP_WIDTH * LVGL_COMPRESS_DISP_HEIGHT;

    cfg.width = LVGL_DISP_WIDTH;
    cfg.height = LVGL_DISP_HEIGHT;
    cfg.render_mode = RENDER_PARTIAL_MODE;
    cfg.rotation = ROTATE_90;
    cfg.output_compress = true;
    cfg.disp_width = LVGL_COMPRESS_DISP_WIDTH;
    cfg.disp_height = LVGL_COMPRESS_DISP_HEIGHT;
    if (s_lvgl_frame_buffer[0] == NULL) {
        s_lvgl_frame_buffer[0] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, frame_buffer_size);
        s_lvgl_frame_buffer[1] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, frame_buffer_size);
        if (!s_lvgl_frame_buffer[0] || !s_lvgl_frame_buffer[1]) {
            LOGE("LVGL frame buffer malloc failed\n");
            return BK_FAIL;
        }
    }
    cfg.frame_buffer[0] = s_lvgl_frame_buffer[0];
    cfg.frame_buffer[1] = s_lvgl_frame_buffer[1];
    s_lvgl_dpu_handle = dpu_handle;
    cfg.args = &s_lvgl_dpu_handle;
    cfg.flush_cb = bk_robot_lvgl_flush_cb;

    // ui_overlay_swipe_set_display_transform(cfg.width, cfg.height, cfg.rotation);
    bk_err_t ret = lv_vendor_init(&cfg);
    if (ret != BK_OK) {
        LOGE("LVGL init failed: %d\n", ret);
        return ret;
    }

#if (CONFIG_TP)
    drv_tp_open(LVGL_DISP_WIDTH, LVGL_DISP_HEIGHT, TP_MIRROR_NONE);
#endif

    LOGI("LVGL ready on %dx%d MIPI (first page pending)\n",
         LVGL_DISP_WIDTH, LVGL_DISP_HEIGHT);
    return BK_OK;
}

static void bk_robot_lvgl_load_first_page(void)
{
    lv_vendor_disp_lock();
    /* Robot bt_music page (spectrum visualizer + transport), hosted as the
     * single product screen. */
    page_bt_music_enter();
    lv_vendor_disp_unlock();

    lv_vendor_start();
}

static bk_err_t bk_robot_display_init(void)
{
    bk_err_t ret;

    if (s_display_active) {
        return BK_OK;
    }

    ret = media_lcd_panel_open(DEFAULT_MIPI_PANEL, BK_PIXEL_FORMAT_ARGB8888, true);
    if (ret != AVDK_ERR_OK) {
        LOGE("display panel open failed: %d\n", ret);
        return ret;
    }

    ret = bk_robot_lvgl_init(media_panel_get_dpu_handle());
    if (ret != BK_OK) {
        media_lcd_panel_close();
        return ret;
    }

    bk_robot_lvgl_load_first_page();
    s_display_active = true;
    LOGI("display init done\n");
    return BK_OK;
}

/* Stop rendering and tear down only the hardware. LVGL objects, timers and the
 * current screen stay in retained RAM so wake-up restores the same UI state. */
void bk_robot_display_power_off(void)
{
    uint32_t start_ms;

    if (!s_display_active) {
        return;
    }

    start_ms = rtos_get_time();
    lv_vendor_stop();
#if (CONFIG_TP)
    drv_tp_suspend();
    rtos_delay_milliseconds(20);
    drv_tp_close();
#endif
#if CONFIG_GPU
    lv_gpu_deinit();
#endif
    media_lcd_panel_close();
    media_board_power_off();
    s_lvgl_dpu_handle = NULL;
    s_display_active = false;
    LOGI("display hardware off in %u ms\n",
         (unsigned)(rtos_get_time() - start_ms));
}

static bk_err_t bk_robot_display_restore(void)
{
    bk_err_t ret;

    if (s_display_active) {
        return BK_OK;
    }

    media_board_power_on();
    ret = media_lcd_panel_open(DEFAULT_MIPI_PANEL, BK_PIXEL_FORMAT_ARGB8888, true);
    if (ret != AVDK_ERR_OK) {
        LOGE("display panel resume failed: %d\n", ret);
        return ret;
    }
    s_lvgl_dpu_handle = media_panel_get_dpu_handle();

#if CONFIG_GPU
    lv_gpu_init(0, 0);
#endif
#if (CONFIG_TP)
    drv_tp_open(LVGL_DISP_WIDTH, LVGL_DISP_HEIGHT, TP_MIRROR_NONE);
#endif

    /* Restart the retained LVGL context, then redraw the current screen into
     * the re-opened panel without rebuilding or resetting the page. */
    lv_vendor_start();
    lv_vendor_disp_lock();
    if (lv_screen_active() != NULL) {
        lv_obj_invalidate(lv_screen_active());
    }
    lv_vendor_disp_unlock();

    s_display_active = true;
    LOGI("display resume done\n");
    return BK_OK;
}

static void bk_robot_display_resume_task(void *arg)
{
    (void)arg;

    while (1) {
        if (rtos_get_semaphore(&s_display_resume_sem, BEKEN_WAIT_FOREVER) != BK_OK) {
            continue;
        }
        if (s_display_active) {
            continue;
        }

        if (bk_robot_display_restore() != BK_OK) {
            LOGE("display resume failed\n");
        }
    }
}

/* PM only schedules the hardware restore; the resume task does the slow work. */
bk_err_t bk_robot_display_resume(void *arg)
{
    (void)arg;
    if (s_display_resume_sem != NULL) {
        rtos_set_semaphore(&s_display_resume_sem);
    }
    return BK_OK;
}

static const pm_ap_power_ops_t s_display_pm_ops = {
    .name = "lvgl_display",
    .resume = bk_robot_display_resume,
    .priority = PM_AP_POWER_PRIORITY_SERVICE,
};

static bk_err_t bk_robot_display_pm_init(void)
{
    bk_err_t ret;

    ret = rtos_init_semaphore_ex(&s_display_resume_sem, 1, 0);
    if (ret != BK_OK) {
        LOGE("display resume sem init failed: %d\n", ret);
        return ret;
    }

    ret = rtos_create_thread(&s_display_resume_thread,
                             BEKEN_DEFAULT_WORKER_PRIORITY,
                             "disp_resume",
                             bk_robot_display_resume_task,
                             DISPLAY_RESUME_TASK_STACK,
                             NULL);
    if (ret != BK_OK) {
        LOGE("display resume thread create failed: %d\n", ret);
        return ret;
    }

    ret = bk_pm_ap_power_ops_register(&s_display_pm_ops);
    if (ret != BK_OK) {
        LOGE("display pm ops register failed: %d\n", ret);
    }
    return ret;
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

int main(void)
{
	bk_init();

	media_service_init();
	bk_frame_buffer_init();
	media_board_power_on(); //power on ai board peripherals

#if CONFIG_LVGL
    AVDK_RETURN_ON_ERROR(bk_robot_display_init(), TAG, "display init error");
    AVDK_RETURN_ON_ERROR(bk_robot_display_pm_init(), TAG, "display pm init error");
#endif

#ifdef CONFIG_LDO3V3_ENABLE
    BK_LOG_ON_ERR(gpio_dev_unmap(LDO3V3_CTRL_GPIO));
    bk_gpio_disable_pull(LDO3V3_CTRL_GPIO); 
    bk_gpio_enable_output(LDO3V3_CTRL_GPIO);
    bk_gpio_set_output_high(LDO3V3_CTRL_GPIO);
#endif

#if AUTO_ENABLE_BLUETOOTH_DEMO
	uint8_t bt_mac[6] = {0};
	char local_name[30] = {0};
	bk_err_t err = bk_bluetooth_get_address(bt_mac);
	if (err == BK_OK)
	{
		snprintf(local_name,
				 sizeof(local_name),
				 "%s_%02x%02x%02x",
				 LOCAL_NAME,
				 bt_mac[2],
				 bt_mac[1],
				 bt_mac[0]);
	}
	else
	{
		snprintf(local_name, sizeof(local_name), "%s", LOCAL_NAME);
	}

	bt_manager_cfg_t bt_manager_cfg =
	{
		.local_name = local_name,
		.device_class = COD_SOUNDBAR,
		.page_scan_interval = PAGE_SCAN_INTV,
		.page_scan_window = PAGE_SCAN_WIN,
		.page_timeout = CONFIG_PAGE_TIMEOUT,
		.reconnect_interval_ms = CONFIG_RECONN_INTERVAL,
		.max_reconnect_count = CONFIG_MAX_RECONN_COUNT,
		.io_capability = BK_BT_IO_CAP_NONE,
	};
	bt_manager_init(&bt_manager_cfg);

#if CONFIG_A2DP_SINK_DEMO
	a2dp_sink_demo_init(0, 1);
#endif

#if CONFIG_HFP_HF_DEMO
	extern int hfp_hf_demo_init(uint8_t msbc_supported);
	hfp_hf_demo_init(0);
#endif

#if CONFIG_BT
	extern int cli_headset_demo_init(void);
	cli_headset_demo_init();
#endif

#if CONFIG_AUDIO_PLAY
	extern int audio_play_cli_init(void);
	audio_play_cli_init();
#endif

#if CONFIG_BLE
    //cli_gatt_param_t param = {.rpa = 0, .p_rpa = &param.rpa, .pa = 0, .p_pa = &param.pa};

    //dm_gatt_main(&param);
    //dm_gatts_main(&param);

    extern int cli_ble_gatt_demo_init(void);
    cli_ble_gatt_demo_init();
#endif

#if CONFIG_BLUETOOTH_A2DP_OFFLOAD_SINK
	bk_a2dp_sink_offload_set_enabled(true);
#endif

#if CONFIG_BLUETOOTH_HFP_SCO_OFFLOAD
	bk_hfp_sco_offload_set_enabled(true);
#endif

#endif

	/* On-board ADC keys: S4 long press powers off the AP. Registered after
	 * all services so the key thread cannot trigger a power cycle while the
	 * display/BT stack is still coming up. */
	(void)ap_key_init();

	/* Low-voltage entry used by the S4 long press: arm GPIO28 as the CP wake
	 * source, vote CP sleep and vote the AP off. */
	(void)ap_key_sleep_init();

	return 0;
}
