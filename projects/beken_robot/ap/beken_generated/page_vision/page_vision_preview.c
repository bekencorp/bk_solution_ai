/**
 * @file page_vision_preview.c
 * @brief page_7 local camera preview rendered as an LVGL image.
 */
#include "page_vision_preview.h"

#include <components/bk_frame_buffer.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/os.h>
#include <string.h>

#include "video_engine.h"
#include "page_chat_anim.h"

#ifdef ROBOT_TEST

#define TAG "page_vision_prv"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Viewfinder corner-bracket frame. Geometry is shared with page_chat_anim.c
 * via page_chat_anim.h. This preview panel owns the whole viewfinder: the live
 * image fills the frame and the corner brackets + scan line are drawn ON TOP of
 * the image (as children of this foreground panel). The chat-anim root that
 * sits behind this panel therefore no longer draws its own brackets/scan line;
 * doing it here means they stay visible over the camera picture. */
#define VISION_FRAME_LEFT         PAGE_VISION_FRAME_LEFT
#define VISION_FRAME_TOP          PAGE_VISION_FRAME_TOP
#define VISION_FRAME_W            PAGE_VISION_FRAME_W
#define VISION_FRAME_H            PAGE_VISION_FRAME_H

/* Panel fills the whole frame. Both dims must stay even (RGB565 stride must be
 * 4-byte aligned) to avoid a frame-buffer overrun. */
#define VISION_PANEL_LEFT         VISION_FRAME_LEFT
#define VISION_PANEL_TOP          VISION_FRAME_TOP
#define VISION_PANEL_W            VISION_FRAME_W
#define VISION_PANEL_H            VISION_FRAME_H

/* Corner brackets + scan line drawn on top of the image (matches the look
 * previously produced by build_vision_overlay()). */
#define VISION_FRAME_COLOR        0x00aaff
#define VISION_CORNER_LEN         24
#define VISION_CORNER_THICK       3
/* Scan-line vertical travel per timer tick (33 ms) -> ~2.3 s per sweep. */
#define VISION_SCAN_STEP          2

#define VISION_PREVIEW_IN_W       160
#define VISION_PREVIEW_IN_H       144
/* 0 = the old 180 view rotated a further 180 deg, i.e. upright landscape. */
#define VISION_PREVIEW_ROTATE     0
#define VISION_PREVIEW_FPS        15
/* Output (and image widget) size equals the panel (whole frame). */
#define VISION_PREVIEW_OUT_W      VISION_PANEL_W
#define VISION_PREVIEW_OUT_H      VISION_PANEL_H
#define VISION_PREVIEW_BYTES      ((uint32_t)VISION_PREVIEW_OUT_W * VISION_PREVIEW_OUT_H * 2U)
#define VISION_PREVIEW_IMG_X      0
#define VISION_PREVIEW_IMG_Y      0

static lv_obj_t *s_panel;
static lv_obj_t *s_image;
static lv_obj_t *s_status;
static lv_obj_t *s_scan;
static int32_t s_scan_y;
static int8_t s_scan_dir = 1;
static lv_timer_t *s_timer;
static beken_mutex_t s_frame_mutex;
static uint8_t s_mutex_ready;
static uint8_t s_active;
static uint8_t s_started;
static uint8_t s_write_index;
static int8_t s_pending_index = -1;
static uint8_t *s_frame_buf[2];
static uint32_t s_start_attempts;
static lv_image_dsc_t s_img_dsc;

static void page_vision_preview_free_buffers(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_frame_buf[i] != NULL) {
            bk_frame_buffer_free(s_frame_buf[i]);
            s_frame_buf[i] = NULL;
        }
    }
}

static void page_vision_preview_sink(const uint8_t *rgb565,
                                     uint16_t width,
                                     uint16_t height,
                                     void *user_data)
{
    (void)user_data;

    if (!s_active || rgb565 == NULL ||
        width != VISION_PREVIEW_OUT_W ||
        height != VISION_PREVIEW_OUT_H ||
        !s_mutex_ready) {
        return;
    }

    rtos_lock_mutex(&s_frame_mutex);
    uint8_t index = s_write_index;
    if (s_frame_buf[index] != NULL) {
        os_memcpy(s_frame_buf[index], rgb565, VISION_PREVIEW_BYTES);
        s_pending_index = (int8_t)index;
        s_write_index ^= 1U;
    }
    rtos_unlock_mutex(&s_frame_mutex);
}

static void page_vision_preview_try_start(void)
{
    if (!s_active || !video_engine_is_running()) {
        return;
    }

    /* The preview worker lives inside the video engine. A rapid Vision
     * exit/re-enter can bounce video_engine_deinit()/init() underneath us,
     * killing the worker that was started against the old engine instance.
     * So instead of latching s_started once, track the engine's actual preview
     * state and (re)start whenever the engine is up but the worker isn't. */
    if (video_engine_preview_is_running()) {
        if (!s_started) {
            s_started = 1;
            if (s_status != NULL && lv_obj_is_valid(s_status)) {
                lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
            }
        }
        return;
    }

    /* Engine running but preview worker not (yet) alive: it either never
     * started or died with a torn-down engine. Clear the latch and (re)start. */
    s_started = 0;

    video_engine_preview_config_t cfg = {
        .width = VISION_PREVIEW_IN_W,
        .height = VISION_PREVIEW_IN_H,
        .fps = VISION_PREVIEW_FPS,
        .rotate = VISION_PREVIEW_ROTATE,
        .out_width = VISION_PREVIEW_OUT_W,
        .out_height = VISION_PREVIEW_OUT_H,
        .sink = page_vision_preview_sink,
        .user_data = NULL,
    };

    s_start_attempts++;
    if (video_engine_preview_start(&cfg) == BK_OK) {
        s_started = 1;
        if (s_status != NULL && lv_obj_is_valid(s_status)) {
            lv_obj_add_flag(s_status, LV_OBJ_FLAG_HIDDEN);
        }
        LOGI("local preview started\r\n");
    } else if ((s_start_attempts % 5U) == 0U) {
        LOGW("local preview start pending, attempts=%u\r\n", (unsigned)s_start_attempts);
    }
}

static lv_obj_t *preview_make_solid(lv_obj_t *parent, int w, int h,
                                    uint32_t hex, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(hex), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, opa, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, 1, LV_PART_MAIN);
    return o;
}

/* Draw the 4 corner brackets and the scan line on top of the live image so the
 * viewfinder stays visible over the camera picture. */
static void preview_build_overlay(lv_obj_t *panel)
{
    for (int i = 0; i < 4; i++) {
        int hx = (i & 1) ? (VISION_PREVIEW_OUT_W - VISION_CORNER_LEN) : 0;
        int hy = (i & 2) ? (VISION_PREVIEW_OUT_H - VISION_CORNER_THICK) : 0;
        int vx = (i & 1) ? (VISION_PREVIEW_OUT_W - VISION_CORNER_THICK) : 0;
        int vy = (i & 2) ? (VISION_PREVIEW_OUT_H - VISION_CORNER_LEN) : 0;

        lv_obj_t *h = preview_make_solid(panel, VISION_CORNER_LEN,
                                         VISION_CORNER_THICK,
                                         VISION_FRAME_COLOR, LV_OPA_COVER);
        lv_obj_set_pos(h, hx, hy);
        lv_obj_t *v = preview_make_solid(panel, VISION_CORNER_THICK,
                                         VISION_CORNER_LEN,
                                         VISION_FRAME_COLOR, LV_OPA_COVER);
        lv_obj_set_pos(v, vx, vy);
    }

    s_scan = preview_make_solid(panel, VISION_PREVIEW_OUT_W - 8, 2,
                                VISION_FRAME_COLOR, LV_OPA_80);
    s_scan_y = 0;
    s_scan_dir = 1;
    lv_obj_set_pos(s_scan, 4, s_scan_y);
}

static void page_vision_preview_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!s_active) {
        return;
    }

    page_vision_preview_try_start();

    if (s_scan != NULL && lv_obj_is_valid(s_scan)) {
        s_scan_y += (int32_t)s_scan_dir * VISION_SCAN_STEP;
        if (s_scan_y >= (VISION_PREVIEW_OUT_H - 2)) {
            s_scan_y = VISION_PREVIEW_OUT_H - 2;
            s_scan_dir = -1;
        } else if (s_scan_y <= 0) {
            s_scan_y = 0;
            s_scan_dir = 1;
        }
        lv_obj_set_y(s_scan, s_scan_y);
    }

    int8_t index = -1;
    if (s_mutex_ready) {
        rtos_lock_mutex(&s_frame_mutex);
        index = s_pending_index;
        s_pending_index = -1;
        rtos_unlock_mutex(&s_frame_mutex);
    }

    if (index >= 0 && s_image != NULL && lv_obj_is_valid(s_image)) {
        s_img_dsc.data = s_frame_buf[index];
        s_img_dsc.data_size = VISION_PREVIEW_BYTES;
        lv_image_set_src(s_image, &s_img_dsc);
        lv_obj_invalidate(s_image);
    }
}

void page_vision_preview_attach(lv_obj_t *parent)
{
    if (parent == NULL) {
        return;
    }

    page_vision_preview_detach();

    if (rtos_init_mutex(&s_frame_mutex) != BK_OK) {
        LOGE("frame mutex init failed\r\n");
        return;
    }
    s_mutex_ready = 1;

    s_frame_buf[0] = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, VISION_PREVIEW_BYTES);
    s_frame_buf[1] = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, VISION_PREVIEW_BYTES);
    if (s_frame_buf[0] == NULL || s_frame_buf[1] == NULL) {
        LOGE("frame buffer alloc failed buf0=%p buf1=%p\r\n", s_frame_buf[0], s_frame_buf[1]);
        page_vision_preview_free_buffers();
        rtos_deinit_mutex(&s_frame_mutex);
        s_mutex_ready = 0;
        return;
    }
    os_memset(s_frame_buf[0], 0, VISION_PREVIEW_BYTES);
    os_memset(s_frame_buf[1], 0, VISION_PREVIEW_BYTES);

    /* Transparent container filling the whole viewfinder frame. The live image
     * fills it, and the corner brackets + scan line are drawn on top of the
     * image so they stay visible over the camera picture. */
    s_panel = lv_obj_create(parent);
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, VISION_PANEL_W, VISION_PANEL_H);
    lv_obj_set_pos(s_panel, VISION_PANEL_LEFT, VISION_PANEL_TOP);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, 0, LV_PART_MAIN);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_status = lv_label_create(s_panel);
    lv_label_set_text(s_status, "CAM ...");
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x7dfcff), LV_PART_MAIN);
    lv_obj_set_pos(s_status, 4, 2);

    s_image = lv_image_create(s_panel);
    lv_obj_set_pos(s_image, VISION_PREVIEW_IMG_X, VISION_PREVIEW_IMG_Y);
    lv_obj_set_size(s_image, VISION_PREVIEW_OUT_W, VISION_PREVIEW_OUT_H);

    os_memset(&s_img_dsc, 0, sizeof(s_img_dsc));
    s_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_img_dsc.header.w = VISION_PREVIEW_OUT_W;
    s_img_dsc.header.h = VISION_PREVIEW_OUT_H;
    s_img_dsc.data_size = VISION_PREVIEW_BYTES;
    s_img_dsc.data = s_frame_buf[0];
    lv_image_set_src(s_image, &s_img_dsc);

    /* Brackets + scan line last so they render above the image. */
    preview_build_overlay(s_panel);

    lv_obj_move_foreground(s_panel);

    s_active = 1;
    s_started = 0;
    s_write_index = 0;
    s_pending_index = -1;
    s_start_attempts = 0;
    s_timer = lv_timer_create(page_vision_preview_timer_cb, 33, NULL);
    page_vision_preview_try_start();
}

void page_vision_preview_detach(void)
{
    s_active = 0;

    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }

    if (s_started) {
        (void)video_engine_preview_stop();
        s_started = 0;
    }

    if (s_panel != NULL && lv_obj_is_valid(s_panel)) {
        lv_obj_delete(s_panel);
    }
    s_panel = NULL;
    s_image = NULL;
    s_status = NULL;
    s_scan = NULL;
    s_scan_y = 0;
    s_scan_dir = 1;

    page_vision_preview_free_buffers();

    if (s_mutex_ready) {
        rtos_deinit_mutex(&s_frame_mutex);
        s_mutex_ready = 0;
    }
    s_write_index = 0;
    s_pending_index = -1;
    s_start_attempts = 0;
    os_memset(&s_img_dsc, 0, sizeof(s_img_dsc));
}

#else  /* !ROBOT_TEST */

void page_vision_preview_attach(lv_obj_t *parent) { (void)parent; }
void page_vision_preview_detach(void) {}

#endif /* ROBOT_TEST */
