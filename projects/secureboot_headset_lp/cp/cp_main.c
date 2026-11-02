#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <modules/pm.h>
#include <driver/pwr_clk.h>
#include <components/log.h>
#include <string.h>
#include "cli.h"
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

static bk_err_t ap_fast_boot_vote(pm_power_module_state_e state)
{
	bk_err_t ret = bk_pm_module_vote_boot_ap_ctrl(PM_BOOT_AP_MODULE_NAME_APP, state);

	BK_LOGI(TAG, "AP vote %s ret=%d\r\n",
		(state == PM_POWER_MODULE_STATE_ON) ? "ON" : "OFF", ret);
	return ret;
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
}

static void cli_ap_fast_boot_cmd(char *pcWriteBuffer, int xWriteBufferLen,
	int argc, char **argv)
{
	(void)pcWriteBuffer;
	(void)xWriteBufferLen;

	if (argc != 2) {
		BK_LOGI(TAG, "usage: ap_fast_boot on|off|cycle\r\n");
		return;
	}

	if (strcmp(argv[1], "on") == 0) {
		(void)ap_fast_boot_vote(PM_POWER_MODULE_STATE_ON);
	} else if (strcmp(argv[1], "off") == 0) {
		(void)ap_fast_boot_vote(PM_POWER_MODULE_STATE_OFF);
	} else if (strcmp(argv[1], "cycle") == 0) {
		if (ap_fast_boot_vote(PM_POWER_MODULE_STATE_OFF) == BK_OK) {
			rtos_delay_milliseconds(1000);
			(void)ap_fast_boot_vote(PM_POWER_MODULE_STATE_ON);
		}
	} else {
		BK_LOGI(TAG, "usage: ap_fast_boot on|off|cycle\r\n");
	}
}

static const struct cli_command s_ap_fast_boot_commands[] = {
	{
		"ap_fast_boot",
		"ap_fast_boot on|off|cycle",
		cli_ap_fast_boot_cmd,
	},
};

int main(void)
{
	rtos_set_user_app_entry((beken_thread_function_t)user_app_main);
	bk_init();
    cli_register_commands(s_ap_fast_boot_commands,
		sizeof(s_ap_fast_boot_commands) / sizeof(s_ap_fast_boot_commands[0]));
	BK_LOGI(TAG, "CLI ready: ap_fast_boot on|off|cycle\r\n");

	return 0;
}
