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
#include "pm_ap_key_sleep.h"

#define TAG "ap_key"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define AP_KEY_THREAD_STACK   2048
#define AP_KEY_THREAD_PRIO    BEKEN_DEFAULT_WORKER_PRIORITY
#define AP_KEY_ADC_SETTLE_MS  (2 * ADCKEY_TICKS_INTERVAL)

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

typedef enum {
	AP_KEY_MSG_POWER_OFF = 0,
	AP_KEY_MSG_RESTORE,
} ap_key_msg_e;

static adc_key_handle_t s_ap_key_handles[AP_KEY_COUNT];
static beken_queue_t s_ap_key_queue = NULL;
static beken_thread_t s_ap_key_thread = NULL;

static bk_err_t ap_key_register_items(void);

static bk_err_t ap_key_post(ap_key_msg_e msg)
{
	if (s_ap_key_queue == NULL) {
		return BK_FAIL;
	}
	return rtos_push_to_queue(&s_ap_key_queue, &msg, BEKEN_NO_WAIT);
}

/* Both the key callback and the PM resume callback only post a message; the
 * actual work runs here in task context. The PM demo releases the saradc
 * client while the CP is still alive: the AP power-off kills the mailbox
 * socket, and calling mb_ipc_close() after the wake would fail (the CP peer is
 * gone) and leave the ipc port allocated forever, so every later
 * mb_ipc_socket() would return "create socket failed". */
static void ap_key_thread(void *arg)
{
	ap_key_msg_e msg;

	(void)arg;

	while (1) {
		if (rtos_pop_from_queue(&s_ap_key_queue, &msg, BEKEN_WAIT_FOREVER) != BK_OK) {
			continue;
		}

		switch (msg) {
		case AP_KEY_MSG_POWER_OFF:
			LOGI("key: request AP power off -> pm demo\n");

			/* PM demo steps: release the ADC sampler, arm GPIO28 as the CP
			 * wake source, vote CP sleep and vote the AP off. */
			if (ap_key_sleep_enter() != BK_OK) {
				LOGE("key: pm demo enter queue failed\n");
			}
			break;
		case AP_KEY_MSG_RESTORE:
				if (ap_key_restore() == BK_OK) {
					LOGI("ADC keys restored after AP wake\n");
				} else {
					LOGE("ADC key restore failed\n");
				}
				break;
		default:
			break;
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

	(void)ap_key_post(AP_KEY_MSG_POWER_OFF);
}

/* Keep the ADC key manager, timer and mutex alive for the whole AP lifetime.
 * Removing the last item stops the CP sampler; after one settling window the
 * empty timer no longer touches SARADC, so its IPC client can be closed safely. */
static bk_err_t ap_key_release_items(void)
{
	bk_err_t first_error = BK_OK;
	bool had_items = false;

	for (uint32_t i = 0; i < AP_KEY_COUNT; i++) {
		if (s_ap_key_handles[i] == NULL) {
			continue;
		}

		had_items = true;
		bk_err_t ret = bk_adc_key_item_unconfigure_ex(s_ap_key_handles[i]);
		if (ret == BK_OK) {
			s_ap_key_handles[i] = NULL;
		} else {
			LOGE("ADC key %s unregister failed: %d\n", s_ap_keys[i].ctx.name, ret);
			first_error = ret;
		}
	}

	if (first_error != BK_OK) {
		return first_error;
	}

	if (had_items) {
		rtos_delay_milliseconds(AP_KEY_ADC_SETTLE_MS);
		(void)bk_saradc_driver_deinit();
	}

	return BK_OK;
}

static bk_err_t ap_key_register_items(void)
{
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

		if (s_ap_key_handles[i] != NULL) {
			continue;
		}

		bk_err_t ret = bk_adc_key_item_configure_ex(&item, &s_ap_key_handles[i]);
		if (ret != BK_OK) {
			LOGE("ADC key %s register failed: %d\n", key->ctx.name, ret);
			(void)ap_key_release_items();
			return ret;
		}
	}

	LOGI("ADC keys ready (S4 long press -> AP off)\n");
	return BK_OK;
}

bk_err_t ap_key_power_off(void)
{
	bk_err_t ret = ap_key_release_items();

	if (ret == BK_OK) {
		LOGI("ADC keys suspended for AP off\n");
	}
	return ret;
}

bk_err_t ap_key_restore(void)
{
	bk_err_t ret = ap_key_release_items();

	if (ret != BK_OK) {
		return ret;
	}

	return ap_key_register_items();
}

static bk_err_t ap_key_resume(void *arg)
{
	(void)arg;

	(void)ap_key_post(AP_KEY_MSG_RESTORE);

	return BK_OK;
}

static const pm_ap_power_ops_t s_ap_key_pm_ops = {
	.name = "ap_key",
	.resume = ap_key_resume,
	.priority = PM_AP_POWER_PRIORITY_PERIPHERAL,
};

int ap_key_init(void)
{
	if (s_ap_key_queue == NULL) {
		if (rtos_init_queue(&s_ap_key_queue, "ap_key", sizeof(ap_key_msg_e), 4) != BK_OK) {
			LOGE("key queue init failed\n");
			return BK_FAIL;
		}
	}

	if (s_ap_key_thread == NULL) {
		if (rtos_create_thread(&s_ap_key_thread, AP_KEY_THREAD_PRIO, "ap_key",
				       ap_key_thread, AP_KEY_THREAD_STACK, NULL) != BK_OK) {
			LOGE("key thread create failed\n");
			return BK_FAIL;
		}
	}

	adc_key_driver_config_t driver_config = {
		.size = sizeof(adc_key_driver_config_t),
		.version = ADC_KEY_CONFIG_VERSION,
		.sample_period_ms = CONFIG_ADC_KEY_SAMPLE_PERIOD_MS,
		.max_channels = 2U,
		.max_items = (uint8_t)AP_KEY_COUNT,
	};
	bk_err_t ret = bk_adc_key_init_ex(&driver_config);

	if (ret == BK_ERR_STATE) {
		/* An unclean AP reset can retain the resident manager and old items. */
		return ap_key_restore();
	}
	if (ret != BK_OK) {
		LOGE("ADC key init failed: %d\n", ret);
		return ret;
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

bk_err_t ap_key_power_off(void)
{
	return BK_OK;
}

bk_err_t ap_key_restore(void)
{
	return BK_OK;
}

#endif /* CONFIG_ADC_KEY && CONFIG_ROBOT_V2_ADC_KEYS */
