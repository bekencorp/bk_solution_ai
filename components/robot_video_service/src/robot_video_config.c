#include "robot_video_service.h"

void robot_video_service_default_config(robot_video_config_t *cfg)
{
    if (!cfg) {
        return;
    }

#ifdef CONFIG_BK_ROBOT_VIDEO_WIDTH
    cfg->width = CONFIG_BK_ROBOT_VIDEO_WIDTH;
#else
    cfg->width = 640;
#endif

#ifdef CONFIG_BK_ROBOT_VIDEO_HEIGHT
    cfg->height = CONFIG_BK_ROBOT_VIDEO_HEIGHT;
#else
    cfg->height = 480;
#endif

#ifdef CONFIG_BK_ROBOT_VIDEO_FPS
    cfg->fps = CONFIG_BK_ROBOT_VIDEO_FPS;
#else
    cfg->fps = 20;
#endif

#ifdef CONFIG_BK_ROBOT_VIDEO_BITRATE_KBPS
    cfg->bitrate_kbps = CONFIG_BK_ROBOT_VIDEO_BITRATE_KBPS;
#else
    cfg->bitrate_kbps = 1000;
#endif

    cfg->codec = ROBOT_VIDEO_CODEC_H264;
}
