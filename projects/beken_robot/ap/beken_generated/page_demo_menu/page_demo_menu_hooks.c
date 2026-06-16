/**
 * @file page_demo_menu_hooks.c
 * @brief page_3 demo grid menu (10 entries): physical-key navigation +
 *        TP click dispatch.
 *
 * Camera preview is an overlay demo: while it is running, lv_screen_active()
 * is still page_3, so the take_photo / resume_live / exit events are
 * intercepted here in the preview branch and dispatched directly to the
 * camera_preview backend.
 */
#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "camera_preview.h"
#include "demo/ai_chat.h"
#include "demo/vision.h"
#include "demo/asr.h"
#include "demo/music.h"
#include "demo/volume.h"
#include "demo/sound_localization.h"
#include "demo/camera_preview_demo.h"
#include "demo/udisk.h"
#include "demo/robot_video.h"
#include "page_edge_ai.h"
#include <components/log.h>

#define TAG "page_demo_menu"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define PAGE3_MENU_COUNT 10

/* page_3 main-menu button idx -> demo start function. The order here
 * MUST match menu_btn() below. Pure-UI pages (splash, top, demo menu)
 * are not in this table because they have no backend. */
typedef int (*bk_demo_start_fn_t)(void);

static const bk_demo_start_fn_t s_menu_start_table[PAGE3_MENU_COUNT] = {
    ai_chat_start,             /* idx 0: AI chat                 -> page_6  */
    vision_start,              /* idx 1: Vision recognition      -> page_7  */
    asr_start,                 /* idx 2: Speech recognition      -> page_8  */
    music_start,               /* idx 3: Music playback          -> page_9  */
    volume_start,              /* idx 4: Volume settings         -> page_10 */
    sound_localization_start,  /* idx 5: Sound source localization -> page_5 */
    page_edge_ai_enter,        /* idx 6: End-side AI submenu              */
    camera_preview_demo_start, /* idx 7: Camera preview (overlay)         */
    udisk_start,               /* idx 8: U-disk (USB MSC)                 */
    robot_video_start,         /* idx 9: Robot video playback    -> page_11 */
};

static int s_menu_idx;

/*
 * Focus / navigation order matches demo_registry.c::s_menu_start_table:
 *
 *     col1 (x=28)      col2 (x=145)     col3 (x=258)
 *  y=51   btn_1            btn_2           btn_3
 *         AI chat         Vision          ASR
 *  y=111  btn_7            btn_5           btn_6
 *         Music           Volume          Sound localization
 *  y=171  btn_8            btn_9           btn_4
 *         Edge AI         Camera preview  U-disk
 *  y=231  btn_10           reserved        reserved
 *         Robot video      (hidden)        (hidden)
 *
 * button_N numbering follows Designer creation order, not the focus idx.
 */
static lv_obj_t *menu_btn(bk_lv_ui_t *ui, int idx)
{
    if (ui == NULL) {
        return NULL;
    }
    switch (idx) {
    case 0: return ui->page_3_button_1;
    case 1: return ui->page_3_button_2;
    case 2: return ui->page_3_button_3;
    case 3: return ui->page_3_button_7;
    case 4: return ui->page_3_button_5;
    case 5: return ui->page_3_button_6;
    case 6: return ui->page_3_button_8;
    case 7: return ui->page_3_button_9;  /* camera preview */
    case 8: return ui->page_3_button_4;  /* U-disk (USB MSC) */
    case 9: return ui->page_3_button_10; /* robot video playback */
    default: return NULL;
    }
}

/*
 * Focus state uses a background recolor (0xc0c0c0) rather than the
 * LV_STATE_DISABLED toggle: the disabled state silently swallows
 * PRESSED / CLICKED in the LVGL indev pipeline, which would break TP
 * clicks on the currently-focused button.
 */
static void apply_menu_focus(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE3_MENU_COUNT; i++) {
        lv_obj_t *b = menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_remove_state(b, LV_STATE_DISABLED);
        uint32_t color = (i == s_menu_idx) ? 0xc0c0c0 : 0x2d75b9;
        lv_obj_set_style_bg_color(b, lv_color_hex(color),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    /* Preview state: FOCUS_PREV = take photo (RUNNING only). */
    if (camera_preview_is_active()) {
        if (camera_preview_is_running()) {
            LOGI("Camera preview: take photo\r\n");
            (void)camera_preview_take_photo();
        }
        return;
    }
    s_menu_idx = (s_menu_idx + PAGE3_MENU_COUNT - 1) % PAGE3_MENU_COUNT;
    apply_menu_focus(ui);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    /* Preview state: FOCUS_NEXT = resume live (FROZEN only). */
    if (camera_preview_is_active()) {
        if (camera_preview_is_frozen()) {
            LOGI("Camera preview: resume live\r\n");
            (void)camera_preview_resume_live();
        }
        return;
    }
    s_menu_idx = (s_menu_idx + 1) % PAGE3_MENU_COUNT;
    apply_menu_focus(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    /* Preview state: SCREEN_PREV = stop preview (16 KB worker stack). */
    if (camera_preview_is_running() || camera_preview_is_frozen()) {
        LOGI("Exit camera preview\r\n");
        (void)camera_preview_stop();
        return;
    }
    /* Ignore back during STARTING / STOPPING to avoid race-through. */
    if (camera_preview_is_active()) {
        return;
    }
    navigate_to_screen((lv_obj_t **)&ui->page_2,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_2);
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    /* Preview state: block menu re-entry. */
    if (camera_preview_is_active()) {
        return;
    }
    LOGI("page_demo_menu enter idx=%d\r\n", s_menu_idx);
    if (s_menu_idx < 0 || s_menu_idx >= PAGE3_MENU_COUNT ||
        s_menu_start_table[s_menu_idx] == NULL) {
        LOGE("page_demo_menu enter: idx=%d not bound\r\n", s_menu_idx);
        return;
    }
    int ret = s_menu_start_table[s_menu_idx]();
    if (ret != 0) {
        LOGE("start demo idx=%d failed: %d\r\n", s_menu_idx, ret);
    }
}

static const ui_page_nav_ops_t page_3_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

static void button_click_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= PAGE3_MENU_COUNT) {
        return;
    }
    if (camera_preview_is_active()) {
        return;
    }
    s_menu_idx = idx;
    apply_menu_focus(&bk_lv_tool_ui);
    on_screen_next(&bk_lv_tool_ui);
}

static void register_button_clicks(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    for (int i = 0; i < PAGE3_MENU_COUNT; i++) {
        lv_obj_t *b = menu_btn(ui, i);
        if (b == NULL) {
            continue;
        }
        lv_obj_add_event_cb(b, button_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
    }
}

static void page_demo_menu_on_init(bk_lv_ui_t *ui)
{
    s_menu_idx = 0;
    apply_menu_focus(ui);
    register_button_clicks(ui);
    (void)ui_nav_register_screen(ui->page_3, &page_3_nav_ops);
}

static void page_demo_menu_on_destroy(bk_lv_ui_t *ui)
{
    ui_nav_unregister_screen(ui->page_3);
}

void page_demo_menu_init_hooks(void)
{
    (void)bk_page_set_init_hook(3, page_demo_menu_on_init);
    (void)bk_page_set_destroy_hook(3, page_demo_menu_on_destroy);
}

int page_demo_menu_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
    return 0;
}

#else  /* !ROBOT_TEST */

void page_demo_menu_init_hooks(void) {}
int  page_demo_menu_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
