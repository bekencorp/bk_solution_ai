// Copyright 2020-2021 Beken
// Ported from doorbell reference project for beken_robot multimedia_device_service component.

#pragma once

#include <common/avdk_pixel_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    struct {
        uint8_t enable;
        uint16_t degree;
        uint16_t src_width;
        uint16_t src_height;
        uint16_t dst_width;
        uint16_t dst_height;
        uint16_t tess_width;
        uint16_t tess_height;
        bk_pixel_format_t src_format;
        bk_pixel_format_t dst_format;
        bool scale;
        bool dst_compress;
        void (*frame_done)(void *frame, uint32_t frame_size, void *args);
        void *frame_done_args;
    } flexa;
} gpu_board_config_t;

#ifdef __cplusplus
}
#endif
