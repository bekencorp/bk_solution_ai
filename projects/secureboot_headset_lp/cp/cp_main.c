#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <modules/pm.h>
#include <driver/pwr_clk.h>
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

/* Let the CP enter low voltage by default. The AP module vote (managed by
 * bk_pm_ap_ctrl) still keeps the CP awake while the AP is running: it is
 * voted 0 on AP power-on and 1 once the AP has been powered off, so this
 * one-time vote only takes effect while the AP is down. */
#define CP_AUTO_SLEEP_VOTE_DELAY_MS  (3000)
#define CP_AUTO_SLEEP_THREAD_STACK   (2048)

static beken_thread_t s_cp_sleep_vote_thd = NULL;

static bk_err_t ap_fast_boot_vote(pm_power_module_state_e state)
{
	bk_err_t ret = bk_pm_module_vote_boot_ap_ctrl(PM_BOOT_AP_MODULE_NAME_APP, state);

	BK_LOGI(TAG, "AP vote %s ret=%d\r\n",
		(state == PM_POWER_MODULE_STATE_ON) ? "ON" : "OFF", ret);
	return ret;
}

static void cp_auto_sleep_vote_thread(void *arg)
{
	(void)arg;

	/* Give the AP time to finish its post-boot display rebuild. */
	rtos_delay_milliseconds(CP_AUTO_SLEEP_VOTE_DELAY_MS);

	bk_err_t ret = bk_pm_module_vote_sleep_ctrl(PM_SLEEP_MODULE_NAME_APP, 1, 0);

	BK_LOGI(TAG, "CP auto sleep vote ret=%d\r\n", ret);

	/* A FreeRTOS task must not return from its entry function. The vote is
	 * one-shot, so release the thread explicitly. */
	s_cp_sleep_vote_thd = NULL;
	rtos_delete_thread(NULL);
}

static void cp_auto_sleep_vote_start(void)
{
	if (s_cp_sleep_vote_thd != NULL) {
		return;
	}

	if (rtos_create_thread(&s_cp_sleep_vote_thd, BEKEN_DEFAULT_WORKER_PRIORITY,
			       "cp_sleep_vote",
			       cp_auto_sleep_vote_thread,
			       CP_AUTO_SLEEP_THREAD_STACK, NULL) != BK_OK) {
		s_cp_sleep_vote_thd = NULL;
		BK_LOGE(TAG, "cp sleep vote thread create failed\r\n");
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

	cp_auto_sleep_vote_start();
}


int main(void)
{
	rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
	bk_init();

	return 0;
}
