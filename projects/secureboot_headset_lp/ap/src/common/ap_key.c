/**
 * @file ap_key.c
 * @brief On-board ADC keys: S4 long press powers off the AP.
 *
 * Hardware matches the beken_robot project:
 *   KEY1 = GPIO13 / ADC chan15  -> S2, S3
 *   KEY2 = GPIO28 / ADC chan4   -> S4, S5
 * Voltage ranges reuse CONFIG_ROBOT_KEY_Sx_VOLTAGE_*.
 *
 * Behaviour:
 *   - Keys are only usable while the AP is powered; they only turn the AP off.
 *   - AP wake-up does not use a key: after the AP is off, the phone starts
 *     streaming again, the ACL data reaches the CP controller and
 *     bt_ipc_mailbox_send_msg() on the CP votes AP ON, so the AP resumes
 *     through the fast-boot path.
 *   - AP power-off resets the GPIO/ADC registers, so a resume callback
 *     re-registers the keys to keep repeated on/off cycles working.
 */
#include <common/sys_config.h>

#if CONFIG_ADC_KEY && CONFIG_ROBOT_V2_ADC_KEYS

#include <components/log.h>
#include <os/os.h>
#include <driver/pwr_clk.h>
#include <modules/pm.h>

#include "adc_key_main.h"
#include "ap_key.h"

#define TAG "ap_key"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define AP_KEY_THREAD_STACK   2048
#define AP_KEY_THREAD_PRIO    BEKEN_DEFAULT_WORKER_PRIORITY

extern bk_err_t bk_saradc_driver_deinit(void);

typedef enum {
	AP_KEY_ROLE_NONE = 0,
	AP_KEY_ROLE_POWER_OFF_AP,
} ap_key_role_t;

typedef struct {
	const char *name;
	ap_key_role_t role;
} ap_key_ctx_t;

typedef struct {
	adc_key_id_t key_id;
	gpio_id_t gpio_id;
	adc_chan_t adc_chan;
	uint16_t voltage_low_mv;
	uint16_t voltage_high_mv;
	ap_key_ctx_t ctx;
} ap_key_cfg_t;

static const ap_key_cfg_t s_ap_keys[] = {
	{
		.key_id = 2U,
		.gpio_id = (gpio_id_t)CONFIG_ROBOT_KEY1_ADC_GPIO,
		.adc_chan = (adc_chan_t)CONFIG_ROBOT_KEY1_ADC_CHAN,
		.voltage_low_mv = CONFIG_ROBOT_KEY_S2_VOLTAGE_LOW,
		.voltage_high_mv = CONFIG_ROBOT_KEY_S2_VOLTAGE_HIGH,
		.ctx = { .name = "S2", .role = AP_KEY_ROLE_NONE },
	},
	{
		.key_id = 3U,
		.gpio_id = (gpio_id_t)CONFIG_ROBOT_KEY1_ADC_GPIO,
		.adc_chan = (adc_chan_t)CONFIG_ROBOT_KEY1_ADC_CHAN,
		.voltage_low_mv = CONFIG_ROBOT_KEY_S3_VOLTAGE_LOW,
		.voltage_high_mv = CONFIG_ROBOT_KEY_S3_VOLTAGE_HIGH,
		.ctx = { .name = "S3", .role = AP_KEY_ROLE_NONE },
	},
	{
		.key_id = 4U,
		.gpio_id = (gpio_id_t)CONFIG_ROBOT_KEY2_ADC_GPIO,
		.adc_chan = (adc_chan_t)CONFIG_ROBOT_KEY2_ADC_CHAN,
		.voltage_low_mv = CONFIG_ROBOT_KEY_S4_VOLTAGE_LOW,
		.voltage_high_mv = CONFIG_ROBOT_KEY_S4_VOLTAGE_HIGH,
		/* Only S4 long press is bound to "AP off"; change it here if needed. */
		.ctx = { .name = "S4", .role = AP_KEY_ROLE_POWER_OFF_AP },
	},
	{
		.key_id = 5U,
		.gpio_id = (gpio_id_t)CONFIG_ROBOT_KEY2_ADC_GPIO,
		.adc_chan = (adc_chan_t)CONFIG_ROBOT_KEY2_ADC_CHAN,
		.voltage_low_mv = CONFIG_ROBOT_KEY_S5_VOLTAGE_LOW,
		.voltage_high_mv = CONFIG_ROBOT_KEY_S5_VOLTAGE_HIGH,
		.ctx = { .name = "S5", .role = AP_KEY_ROLE_NONE },
	},
};

#define AP_KEY_COUNT (sizeof(s_ap_keys) / sizeof(s_ap_keys[0]))

static adc_key_handle_t s_ap_key_handles[AP_KEY_COUNT];
static beken_semaphore_t s_ap_off_sema = NULL;
static beken_thread_t s_ap_off_thread = NULL;
static beken_semaphore_t s_key_restore_sema = NULL;
static beken_thread_t s_key_restore_thread = NULL;

static bk_err_t ap_key_register_items(void);

/* The key callback must not block, so the vote runs in its own thread.
 *
 * The saradc client is released here, while the CP is still alive, because the
 * AP power-off kills the mailbox socket: calling mb_ipc_close() after the wake
 * would fail (the CP peer is gone) and leave the ipc port allocated forever,
 * so every later mb_ipc_socket() would return "create socket failed". */
static void ap_power_off_thread(void *arg)
{
	(void)arg;

	while (1) {
		if (rtos_get_semaphore(&s_ap_off_sema, BEKEN_WAIT_FOREVER) != BK_OK) {
			continue;
		}

		LOGI("key: request AP power off\n");

		(void)bk_adc_key_deinit_ex();
		(void)bk_saradc_driver_deinit();

		bk_err_t ret = bk_pm_module_vote_boot_ap_ctrl(PM_BOOT_AP_MODULE_NAME_APP,
							     PM_POWER_MODULE_STATE_OFF);
		LOGI("key: AP power off vote ret=%d\n", ret);

		if (ret != BK_OK) {
			/* The AP stays up, so bring the keys back. */
			(void)ap_key_register_items();
		}
	}
}

static void ap_key_long_cb(void *user_data)
{
	const ap_key_ctx_t *ctx = (const ap_key_ctx_t *)user_data;

	if ((ctx == NULL) || (ctx->role == AP_KEY_ROLE_NONE)) {
		return;
	}

	LOGI("ADC KEY %s long press\n", ctx->name);

	if (s_ap_off_sema != NULL) {
		(void)rtos_set_semaphore(&s_ap_off_sema);
	}
}

static bk_err_t ap_key_register_items(void)
{
	adc_key_driver_config_t driver_config = {
		.size = sizeof(adc_key_driver_config_t),
		.version = ADC_KEY_CONFIG_VERSION,
		.sample_period_ms = CONFIG_ADC_KEY_SAMPLE_PERIOD_MS,
		.max_channels = 2U,
		.max_items = (uint8_t)AP_KEY_COUNT,
	};

	bk_err_t ret = bk_adc_key_init_ex(&driver_config);
	if (ret != BK_OK) {
		LOGE("ADC key init failed: %d\n", ret);
		return ret;
	}

	for (uint32_t i = 0; i < AP_KEY_COUNT; i++) {
		const ap_key_cfg_t *key = &s_ap_keys[i];
		adc_key_item_config_ex_t item = {
			.size = sizeof(adc_key_item_config_ex_t),
			.version = ADC_KEY_CONFIG_VERSION,
			.key_id = key->key_id,
			.gpio_id = key->gpio_id,
			.adc_chan = key->adc_chan,
			.lowest_level = key->voltage_low_mv,
			.highest_level = key->voltage_high_mv,
			.short_press_cb = NULL,
			.double_press_cb = NULL,
			.long_press_cb = ap_key_long_cb,
			.hold_press_cb = NULL,
			.user_data = (void *)&key->ctx,
		};

		ret = bk_adc_key_item_configure_ex(&item, &s_ap_key_handles[i]);
		if (ret != BK_OK) {
			LOGE("ADC key %s register failed: %d\n", key->ctx.name, ret);
			(void)bk_adc_key_deinit_ex();
			return ret;
		}
	}

	LOGI("ADC keys ready (S4 long press -> AP off)\n");
	return BK_OK;
}

/* Mirror of the power-off side: the saradc client was released before the AP
 * went down, so only the key registration has to be rebuilt here. */
static void ap_key_restore_thread(void *arg)
{
	(void)arg;

	while (1) {
		if (rtos_get_semaphore(&s_key_restore_sema, BEKEN_WAIT_FOREVER) != BK_OK) {
			continue;
		}

		(void)ap_key_register_items();

		LOGI("ADC key re-init after AP wake\n");
	}
}

static bk_err_t ap_key_resume(void *arg)
{
	(void)arg;

	if (s_key_restore_sema != NULL) {
		(void)rtos_set_semaphore(&s_key_restore_sema);
	}

	return BK_OK;
}

static const pm_ap_power_ops_t s_ap_key_pm_ops = {
	.name = "ap_key",
	.resume = ap_key_resume,
	.priority = PM_AP_POWER_PRIORITY_PERIPHERAL,
};

int ap_key_init(void)
{
	if (rtos_init_semaphore_ex(&s_ap_off_sema, 1, 0) != BK_OK) {
		LOGE("ap off sem init failed\n");
		return BK_FAIL;
	}

	if (rtos_create_thread(&s_ap_off_thread, AP_KEY_THREAD_PRIO, "ap_key_pwr",
			       ap_power_off_thread, AP_KEY_THREAD_STACK, NULL) != BK_OK) {
		LOGE("ap off thread create failed\n");
		(void)rtos_deinit_semaphore(&s_ap_off_sema);
		s_ap_off_sema = NULL;
		return BK_FAIL;
	}

	if (rtos_init_semaphore_ex(&s_key_restore_sema, 1, 0) != BK_OK) {
		LOGE("key restore sem init failed\n");
		return BK_FAIL;
	}

	if (rtos_create_thread(&s_key_restore_thread, AP_KEY_THREAD_PRIO, "ap_key_rst",
			       ap_key_restore_thread, AP_KEY_THREAD_STACK, NULL) != BK_OK) {
		LOGE("key restore thread create failed\n");
		return BK_FAIL;
	}

	if (ap_key_register_items() != BK_OK) {
		return BK_FAIL;
	}

	if (bk_pm_ap_power_ops_register(&s_ap_key_pm_ops) != BK_OK) {
		LOGW("key pm ops register failed\n");
	}

	return BK_OK;
}

#else  /* !(CONFIG_ADC_KEY && CONFIG_ROBOT_V2_ADC_KEYS) */

int ap_key_init(void)
{
	return BK_OK;
}

#endif /* CONFIG_ADC_KEY && CONFIG_ROBOT_V2_ADC_KEYS */
