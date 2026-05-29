#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ROBOT_VIDEO_CODEC_H264 = 0,
    ROBOT_VIDEO_CODEC_MJPEG,
} robot_video_codec_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t fps;
    uint16_t bitrate_kbps;
    robot_video_codec_t codec;
} robot_video_config_t;

typedef struct {
    bool running;
    robot_video_config_t config;
} robot_video_status_t;

#ifdef __cplusplus
}
#endif
