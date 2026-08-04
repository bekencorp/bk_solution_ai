/**
 * @file vision.c
 * @brief Backend for the vision recognition demo (page_7). LVGL-free:
 *        wraps bk_smart_config "vision mode" start/exit.
 */
#include "demo/vision.h"

#ifdef ROBOT_TEST

#include "audio_engine.h"
#include "bk_smart_config.h"
#if CONFIG_BK_NETWORK_ENGINE
#include "network_engine.h"
#endif
#if CONFIG_APP_EVT
#include "app_event.h"
#endif
#include <components/log.h>

#define TAG "vision_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

int vision_start_service(void)
{
#if CONFIG_BK_NETWORK_ENGINE
    if (ntwk_eng_rtc_init() != 0) {
        LOGW("ntwk_eng_rtc_init failed\r\n");
        return -1;
    }
#endif
    if (!audio_engine_is_running() && audio_engine_init() != AUDIO_ENGINE_SUCCESS) {
        LOGW("Vision recognition restore audio engine failed\r\n");
        return -1;
    }
    if (bk_sconf_enter_vision_mode() != BK_OK) {
        return -1;
    }

    return 0;
}

int vision_request_exit(void)
{
    /* Cancel any still-pending early camera bring-up before requesting the
     * (serialized) teardown, so a late worker can't re-open the camera. */
    bk_sconf_vision_video_prestop();
    int ret = (bk_sconf_exit_ai_mode_async(1) == BK_OK) ? 0 : -1;
    if (audio_engine_is_running()) {
        (void)audio_engine_stop();
    }
    return ret;
}

int vision_init(void)  { return 0; }

extern int page_vision_enter(void);

int vision_start(void)
{
    LOGI("Vision recognition -> page_7\r\n");
    /* Kick the camera/video-engine bring-up as early as possible (before the
     * page loads and independent of the sconf RTC op queue) so the local
     * preview appears promptly instead of waiting behind a prior mode op.
     * Safe w.r.t. an in-flight exit: prestart only sets the want latch / worker;
     * video_engine_init is serialized on s_vision_video_lock with deinit, and
     * page_vision_preview refuses to start until video_engine_is_preview_ready()
     * (false for the whole stop/deinit barrier */
    (void)bk_sconf_vision_video_prestart();
    return page_vision_enter();
}

int vision_stop(void)
{
    (void)vision_request_exit();
    return 0;
}

#else  /* !ROBOT_TEST */

int vision_start_service(void) { return 0; }
int vision_request_exit(void)  { return 0; }
int vision_init(void)  { return 0; }
int vision_start(void) { return 0; }
int vision_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_vision = {
    "vision",
    vision_init,
    vision_start,
    vision_stop,
};
