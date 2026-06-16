/**
 * @file page_asr_hooks.c
 * @brief page_8 ASR (speech recognition) UI hooks: spinner + dynamic
 *        labels reflecting "listening" / "recognized" states pushed from
 *        the backend (src/demo/asr.c).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"
#include "demo/asr.h"

#ifdef ROBOT_TEST

#include "ui_nav_router.h"
#include "ui_list_menu.h"
#include <components/log.h>

#define TAG "page_asr"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

/* ----------------------------------------------------------------------
 * Page widgets (created on top of the page_8 root which Designer leaves
 * empty for us) and animation state.
 * -------------------------------------------------------------------- */
static lv_timer_t *s_anim_timer;
static lv_obj_t *s_label_state;
static lv_obj_t *s_spinner;
static lv_obj_t *s_label_hint;
static lv_obj_t *s_label_action;
static uint32_t s_recognized_start_ms;
static uint32_t s_spinner_kick_ms;
static bool s_recognized_active;
static char s_action_text[32] = "前进";

static void apply_listening_view(void)
{
    if (s_label_state) {
        lv_label_set_text(s_label_state, "聆听中...");
    }
    if (s_label_hint) {
        lv_label_set_text(s_label_hint, "你可以说: 前进 后退 向左转 向右转");
        lv_obj_clear_flag(s_label_hint, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_spinner) {
        lv_obj_clear_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
        /* Long visibility cycles occasionally stall the spinner; resetting
         * the anim params here brings it back. */
        lv_spinner_set_anim_params(s_spinner, 1200, 90);
        s_spinner_kick_ms = lv_tick_get();
    }
    if (s_label_action) {
        lv_obj_add_flag(s_label_action, LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_recognized_view(void)
{
    if (s_label_state) {
        lv_label_set_text(s_label_state, "已识别");
    }
    if (s_label_hint) {
        lv_obj_add_flag(s_label_hint, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_spinner) {
        lv_obj_add_flag(s_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_label_action) {
        lv_label_set_text(s_label_action, s_action_text);
        lv_obj_clear_flag(s_label_action, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_phrase_recognized(const char *phrase)
{
    if (phrase == NULL) {
        return;
    }
    if ((strcmp(phrase, "nihaobaotong") == 0) || (strcmp(phrase, "nihaobotong") == 0)) {
        snprintf(s_action_text, sizeof(s_action_text), "你好博通");
    } else if (strcmp(phrase, "zaijianbotong") == 0) {
        snprintf(s_action_text, sizeof(s_action_text), "再见博通");
    } else {
        return;
    }
    apply_recognized_view();
    s_recognized_start_ms = lv_tick_get();
    s_recognized_active = true;
}

static void anim_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    char phrase[32] = {0};
    if (asr_consume_phrase_trigger(phrase, sizeof(phrase))) {
        on_phrase_recognized(phrase);
    }
    if (s_recognized_active && lv_tick_elaps(s_recognized_start_ms) >= 8000) {
        apply_listening_view();
        s_recognized_active = false;
    }
    if (!s_recognized_active && s_spinner && lv_tick_elaps(s_spinner_kick_ms) >= 3000) {
        lv_spinner_set_anim_params(s_spinner, 1200, 90);
        s_spinner_kick_ms = lv_tick_get();
    }
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    LOGI("page_asr back -> page_3\r\n");
    (void)asr_stop_service();
    (void)ui_demo_return_to_menu();
}

static const ui_page_nav_ops_t page_8_nav_ops = {
    .on_focus_prev  = NULL,
    .on_focus_next  = NULL,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = NULL,
};

static void asr_build_widgets(bk_lv_ui_t *ui)
{
    s_label_state = lv_label_create(ui->page_8);
    lv_obj_set_style_text_color(s_label_state, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_label_state, &lv_font_zh_demo_32,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_label_state, 122, 36);

    s_spinner = lv_spinner_create(ui->page_8);
    lv_spinner_set_anim_params(s_spinner, 1200, 90);
    lv_obj_set_size(s_spinner, 88, 88);
    lv_obj_set_pos(s_spinner, 136, 154);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(0x33d6ff),
                               LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(s_spinner, 8, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(0x1d4ed8),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(s_spinner, 8, LV_PART_MAIN | LV_STATE_DEFAULT);

    s_label_hint = lv_label_create(ui->page_8);
    lv_obj_set_width(s_label_hint, 320);
    lv_label_set_long_mode(s_label_hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_color(s_label_hint, lv_color_hex(0xffffff),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_label_hint, &lv_font_zh_demo_20,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_label_hint, 48, 88);

    s_label_action = lv_label_create(ui->page_8);
    lv_obj_set_style_text_color(s_label_action, lv_color_hex(0xff3b30),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(s_label_action, &lv_font_zh_demo_56,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_label_action, 92, 124);
    lv_obj_add_flag(s_label_action, LV_OBJ_FLAG_HIDDEN);
}

static void page_asr_on_init(bk_lv_ui_t *ui)
{
    asr_build_widgets(ui);
    s_recognized_active = false;
    s_recognized_start_ms = 0;
    asr_reset_phrase_trigger();
    s_anim_timer = lv_timer_create(anim_timer_cb, 220, NULL);
    s_spinner_kick_ms = lv_tick_get();
    apply_listening_view();
    (void)ui_nav_register_screen(ui->page_8, &page_8_nav_ops);
}

static void page_asr_on_destroy(bk_lv_ui_t *ui)
{
    ui_nav_unregister_screen(ui->page_8);
    if (s_anim_timer != NULL) {
        lv_timer_del(s_anim_timer);
        s_anim_timer = NULL;
    }
    /* page_8 root is deleted by destroy_page_page_8(); clear cached
     * pointers so a future re-attach does not poke freed objects. */
    s_label_state = NULL;
    s_spinner = NULL;
    s_label_hint = NULL;
    s_label_action = NULL;
}

void page_asr_init_hooks(void)
{
    (void)bk_page_set_init_hook(8, page_asr_on_init);
    (void)bk_page_set_destroy_hook(8, page_asr_on_destroy);
}

int page_asr_enter(void)
{
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_8,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_8);
    if (bk_lv_tool_ui.page_8 == NULL || !lv_obj_is_valid(bk_lv_tool_ui.page_8)) {
        return -1;
    }
    return 0;
}

#else  /* !ROBOT_TEST */

void page_asr_init_hooks(void) {}
int  page_asr_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
