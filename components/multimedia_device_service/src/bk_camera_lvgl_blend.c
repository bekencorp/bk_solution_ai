#include "bk_camera_lvgl_blend.h"

#include <common/bk_include.h>
#include <os/mem.h>
#include <os/os.h>
#include <modules/vg_lite_gpu/vg_lite.h>

#include "app_display.h"
#include "app_gpu.h"
#include "lv_camera_blend.h"

#define TAG "cam-lvgl-blend"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BLEND_QUEUE_DEPTH       4
#define BLEND_THREAD_STACK_SIZE (1024 * 6)
#define BLEND_THREAD_PRIORITY   BEKEN_DEFAULT_WORKER_PRIORITY
#define BLEND_MAX_BOXES         8
#define BLEND_BOX_BORDER_W      2

typedef struct {
    bool active;
    bool stopping;
    bool suspended;
    bk_camera_lvgl_blend_config_t config;
    lv_camera_blend_async_handle_t async_handle;
    beken_mutex_t box_mutex;
    bk_camera_lvgl_blend_rect_t boxes[BLEND_MAX_BOXES];
    uint32_t box_count;
} blend_ctx_t;

static blend_ctx_t s_blend;

static void blend_draw_box_edge(vg_lite_buffer_t *dst_buf, int32_t x, int32_t y,
                                int32_t w, int32_t h, vg_lite_color_t color)
{
    if (w <= 0 || h <= 0) {
        return;
    }

    vg_lite_rectangle_t rect = {
        .x = (int16_t)x,
        .y = (int16_t)y,
        .width = (int16_t)w,
        .height = (int16_t)h,
    };
    (void)vg_lite_clear(dst_buf, &rect, color);
}

static void blend_draw_boxes(vg_lite_buffer_t *dst_buf)
{
    bk_camera_lvgl_blend_rect_t boxes[BLEND_MAX_BOXES];
    uint32_t count = 0;
    const bk_camera_lvgl_blend_config_t *cfg = &s_blend.config;
    const int32_t border = BLEND_BOX_BORDER_W;
    const vg_lite_color_t color = 0xFF00FF00;

    rtos_lock_mutex(&s_blend.box_mutex);
    count = s_blend.box_count;
    if (count > BLEND_MAX_BOXES) {
        count = BLEND_MAX_BOXES;
    }
    os_memcpy(boxes, s_blend.boxes, count * sizeof(boxes[0]));
    rtos_unlock_mutex(&s_blend.box_mutex);

    for (uint32_t i = 0; i < count; i++) {
        int32_t x1 = cfg->fg_x + boxes[i].x1;
        int32_t y1 = cfg->fg_y + boxes[i].y1;
        int32_t x2 = cfg->fg_x + boxes[i].x2;
        int32_t y2 = cfg->fg_y + boxes[i].y2;

        if (x1 < 0) x1 = 0;
        if (y1 < 0) y1 = 0;
        if (x2 >= cfg->bg_width) x2 = cfg->bg_width - 1;
        if (y2 >= cfg->bg_height) y2 = cfg->bg_height - 1;
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        int32_t width = x2 - x1 + 1;
        int32_t height = y2 - y1 + 1;

        blend_draw_box_edge(dst_buf, x1, y1, width, border, color);
        blend_draw_box_edge(dst_buf, x1, y2 - border + 1, width, border, color);
        blend_draw_box_edge(dst_buf, x1, y1, border, height, color);
        blend_draw_box_edge(dst_buf, x2 - border + 1, y1, border, height, color);
    }
}

static bk_err_t blend_gpu_lock_cb(void *user_data)
{
    (void)user_data;

    if (app_gpu_handle_get() == NULL || app_gpu_lock() != AVDK_ERR_OK) {
        LOGW("app_gpu_lock failed\n");
        return BK_FAIL;
    }

    return BK_OK;
}

static bk_err_t blend_gpu_unlock_cb(void *user_data)
{
    (void)user_data;

    if (app_gpu_unlock() != AVDK_ERR_OK) {
        LOGW("app_gpu_unlock failed\n");
        return BK_FAIL;
    }

    return BK_OK;
}

static bk_err_t blend_output_cb(void *user_data,
                                void *frame_buffer,
                                lv_camera_blend_free_cb_t free_cb)
{
    (void)user_data;

    return app_mipi_lcd_flush(frame_buffer, (avdk_err_t (*)(void *))free_cb) == AVDK_ERR_OK ?
           BK_OK :
           BK_FAIL;
}

static void blend_overlay_cb(void *user_data, vg_lite_buffer_t *output)
{
    (void)user_data;

    blend_draw_boxes(output);
}

static int blend_camera_frame_free_cb(void *frame)
{
    return app_gpu_frame_free(frame);
}

bk_err_t bk_camera_lvgl_blend_start(const bk_camera_lvgl_blend_config_t *config)
{
    if (config == NULL || config->bg_frame_size == 0 ||
        config->bg_width == 0 || config->bg_height == 0 ||
        config->fg_width == 0 || config->fg_height == 0) {
        return BK_ERR_PARAM;
    }

    if (s_blend.active) {
        return BK_OK;
    }

    os_memset(&s_blend, 0, sizeof(s_blend));
    s_blend.config = *config;

    if (rtos_init_mutex(&s_blend.box_mutex) != BK_OK) {
        LOGE("box mutex init failed\n");
        goto fail;
    }

    lv_camera_blend_async_config_t async_cfg = {
        .blend = {
            .width = config->bg_width,
            .height = config->bg_height,
            .lvgl_format = BK_PIXEL_FORMAT_ARGB8888,
            .lvgl_compress = true,
            .output_format = BK_PIXEL_FORMAT_ARGB8888,
            .output_compress = true,
        },
        .output_buffer_size = config->bg_frame_size,
        .camera_frame_cache_size = 0,
        .queue_depth = BLEND_QUEUE_DEPTH,
        .thread_stack_size = BLEND_THREAD_STACK_SIZE,
        .thread_priority = BLEND_THREAD_PRIORITY,
        .lock_cb = blend_gpu_lock_cb,
        .unlock_cb = blend_gpu_unlock_cb,
        .output_cb = blend_output_cb,
        .overlay_cb = blend_overlay_cb,
    };
    if (lv_camera_blend_async_start(&s_blend.async_handle, &async_cfg) != BK_OK) {
        LOGE("lv async camera blend start failed\n");
        goto fail;
    }

    s_blend.active = true;
    s_blend.stopping = false;

    LOGI("started bg=%ux%u fg=%ux%u@(%u,%u)\n",
         config->bg_width, config->bg_height,
         config->fg_width, config->fg_height,
         config->fg_x, config->fg_y);
    return BK_OK;

fail:
    if (s_blend.async_handle != NULL) {
        lv_camera_blend_async_stop(s_blend.async_handle);
    }
    if (s_blend.box_mutex != NULL) {
        rtos_deinit_mutex(&s_blend.box_mutex);
    }
    os_memset(&s_blend, 0, sizeof(s_blend));
    return BK_FAIL;
}

bk_err_t bk_camera_lvgl_blend_stop(void)
{
    if (!s_blend.active) {
        return BK_OK;
    }

    s_blend.stopping = true;
    s_blend.active = false;

    if (s_blend.async_handle != NULL) {
        lv_camera_blend_async_stop(s_blend.async_handle);
    }
    if (s_blend.box_mutex != NULL) {
        rtos_deinit_mutex(&s_blend.box_mutex);
    }

    os_memset(&s_blend, 0, sizeof(s_blend));
    LOGI("stopped\n");
    return BK_OK;
}

bk_err_t bk_camera_lvgl_blend_set_boxes(const bk_camera_lvgl_blend_rect_t *rects, uint32_t count)
{
    if (!s_blend.active || s_blend.box_mutex == NULL) {
        return BK_FAIL;
    }

    if (count > BLEND_MAX_BOXES) {
        count = BLEND_MAX_BOXES;
    }

    rtos_lock_mutex(&s_blend.box_mutex);
    if (count > 0 && rects != NULL) {
        os_memcpy(s_blend.boxes, rects, count * sizeof(s_blend.boxes[0]));
        s_blend.box_count = count;
    } else {
        s_blend.box_count = 0;
    }
    rtos_unlock_mutex(&s_blend.box_mutex);

    return BK_OK;
}

bool bk_camera_lvgl_blend_is_active(void)
{
    return s_blend.active;
}

void bk_camera_lvgl_blend_set_suspended(bool suspended)
{
    s_blend.suspended = suspended;
}

bk_err_t bk_camera_lvgl_blend_push_camera_frame(void *frame, uint32_t frame_size)
{
    if (frame == NULL) {
        return BK_ERR_PARAM;
    }

    if (!s_blend.active || s_blend.stopping || s_blend.suspended ||
        s_blend.async_handle == NULL) {
        app_gpu_frame_free(frame);
        return s_blend.suspended ? BK_OK : BK_FAIL;
    }

    lv_camera_blend_camera_frame_t camera = {
        .buffer = frame,
        .src_x = 0,
        .src_y = 0,
        .src_width = s_blend.config.fg_width,
        .src_height = s_blend.config.fg_height,
        .src_format = BK_PIXEL_FORMAT_ARGB8888,
        .src_compress = true,
        .dst_x = s_blend.config.fg_x,
        .dst_y = s_blend.config.fg_y,
        .rotate_degree = 0,
        .alpha_blend = true,
    };

    return lv_camera_blend_async_push_camera_frame(s_blend.async_handle,
                                                   &camera,
                                                   frame_size,
                                                   blend_camera_frame_free_cb);
}

bk_err_t bk_camera_lvgl_blend_update_lvgl_frame(void *frame, int (*release_cb)(void *args))
{
    if (!s_blend.active || s_blend.async_handle == NULL || frame == NULL) {
        return BK_FAIL;
    }

    if (s_blend.suspended) {
        if (release_cb != NULL) {
            (void)release_cb(frame);
        }
        return BK_OK;
    }

    return lv_camera_blend_async_update_lvgl_frame(s_blend.async_handle,
                                                   frame,
                                                   (lv_camera_blend_free_cb_t)release_cb);
}
