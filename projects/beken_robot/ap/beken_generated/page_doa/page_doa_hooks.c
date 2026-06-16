/**
 * @file page_doa_hooks.c
 * @brief page_5 sound source localization UI hooks: lv_arc KNOB
 *        position + central eyes/smile widget; the live angle comes
 *        from src/demo/sound_localization.c via a registered UI sink.
 */
#include <stddef.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "page_doa_eyes.h"
#include "demo/sound_localization.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "lv_vendor.h"
#include <components/log.h>

#define TAG "page_doa"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

#define PAGE5_ARC_RANGE_MIN  0
#define PAGE5_ARC_RANGE_MAX  360

static void apply_arrow_state_locked(bk_lv_ui_t *ui, int degrees)
{
    if (ui == NULL) {
        return;
    }
    if (ui->page_5_arc_1 == NULL || !lv_obj_is_valid(ui->page_5_arc_1)) {
        return;
    }
    lv_arc_set_value(ui->page_5_arc_1, degrees);
    /* Already inside the vendor lock: page_5_eyes_set_gaze does not
     * relock. */
    page_5_eyes_set_gaze(degrees);
}

/* Called from src/demo/sound_localization.c (any task). */
static void page_doa_ui_sink(int degrees)
{
    lv_vendor_disp_lock();
    apply_arrow_state_locked(&bk_lv_tool_ui, degrees);
    lv_vendor_disp_unlock();
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page5 back -> page_3\r\n");
    (void)sound_localization_stop_service();
    navigate_to_screen((lv_obj_t **)&ui->page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
}

static const ui_page_nav_ops_t page_5_nav_ops = {
    .on_focus_prev  = NULL,
    .on_focus_next  = NULL,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = NULL,
};

static void page_doa_on_init(bk_lv_ui_t *ui)
{
    /* Force 90 deg on first frame to avoid an early 0-deg push pinning
     * the ball to the right side / pupils to amber. */
    int initial_deg = 90;

    lv_arc_set_range(ui->page_5_arc_1, PAGE5_ARC_RANGE_MIN, PAGE5_ARC_RANGE_MAX);

    /* Page size override: ap_main sets ROTATE_90 so the logical screen
     * is 385x320 (landscape), but Designer wrote lv_obj_set_size(page_5,
     * 320, 385) for the physical orientation. Override here so page
     * local coordinates match the application layer's. */
    lv_obj_set_size(ui->page_5, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);

    lv_obj_set_width(ui->page_5_arc_1, PAGE_5_RING_W);
    lv_obj_set_height(ui->page_5_arc_1, PAGE_5_RING_H);

    const lv_coord_t ring_x_base =
        (LOGICAL_SCREEN_WIDTH / 2 - PAGE_5_RING_W / 2) + PAGE_5_FACE_NUDGE_X;
    const lv_coord_t ring_y_base =
        (LOGICAL_SCREEN_HEIGHT / 2 - PAGE_5_RING_H / 2) + PAGE_5_FACE_NUDGE_Y;
    lv_obj_set_x(ui->page_5_arc_1, ring_x_base);
    lv_obj_set_y(ui->page_5_arc_1, ring_y_base);

    /* Create central eyes + smile; must precede apply_arrow_state_locked.
     * Eyes/mouth anchor to the arc's CURRENT (pre-drop) center and are
     * children of page_5, so the later ring drop leaves them untouched. */
    page_5_eyes_create(ui->page_5, ui->page_5_arc_1);

    /* Slide only the ring (+ its KNOB) down; the face features stay put. */
    lv_obj_set_y(ui->page_5_arc_1, ring_y_base + PAGE_5_RING_EXTRA_Y);

    apply_arrow_state_locked(ui, initial_deg);
    (void)ui_nav_register_screen(ui->page_5, &page_5_nav_ops);

    /* Wire the angle setter so DOA pushes land on the UI. */
    sound_localization_set_angle(initial_deg);
    sound_localization_register_ui_sink(page_doa_ui_sink);
}

static void page_doa_on_destroy(bk_lv_ui_t *ui)
{
    sound_localization_register_ui_sink(NULL);
    ui_nav_unregister_screen(ui->page_5);
    /* Stop the blink animation + release eye objects BEFORE
     * lv_obj_del(page_5); otherwise the anim callback would write into
     * freed lv_obj memory. */
    page_5_eyes_destroy();
}

void page_doa_init_hooks(void)
{
    (void)bk_page_set_init_hook(5, page_doa_on_init);
    (void)bk_page_set_destroy_hook(5, page_doa_on_destroy);
    /* Register `eyes` debug CLI (fixed name, idempotent). The `arrow`
     * CLI is owned by src/demo/sound_localization.c. */
    (void)page_5_eyes_cli_init();
}

int page_doa_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_5,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_5);
    if (bk_lv_tool_ui.page_5 == NULL || !lv_obj_is_valid(bk_lv_tool_ui.page_5)) {
        return -1;
    }
    return 0;
}

#else  /* !ROBOT_TEST */

void page_doa_init_hooks(void) {}
int  page_doa_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
