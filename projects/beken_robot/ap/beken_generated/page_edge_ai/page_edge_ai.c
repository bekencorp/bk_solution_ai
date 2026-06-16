/**
 * @file page_edge_ai.c
 * @brief End-side AI menu plus a local SP-camera solution example.
 */
#include "page_edge_ai.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <components/bk_frame_buffer.h>
#include <components/log.h>
#include <os/mem.h>
#include <os/os.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "ui_nav_router.h"
#include "demo/palm_tracking.h"
#include "video_engine.h"

#ifdef ROBOT_TEST

#define TAG "page_edge_ai"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define EDGE_SCREEN_W  390
#define EDGE_SCREEN_H  360

#define EDGE_MENU_MAX_COUNT    18
#define EDGE_MENU_PAGE_SIZE    9
#define EDGE_MENU_COLS         3
#define EDGE_MENU_BTN_W        106
#define EDGE_MENU_BTN_H        58
#define EDGE_MENU_PANEL_Y      52
#define EDGE_MENU_PANEL_H      236
#define EDGE_MENU_X0           24
#define EDGE_MENU_Y0           8
#define EDGE_MENU_X_GAP        12
#define EDGE_MENU_Y_GAP        12
#define EDGE_STATUS_Y          298

#define SOL_CAMERA_SP_W        256
#define SOL_CAMERA_SP_H        256
#define SOL_CAMERA_VIEW_X      25
#define SOL_CAMERA_VIEW_Y      22
#define SOL_CAMERA_VIEW_W      256
#define SOL_CAMERA_VIEW_H      256
#define SOL_TEXT_H             20
#define SOL_CAMERA_BORDER_W    2
#define SOL_CAMERA_RADIUS      15
#define SOL_CAMERA_PANEL_W     (SOL_CAMERA_VIEW_W + SOL_CAMERA_BORDER_W * 2)
#define SOL_CAMERA_PANEL_H     (SOL_CAMERA_VIEW_H + SOL_TEXT_H + SOL_CAMERA_BORDER_W * 2)
#define SOL_TEXT_Y             (SOL_CAMERA_VIEW_Y + SOL_CAMERA_VIEW_H)
#define SOL_BUTTON_X           296
#define SOL_BUTTON_W           82
#define SOL_BUTTON_H           38
#define SOL_BUTTON_GAP         18
#define SOL_BUTTON_Y0          46
#define SOL_PREVIEW_FPS        10

static lv_obj_t *s_edge_screen;
static lv_obj_t *s_edge_status;
static lv_obj_t *s_edge_buttons[EDGE_MENU_MAX_COUNT];
static lv_obj_t *s_edge_button_labels[EDGE_MENU_MAX_COUNT];
static int       s_edge_idx;

static lv_obj_t *s_solution_screen;
static lv_obj_t *s_solution_image;
static lv_obj_t *s_solution_status;
static lv_obj_t *s_solution_buttons[4];
static lv_timer_t *s_solution_timer;
static lv_image_dsc_t s_solution_img_dsc;

static beken_mutex_t  s_solution_mutex;
static uint8_t        s_solution_mutex_ready;
static volatile uint8_t s_solution_active;
static uint8_t       *s_solution_rgb[2];
static uint8_t        s_solution_write_index;
static int8_t         s_solution_pending_index = -1;

static const char * const s_edge_titles[] = {
    "手掌跟随",
    "人脸检测",
    "手势识别",
    "方案示例1",
    "方案示例2",
    "方案示例3",
    "方案示例4",
    "方案示例5",
    "方案示例6",
};

#define EDGE_MENU_COUNT ((int)(sizeof(s_edge_titles) / sizeof(s_edge_titles[0])))

static const char * const s_solution_btn_titles[4] = {
    "功能一",
    "功能二",
    "功能三",
    "功能四",
};

static void edge_ai_menu_apply_focus(void);
static void solution_preview_detach(void);

static void set_label_font(lv_obj_t *label, const lv_font_t *font)
{
    if (label != NULL) {
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static lv_obj_t *create_label(lv_obj_t *parent, const char *text,
                              int x, int y, int w, int h,
                              const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_label_font(label, font);
    return label;
}

static lv_obj_t *create_menu_button(lv_obj_t *parent, const char *text,
                                    int x, int y, int w, int h,
                                    lv_obj_t **out_label)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2d75b9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x1e7fcf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    set_label_font(label, &lv_font_ali_16);
    if (out_label != NULL) {
        *out_label = label;
    }
    return btn;
}

static void edge_ai_menu_apply_focus(void)
{
    for (int i = 0; i < EDGE_MENU_COUNT; i++) {
        lv_obj_t *btn = s_edge_buttons[i];
        if (btn == NULL || !lv_obj_is_valid(btn)) {
            continue;
        }
        uint32_t bg = (i == s_edge_idx) ? 0xc0c0c0 : 0x2d75b9;
        uint32_t fg = 0xffffff;
        lv_obj_set_style_bg_color(btn, lv_color_hex(bg), LV_PART_MAIN | LV_STATE_DEFAULT);
        if (s_edge_button_labels[i] != NULL) {
            lv_obj_set_style_text_color(s_edge_button_labels[i], lv_color_hex(fg),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if (i == s_edge_idx) {
            lv_obj_scroll_to_view(btn, LV_ANIM_OFF);
        }
    }
}

static void edge_ai_show_status(const char *text)
{
    if (s_edge_status != NULL && lv_obj_is_valid(s_edge_status)) {
        lv_label_set_text(s_edge_status, text);
    }
}

static void edge_ai_enter_selected(void)
{
    LOGI("edge AI enter idx=%d\r\n", s_edge_idx);

    switch (s_edge_idx) {
    case 0:
        palm_tracking_set_return_to_edge_ai(true);
        (void)palm_tracking_start();
        break;
    case 1:
        edge_ai_show_status("人脸检测功能暂未实现");
        break;
    case 2:
        edge_ai_show_status("手势识别功能暂未实现");
        break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
        (void)page_edge_ai_solution_enter();
        break;
    default:
        break;
    }
}

static void edge_ai_on_focus_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    s_edge_idx = (s_edge_idx + EDGE_MENU_COUNT - 1) % EDGE_MENU_COUNT;
    edge_ai_menu_apply_focus();
}

static void edge_ai_on_focus_next(bk_lv_ui_t *ui)
{
    (void)ui;
    s_edge_idx = (s_edge_idx + 1) % EDGE_MENU_COUNT;
    edge_ai_menu_apply_focus();
}

static void edge_ai_on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
    if (s_edge_screen != NULL && lv_obj_is_valid(s_edge_screen)) {
        ui_nav_unregister_screen(s_edge_screen);
        lv_obj_del(s_edge_screen);
    }
    s_edge_screen = NULL;
}

static void edge_ai_on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    edge_ai_enter_selected();
}

static const ui_page_nav_ops_t s_edge_ai_nav_ops = {
    .on_focus_prev = edge_ai_on_focus_prev,
    .on_focus_next = edge_ai_on_focus_next,
    .on_screen_prev = edge_ai_on_screen_prev,
    .on_screen_next = edge_ai_on_screen_next,
};

static void edge_ai_button_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= EDGE_MENU_COUNT) {
        return;
    }
    s_edge_idx = idx;
    edge_ai_menu_apply_focus();
    edge_ai_enter_selected();
}

int page_edge_ai_enter(void)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    (void)ui;

    if (s_edge_screen != NULL && lv_obj_is_valid(s_edge_screen)) {
        ui_nav_unregister_screen(s_edge_screen);
        lv_obj_del(s_edge_screen);
    }

    s_edge_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_edge_screen, EDGE_SCREEN_W, EDGE_SCREEN_H);
    lv_obj_set_scrollbar_mode(s_edge_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_edge_screen, lv_color_hex(0x07111f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_edge_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *panel = lv_obj_create(s_edge_screen);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, 0, EDGE_MENU_PANEL_Y);
    lv_obj_set_size(panel, EDGE_SCREEN_W, EDGE_MENU_PANEL_H);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(panel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    for (int i = 0; i < EDGE_MENU_COUNT; i++) {
        int page = i / EDGE_MENU_PAGE_SIZE;
        int pos = i % EDGE_MENU_PAGE_SIZE;
        int row = pos / EDGE_MENU_COLS;
        int col = pos % EDGE_MENU_COLS;
        int x = EDGE_MENU_X0 + col * (EDGE_MENU_BTN_W + EDGE_MENU_X_GAP);
        int y = page * 216 + row * (EDGE_MENU_BTN_H + EDGE_MENU_Y_GAP);
        s_edge_buttons[i] = create_menu_button(panel, s_edge_titles[i],
                                               x, y, EDGE_MENU_BTN_W, EDGE_MENU_BTN_H,
                                               &s_edge_button_labels[i]);
        lv_obj_add_event_cb(s_edge_buttons[i], edge_ai_button_click_cb,
                            LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    s_edge_status = create_label(s_edge_screen, "人脸检测 手势识别 暂未实现",
                                 0, EDGE_STATUS_Y, EDGE_SCREEN_W, 22,
                                 &lv_font_ali_16, 0x9eb7d9);

    s_edge_idx = 0;
    edge_ai_menu_apply_focus();
    lv_screen_load(s_edge_screen);
    (void)ui_nav_register_screen(s_edge_screen, &s_edge_ai_nav_ops);
    return 0;
}

static void solution_set_status(const char *text)
{
    if (s_solution_status != NULL && lv_obj_is_valid(s_solution_status)) {
        lv_label_set_text(s_solution_status, text);
    }
}

static void solution_preview_free_buffers(void)
{
    for (int i = 0; i < 2; i++) {
        if (s_solution_rgb[i] != NULL) {
            bk_frame_buffer_free(s_solution_rgb[i]);
            s_solution_rgb[i] = NULL;
        }
    }
}

static void solution_preview_sink(const uint8_t *rgb565,
                                  uint16_t width,
                                  uint16_t height,
                                  void *user_data)
{
    (void)user_data;

    if (!s_solution_active || rgb565 == NULL ||
        width != SOL_CAMERA_VIEW_W ||
        height != SOL_CAMERA_VIEW_H ||
        !s_solution_mutex_ready) {
        return;
    }

    rtos_lock_mutex(&s_solution_mutex);
    uint8_t index = s_solution_write_index;
    if (s_solution_rgb[index] != NULL) {
        os_memcpy(s_solution_rgb[index], rgb565,
                  (uint32_t)SOL_CAMERA_VIEW_W * SOL_CAMERA_VIEW_H * 2U);
        s_solution_pending_index = (int8_t)index;
        s_solution_write_index ^= 1U;
    }
    rtos_unlock_mutex(&s_solution_mutex);
}

static void solution_preview_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_solution_active) {
        return;
    }

    int8_t index = -1;
    if (s_solution_mutex_ready) {
        rtos_lock_mutex(&s_solution_mutex);
        index = s_solution_pending_index;
        s_solution_pending_index = -1;
        rtos_unlock_mutex(&s_solution_mutex);
    }

    if (index >= 0 && s_solution_image != NULL && lv_obj_is_valid(s_solution_image)) {
        s_solution_img_dsc.data = s_solution_rgb[index];
        s_solution_img_dsc.data_size = (uint32_t)SOL_CAMERA_VIEW_W * SOL_CAMERA_VIEW_H * 2U;
        lv_image_set_src(s_solution_image, &s_solution_img_dsc);
        lv_obj_invalidate(s_solution_image);
        solution_set_status("摄像头预览");
    }
}

static void solution_preview_attach(void)
{
    const uint32_t rgb_size = (uint32_t)SOL_CAMERA_VIEW_W * SOL_CAMERA_VIEW_H * 2U;

    if (rtos_init_mutex(&s_solution_mutex) != BK_OK) {
        LOGE("solution mutex init failed\r\n");
        solution_set_status("预览初始化失败");
        return;
    }
    s_solution_mutex_ready = 1;

    s_solution_rgb[0] = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, rgb_size);
    s_solution_rgb[1] = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, rgb_size);
    if (s_solution_rgb[0] == NULL || s_solution_rgb[1] == NULL) {
        LOGE("solution rgb alloc failed %p %p\r\n", s_solution_rgb[0], s_solution_rgb[1]);
        solution_preview_free_buffers();
        rtos_deinit_mutex(&s_solution_mutex);
        s_solution_mutex_ready = 0;
        solution_set_status("预览缓冲申请失败");
        return;
    }
    os_memset(s_solution_rgb[0], 0, rgb_size);
    os_memset(s_solution_rgb[1], 0, rgb_size);

    os_memset(&s_solution_img_dsc, 0, sizeof(s_solution_img_dsc));
    s_solution_img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_solution_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_solution_img_dsc.header.w = SOL_CAMERA_VIEW_W;
    s_solution_img_dsc.header.h = SOL_CAMERA_VIEW_H;
    s_solution_img_dsc.data_size = rgb_size;
    s_solution_img_dsc.data = s_solution_rgb[0];
    lv_image_set_src(s_solution_image, &s_solution_img_dsc);

    s_solution_write_index = 0;
    s_solution_pending_index = -1;
    s_solution_timer = lv_timer_create(solution_preview_timer_cb, 33, NULL);

    s_solution_active = 1;
    video_engine_preview_config_t cfg = {
        .width = SOL_CAMERA_SP_W,
        .height = SOL_CAMERA_SP_H,
        .fps = SOL_PREVIEW_FPS,
        .rotate = 0,
        .out_width = SOL_CAMERA_VIEW_W,
        .out_height = SOL_CAMERA_VIEW_H,
        .sink = solution_preview_sink,
        .user_data = NULL,
    };

    if (video_engine_preview_start(&cfg) != BK_OK) {
        LOGE("video_engine_preview_start failed\r\n");
        s_solution_active = 0;
        if (s_solution_timer != NULL) {
            lv_timer_delete(s_solution_timer);
            s_solution_timer = NULL;
        }
        solution_preview_free_buffers();
        rtos_deinit_mutex(&s_solution_mutex);
        s_solution_mutex_ready = 0;
        solution_set_status("摄像头未打开");
    }
}

static void solution_preview_detach(void)
{
    s_solution_active = 0;

    if (s_solution_timer != NULL) {
        lv_timer_delete(s_solution_timer);
        s_solution_timer = NULL;
    }

    (void)video_engine_preview_stop();

    solution_preview_free_buffers();

    if (s_solution_mutex_ready) {
        rtos_deinit_mutex(&s_solution_mutex);
        s_solution_mutex_ready = 0;
    }

    s_solution_image = NULL;
    s_solution_status = NULL;
    s_solution_write_index = 0;
    s_solution_pending_index = -1;
    os_memset(&s_solution_img_dsc, 0, sizeof(s_solution_img_dsc));
}

static void solution_on_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    solution_preview_detach();
    (void)page_edge_ai_enter();
    if (s_solution_screen != NULL && lv_obj_is_valid(s_solution_screen)) {
        ui_nav_unregister_screen(s_solution_screen);
        lv_obj_del(s_solution_screen);
    }
    s_solution_screen = NULL;
}

static const ui_page_nav_ops_t s_solution_nav_ops = {
    .on_focus_prev = NULL,
    .on_focus_next = NULL,
    .on_screen_prev = solution_on_screen_prev,
    .on_screen_next = NULL,
};

int page_edge_ai_solution_enter(void)
{
    if (s_solution_screen != NULL && lv_obj_is_valid(s_solution_screen)) {
        solution_preview_detach();
        ui_nav_unregister_screen(s_solution_screen);
        lv_obj_del(s_solution_screen);
    }

    s_solution_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_solution_screen, EDGE_SCREEN_W, EDGE_SCREEN_H);
    lv_obj_set_scrollbar_mode(s_solution_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_solution_screen, lv_color_hex(0x050910), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_solution_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *cam_panel = lv_obj_create(s_solution_screen);
    lv_obj_remove_style_all(cam_panel);
    lv_obj_set_pos(cam_panel, SOL_CAMERA_VIEW_X, SOL_CAMERA_VIEW_Y);
    lv_obj_set_size(cam_panel, SOL_CAMERA_PANEL_W, SOL_CAMERA_PANEL_H);
    lv_obj_set_style_radius(cam_panel, SOL_CAMERA_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(cam_panel, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cam_panel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cam_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cam_panel, LV_OBJ_FLAG_SCROLLABLE);

    s_solution_image = lv_image_create(cam_panel);
    lv_obj_set_pos(s_solution_image, SOL_CAMERA_BORDER_W, SOL_CAMERA_BORDER_W);
    lv_obj_set_size(s_solution_image, SOL_CAMERA_VIEW_W, SOL_CAMERA_VIEW_H);

    s_solution_status = create_label(cam_panel, "摄像头预览",
                                     SOL_CAMERA_BORDER_W,
                                     SOL_CAMERA_BORDER_W + SOL_CAMERA_VIEW_H,
                                     SOL_CAMERA_VIEW_W, SOL_TEXT_H,
                                     &lv_font_ali_16, 0xffffff);
    lv_obj_set_style_bg_color(s_solution_status, lv_color_hex(0x10151f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_solution_status, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *cam_border = lv_obj_create(s_solution_screen);
    lv_obj_remove_style_all(cam_border);
    lv_obj_set_pos(cam_border, SOL_CAMERA_VIEW_X, SOL_CAMERA_VIEW_Y);
    lv_obj_set_size(cam_border, SOL_CAMERA_PANEL_W, SOL_CAMERA_PANEL_H);
    lv_obj_set_style_bg_opa(cam_border, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cam_border, SOL_CAMERA_BORDER_W, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(cam_border, lv_color_hex(0x32d5ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(cam_border, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(cam_border, SOL_CAMERA_RADIUS, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(cam_border, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) {
        int y = SOL_BUTTON_Y0 + i * (SOL_BUTTON_H + SOL_BUTTON_GAP);
        s_solution_buttons[i] = create_menu_button(s_solution_screen, s_solution_btn_titles[i],
                                                   SOL_BUTTON_X, y,
                                                   SOL_BUTTON_W, SOL_BUTTON_H,
                                                   NULL);
    }

    lv_screen_load(s_solution_screen);
    (void)ui_nav_register_screen(s_solution_screen, &s_solution_nav_ops);
    solution_preview_attach();
    return 0;
}

#else  /* !ROBOT_TEST */

int page_edge_ai_enter(void) { return 0; }
int page_edge_ai_solution_enter(void) { return 0; }

#endif /* ROBOT_TEST */
