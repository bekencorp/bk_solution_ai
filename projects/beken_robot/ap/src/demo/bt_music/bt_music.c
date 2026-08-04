/**
 * @file bt_music.c
 * @brief Demo catalog glue for the Bluetooth music page.
 *
 * Same split as robot_video.c: start enters the LVGL page and brings up the
 * backend; stop tears down a2dp_sink + bt_rhythm. On page exit the UI calls
 * g_demo_bt_music.stop(); do not duplicate backend teardown in page code.
 */
#include <os/os.h>
#include <components/log.h>

#include "demo/bt_music.h"
#include "demo/a2dp_sink.h"
#include "demo/bt_rhythm.h"
#include "audio_engine.h"
#include "page_bt_music.h"
#include "bk_wifi.h"

#define TAG "bt_music"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BT_MUSIC_START_MIN_HEAP  (18U * 1024U)

static bool s_wifi_paused;
static void bt_music_wifi_pause(void)
{
#if CONFIG_BK_SMART_CONFIG
    /* Always stop STA on enter regardless of link/provisioned flags. sta_stop() is
     * a no-op if already stopped; avoids RF conflict when s_network_provisioned
     * was cleared by RECONNECT_FAILED while WiFi scan/assoc still runs. */
    LOGI("pause wifi for bt music\n");
    if (bk_wifi_sta_stop() == BK_OK) {
        s_wifi_paused = true;
    } else {
        s_wifi_paused = false;
        LOGW("wifi stop failed\n");
    }
#else
    (void)s_wifi_paused;
#endif
}

static void bt_music_wifi_resume(void)
{
#if CONFIG_BK_SMART_CONFIG
    if (!s_wifi_paused) {
        return;
    }

    s_wifi_paused = false;
    LOGI("resume wifi after bt music\n");
    if (bk_wifi_sta_start() != BK_OK) {
        LOGW("wifi start failed\n");
        return;
    }
#endif
}

int bt_music_init(void)
{
    return 0;
}

int bt_music_start(void)
{
    uint32_t heap_before_bt;

    if (audio_engine_is_running()) {
        (void)audio_engine_stop();
    }

    heap_before_bt = rtos_get_free_heap_size();
    if (heap_before_bt < BT_MUSIC_START_MIN_HEAP) {
        LOGW("start blocked, iram free=%u min=%u\n",
             (unsigned)heap_before_bt,
             (unsigned)rtos_get_minimum_free_heap_size());
        page_bt_music_show_low_mem_hint();
        return -1;
    }

    bt_music_wifi_pause();

    if (page_bt_music_enter() != 0) {
        LOGW("page enter failed\n");
        bt_music_wifi_resume();
        return -1;
    }

#if CONFIG_BT
    if (a2dp_sink_demo_start(0 /* aac off */, 1 /* auto-accept */) != 0) {
        LOGE("a2dp_sink_demo_start failed\n");
        (void)a2dp_sink_demo_stop();
        bt_music_wifi_resume();
        page_bt_music_show_low_mem_hint();
        return -1;
    }
#endif

    return 0;
}

int bt_music_stop(void)
{
    bt_rhythm_deinit();
#if CONFIG_BT
    (void)a2dp_sink_demo_stop();
#endif
    bt_music_wifi_resume();
    return 0;
}

const bk_demo_iface_t g_demo_bt_music = {
    .name  = "bt_music",
    .init  = bt_music_init,
    .start = bt_music_start,
    .stop  = bt_music_stop,
};
