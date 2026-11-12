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
 *   "apks rtc <ms> [cnt]" takes the same power-off path but swaps the GPIO
 *   wake source for a periodic RTC wake source through
 *   bk_pm_ap_rtc_regsiter_wakeup(); the RTC expiry fast-boots the AP and the
 *   wakeup callback restores the votes.
 */
#include <common/sys_config.h>

#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <os/str.h>
#include <components/shell_task.h>
#include <driver/gpio.h>
#include <driver/gpio_types.h>
#include <driver/pwr_clk.h>
#include <modules/pm.h>
#include "driver/pm_ap_core.h"
#include "cli.h"

#include "ap_key.h"
#include "pm_ap_key_sleep.h"

#if CONFIG_AUDIO_PLAY
#include "spk_service.h"
#endif

#if CONFIG_LVGL
extern void bk_robot_display_power_off(void);
extern bk_err_t bk_robot_display_resume(void *arg);
#endif

#define TAG "apks"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* S4 sits on GPIO28 / ADC channel 4 on this board. */
#define APKS_WAKE_GPIO        ((gpio_id_t)28)
#define APKS_WAKE_INT_TYPE    GPIO_INT_TYPE_FALLING_EDGE

#define APKS_STACK_SIZE       (6144)
#define APKS_QUEUE_NUM        (4)

typedef enum {
	APKS_MSG_ENTER = 0,
	APKS_MSG_WAKE,
	APKS_MSG_RTC_ENTER,
} apks_msg_e;

typedef struct {
	apks_msg_e event;
	uint32_t param1;
	uint32_t param2;
} apks_msg_t;

static beken_thread_t s_thd = NULL;
static beken_queue_t s_queue = NULL;

static bk_err_t apks_send_ex(apks_msg_e event, uint32_t param1, uint32_t param2)
{
	apks_msg_t msg;
	msg.event = event;
	msg.param1 = param1;
	msg.param2 = param2;

	if (s_queue == NULL) {
		return BK_FAIL;
	}
	return rtos_push_to_queue(&s_queue, &msg, BEKEN_NO_WAIT);
}

static bk_err_t apks_send(apks_msg_e event)
{
	return apks_send_ex(event, 0, 0);
}

/* Runs in GPIO interrupt context; only queue the restore work. */
static void apks_wake_isr(gpio_id_t gpio_id)
{
	(void)gpio_id;
	(void)apks_send(APKS_MSG_WAKE);
}

/* Common teardown before arming a wake source and voting the AP off.
 * Returns BK_OK when it is safe to proceed to the vote sequence. */
static bk_err_t apks_power_off_prepare(void)
{
	bk_err_t ret = BK_OK;

#if CONFIG_AUDIO_PLAY
	/* Tear the speaker/DAC down now instead of waiting for the idle-linger
	 * timer: the DAC DMA channels are still busy at the PM fast-suspend
	 * quiesce otherwise and the whole power-off is aborted ("AP fast suspend:
	 * DMA still busy"). A2DP/HFP lazily re-init the speaker on the next
	 * stream, so nothing needs to be restored on the resume path. */
	if (spk_service_is_running()) {
		LOGI("speaker active, deinit before AP off\r\n");
		(void)spk_service_deinit();
	}
#endif

#if CONFIG_LVGL
	/* Stop LVGL and tear down the display hardware while retaining the current
	 * page. The PM resume callback schedules the hardware restore. */
	bk_robot_display_power_off();
#endif

#if CONFIG_ADC_KEY
	/* Unregister the keys and stop ADC sampling before GPIO28 becomes a plain
	 * wake input. The ADC key manager/timer/mutex stay resident across AP off. */
	ret = ap_key_power_off();
	if (ret != BK_OK) {
		LOGE("adc key power off failed: %d\r\n", ret);
#if CONFIG_LVGL
		(void)bk_robot_display_resume(NULL);
#endif
	}
#endif

	return ret;
}

/* Undo apks_power_off_prepare() when the AP could not be powered off. */
static void apks_power_off_abort(void)
{
	(void)ap_key_restore();
#if CONFIG_LVGL
	(void)bk_robot_display_resume(NULL);
#endif
}

static void apks_enter_low_voltage(void)
{
	bk_err_t ret;

	ret = apks_power_off_prepare();
	if (ret != BK_OK) {
		return;
	}

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
		/* The AP stays up: restore the ADC keys and restart the display. */
		apks_power_off_abort();
	}
}

/* Runs in the PM wakeup path after the RTC fast-boots the AP; queue the
 * restore work just like the GPIO replay does. */
static bk_err_t apks_rtc_wakeup_callback(pm_sleep_mode_e sleep_mode,
					 pm_wakeup_source_e wake_source, void *param_p)
{
	(void)sleep_mode;
	(void)wake_source;
	(void)param_p;
	(void)apks_send(APKS_MSG_WAKE);
	return BK_OK;
}

static void apks_enter_low_voltage_rtc(uint32_t sleep_ms, uint32_t repeat_cnt)
{
	bk_err_t ret;
	pm_ap_rtc_info_t rtc_info = {0};

	if (sleep_ms == 0) {
		sleep_ms = 50 * 1000;
	}
	if (repeat_cnt == 0) {
		repeat_cnt = 1;
	}

	ret = apks_power_off_prepare();
	if (ret != BK_OK) {
		return;
	}

	/* RTC periodic wake replaces the GPIO wake source: no ISR is registered
	 * and GPIO28 stays with the ADC key manager on the AP side. */
	rtc_info.period_tick = sleep_ms;
	rtc_info.period_cnt  = repeat_cnt;
	rtc_info.callback    = apks_rtc_wakeup_callback;
	rtc_info.param_p     = NULL;
	ret = bk_pm_ap_rtc_regsiter_wakeup(PM_MODE_LOW_VOLTAGE, &rtc_info);
	LOGI("rtc wake register ret=%d period=%ums cnt=%u\r\n", ret, sleep_ms,
	     repeat_cnt);
	if (ret != BK_OK) {
		apks_power_off_abort();
		return;
	}

	ret = bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_APP, 0x1, 0x0);
	LOGI("cp sleep vote ret=%d\r\n", ret);

	ret = bk_pm_module_vote_boot_ap_ctrl(PM_BOOT_AP_MODULE_NAME_APP,
					     PM_POWER_MODULE_STATE_OFF);
	LOGI("ap power off vote ret=%d\r\n", ret);

	if (ret != BK_OK) {
		apks_power_off_abort();
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
		case APKS_MSG_RTC_ENTER:
			apks_enter_low_voltage_rtc(msg.param1, msg.param2);
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
	} else if ((argc >= 3) && (os_strcmp(argv[1], "rtc") == 0)) {
		uint32_t sleep_ms = (uint32_t)os_strtoul(argv[2], NULL, 10);
		uint32_t repeat_cnt = (argc >= 4) ?
				      (uint32_t)os_strtoul(argv[3], NULL, 10) : 1;
		(void)apks_send_ex(APKS_MSG_RTC_ENTER, sleep_ms, repeat_cnt);
	} else {
		CLI_LOGI("Usage:\r\n"
			 " apks enter        - power off AP, CP enters low voltage, GPIO28 wakes\r\n"
			 " apks rtc <ms> [n] - power off AP, CP enters low voltage, RTC wakes\r\n"
			 "                     ms: wake period in milliseconds (0 = default 50s)\r\n"
			 "                     n:  repeat count (0 = default 1, 0xffffffff = forever)\r\n"
			 " apks wake         - restore the sleep votes manually\r\n");
		msg = CLI_CMD_RSP_ERROR;
		os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
		return;
	}

	msg = CLI_CMD_RSP_SUCCEED;
	os_memcpy(pcWriteBuffer, msg, os_strlen(msg));
}

static const struct cli_command s_apks_commands[] = {
	{"apks", "apks {enter|wake|rtc <ms> [cnt]}", cmd_apks},
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
