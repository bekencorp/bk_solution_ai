/**
 * @file voice_chat_page.c
 * @brief Voice-chat page: full-screen Jolly background video (A: voice input /
 *        question: waiting for the reply / B: reply bubble) with the assistant
 *        reply overlaid as auto-scrolling text. Voice turns run hands-free
 *        (KWS wakeup + VAD auto-send); the
 *        on-screen mic button UI is retired — S4 still toggles PTT as a
 *        hardware fallback. Same dynamic-screen lifetime model as
 *        muse_status.c (torn down eagerly on back; timer never outlives the
 *        screen).
 */
#include "ui/voice_chat_page.h"

#if CONFIG_MUSE_GADGET

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#include <components/log.h>
#include <os/os.h>          /* rtos_get_time */

#include "lvgl.h"
#include "beken_ui.h"
#include "page_hooks.h"       /* bk_page_attach_right_swipe_gesture */
#include "ui_theme.h"
#include "ui_i18n.h"
#include "ui_nav_router.h"
#include "jolly/jolly_video.h"
#include "muse_service.h"
#include "muse_voice.h"
#include "muse_display.h"     /* muse_cjk_font: full-CJK text rendering */
#include "voice_history.h"

#define TAG "voice_chat"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

extern int page_top_enter(void);

static lv_obj_t  *s_screen;
static lv_obj_t  *s_bubble_label;  
static lv_timer_t *s_timer;
static char       s_bubble_last[VOICE_HISTORY_BODY_MAX];  

static int        s_bubble_layout;
static bool       s_video_pending;
static uint32_t   s_push_show_until;

void voice_chat_page_show_push(const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    voice_history_push("Muse", text);
    s_push_show_until = rtos_get_time() + 30000U;
}

static void bubble_scroll_tick(bool active)
{
    static int s_phase; 
    static int s_ticks;

    if (s_bubble_label == NULL || !lv_obj_is_valid(s_bubble_label)) {
        return;
    }
    if (!active) {
        s_phase = 0;
        s_ticks = 0;
        if (lv_obj_get_scroll_y(s_bubble_label) != 0) {
            lv_obj_scroll_to_y(s_bubble_label, 0, LV_ANIM_OFF);
        }
        return;
    }
    lv_coord_t bottom = lv_obj_get_scroll_y(s_bubble_label) +
                        lv_obj_get_scroll_bottom(s_bubble_label);
    if (bottom <= 0) {
        s_phase = 0;
        s_ticks = 0;
        return;
    }
    switch (s_phase) {
    case 0:
        if (++s_ticks >= 6) {
            lv_obj_scroll_to_y(s_bubble_label, bottom, LV_ANIM_ON);
            s_phase = 1;
            s_ticks = 0;
        }
        break;
    case 1:
        if (lv_obj_get_scroll_bottom(s_bubble_label) <= 0) {
            s_phase = 2;
            s_ticks = 0;
        }
        break;
    default:
        if (++s_ticks >= 8) {
            lv_obj_scroll_to_y(s_bubble_label, 0, LV_ANIM_ON);
            s_phase = 0;
            s_ticks = 0;
        }
        break;
    }
}

#define BUBBLE_TEXT_X    (32 + 24)
#define BUBBLE_TEXT_Y    200
#define BUBBLE_TEXT_W    272
#define BUBBLE_TEXT_H    93

static inline ui_lang_t vc_lang(void)
{
    return ui_i18n_get_lang();
}

static bool muse_connected(void)
{
    muse_service_status_t st;
    muse_service_get_status(&st);
    return st.state == MUSE_SERVICE_CONNECTED;
}

static void bubble_apply_layout(int mode)
{
    if (s_bubble_label == NULL || !lv_obj_is_valid(s_bubble_label)) {
        return;
    }
    if (s_bubble_layout == mode) {
        return;
    }
    s_bubble_layout = mode;
    if (mode == 0) {
        /* B 对话框内：白底框里的深灰正文，不加底色。 */
        lv_obj_set_pos(s_bubble_label, BUBBLE_TEXT_X, BUBBLE_TEXT_Y);
        lv_obj_set_size(s_bubble_label, BUBBLE_TEXT_W, BUBBLE_TEXT_H);
        lv_obj_set_style_text_color(s_bubble_label, lv_color_hex(0x333333),
                                    LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bubble_label, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_all(s_bubble_label, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(s_bubble_label, 0, LV_PART_MAIN);
    } else {
        lv_obj_set_size(s_bubble_label, 220, 36);
        lv_obj_set_pos(s_bubble_label, 82, 270);
        lv_obj_set_style_text_color(s_bubble_label, lv_color_hex(0xFFFFFF),
                                    LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_bubble_label, lv_color_hex(0x0A1633),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_bubble_label, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_radius(s_bubble_label, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(s_bubble_label, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(s_bubble_label, 4, LV_PART_MAIN);
    }
    s_bubble_last[0] = '\x01';
    s_bubble_last[1] = '\0';
}


static void bubble_raise(void)
{
    if (s_screen == NULL || s_bubble_label == NULL ||
        !lv_obj_is_valid(s_screen) || !lv_obj_is_valid(s_bubble_label)) {
        return;
    }
    if (muse_display_image_showing()) {
        return;
    }
    uint32_t cnt = lv_obj_get_child_count(s_screen);
    if (cnt == 0U || lv_obj_get_child(s_screen, (int32_t)cnt - 1) != s_bubble_label) {
        lv_obj_move_foreground(s_bubble_label);
    }
}

static void bubble_label_create(lv_obj_t *parent)
{
    s_bubble_label = lv_label_create(parent);
    lv_obj_set_pos(s_bubble_label, BUBBLE_TEXT_X, BUBBLE_TEXT_Y);
    lv_obj_set_size(s_bubble_label, BUBBLE_TEXT_W, BUBBLE_TEXT_H);
    lv_obj_set_style_text_font(s_bubble_label, muse_cjk_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_bubble_label, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_bubble_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(s_bubble_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_scrollbar_mode(s_bubble_label, LV_SCROLLBAR_MODE_OFF);
    lv_label_set_text(s_bubble_label, "");
    lv_obj_clear_flag(s_bubble_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_bubble_label, LV_OBJ_FLAG_HIDDEN);
    s_bubble_layout = 0;
    s_bubble_last[0] = '\0';
}

static void voice_chat_refresh(void)
{
    if (s_screen == NULL || !lv_obj_is_valid(s_screen)) {
        return;
    }

    if (s_bubble_label == NULL || !lv_obj_is_valid(s_bubble_label)) {
        return;
    }
    bool connected = muse_connected();
    bool connecting = !connected;
    muse_voice_ui_phase_t phase = muse_voice_ui_phase();
    bool holding = muse_voice_ui_holding();
    bool waiting = (phase == MUSE_VOICE_UI_WAITING) || holding;

    char notice[96];
    bool has_notice = (phase == MUSE_VOICE_UI_SHOWING) &&
                      muse_voice_ui_notice(notice, sizeof(notice));
    if (!has_notice) {
        notice[0] = '\0';
    }
    bool want_bubble = connecting || waiting || (phase == MUSE_VOICE_UI_SHOWING) ||
                       (int32_t)(rtos_get_time() - s_push_show_until) < 0;
    jolly_clip_t clip = JOLLY_CLIP_VOICE_INPUT;
    if (!connecting && waiting) {
        clip = JOLLY_CLIP_QUESTION;
    } else if (!connecting && want_bubble) {
        clip = JOLLY_CLIP_REPLY_BUBBLE;
    }
    (void)jolly_video_play(clip);

    bubble_apply_layout(connecting ? 2 : (waiting ? 1 : 0));
    char body[VOICE_HISTORY_BODY_MAX];
    const char *bubble;
    if (connecting) {
        bubble = "正在连接…";
    } else if (waiting) {
        bubble = "正在思考，请稍等";
    } else if (notice[0] != '\0') {
        bubble = notice;
    } else {
        bubble = voice_history_get(0, NULL, 0, body, sizeof(body))
                     ? body : "";
    }
    if (strcmp(s_bubble_last, bubble) != 0) {
        snprintf(s_bubble_last, sizeof(s_bubble_last), "%s", bubble);
        lv_label_set_text(s_bubble_label, bubble);
        lv_obj_scroll_to_y(s_bubble_label, 0, LV_ANIM_OFF);
    }


    if (muse_display_image_showing()) {
        lv_obj_add_flag(s_bubble_label, LV_OBJ_FLAG_HIDDEN);
        bubble_scroll_tick(false);
        return;
    }

    if (want_bubble) {
        bubble_raise();
        lv_obj_clear_flag(s_bubble_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_bubble_label, LV_OBJ_FLAG_HIDDEN);
    }
    bubble_scroll_tick(want_bubble);


    {
        static int s_dbg_state = -1;
        int dbg = connecting ? 2 : (waiting ? 1 : 0);
        if (dbg != s_dbg_state) {
            s_dbg_state = dbg;
            lv_area_t box;
            lv_obj_get_coords(s_bubble_label, &box);
            LOGI("bubble state=%d (connecting=%d waiting=%d phase=%d vis=%d) box=%d,%d-%d,%d txt=%s\r\n",
                 dbg, (int)connecting, (int)waiting, (int)phase,
                 (int)!lv_obj_has_flag(s_bubble_label, LV_OBJ_FLAG_HIDDEN),
                 (int)box.x1, (int)box.y1, (int)box.x2, (int)box.y2,
                 lv_label_get_text(s_bubble_label));
        }
    }
}

static void voice_chat_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_video_pending) {
        s_video_pending = false;
        if (s_screen != NULL && lv_obj_is_valid(s_screen)) {
            (void)jolly_video_create(s_screen);
            bubble_label_create(s_screen);
        }
    }
    voice_chat_refresh();
}

static void voice_chat_toggle_ptt(void)
{
    if (!muse_connected()) {
        LOGI("PTT ignored: muse not connected\r\n");
        return;
    }
    int rc = muse_voice_ptt_toggle();
    LOGI("voice page PTT toggle -> %d (recording=%u)\r\n", rc,
         (unsigned)muse_voice_recording());
    if (rc != 0) {
        ui_theme_create_popup(vc_lang() == UI_LANG_ZH ?
                              "请稍后再试" : "Please try again");
    }
    voice_chat_refresh();
}

static void voice_chat_teardown(void)
{
    lv_obj_t *scr = s_screen;
    s_screen = NULL;
    s_bubble_label = NULL;
    s_bubble_layout = 0;
    s_bubble_last[0] = '\0';
    s_video_pending = false;
    if (s_timer != NULL) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    jolly_video_destroy();
    if (scr != NULL && lv_obj_is_valid(scr)) {
        ui_nav_unregister_screen(scr);
    }
    (void)page_top_enter();  /* switch active screen first */
    if (scr != NULL && lv_obj_is_valid(scr)) {
        lv_obj_del(scr);
    }
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    voice_chat_teardown();
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    voice_chat_toggle_ptt();
}

static const ui_page_nav_ops_t s_voice_chat_nav_ops = {
    .on_focus_prev = NULL,
    .on_focus_next = NULL,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
    .on_confirm_long = NULL,
};

int voice_chat_page_enter(void)
{
    if (s_screen != NULL && lv_obj_is_valid(s_screen)) {
        lv_screen_load(s_screen);

        voice_chat_refresh();
        return 0;
    }
    lv_obj_t *screen = lv_obj_create(NULL);
    ui_theme_apply_screen(screen);

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B1E3C),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(0x0B1E3C),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_NONE,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);

    if (ui_disp_is_lvgl_thread()) {
        (void)jolly_video_create(screen);
        bubble_label_create(screen);
    } else {
        s_video_pending = true;
    }

    s_screen = screen;
    bk_page_attach_right_swipe_gesture(screen);
    voice_chat_refresh();
    lv_screen_load(screen);
    (void)ui_nav_register_screen(screen, &s_voice_chat_nav_ops);

    s_timer = lv_timer_create(voice_chat_timer_cb, 300, NULL);
    LOGI("voice chat page entered\r\n");
    return 0;
}

bool voice_chat_page_is_active(void)
{
    return s_screen != NULL && lv_obj_is_valid(s_screen);
}

#else  /* !CONFIG_MUSE_GADGET */

int voice_chat_page_enter(void) { return 0; }

bool voice_chat_page_is_active(void) { return false; }

#endif /* CONFIG_MUSE_GADGET */
