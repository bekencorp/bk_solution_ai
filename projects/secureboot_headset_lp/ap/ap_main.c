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
/* The deinit/open chain plus page rebuild needs well over 4 KB (the panel
 * teardown alone is >2 KB), so keep some headroom here. */
#define DISPLAY_RESUME_TASK_STACK 6144

static bk_display_ctlr_handle_t s_lvgl_dpu_handle = NULL;

/* AP power-off keeps SRAM/PSRAM but resets every peripheral register, so the
 * DSI/DPU/GPU stack must be rebuilt before LVGL may touch it again. This flag
 * is cleared as soon as the AP close request is probed and set again only when
 * the whole display stack has been re-initialized. */
static volatile bool s_display_hw_ready = true;

/* Frame buffers live in retained PSRAM; allocate them once so wake-up cycles
 * do not leak a fresh pair every time. */
static void *s_lvgl_frame_buffer[2] = {NULL, NULL};

static beken_thread_t s_display_resume_thread = NULL;
static beken_semaphore_t s_display_resume_sem = NULL;
static volatile bool s_display_reinit_pending = false;

/* lv_vendor_stop() blocks forever on the task semaphore when LVGL was never
 * started, so track the started state ourselves. */
static volatile bool s_lvgl_started = false;

static void bk_robot_lvgl_flush_to_display(void *args, void *frame_buffer, int (*cb)(void *args))
{
    bk_display_ctlr_handle_t *slot = (bk_display_ctlr_handle_t *)args;

    /* Hardware is gone between AP power-off and the resume rebuild: drop the
     * frame instead of writing into a reset DPU. */
    if (!s_display_hw_ready) {
        if (cb != NULL) {
            cb(frame_buffer);
        }
        return;
    }

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
    lv_vendor_init(&cfg);

#if (CONFIG_TP)
    drv_tp_open(LVGL_DISP_WIDTH, LVGL_DISP_HEIGHT, TP_MIRROR_NONE);
#endif

    lv_vendor_disp_lock();
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
    /* Robot bt_music page (spectrum visualizer + transport), hosted as the
     * single product screen. */
    page_bt_music_enter();
    lv_vendor_disp_unlock();

    lv_vendor_start();
    s_lvgl_started = true;
}

/* Rebuild DSI/panel/DPU/GPU/TP after an AP power-off wake-up. The LVGL object
 * tree and the frame buffers survive in retained RAM; only the hardware and the
 * rendered content have to be recreated. */
static void bk_robot_display_reinit(void)
{
    LOGI("display reinit after AP wake\n");

    /* Normally already parked by quiesce() before power-off; only stop here if
     * the close was rolled back while LVGL was still running. */
    if (s_lvgl_started) {
        lv_vendor_stop();
        s_lvgl_started = false;
    }

    media_lcd_panel_close();
    media_board_power_on();
    if (media_lcd_panel_open(DEFAULT_MIPI_PANEL, BK_PIXEL_FORMAT_ARGB8888, true) != AVDK_ERR_OK) {
        LOGE("display reopen failed\n");
    }
    s_lvgl_dpu_handle = media_panel_get_dpu_handle();

#if CONFIG_GPU
    /* output_compress runs through the GPU, whose registers were reset too. */
    lv_gpu_deinit();
    lv_gpu_init(0, 0);
#endif

#if (CONFIG_TP)
    drv_tp_close();
    drv_tp_open(LVGL_DISP_WIDTH, LVGL_DISP_HEIGHT, TP_MIRROR_NONE);
#endif

    /* The DPU/GPU are alive again from here on: let flushes through before the
     * LVGL task is restarted, otherwise the first frame is dropped. */
    s_display_hw_ready = true;

    /* page_bt_music_enter() drops the old screen/timers and rebuilds the UI. */
    bk_robot_lvgl_load_first_page();

    LOGI("display reinit done\n");
}

static void bk_robot_display_resume_task(void *arg)
{
    (void)arg;

    while (1) {
        if (rtos_get_semaphore(&s_display_resume_sem, BEKEN_WAIT_FOREVER) != BK_OK) {
            continue;
        }
        if (!s_display_reinit_pending) {
            continue;
        }
        s_display_reinit_pending = false;
        bk_robot_display_reinit();
    }
}

/* Probed from the AP idle path before power-off. Must stay constant time and
 * must not use RTOS services. */
static bk_err_t bk_robot_display_prepare_power_off(void *arg)
{
    (void)arg;
    s_display_hw_ready = false;
    return BK_OK;
}

/* Runs in the AP PM task right before the power-off sequence, while the DPU and
 * GPU are still alive. The LVGL task has to be parked here: after the AP is off
 * a stop request issued from the wake path never gets its ack (the in-flight
 * frame can no longer complete), so lv_vendor_stop() would block forever.
 * The backlight and the always-on board rails are driven off as well: the panel
 * is powered by GPIO53/GPIO32 and the display aux LDO, none of which are reset
 * by the AP power-down, so they would keep drawing current for the whole AP-off
 * window. Only the DSI/DPU registers are left alone -- they sit in the AP power
 * domain and are reset by the power-down itself. The full panel sleep-in DCS
 * sequence is NOT sent here because it alone costs >100 ms; it runs on wake
 * instead, where bk_robot_display_reinit() has no such limit.
 * Must finish within PM_AP_QUIESCE_CALLBACK_TIMEOUT_MS (100 ms). */
static bk_err_t bk_robot_display_quiesce(void *arg)
{
    uint32_t start_ms;

    (void)arg;

    /* prepare_power_off() already ran and gated the flush path, so key the
     * decision on s_lvgl_started only. Calling lv_vendor_stop() twice is a
     * no-op. */
    start_ms = rtos_get_time();
    if (s_lvgl_started) {
        lv_vendor_stop();
        s_lvgl_started = false;
    }
    media_lcd_backlight_off();
    media_board_power_off();
    LOGI("display quiesce: lvgl stopped + panel powered off in %u ms\n",
         (unsigned)(rtos_get_time() - start_ms));
    return BK_OK;
}

/* Runs both on a real wake-up and when the power-off attempt is rolled back:
 * re-initializing an intact display is harmless, so no state is required. */
static bk_err_t bk_robot_display_resume(void *arg)
{
    (void)arg;
    s_display_reinit_pending = true;
    if (s_display_resume_sem != NULL) {
        rtos_set_semaphore(&s_display_resume_sem);
    }
    return BK_OK;
}

static const pm_ap_power_ops_t s_display_pm_ops = {
    .name = "lvgl_display",
    .prepare_power_off = bk_robot_display_prepare_power_off,
    .quiesce = bk_robot_display_quiesce,
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
    AVDK_RETURN_ON_ERROR(media_lcd_panel_open(DEFAULT_MIPI_PANEL, BK_PIXEL_FORMAT_ARGB8888, true), TAG, "media lcd panel open error");
    AVDK_RETURN_ON_ERROR(bk_robot_lvgl_init(media_panel_get_dpu_handle()), TAG, "bk robot lvgl init error");

    /* Register per-page business hooks before materializing page_1; pages
     * fire their init hooks from the tail of init_page_page_N(). Then
     * beken_ui_init() builds page_1, lv_screen_load()s it, and
     * lv_vendor_start() begins LVGL rendering. */
    // bk_pages_init_all_hooks();
    bk_robot_lvgl_load_first_page(); 

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

	return 0;
}
