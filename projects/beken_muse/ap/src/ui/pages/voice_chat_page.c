/**
 * @file voice_chat_page.c
 * @brief Voice-chat page: full-screen Jolly background video (A: voice input /
 *        B: reply bubble) with the assistant reply overlaid as auto-scrolling
 *        text. Voice turns run hands-free (KWS wakeup + VAD auto-send); the
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
static lv_obj_t  *s_bubble_label;   /* 视频 B 对话框里的回复文字 */
static lv_timer_t *s_timer;
static char       s_bubble_last[VOICE_HISTORY_BODY_MAX];  /* 上次气泡文字（判变用） */

/* 气泡文字超出可视高度时的纵向自动滚动：顶部停 1.8s -> 平滑滚到底 ->
 * 底部停 2.4s -> 回顶，循环。由 300ms 页面定时器驱动。 */
static void bubble_scroll_tick(bool active)
{
    static int s_phase;   /* 0=顶停 1=滚动中 2=底停 */
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
    if (bottom <= 0) {   /* 内容放得下，钉在顶部 */
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

/* 视频 B 对话框内文字区（预览页对齐数值：左 7.5% 上 62.5% 宽 85% 高 29%）。
 * 片源是 320x320 内容居中嵌进 384x320 帧（左右各 32px 同色补边，无裁剪），
 * 有效内容区等价于"320 帧 + x 偏移 32"，沿用原坐标即可。 */
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

static void voice_chat_refresh(void)
{
    if (s_screen == NULL || !lv_obj_is_valid(s_screen)) {
        return;
    }
    bool connected = muse_connected();
    muse_voice_ui_phase_t phase = muse_voice_ui_phase();

    /* ASR 回声（服务器听清了什么）说完后 ~1s 就到：等回复期间先上屏并切
     * 到对话框场景，给用户即时反馈；回复到达进入 SHOWING 后由
     * voice_history 的正文自然替换。 */
    char heard[192];
    bool interim = (phase == MUSE_VOICE_UI_WAITING) &&
                   muse_voice_heard(heard, sizeof(heard));
    bool want_bubble = (phase == MUSE_VOICE_UI_SHOWING) || interim;

    /* 视频状态机：语音输入页(A) / 回复对话框页(B)；素材缺失时 jolly_video
     * 隐藏自己，页面只剩气泡文字。录音/空闲 = A，等回声与显示回复 = B。
     * B→A 的整轮重建发生在唤醒后（应答音播放窗口内，不产语音数据），
     * TCP 窗口修复后上行 ~80KB/s 有 2 倍余量，不再避让（2026-11-06 用户
     * 明确要求唤醒即回 A 场景）。 */
    if (connected) {
        (void)jolly_video_play(want_bubble ? JOLLY_CLIP_REPLY_BUBBLE
                                           : JOLLY_CLIP_VOICE_INPUT);
    } else {
        jolly_video_stop();
    }
    bool show_bubble = want_bubble;

    /* 气泡文字只在内容变化时重写（set_text 会重置滚动位置）；新内容来了
     * 从头开始滚。 */
    char body[VOICE_HISTORY_BODY_MAX];
    const char *bubble;
    if (interim) {
        snprintf(body, sizeof(body), "听到了：%s", heard);
        bubble = body;
    } else {
        bubble = voice_history_get(0, NULL, 0, body, sizeof(body))
                     ? body : "";
    }
    if (strcmp(s_bubble_last, bubble) != 0) {
        snprintf(s_bubble_last, sizeof(s_bubble_last), "%s", bubble);
        lv_label_set_text(s_bubble_label, bubble);
        lv_obj_scroll_to_y(s_bubble_label, 0, LV_ANIM_OFF);
    }

    /* 回复文字只在 SHOWING 阶段显示。 */
    if (show_bubble) {
        lv_obj_clear_flag(s_bubble_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_bubble_label, LV_OBJ_FLAG_HIDDEN);
    }
    bubble_scroll_tick(show_bubble);
}

static void voice_chat_timer_cb(lv_timer_t *timer)
{
    (void)timer;
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
    if (rc == -2 || rc == -6) {
        /* The link died silently while idle (no app-level ping) and the
         * take was dropped; without feedback this looked like a dead
         * button and the user hammered the mic. The keepalive makes the
         * reconnect heal in the background — just say when to retry. */
        ui_theme_create_popup(vc_lang() == UI_LANG_ZH ?
                              "连接断开，重连后请再说一次" :
                              "Link dropped, please speak again once reconnected");
    } else if (rc != 0) {
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
        /* 再入页面时在返回前同步一次状态：渲染发生在之后的
         * lv_timer_handler 里，这里先刷新就不会闪出过期画面。 */
        voice_chat_refresh();
        return 0;
    }
    lv_obj_t *screen = lv_obj_create(NULL);
    ui_theme_apply_screen(screen);

    /* 帧序列播放器：铺满整屏；素材缺失时它自己隐藏。 */
    (void)jolly_video_create(screen);

    /* 视频 B 对话框里的回复文字：最顶层，默认隐藏，SHOWING 时显示。 */
    s_bubble_label = lv_label_create(screen);
    lv_obj_set_pos(s_bubble_label, BUBBLE_TEXT_X, BUBBLE_TEXT_Y);
    lv_obj_set_size(s_bubble_label, BUBBLE_TEXT_W, BUBBLE_TEXT_H);
    lv_obj_set_style_text_font(s_bubble_label, muse_cjk_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_bubble_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_bubble_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(s_bubble_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_scrollbar_mode(s_bubble_label, LV_SCROLLBAR_MODE_OFF);
    lv_label_set_text(s_bubble_label, "");
    lv_obj_clear_flag(s_bubble_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_bubble_label, LV_OBJ_FLAG_HIDDEN);

    s_screen = screen;
    bk_page_attach_right_swipe_gesture(screen);
    /* 首帧渲染前先同步一次状态（视频 clip 选择 + 气泡可见性）。 */
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
