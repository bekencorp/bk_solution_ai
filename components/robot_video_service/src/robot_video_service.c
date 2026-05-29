#include "robot_video_service.h"

#include <os/mem.h>
#include <components/log.h>
#include "video_engine.h"

#define TAG "robot_video"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static bool s_initialized;
static robot_video_config_t s_config;

bk_err_t robot_video_service_init(void)
{
    if (!s_initialized) {
        robot_video_service_default_config(&s_config);
        s_initialized = true;
    }
    return BK_OK;
}

bk_err_t robot_video_service_deinit(void)
{
    robot_video_service_stop();
    s_initialized = false;
    return BK_OK;
}

bk_err_t robot_video_service_start(const robot_video_config_t *cfg)
{
    if (!s_initialized) {
        robot_video_service_init();
    }

    if (cfg) {
        s_config = *cfg;
    }

    if (video_engine_is_running()) {
        LOGI("video already running\n");
        return BK_OK;
    }

    int ret = video_engine_init();
    if (ret != BK_OK) {
        LOGE("video_engine_init failed ret=%d\n", ret);
        return BK_FAIL;
    }

    LOGI("video started %ux%u@%u bitrate=%u\n",
         s_config.width, s_config.height, s_config.fps, s_config.bitrate_kbps);
    return BK_OK;
}

bk_err_t robot_video_service_stop(void)
{
    if (!video_engine_is_running()) {
        return BK_OK;
    }

    int ret = video_engine_deinit();
    if (ret != BK_OK) {
        LOGE("video_engine_deinit failed ret=%d\n", ret);
        return BK_FAIL;
    }

    LOGI("video stopped\n");
    return BK_OK;
}

bk_err_t robot_video_service_request_keyframe(void)
{
    LOGW("request keyframe is not supported by current video_engine\n");
    return BK_FAIL;
}

bk_err_t robot_video_service_set_bitrate(uint16_t bitrate_kbps)
{
    s_config.bitrate_kbps = bitrate_kbps;
    LOGW("set bitrate is not supported by current video_engine\n");
    return BK_FAIL;
}

bool robot_video_service_is_running(void)
{
    return video_engine_is_running();
}

bk_err_t robot_video_service_get_status(robot_video_status_t *status)
{
    if (!status) {
        return BK_ERR_PARAM;
    }

    status->running = robot_video_service_is_running();
    status->config = s_config;
    return BK_OK;
}
