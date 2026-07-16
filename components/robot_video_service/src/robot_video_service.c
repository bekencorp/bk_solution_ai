#include "robot_video_service.h"

#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include "video_engine.h"

#define TAG "robot_video"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

static bool s_initialized;
static robot_video_config_t s_config;
static beken_mutex_t s_video_lock;

static bk_err_t robot_video_service_ensure_lock(void)
{
    if (s_video_lock) {
        return BK_OK;
    }

    if (rtos_init_mutex(&s_video_lock) != BK_OK) {
        LOGE("video service mutex init failed\n");
        return BK_FAIL;
    }

    return BK_OK;
}

bk_err_t robot_video_service_init(void)
{
    if (!s_initialized) {
        if (robot_video_service_ensure_lock() != BK_OK) {
            return BK_FAIL;
        }
        robot_video_service_default_config(&s_config);
        s_initialized = true;
    }
    return BK_OK;
}

bk_err_t robot_video_service_deinit(void)
{
    robot_video_service_stop();
    if (s_video_lock) {
        rtos_deinit_mutex(&s_video_lock);
        s_video_lock = NULL;
    }
    s_initialized = false;
    return BK_OK;
}

bk_err_t robot_video_service_start(const robot_video_config_t *cfg)
{
    bk_err_t result = BK_OK;

    LOGI("robot_video_service_start enter initialized=%d running=%d cfg=%p\n",
         s_initialized, video_engine_is_running(), cfg);

    if (!s_initialized) {
        if (robot_video_service_init() != BK_OK) {
            return BK_FAIL;
        }
    } else {
        if (robot_video_service_ensure_lock() != BK_OK) {
            return BK_FAIL;
        }
    }

    LOGI("robot_video_service_start wait lock\n");
    rtos_lock_mutex(&s_video_lock);
    LOGI("robot_video_service_start lock acquired\n");

    if (cfg) {
        s_config = *cfg;
    }

    if (video_engine_is_running()) {
        LOGI("video already running\n");
        goto exit;
    }

    int ret = video_engine_init();
    if (ret != BK_OK) {
        LOGE("video_engine_init failed ret=%d\n", ret);
        result = BK_FAIL;
        goto exit;
    }

    LOGI("video started %ux%u@%u bitrate=%u\n",
         s_config.width, s_config.height, s_config.fps, s_config.bitrate_kbps);

exit:
    rtos_unlock_mutex(&s_video_lock);
    return result;
}

bk_err_t robot_video_service_stop(void)
{
    bk_err_t result = BK_OK;

    if (robot_video_service_ensure_lock() != BK_OK) {
        return BK_FAIL;
    }

    LOGI("robot_video_service_stop wait lock\n");
    rtos_lock_mutex(&s_video_lock);
    LOGI("robot_video_service_stop lock acquired\n");

    bool running = video_engine_is_running();
    LOGI("robot_video_service_stop enter initialized=%d running=%d\n", s_initialized, running);

    if (!running) {
        LOGI("robot_video_service_stop skip, video engine not running\n");
        goto exit;
    }

    LOGI("robot_video_service_stop before video_engine_deinit\n");
    int ret = video_engine_deinit();
    LOGI("robot_video_service_stop after video_engine_deinit ret=%d running=%d\n",
         ret, video_engine_is_running());
    if (ret != BK_OK) {
        LOGE("video_engine_deinit failed ret=%d\n", ret);
        result = BK_FAIL;
        goto exit;
    }

    LOGI("video stopped\n");

exit:
    rtos_unlock_mutex(&s_video_lock);
    return result;
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
