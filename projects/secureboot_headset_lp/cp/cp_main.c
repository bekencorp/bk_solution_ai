#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <modules/pm.h>
#include <driver/pwr_clk.h>
#include <driver/gpio.h>
#include <components/log.h>
#include <string.h>
#if CONFIG_BLUETOOTH_A2DP_OFFLOAD_SINK
#include "a2dp_sink_offload.h"
#include "a2dp_sink_offload_player.h"
#endif
#if CONFIG_BLUETOOTH_HFP_SCO_OFFLOAD
#include "hfp_sco_offload.h"
#include "hfp_sco_offload_player.h"
#endif

#define TAG "pm_secureboot_ap_fast_boot"

extern void rtos_set_user_app_entry(beken_thread_function_t entry);

#define CP_KEY_WAKE_GPIO             ((gpio_id_t)28)
#define CP_KEY_VOTE_THREAD_STACK     (1024)

static beken_semaphore_t s_key_vote_sema = NULL;
static beken_thread_t s_key_vote_thread = NULL;
static volatile uint8_t s_key_vote_voted = 0;

static void cp_key_wake_isr(gpio_id_t gpio_id);

static bk_err_t ap_fast_boot_vote(pm_power_module_state_e state)
{
	bk_err_t ret = bk_pm_module_vote_boot_ap_ctrl(PM_BOOT_AP_MODULE_NAME_APP, state);
	BK_LOGI(TAG, "AP vote %s ret=%d\r\n", (state == PM_POWER_MODULE_STATE_ON) ? "ON" : "OFF", ret);
	return ret;
}

/* The vote locks the CP-PM mutex and blocks for the whole AP fast-boot
 * handshake, so it must never run in the GPIO ISR. The ISR only signals this
 * thread, which votes on the first falling edge while the AP is down. */
static void cp_key_vote_thread(void *arg)
{
	(void)arg;

	while (1) {
		if (rtos_get_semaphore(&s_key_vote_sema, BEKEN_WAIT_FOREVER) != BK_OK) {
			break;
		}
		if (bk_pm_ap_boot_success_get()) {
			break;          /* BT or another wake source already started the AP */
		}

		/* this cycle was woken by the key */
		if(s_key_vote_voted){
			bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_APP, 0x0, 0x0);
			ap_fast_boot_vote(PM_POWER_MODULE_STATE_ON);
			BK_LOGI(TAG, "key wake: vote AP ON\r\n");
		}
		break;
	}

	s_key_vote_thread = NULL;
	rtos_delete_thread(NULL);
}

/* Called from the AP power-off callback, which runs in task context. The
 * worker exists only while the AP is down, so normal AP/Wi-Fi operation does
 * not carry its 1 KB stack. */
static bk_err_t cp_key_vote_worker_start(void)
{
	bk_err_t ret;

	if (s_key_vote_thread != NULL) {
		return BK_OK;
	}
	if (s_key_vote_sema == NULL) {
		return BK_ERR_STATE;
	}

	ret = rtos_create_thread(&s_key_vote_thread, BEKEN_DEFAULT_WORKER_PRIORITY,
				 "cp_key_vote", cp_key_vote_thread,
				 CP_KEY_VOTE_THREAD_STACK, NULL);
	if (ret != BK_OK) {
		BK_LOGE(TAG, "key vote thread create failed\r\n");
	}
	return ret;
}

/* AP is down: ADC4 is no longer being sampled, so turn the pad into a plain
 * digital input and open its interrupt. The AP side never touches GPIO28, and
 * its gpio_default_map_init() disables the interrupt on every boot, so the
 * ownership has to move here. */
static void cp_key_ap_power_off_cb(void *arg)
{
	gpio_config_t in = {
		.io_mode = GPIO_INPUT_ENABLE,
		.pull_mode = GPIO_PULL_UP_EN,
		.func_mode = GPIO_SECOND_FUNC_DISABLE,
	};

	(void)arg;

	if (bk_gpio_set_config(CP_KEY_WAKE_GPIO, &in) != BK_OK) {
		BK_LOGE(TAG, "key wake pin config failed\r\n");
	}

	/* The AP-side PM demo already registered GPIO28 as a CP wake source and
	 * installed pm_core_gpio_callback; only the pad/edge/ISR/enable are needed
	 * here so the key also works when the CP never enters low voltage. */
	bk_gpio_set_interrupt_type(CP_KEY_WAKE_GPIO, GPIO_INT_TYPE_FALLING_EDGE);
	bk_gpio_register_isr(CP_KEY_WAKE_GPIO, cp_key_wake_isr);
	bk_gpio_enable_interrupt(CP_KEY_WAKE_GPIO);

	BK_LOGI(TAG, "key wake armed: gpio=%d level=%d\r\n", CP_KEY_WAKE_GPIO, bk_gpio_get_input(CP_KEY_WAKE_GPIO));

	/* Fresh cycle: clear the key-wake flag before the worker starts. */
	s_key_vote_voted = 0;

	(void)cp_key_vote_worker_start();
}

/* AP is back: close the interrupt and return the pad to the high-impedance
 * state ADC4 needs. It has no explicit ADC function code at boot either. */
static void cp_key_ap_power_on_cb(void *arg)
{
	gpio_config_t high_z = {
		.io_mode = GPIO_IO_DISABLE,
		.pull_mode = GPIO_PULL_DISABLE,
		.func_mode = GPIO_SECOND_FUNC_DISABLE,
	};

	(void)arg;

	(void)bk_gpio_disable_interrupt(CP_KEY_WAKE_GPIO);
	(void)bk_gpio_set_config(CP_KEY_WAKE_GPIO, &high_z);

	/* If the AP was started by BT or another source, wake the one-shot key
	 * worker so it can observe the AP state and clean itself up. */
	if (s_key_vote_sema != NULL && !s_key_vote_voted) {
		(void)rtos_set_semaphore(&s_key_vote_sema);
	}
}

static void cp_key_wake_isr(gpio_id_t gpio_id)
{
	(void)gpio_id;

	s_key_vote_voted = 1;
	/* Interrupt context: only wake the vote thread; the AP vote runs later in
	 * task context and does not depend on the software-timer service. */
	if (s_key_vote_sema != NULL) {
		(void)rtos_set_semaphore(&s_key_vote_sema);
	}
}

static void cp_key_wake_start(void)
{
	if (rtos_init_semaphore_ex(&s_key_vote_sema, 1, 0) != BK_OK) {
		BK_LOGE(TAG, "key vote sem init failed\r\n");
		return;
	}

	/* GPIO28 ownership follows the AP: taken over on power-off, handed back
	 * to the ADC4 sampler on power-on. */
	if (bk_pm_ap_ctrl_callback_register(cp_key_ap_power_off_cb, NULL,
					    PM_AP_CTRL_CB_TYPE_POWER_OFF) != BK_OK) {
		BK_LOGW(TAG, "AP power-off callback register failed\r\n");
	}
	if (bk_pm_ap_ctrl_callback_register(cp_key_ap_power_on_cb, NULL,
					    PM_AP_CTRL_CB_TYPE_POWER_ON) != BK_OK) {
		BK_LOGW(TAG, "AP power-on callback register failed\r\n");
	}
}

void user_app_main(void)
{
	/* Start the AP through the common PM vote + multicore path. TF-M performs
	 * only the Secure-privilege AP prepare via the psa_ap_secure_prepare NSC
	 * inside multicore_hal_start(). */
	if (ap_fast_boot_vote(PM_POWER_MODULE_STATE_ON) != BK_OK) {
		BK_LOGE(TAG, "AP boot vote failed\r\n");
	}
	BK_LOGI(NULL, "secureboot_headset_lp: CP NS world reached (secure boot OK)\r\n");

#if CONFIG_ADK
	extern int bk_audio_osi_funcs_init(void);
	bk_audio_osi_funcs_init();
#endif

#if CONFIG_BLUETOOTH_A2DP_OFFLOAD_SINK
    /* A2DP Sink Offload: shim first (it clears the callback slots), then the
     * playback driver that registers into them. Failure does not block boot. */
    if (a2dp_sink_offload_init() != BK_OK)
    {
        BK_LOGW(NULL, "a2dp sink offload init failed\r\n");
    }
    if (a2dp_sink_offload_player_init() != BK_OK)
    {
        BK_LOGW(NULL, "a2dp sink offload player init failed\r\n");
    }
#endif

#if CONFIG_BLUETOOTH_HFP_SCO_OFFLOAD
    if (hfp_sco_offload_init() != BK_OK)
    {
        BK_LOGW(NULL, "hfp sco offload init failed\r\n");
    }
    if (hfp_sco_offload_player_init() != BK_OK)
    {
        BK_LOGW(NULL, "hfp sco offload player init failed\r\n");
    }
#endif

	cp_key_wake_start();
}


int main(void)
{
	rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
	bk_init();

	return 0;
}
