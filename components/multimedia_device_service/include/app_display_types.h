// Copyright 2020-2021 Beken
// Ported from doorbell reference project for beken_robot multimedia_device_service component.

#pragma once
#include <components/bk_display_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    struct {
        uint8_t enable;
        int8_t pin_reset;
        int8_t pin_scl;
        int8_t pin_sda;
        const bk_display_dsi_panel_t *panel;
    } mipi;

    struct {
        bool enable;
        bool decompress;
        bk_pixel_format_t format;
    } dpu_video;

    struct {
        uint8_t enable;
    } rgb;
} display_board_config_t;

#ifdef __cplusplus
}
#endif
