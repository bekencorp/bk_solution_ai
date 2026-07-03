#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <common/bk_err.h>

typedef struct {
    uint16_t bg_width;
    uint16_t bg_height;
    uint32_t bg_frame_size;
    uint16_t fg_width;
    uint16_t fg_height;
    uint16_t fg_x;
    uint16_t fg_y;
} bk_camera_lvgl_blend_config_t;

typedef struct {
    int16_t x1;
    int16_t y1;
    int16_t x2;
    int16_t y2;
} bk_camera_lvgl_blend_rect_t;

bk_err_t bk_camera_lvgl_blend_start(const bk_camera_lvgl_blend_config_t *config);
bk_err_t bk_camera_lvgl_blend_stop(void);
bool bk_camera_lvgl_blend_is_active(void);

bk_err_t bk_camera_lvgl_blend_push_camera_frame(void *frame, uint32_t frame_size);
bk_err_t bk_camera_lvgl_blend_update_lvgl_frame(void *frame, int (*release_cb)(void *args));
bk_err_t bk_camera_lvgl_blend_set_boxes(const bk_camera_lvgl_blend_rect_t *rects, uint32_t count);

#ifdef __cplusplus
}
#endif
