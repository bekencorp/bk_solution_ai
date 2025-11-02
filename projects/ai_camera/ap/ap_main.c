#include "bk_private/bk_init.h"
#include <components/system.h>
#include <os/os.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#include <media_service.h>
#if CONFIG_BK_NETWORK_TRANSFER
#include "network_transfer.h"
#endif
#if CONFIG_BK_AUDIO_ENGINE
#include "audio_engine.h"
#endif
#if CONFIG_BK_SMART_CONFIG
#include "bk_smart_config.h"
#endif
#if CONFIG_APP_EVT
#include "app_event.h"
#endif

#define TAG "ap_main"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

int main(void)
{
    bk_init();

#if CONFIG_GSENSOR_ENABLE
    extern bk_err_t gsensor_demo_open();
    gsensor_demo_open();
    extern bk_err_t gsensor_demo_lowpower_wakeup();
    gsensor_demo_lowpower_wakeup();
#endif

    media_service_init();
#if CONFIG_APP_EVT
    app_event_init();
#endif

#if CONFIG_BK_NETWORK_TRANSFER
    ntwk_trans_init();
#endif

#if CONFIG_BK_SMART_CONFIG
    bk_sconf_init();
#endif

#if CONFIG_BK_AUDIO_ENGINE
    audio_engine_init();
#endif

	return 0;
}
