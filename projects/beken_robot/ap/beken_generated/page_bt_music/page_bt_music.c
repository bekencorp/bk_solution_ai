/**
 * @file page_bt_music.c
 * @brief Bluetooth A2DP music page -- "Glass Spectrum" (design scheme 2).
 *
 * Layout (385 x 320):
 *   - top status bar: connection dot + state text (i18n via lv_font_ali_16);
 *   - a cyan/purple "glass" panel with lightweight accent lines and 6 rainbow
 *     bars, one per hand joint (5 fingers low->high freq + wrist/base);
 *   - bottom row like the reference: prev / play / next / vol- / vol+ /
 *     robot claw on/off.
 *
 * Everything is solid fill + alpha blend only -- no per-bar gradients, no box
 * shadows, no layers -- to keep the first-paint internal-RAM peak small (the
 * spectrum draw shares the scarce internal pool with the classic-BT host).
 */
#include "page_bt_music.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include <os/os.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "page_hooks.h"
#include "ui_nav_router.h"
#include "ui_list_menu.h"
#include "ui_i18n.h"
#include "demo/a2dp_sink.h"
#include "demo/bt_music.h"
#include "demo/bt_rhythm.h"
#if CONFIG_BT
#include "bt_manager.h"
#endif

#ifdef ROBOT_TEST

#define BTM_TEXT_FONT  (&lv_font_ali_16)

/* ---- Neon palette (dark cyber theme) ---- */
#define C_BG_BOTTOM    0x05060f
#define C_BAR_TRACK    0x101934
#define C_BTN_BG       0x111936
#define C_BTN_BORDER   0x3d7cff
#define C_BTN_FG       0xcfe0ff
#define C_PLAY_1       0x5ce1ff
#define C_RING         0x8ff0ff   /* neon ring around the play button */
#define C_TITLE        0xeaf6ff
#define C_BASE_GLOW    0x27dfff
#define C_LINK_OK      0x36e07f   /* connected (green) */
#define C_LINK_OFF     0x6b7aa0   /* disconnected (dim) */
#define C_DIVIDER      0x1c2746
#define C_ROBOT_ON     0xb75cff
#define C_ROBOT_OFF    0x52658f

/* ---- Layout ----
 * 6 bars = the 6 hand joints (NOT a generic spectrum): bars 0..4 are the five
 * fingers in low-freq -> high-freq order (bar0 = pinky/lowest, bar4 =
 * thumb/highest), bar5 = the wrist/base rotation. Each bar mirrors that servo's
 * live travel via bt_rhythm_get_pose_levels(). */
#define BTM_BARS            6
#define BTM_BAR_W           34
#define BTM_BAR_GAP         20
#define BTM_BAR_TOP         50
#define BTM_BAR_H           144     /* baseline at y = TOP + H = 194 */
#define BTM_BAR_MIN         6
#define BTM_CAP_H           2
#define BTM_BASE_Y          (BTM_BAR_TOP + BTM_BAR_H)
#define BTM_BAR_VIS_MAX      84.0f  /* UI-only cap: hand can be 100, bar won't look full */
#define BTM_BAR_VIS_GAIN     0.96f  /* compression starts before the top */
#define BTM_BAR_TOP_COMPRESS 0.12f  /* stronger compression near 100 */
#define BTM_IMM_TIMEOUT_MS   5000U
#define BTM_TRANSPORT_DEBOUNCE_MS 300U
#define BTM_IMM_BAR_W        38
#define BTM_IMM_BAR_GAP      17
#define BTM_IMM_BAR_TOP      30
#define BTM_IMM_BAR_H        236
#define BTM_IMM_BASE_Y       (BTM_IMM_BAR_TOP + BTM_IMM_BAR_H)
#define BTM_IMM_CAP_H        3

typedef enum {
    BTM_ACT_NONE = 0,
    BTM_ACT_VOL_DOWN,
    BTM_ACT_PREV,
    BTM_ACT_PLAY,
    BTM_ACT_NEXT,
    BTM_ACT_VOL_UP,
    BTM_ACT_ROBOT,
} btm_action_t;

/* Per-bar neon hue ramp: 5 fingers (low->high freq) then the base bar (warm). */
static const uint32_t s_bar_hue[BTM_BARS] = {
    0x00f0c0, 0x29b6ff, 0x6a5cff, 0xc45cff, 0xff4fb0, 0xffb14f,
};

static lv_obj_t  *s_screen;
static lv_obj_t  *s_chrome;       /* one custom-drawn object for status + transport */
static lv_obj_t  *s_visualizer;   /* one custom-drawn object for bars + caps */
static lv_obj_t  *s_imm_catch;    /* transparent full-screen tap-to-exit catcher */
static lv_timer_t *s_meter_timer;
static lv_timer_t *s_kick_timer;
static lv_obj_t   *s_toast;       /* low-memory hint, lives on the top layer */
static lv_timer_t *s_toast_timer;

static float s_disp[BTM_BARS];
static float s_peak[BTM_BARS];
static bool  s_immersive;
static uint32_t s_last_touch_ms;
static uint32_t s_last_transport_ms;
static uint32_t s_tick;

/* Cached UI states so the ~per-second refreshes only touch LVGL on change. */
static int s_ui_linked  = -1;
static int s_ui_dancing = -1;
static int s_ui_playing = -1;

#define BTM_KEY_ACTION_COUNT  6
static const btm_action_t s_transport_actions[BTM_KEY_ACTION_COUNT] = {
    BTM_ACT_VOL_DOWN, BTM_ACT_PREV, BTM_ACT_PLAY, BTM_ACT_NEXT, BTM_ACT_VOL_UP,
    BTM_ACT_ROBOT,
};
static int s_key_focus_idx = 2; /* default: play/pause */
static bool s_exit_pending;
static bool s_dance_user_enabled = true;
static bool s_render_ready;
static bool s_playing;
static bool s_streaming;

static void apply_rhythm_state(void)
{
    bt_rhythm_set_enabled(s_render_ready && s_streaming);
    bt_rhythm_set_hand_output(s_dance_user_enabled);
}

static void on_playback_start(void)
{
    if (s_playing) {
        return;
    }
    s_playing = true;
}

static void on_playback_stop(void)
{
    if (!s_playing) {
        return;
    }
    s_playing = false;
}

static void on_a2dp_stream_start(void)
{
    if (s_streaming) {
        return;
    }
    s_streaming = true;
    apply_rhythm_state();
}

static void on_a2dp_stream_stop(void)
{
    if (!s_streaming) {
        return;
    }
    s_streaming = false;
    apply_rhythm_state();
}

static void enable_speaker_after_first_paint(void)
{
#if CONFIG_BT
    s_render_ready = true;
    apply_rhythm_state();
    a2dp_sink_demo_audio_spk_enable(1);
#endif
}

static bool phone_is_connected(void)
{
#if CONFIG_BT
    if (!a2dp_sink_demo_is_active()) {
        return false;
    }
    if (bt_manager_get_connect_state() < BT_STATE_LINK_CONNECTED) {
        return false;
    }
    return a2dp_sink_demo_is_connected() != 0;
#else
    return false;
#endif
}

static void set_immersive(bool enable);
static void refresh_hand(void);
static void dismiss_toast(void);
static void show_toast(const char *message, const lv_font_t *font);
static void show_i18n_toast(ui_str_id_t id, const char *en_symbol);
static void clear_refs(void);

/* ---------------- small builders ---------------- */

static uint32_t lighten(uint32_t c, uint8_t pct)
{
    uint32_t r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
    r += (255 - r) * pct / 100;
    g += (255 - g) * pct / 100;
    b += (255 - b) * pct / 100;
    return (r << 16) | (g << 8) | b;
}

static float pose_to_bar_value(uint8_t pose)
{
    /* Pose levels are the REAL hand motion: fingers 0..100 = curled..open,
     * base 0..100 = center..max offset. The UI should not look pinned at full
     * just because the hand reaches a wide pose, so display uses a compressed
     * curve while the mechanical motion remains unchanged. */
    float raw = (float)pose;
    float norm = raw / 100.0f;
    float visual = raw * (BTM_BAR_VIS_GAIN - BTM_BAR_TOP_COMPRESS * norm);

    if (visual > BTM_BAR_VIS_MAX) {
        visual = BTM_BAR_VIS_MAX;
    }
    if (visual < (float)BTM_BAR_MIN) {
        visual = (float)BTM_BAR_MIN;
    }
    return visual;
}

static void draw_rect(lv_layer_t *layer, int x, int y, int w, int h,
                      uint32_t color, lv_opa_t opa, int radius)
{
    if (layer == NULL || w <= 0 || h <= 0 || opa == LV_OPA_TRANSP) {
        return;
    }

    lv_area_t area;
    area.x1 = x;
    area.y1 = y;
    area.x2 = x + w - 1;
    area.y2 = y + h - 1;

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = opa;
    dsc.radius = radius;
    dsc.border_width = 0;
    lv_draw_rect(layer, &dsc, &area);
}

static void draw_text(lv_layer_t *layer, const char *text, int x, int y, int w, int h,
                      const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    if (layer == NULL || text == NULL || w <= 0 || h <= 0) {
        return;
    }

    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.font = font;
    dsc.color = lv_color_hex(color);
    dsc.opa = LV_OPA_COVER;
    dsc.align = align;
    dsc.text_static = 1;

    lv_point_t txt_size;
    lv_text_get_size(&txt_size, text, font, 0, 0, w, LV_TEXT_FLAG_NONE);
    int text_y = y + (h - txt_size.y) / 2;
    if (text_y < y) {
        text_y = y;
    }

    lv_area_t area = {
        .x1 = x,
        .y1 = text_y,
        .x2 = x + w - 1,
        .y2 = text_y + txt_size.y - 1,
    };
    lv_draw_label(layer, &dsc, &area);
}

static void draw_button_base(lv_layer_t *layer, int x, int y, int w, int h,
                             bool primary, bool state_active, bool key_focus)
{
    int border_w;
    uint32_t border;

    if (key_focus) {
        /* Key focus: bright cyan ring, distinct from play default and robot ON. */
        border = C_RING;
        border_w = 3;
    } else if (primary) {
        border = C_RING;
        border_w = 2;
    } else if (state_active) {
        border = C_ROBOT_ON;
        border_w = 2;
    } else {
        border = C_BTN_BORDER;
        border_w = 1;
    }

    int radius = h / 2;

    draw_rect(layer, x, y, w, h, border,
              (primary && !key_focus) ? LV_OPA_80 : LV_OPA_COVER, radius);
    draw_rect(layer, x + border_w, y + border_w, w - border_w * 2, h - border_w * 2,
              primary ? C_PLAY_1 : C_BTN_BG, LV_OPA_COVER,
              (radius > border_w) ? (radius - border_w) : 0);
}

static void get_button_area(btm_action_t action, int *x, int *y, int *w, int *h)
{
    const int dn = 42, dp = 52, gap = 12;
    const int row_w = dn * 4 + dp + gap * 4;
    const int rx = (LOGICAL_SCREEN_WIDTH - row_w) / 2;
    const int yn = 216, yp = 211;

    switch (action) {
    case BTM_ACT_VOL_DOWN:
        *x = rx; *y = yn; *w = dn; *h = dn;
        break;
    case BTM_ACT_PREV:
        *x = rx + (dn + gap); *y = yn; *w = dn; *h = dn;
        break;
    case BTM_ACT_PLAY:
        *x = rx + 2 * (dn + gap); *y = yp; *w = dp; *h = dp;
        break;
    case BTM_ACT_NEXT:
        *x = rx + 2 * (dn + gap) + dp + gap; *y = yn; *w = dn; *h = dn;
        break;
    case BTM_ACT_VOL_UP:
        *x = rx + 3 * (dn + gap) + dp + gap; *y = yn; *w = dn; *h = dn;
        break;
    case BTM_ACT_ROBOT:
        *x = 54; *y = 274; *w = 276; *h = 34;
        break;
    default:
        *x = *y = *w = *h = 0;
        break;
    }
}

static bool point_in_button(const lv_point_t *p, btm_action_t action)
{
    int x, y, w, h;
    get_button_area(action, &x, &y, &w, &h);
    return p != NULL &&
           p->x >= x && p->x < x + w &&
           p->y >= y && p->y < y + h;
}

static btm_action_t action_from_point(const lv_point_t *p)
{
    for (uint32_t i = 0; i < BTM_KEY_ACTION_COUNT; i++) {
        if (point_in_button(p, s_transport_actions[i])) {
            return s_transport_actions[i];
        }
    }
    return BTM_ACT_NONE;
}

static void visualizer_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) {
        return;
    }

    lv_layer_t *layer = lv_event_get_layer(e);
    int bw = s_immersive ? BTM_IMM_BAR_W : BTM_BAR_W;
    int bh = s_immersive ? BTM_IMM_BAR_H : BTM_BAR_H;
    int btop = s_immersive ? BTM_IMM_BAR_TOP : BTM_BAR_TOP;
    int gap = s_immersive ? BTM_IMM_BAR_GAP : BTM_BAR_GAP;
    int base_y = s_immersive ? BTM_IMM_BASE_Y : BTM_BASE_Y;
    int cap_h = s_immersive ? BTM_IMM_CAP_H : BTM_CAP_H;
    int span = BTM_BARS * bw + (BTM_BARS - 1) * gap;
    int x0 = (LOGICAL_SCREEN_WIDTH - span) / 2;

    draw_rect(layer, 32, base_y + 3, LOGICAL_SCREEN_WIDTH - 64, 2,
              C_BASE_GLOW, LV_OPA_40, 1);

    for (int i = 0; i < BTM_BARS; i++) {
        int x = x0 + i * (bw + gap);
        int bar_h = (int)(s_disp[i] / 100.0f * (float)bh + 0.5f);
        int peak_h = (int)(s_peak[i] / 100.0f * (float)bh + 0.5f);
        int cap_y = base_y - peak_h - cap_h;

        if (bar_h < BTM_BAR_MIN) {
            bar_h = BTM_BAR_MIN;
        }
        if (cap_y < btop) {
            cap_y = btop;
        }

        draw_rect(layer, x, btop, bw, bh, C_BAR_TRACK, LV_OPA_COVER, 0);
        draw_rect(layer, x, base_y - bar_h, bw, bar_h, s_bar_hue[i],
                  LV_OPA_COVER, 0);
        draw_rect(layer, x, cap_y, bw, cap_h, lighten(s_bar_hue[i], 55),
                  LV_OPA_COVER, 0);
    }
}

static void note_user_touch(void)
{
    s_last_touch_ms = (uint32_t)rtos_get_time();
    if (s_immersive) {
        set_immersive(false);
    }
}

static void on_any_press(lv_event_t *e)
{
    (void)e;
    note_user_touch();
}

static const char *button_symbol(btm_action_t action)
{
    switch (action) {
    case BTM_ACT_VOL_DOWN: return LV_SYMBOL_VOLUME_MID;
    case BTM_ACT_PREV:     return LV_SYMBOL_PREV;
    case BTM_ACT_PLAY:     return (s_ui_playing > 0) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
    case BTM_ACT_NEXT:     return LV_SYMBOL_NEXT;
    case BTM_ACT_VOL_UP:   return LV_SYMBOL_VOLUME_MAX;
    default:               return "";
    }
}

static void draw_robot_icon(lv_layer_t *layer, int x, int y, int h)
{
    draw_rect(layer, x + 9,  y + 11, 18, 15, C_ROBOT_ON, LV_OPA_COVER, 4);
    draw_rect(layer, x + 17, y + 6,   2,  5, C_ROBOT_ON, LV_OPA_COVER, 1);
    draw_rect(layer, x + 14, y + 4,   8,  3, C_ROBOT_ON, LV_OPA_COVER, 2);
    draw_rect(layer, x + 13, y + 17,  3,  3, C_BG_BOTTOM, LV_OPA_COVER, 2);
    draw_rect(layer, x + 21, y + 17,  3,  3, C_BG_BOTTOM, LV_OPA_COVER, 2);
    draw_rect(layer, x + 36, y + 7,   1, h - 14, C_ROBOT_ON, LV_OPA_60, 0);
}

static void draw_transport_button(lv_layer_t *layer, btm_action_t action)
{
    int x, y, w, h;
    get_button_area(action, &x, &y, &w, &h);
    bool primary = (action == BTM_ACT_PLAY);
    bool focused = (action == s_transport_actions[s_key_focus_idx]);

    draw_button_base(layer, x, y, w, h, primary, false, focused);
    draw_text(layer, button_symbol(action), x, y, w, h,
              LV_FONT_DEFAULT, primary ? 0x04122e : C_BTN_FG, LV_TEXT_ALIGN_CENTER);
}

static void draw_robot_button(lv_layer_t *layer)
{
    int x, y, w, h;
    bool dancing = (s_ui_dancing > 0);
    bool focused = (s_transport_actions[s_key_focus_idx] == BTM_ACT_ROBOT);

    get_button_area(BTM_ACT_ROBOT, &x, &y, &w, &h);
    draw_button_base(layer, x, y, w, h, false, dancing && !focused, focused);
    draw_robot_icon(layer, x, y, h);
    draw_text(layer, dancing ? "beken claw:on" : "beken claw:off",
              x, y + 2, w, h - 4, LV_FONT_DEFAULT,
              dancing ? C_TITLE : C_LINK_OFF, LV_TEXT_ALIGN_CENTER);
}

static void chrome_draw_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) {
        return;
    }

    lv_layer_t *layer = lv_event_get_layer(e);
    bool linked = (s_ui_linked > 0);

    draw_rect(layer, 28, 24, 9, 9, linked ? C_LINK_OK : C_LINK_OFF, LV_OPA_COVER, 5);
    draw_text(layer, LV_SYMBOL_BLUETOOTH, 46, 18, 16, 18, LV_FONT_DEFAULT,
              linked ? C_TITLE : C_LINK_OFF, LV_TEXT_ALIGN_LEFT);
    draw_text(layer, ui_tr(linked ? STR_BT_MUSIC_CONNECTED : STR_BT_MUSIC_SEARCHING),
              62, 18, LOGICAL_SCREEN_WIDTH - 88, 18, BTM_TEXT_FONT,
              linked ? C_TITLE : C_LINK_OFF, LV_TEXT_ALIGN_LEFT);
    draw_rect(layer, 16, 46, LOGICAL_SCREEN_WIDTH - 32, 1, C_DIVIDER, LV_OPA_COVER, 0);

    draw_transport_button(layer, BTM_ACT_VOL_DOWN);
    draw_transport_button(layer, BTM_ACT_PREV);
    draw_transport_button(layer, BTM_ACT_PLAY);
    draw_transport_button(layer, BTM_ACT_NEXT);
    draw_transport_button(layer, BTM_ACT_VOL_UP);
    draw_robot_button(layer);
}

static bool action_needs_link(btm_action_t action)
{
    return action == BTM_ACT_VOL_DOWN ||
           action == BTM_ACT_PREV ||
           action == BTM_ACT_PLAY ||
           action == BTM_ACT_NEXT ||
           action == BTM_ACT_VOL_UP;
}

static void dispatch_action(btm_action_t action)
{
    if (action_needs_link(action) && !phone_is_connected()) {
        show_i18n_toast(STR_BT_MUSIC_CONNECT_PHONE, LV_SYMBOL_BLUETOOTH);
        return;
    }

    if (action_needs_link(action)) {
        uint32_t now = (uint32_t)rtos_get_time();
        if ((uint32_t)(now - s_last_transport_ms) < BTM_TRANSPORT_DEBOUNCE_MS) {
            return;
        }
        s_last_transport_ms = now;
    }

    switch (action) {
    case BTM_ACT_VOL_DOWN:
        a2dp_sink_demo_vol_down();
        break;
    case BTM_ACT_PREV:
        a2dp_sink_demo_prev();
        break;
    case BTM_ACT_PLAY:
        if (s_playing) {
            on_playback_stop();
            a2dp_sink_demo_pause();
        } else {
            on_playback_start();
            a2dp_sink_demo_play();
        }
        break;
    case BTM_ACT_NEXT:
        a2dp_sink_demo_next();
        break;
    case BTM_ACT_VOL_UP:
        a2dp_sink_demo_vol_up();
        break;
    case BTM_ACT_ROBOT:
        s_dance_user_enabled = !s_dance_user_enabled;
        apply_rhythm_state();
        refresh_hand();
        break;
    default:
        break;
    }
}

static void chrome_click_cb(lv_event_t *e)
{
    (void)e;

    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) {
        return;
    }

    lv_point_t p;
    lv_indev_get_point(indev, &p);
    dispatch_action(action_from_point(&p));
}

/* ---------------- dynamic refreshes ---------------- */

static void refresh_status(void)
{
    bool linked = phone_is_connected();
    if (s_ui_linked == (int)linked) {
        return;
    }
    s_ui_linked = (int)linked;
    if (s_chrome != NULL && lv_obj_is_valid(s_chrome)) {
        lv_obj_invalidate(s_chrome);
    }
}

static void refresh_hand(void)
{
    bool dancing = s_dance_user_enabled;
    if (s_ui_dancing == (int)dancing) {
        return;
    }
    s_ui_dancing = (int)dancing;
    if (s_chrome != NULL && lv_obj_is_valid(s_chrome)) {
        lv_obj_invalidate(s_chrome);
    }
}

static void set_play_symbol(bool playing)
{
    if (s_ui_playing == (int)playing) {
        return;
    }
    s_ui_playing = (int)playing;
    if (s_chrome != NULL && lv_obj_is_valid(s_chrome)) {
        lv_obj_invalidate(s_chrome);
    }
}

/* Reposition/resize bars between compact and immersive geometry. */
static void set_immersive(bool enable)
{
    if (enable == s_immersive) {
        s_last_touch_ms = (uint32_t)rtos_get_time();
        return;
    }
    if (enable && (s_screen == NULL || !lv_obj_is_valid(s_screen))) {
        return;
    }

    s_immersive = enable;
    if (s_visualizer != NULL && lv_obj_is_valid(s_visualizer)) {
        lv_obj_invalidate(s_visualizer);
    }

    if (s_chrome != NULL && lv_obj_is_valid(s_chrome)) {
        if (enable) {
            lv_obj_add_flag(s_chrome, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_chrome, LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* A transparent full-screen catcher sits above the bars only while immersive,
     * so a tap anywhere leaves immersive without redrawing an opaque overlay. */
    if (s_imm_catch != NULL && lv_obj_is_valid(s_imm_catch)) {
        if (enable) {
            lv_obj_remove_flag(s_imm_catch, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(s_imm_catch);
        } else {
            lv_obj_add_flag(s_imm_catch, LV_OBJ_FLAG_HIDDEN);
        }
    }
    s_last_touch_ms = (uint32_t)rtos_get_time();
}

/* ---------------- meter ---------------- */

static void meter_kick_cb(lv_timer_t *timer)
{
    if (s_meter_timer != NULL) {
        lv_timer_resume(s_meter_timer);
    }
    enable_speaker_after_first_paint();
    s_kick_timer = NULL;
    lv_timer_delete(timer);
}

static void meter_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    /* Each bar follows one hand joint (5 fingers low->high + base). The source
     * is still the real pose level, but the display value is compressed so the
     * UI leaves headroom and doesn't look pinned near full. */
    uint8_t sp[BTM_BARS] = {0};
    bt_rhythm_get_pose_levels(sp, BTM_BARS);
    uint32_t now = (uint32_t)rtos_get_time();
    bool playing = s_playing;

    for (int i = 0; i < BTM_BARS; i++) {
        float target = pose_to_bar_value(sp[i]);
        float a = (target > s_disp[i]) ? 0.60f : 0.34f;   /* faster fall */
        s_disp[i] += (target - s_disp[i]) * a;

        if (s_disp[i] >= s_peak[i]) {
            s_peak[i] = s_disp[i];
        } else {
            s_peak[i] -= 2.4f;   /* peak cap drops faster too */
            if (s_peak[i] < s_disp[i]) {
                s_peak[i] = s_disp[i];
            }
        }
    }

    if (s_visualizer != NULL && lv_obj_is_valid(s_visualizer)) {
        lv_obj_invalidate(s_visualizer);
    }

    if (!playing) {
        if (s_immersive) {
            set_immersive(false);
        }
        s_last_touch_ms = now;
    } else if (!s_immersive &&
               (uint32_t)(now - s_last_touch_ms) >= BTM_IMM_TIMEOUT_MS) {
        set_immersive(true);
    }

    set_play_symbol(playing);

    if ((++s_tick % 12U) == 0U) {
        refresh_hand();
        refresh_status();
    }
}

/* ---------------- navigation ---------------- */

/* Tear down BT/audio/timers; shared by every page-exit path. */
static void bt_music_page_teardown(void)
{
    dismiss_toast();
    set_immersive(false);
    if (s_meter_timer != NULL) {
        lv_timer_delete(s_meter_timer);
        s_meter_timer = NULL;
    }
    if (s_kick_timer != NULL) {
        lv_timer_delete(s_kick_timer);
        s_kick_timer = NULL;
    }
#if CONFIG_BT
    a2dp_sink_demo_set_playback_listener(NULL, NULL);
    a2dp_sink_demo_set_stream_listener(NULL, NULL);
#endif
    s_render_ready = false;
    s_playing = false;
    s_streaming = false;
    if (g_demo_bt_music.stop != NULL) {
        (void)g_demo_bt_music.stop();
    }
}

/* Right-swipe (ui_touch) and S4-double both dispatch SCREEN_PREV -> here. */
static void bt_music_exit_to_menu(bk_lv_ui_t *ui)
{
    if (ui == NULL) {
        return;
    }
    bt_music_page_teardown();
    (void)ui_demo_return_to_menu();
    if (s_screen != NULL && lv_obj_is_valid(s_screen)) {
        ui_nav_unregister_screen(s_screen);
        lv_obj_del(s_screen);
    }
    s_screen = NULL;
    clear_refs();
}

static void clear_refs(void)
{
    s_chrome = NULL;
    s_visualizer = NULL;
    s_imm_catch = NULL;
    s_immersive = false;
    s_tick = 0;
    s_last_touch_ms = 0;
    s_last_transport_ms = 0;
    s_ui_linked = -1;
    s_ui_dancing = -1;
    s_ui_playing = -1;
    s_key_focus_idx = 2;
    for (int i = 0; i < BTM_BARS; i++) {
        s_disp[i] = (float)BTM_BAR_MIN;
        s_peak[i] = (float)BTM_BAR_MIN;
    }
}

static void apply_key_focus(void)
{
    note_user_touch();
    if (s_chrome != NULL && lv_obj_is_valid(s_chrome)) {
        lv_obj_invalidate(s_chrome);
    }
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    s_key_focus_idx = (s_key_focus_idx + BTM_KEY_ACTION_COUNT - 1) % BTM_KEY_ACTION_COUNT;
    apply_key_focus();
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    (void)ui;
    s_key_focus_idx = (s_key_focus_idx + 1) % BTM_KEY_ACTION_COUNT;
    apply_key_focus();
}

static void bt_music_exit_async(void *arg)
{
    bk_lv_ui_t *ui = (bk_lv_ui_t *)arg;

    s_exit_pending = false;
    bt_music_exit_to_menu(ui);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    /* Key nav enters via ui_nav_dispatch_event() on the key task (disp_lock).
     * a2dp_sink_demo_stop() may block while tearing down BT; defer to LVGL
     * thread so the key task returns immediately (touch swipe already LVGL). */
    if (s_exit_pending) {
        return;
    }
    s_exit_pending = true;
#if CONFIG_BT
    a2dp_sink_demo_begin_teardown();
#endif
    if (lv_async_call(bt_music_exit_async, ui) != LV_RESULT_OK) {
        s_exit_pending = false;
        bt_music_exit_to_menu(ui);
    }
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    note_user_touch();
    dispatch_action(s_transport_actions[s_key_focus_idx]);
}

static const ui_page_nav_ops_t s_bt_music_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
};

/* ---------------- low-memory hint ---------------- */

static void toast_close_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_toast != NULL && lv_obj_is_valid(s_toast)) {
        lv_obj_del(s_toast);
    }
    s_toast = NULL;
    s_toast_timer = NULL;
}

/* ---------------- toast hints ---------------- */

static void dismiss_toast(void)
{
    if (s_toast_timer != NULL) {
        lv_timer_t *t = s_toast_timer;
        s_toast_timer = NULL;
        lv_timer_delete(t);
    }
    if (s_toast != NULL && lv_obj_is_valid(s_toast)) {
        lv_obj_del(s_toast);
    }
    s_toast = NULL;
}

static void show_toast(const char *message, const lv_font_t *font)
{
    if (message == NULL || message[0] == '\0') {
        return;
    }
    if (font == NULL) {
        font = LV_FONT_DEFAULT;
    }
    if (s_toast != NULL && lv_obj_is_valid(s_toast)) {
        dismiss_toast();
    }

    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_width(s_toast, LOGICAL_SCREEN_WIDTH - 56);
    lv_obj_set_height(s_toast, LV_SIZE_CONTENT);
    lv_obj_center(s_toast);
    lv_obj_set_style_pad_all(s_toast, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_toast, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x10182e), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_toast, lv_color_hex(C_BTN_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_toast, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_toast, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *lbl = lv_label_create(s_toast);
    lv_obj_set_width(lbl, LOGICAL_SCREEN_WIDTH - 56 - 28);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl, lv_color_hex(C_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(lbl, message);
    lv_obj_center(lbl);

    s_toast_timer = lv_timer_create(toast_close_cb, 1000, NULL);
    if (s_toast_timer != NULL) {
        lv_timer_set_repeat_count(s_toast_timer, 1);
    } else {
        if (s_toast != NULL && lv_obj_is_valid(s_toast)) {
            lv_obj_del(s_toast);
        }
        s_toast = NULL;
    }
}

static void show_i18n_toast(ui_str_id_t id, const char *en_symbol)
{
    if (en_symbol != NULL && ui_i18n_get_lang() == UI_LANG_EN) {
        char buf[96];
        lv_snprintf(buf, sizeof(buf), "%s %s", en_symbol, ui_tr(id));
        show_toast(buf, LV_FONT_DEFAULT);
    } else {
        show_toast(ui_tr(id), BTM_TEXT_FONT);
    }
}

void page_bt_music_show_low_mem_hint(void)
{
    show_i18n_toast(STR_BT_MUSIC_LOW_MEM, LV_SYMBOL_WARNING);
}

int page_bt_music_enter(void)
{
    dismiss_toast();

    if (s_meter_timer != NULL) {
        lv_timer_delete(s_meter_timer);
        s_meter_timer = NULL;
    }
    if (s_kick_timer != NULL) {
        lv_timer_delete(s_kick_timer);
        s_kick_timer = NULL;
    }
    if (s_screen != NULL && lv_obj_is_valid(s_screen)) {
        ui_nav_unregister_screen(s_screen);
        lv_obj_del(s_screen);
    }
    s_screen = NULL;
    clear_refs();
    s_exit_pending = false;
    s_dance_user_enabled = true;
    s_render_ready = false;
    s_playing = false;
    s_streaming = false;
#if CONFIG_BT
    a2dp_sink_demo_set_playback_listener(on_playback_start, on_playback_stop);
    a2dp_sink_demo_set_stream_listener(on_a2dp_stream_start, on_a2dp_stream_stop);
#endif

    /* ---- screen: flat dark ---- */
    s_screen = lv_obj_create(NULL);
    lv_obj_set_scrollbar_mode(s_screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_screen, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_style_pad_all(s_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(s_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(C_BG_BOTTOM), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(s_screen, on_any_press, LV_EVENT_PRESSED, NULL);
    s_last_touch_ms = (uint32_t)rtos_get_time();

    /* Status + transport controls are custom-drawn into one object. This keeps
     * the same look without allocating one LVGL object per button/label. */
    s_chrome = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_chrome);
    lv_obj_set_size(s_chrome, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_pos(s_chrome, 0, 0);
    lv_obj_clear_flag(s_chrome, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_chrome, LV_OBJ_FLAG_CLICKABLE |
                              LV_OBJ_FLAG_EVENT_BUBBLE |
                              LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_chrome, chrome_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_chrome, on_any_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_chrome, chrome_click_cb, LV_EVENT_CLICKED, NULL);

    refresh_status();
    refresh_hand();
    set_play_symbol(false);

    /* One full-screen custom-drawn visualizer replaces 6 lv_bar + 6 cap
     * objects + the baseline object. The bars still look identical, but LVGL
     * only stores one object and redraws the rectangles directly. */
    s_visualizer = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_visualizer);
    lv_obj_set_pos(s_visualizer, 0, 0);
    lv_obj_set_size(s_visualizer, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_clear_flag(s_visualizer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_visualizer, visualizer_draw_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_move_background(s_visualizer);

    /* Transparent tap-to-exit catcher, shown only while immersive. */
    s_imm_catch = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_imm_catch);
    lv_obj_set_size(s_imm_catch, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_pos(s_imm_catch, 0, 0);
    lv_obj_add_flag(s_imm_catch, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_imm_catch, on_any_press, LV_EVENT_PRESSED, NULL);

    s_meter_timer = lv_timer_create(meter_timer_cb, 40, NULL);
    if (s_meter_timer != NULL) {
        lv_timer_pause(s_meter_timer);
        lv_timer_set_repeat_count(s_meter_timer, -1);
    }

    bk_page_attach_right_swipe_gesture(s_screen);
    lv_screen_load(s_screen);
    (void)ui_nav_register_screen(s_screen, &s_bt_music_nav_ops);

    if (s_meter_timer != NULL) {
        s_kick_timer = lv_timer_create(meter_kick_cb, 80, NULL);
        if (s_kick_timer != NULL) {
            lv_timer_set_repeat_count(s_kick_timer, 1);
        }
    }
    return 0;
}

#else  /* !ROBOT_TEST */

int page_bt_music_enter(void) { return 0; }
void page_bt_music_show_low_mem_hint(void) { }

#endif /* ROBOT_TEST */
