/**
 * @file pm_ap_key_sleep.c
 * @brief AP-initiated low-voltage flow, following the SDK pm_ap_demo steps:
 *
 *          - release the ADC key sampler (GPIO28 is ADC4 while AP is alive);
 *          - register the GPIO ISR and arm GPIO28 (S4, falling edge) as the
 *            CP wake source through bk_gpio_set_wakeup(), so the CP side wake
 *            table and pm_core_gpio_callback are installed as well;
 *          - vote CP sleep and vote the AP off.
 *
 * Flow:
 *   S4 long press (or "apks enter") -> the steps above. A key edge then wakes
 *   the CP, the CP fast-boots the AP, and the latched GPIO replay lands in
 *   apks_wake_isr() which restores the votes.
 *
 * No RTC wake source is registered: the key is the only way back.
 */
#include <common/sys_config.h>

#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <components/shell_task.h>
#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include <driver/pwr_clk.h>
#include <modules/pm.h>
#include "driver/pm_ap_core.h"
#include "cli.h"

#include "ap_key.h"
#include "pm_ap_key_sleep.h"

#if CONFIG_ADC_KEY
#include "adc_key_main.h"
extern bk_err_t bk_saradc_driver_deinit(void);
#endif

#define TAG "apks"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* S4 sits on GPIO28 / ADC channel 4 on this board. */
#define APKS_WAKE_GPIO        ((gpio_id_t)28)
#define APKS_WAKE_INT_TYPE    GPIO_INT_TYPE_FALLING_EDGE

#define APKS_STACK_SIZE       (1024)
#define APKS_QUEUE_NUM        (4)

typedef enum {
	APKS_MSG_ENTER = 0,
	APKS_MSG_WAKE,
} apks_msg_e;

typedef struct {
	apks_msg_e event;
} apks_msg_t;

static beken_thread_t s_thd = NULL;
static beken_queue_t s_queue = NULL;

static bk_err_t apks_send(apks_msg_e event)
{
	apks_msg_t msg;
	msg.event = event;

	if (s_queue == NULL) {
		return BK_FAIL;
	}
	return rtos_push_to_queue(&s_queue, &msg, BEKEN_NO_WAIT);
}

/* Runs in GPIO interrupt context; only queue the restore work. */
static void apks_wake_isr(gpio_id_t gpio_id)
{
	(void)gpio_id;
	(void)apks_send(APKS_MSG_WAKE);
}

static void apks_enter_low_voltage(void)
{
	bk_err_t ret;

#if CONFIG_ADC_KEY
	/* GPIO28 is S4's ADC channel while the AP is alive. Release the sampler
	 * here, before the AP is gone, so nothing reconfigures the pad after the
	 * CP has taken it over as a plain digital input. */
	(void)bk_adc_key_deinit_ex();
	(void)bk_saradc_driver_deinit();
#endif

	ret = bk_gpio_register_isr(APKS_WAKE_GPIO, apks_wake_isr);
	LOGI("gpio%d isr ret=%d level=%d\r\n", APKS_WAKE_GPIO, ret,
	     bk_gpio_get_input(APKS_WAKE_GPIO));

	ret = bk_gpio_set_wakeup(APKS_WAKE_GPIO, APKS_WAKE_INT_TYPE, true);
	LOGI("gpio%d wake arm ret=%d\r\n", APKS_WAKE_GPIO, ret);

	ret = bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_APP, 0x1, 0x0);
	LOGI("cp sleep vote ret=%d\r\n", ret);

	ret = bk_pm_module_vote_boot_ap_ctrl(PM_BOOT_AP_MODULE_NAME_APP,
					     PM_POWER_MODULE_STATE_OFF);
	LOGI("ap power off vote ret=%d\r\n", ret);

	if (ret != BK_OK) {
		/* The AP stays up: the ADC keys were released above, so bring them
		 * back. */
		(void)ap_key_restore();
	}
}

static void apks_handle_wake(void)
{
	(void)bk_pm_ap_sleep_mode_set(PM_MODE_DEFAULT);
	(void)bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_APP, 0x0, 0x0);
	LOGI("wake restore done, src=%d\r\n", bk_pm_exit_low_vol_wakeup_source_get());
}

static void apks_thread(void *arg)
{
	apks_msg_t msg;

	(void)arg;

	while (1) {
		if (rtos_pop_from_queue(&s_queue, &msg, BEKEN_WAIT_FOREVER) != BK_OK) {
			continue;
		}

		switch (msg.event) {
		case APKS_MSG_ENTER:
			apks_enter_low_voltage();
			break;
		case APKS_MSG_WAKE:
			apks_handle_wake();
			break;
		default:
			break;
		}
	}
}

bk_err_t ap_key_sleep_enter(void)
{
	return apks_send(APKS_MSG_ENTER);
}

static void cmd_apks(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
	char *msg;

	if ((argc >= 2) && (os_strcmp(argv[1], "enter") == 0)) {
		(void)apks_send(APKS_MSG_ENTER);
	} else if ((argc >= 2) && (os_strcmp(argv[1], "wake") == 0)) {
		(void)apks_send(APKS_MSG_WAKE);
	} else {
		CLI_LOGI("Usage:\r\n"
			 " apks enter  - power off AP, CP enters low voltage, GPIO28 wakes\r\n"
			 " apks wake   - restore the sleep votes manually\r\n");
		msg = CLI_CMD_RSP_ERROR;
		os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
		return;
	}

	msg = CLI_CMD_RSP_SUCCEED;
	os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
}

static const struct cli_command s_apks_commands[] = {
	{"apks", "apks {enter|wake}", cmd_apks},
};

int ap_key_sleep_init(void)
{
	int rc;

	rc = cli_register_commands(s_apks_commands,
				   sizeof(s_apks_commands) / sizeof(s_apks_commands[0]));
	if (rc != 0) {
		LOGW("cli register rc=%d\r\n", rc);
	}

	/* Fast resume keeps s_thd/s_queue alive; do not create duplicates. */
	if (s_queue == NULL) {
		if (rtos_init_queue(&s_queue, "apks_queue", sizeof(apks_msg_t),
				    APKS_QUEUE_NUM) != BK_OK) {
			LOGE("queue init failed\r\n");
			return BK_FAIL;
		}
	}

	if (s_thd == NULL) {
		if (rtos_create_thread(&s_thd, BEKEN_DEFAULT_WORKER_PRIORITY, "apks",
				       (beken_thread_function_t)apks_thread,
				       APKS_STACK_SIZE, NULL) != BK_OK) {
			LOGE("thread create failed\r\n");
			return BK_FAIL;
		}
	}

	return BK_OK;
}
