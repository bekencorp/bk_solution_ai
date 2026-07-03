/**
 * @file page_bt_music.c
 * @brief Bluetooth A2DP music page -- "Glass Spectrum" (design scheme 2).
 *
 * Layout (385 x 320):
 *   - top status bar: connection dot + state text (ASCII; remote CJK metadata is
 *     intentionally not shown, the sparse font subset can't render it);
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
 
 #include <components/log.h>
 #include <os/os.h>
 
 #include "lvgl.h"
 #include "beken_ui.h"
 #include "event_runtime.h"
 #include "page_hooks.h"
 #include "ui_nav_router.h"
 #include "ui_list_menu.h"
 #include "ui_theme.h"
 #include "demo/bt_music.h"
 
 #ifdef ROBOT_TEST
 
 #define TAG "page_bt_music"
 #define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
 
 #define BTM_TEXT_FONT  (&lv_font_ali_16)   /* ASCII labels/captions */
 
 /* ---- Neon palette (dark cyber theme) ---- */
 #define C_BG_BOTTOM    0x05060f
 #define C_PANEL        0x0c1a3d   /* glass panel fill (drawn semi-transparent) */
 #define C_PANEL_EDGE   0x5ce1ff   /* cyan panel edge */
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
 #define C_GRID         0x1d3764
 #define C_GLOW_CYAN    0x00dfff
 #define C_GLOW_PURPLE  0xb75cff
 #define C_ROBOT_ON     0xb75cff
 #define C_ROBOT_OFF    0x52658f
 
 /* ---- Layout ----
  * 6 bars = the 6 hand joints (NOT a generic spectrum): bars 0..4 are the five
  * fingers in low-freq -> high-freq order (bar0 = pinky/lowest, bar4 =
  * thumb/highest), bar5 = the wrist/base rotation. Each bar mirrors that servo's
  * live travel via bt_music_get_pose_levels(). */
 #define BTM_BARS       6
 #define BTM_BAR_W      34
 #define BTM_BAR_GAP    20
 #define BTM_BAR_TOP    50
 #define BTM_BAR_H      144         /* baseline at y = TOP + H = 194 */
 #define BTM_BAR_MIN    6
 #define BTM_CAP_H      2
 #define BTM_BASE_Y     (BTM_BAR_TOP + BTM_BAR_H)
#define BTM_BAR_VIS_MAX 84.0f       /* UI-only cap: hand can be 100, bar won't look full */
#define BTM_BAR_VIS_GAIN 0.96f      /* compression starts before the top */
#define BTM_BAR_TOP_COMPRESS 0.12f  /* stronger compression near 100 */
#define BTM_IMM_TIMEOUT_MS 3000U
#define BTM_IMM_BAR_W     38
#define BTM_IMM_BAR_GAP   17
#define BTM_IMM_BAR_TOP   30
#define BTM_IMM_BAR_H     236
#define BTM_IMM_BASE_Y    (BTM_IMM_BAR_TOP + BTM_IMM_BAR_H)
#define BTM_IMM_CAP_H     3
/* Page footprint is now small (no duplicate immersive layer, no per-frame
 * masks, shared button style), so the entry/low-render gates can be lower. */
 #define BTM_ENTRY_BLOCK_HEAP (24U * 1024U)
 #define BTM_LOW_RENDER_HEAP  (32U * 1024U)
 
 /* Per-bar neon hue ramp: 5 fingers (low->high freq) then the base bar (warm). */
 static const uint32_t s_bar_hue[BTM_BARS] = {
     0x00f0c0, 0x29b6ff, 0x6a5cff, 0xc45cff, 0xff4fb0, 0xffb14f,
 };
 
static lv_obj_t  *s_screen;
static lv_obj_t  *s_chrome;       /* transparent holder for status + transport */
static lv_obj_t  *s_imm_catch;    /* transparent full-screen tap-to-exit catcher */
static lv_obj_t  *s_bar[BTM_BARS];
static lv_obj_t  *s_cap[BTM_BARS];
static lv_obj_t  *s_play_label;
static lv_obj_t  *s_link_dot;
static lv_obj_t  *s_link_label;
static lv_obj_t  *s_robot_btn;
static lv_obj_t  *s_robot_label;
static lv_timer_t *s_meter_timer;
static lv_timer_t *s_kick_timer;
static lv_obj_t   *s_toast;       /* low-memory hint, lives on the top layer */
static lv_timer_t *s_toast_timer;

static lv_style_t s_btn_style;    /* shared style for the 5 secondary round btns */
static bool       s_btn_style_inited;

static float s_disp[BTM_BARS];
static float s_peak[BTM_BARS];
static bool  s_low_render_mode;
static bool  s_immersive;
static uint32_t s_last_touch_ms;
static uint32_t s_tick;

/* Cached UI states so the ~per-second refreshes only touch LVGL on change. */
static int s_ui_linked  = -1;
static int s_ui_dancing = -1;
static int s_ui_playing = -1;

static void set_immersive(bool enable);
void page_bt_music_show_low_mem_hint(void);
 
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

 /* Square + opaque on purpose: radius/alpha on a 30 FPS object forces LVGL to
  * build a mask and alpha-blend every frame. Sharp neon bars look the same. */
 static lv_obj_t *create_eq_bar(lv_obj_t *parent, int x, int idx)
 {
     uint32_t base = s_bar_hue[idx];
 
     lv_obj_t *bar = lv_bar_create(parent);
     lv_obj_set_pos(bar, x, BTM_BAR_TOP);
     lv_obj_set_size(bar, BTM_BAR_W, BTM_BAR_H);
     lv_bar_set_range(bar, 0, 100);
     lv_bar_set_value(bar, BTM_BAR_MIN, LV_ANIM_OFF);
     lv_obj_set_style_bg_color(bar, lv_color_hex(C_BAR_TRACK), LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_radius(bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_color(bar, lv_color_hex(base), LV_PART_INDICATOR | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
     lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
     return bar;
 }

 static lv_obj_t *create_cap(lv_obj_t *parent, int x, int idx)
 {
     lv_obj_t *cap = lv_obj_create(parent);
     lv_obj_remove_style_all(cap);
     lv_obj_set_size(cap, BTM_BAR_W, BTM_CAP_H);
     lv_obj_set_pos(cap, x, BTM_BASE_Y - BTM_BAR_MIN - BTM_CAP_H);
     lv_obj_set_style_bg_color(cap, lv_color_hex(lighten(s_bar_hue[idx], 55)),
                               LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_radius(cap, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
     return cap;
 }
 
 /* All 5 secondary transport buttons are identical, so they share one style
  * object instead of each carrying ~8 local style properties on the heap. */
 static void btn_style_init(void)
 {
     if (s_btn_style_inited) {
         return;
     }
     lv_style_init(&s_btn_style);
     lv_style_set_pad_all(&s_btn_style, 0);
     lv_style_set_shadow_width(&s_btn_style, 0);
     lv_style_set_bg_color(&s_btn_style, lv_color_hex(C_BTN_BG));
     lv_style_set_bg_opa(&s_btn_style, LV_OPA_COVER);
     lv_style_set_border_color(&s_btn_style, lv_color_hex(C_BTN_BORDER));
     lv_style_set_border_width(&s_btn_style, 1);
     lv_style_set_border_opa(&s_btn_style, LV_OPA_80);
     s_btn_style_inited = true;
 }
 
 static lv_obj_t *create_round_btn(lv_obj_t *parent, const char *symbol,
                                   int x, int y, int d, bool primary,
                                   lv_event_cb_t cb, lv_obj_t **out_label)
 {
     lv_obj_t *btn = lv_btn_create(parent);
     lv_obj_set_pos(btn, x, y);
     lv_obj_set_size(btn, d, d);
     lv_obj_set_style_radius(btn, d / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
 
     if (primary) {
         lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
         lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
         lv_obj_set_style_bg_color(btn, lv_color_hex(C_PLAY_1), LV_PART_MAIN | LV_STATE_DEFAULT);
         lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
         lv_obj_set_style_border_color(btn, lv_color_hex(C_RING), LV_PART_MAIN | LV_STATE_DEFAULT);
         lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
         lv_obj_set_style_border_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
     } else {
         lv_obj_add_style(btn, &s_btn_style, LV_PART_MAIN | LV_STATE_DEFAULT);
     }
 
     lv_obj_t *label = lv_label_create(btn);
     lv_label_set_text(label, symbol);
     lv_obj_center(label);
     lv_obj_set_style_text_color(label, lv_color_hex(primary ? 0x04122e : C_BTN_FG),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
 
     if (cb != NULL) {
         lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
     }
    lv_obj_add_event_cb(btn, on_any_press, LV_EVENT_PRESSED, NULL);
     if (out_label != NULL) {
         *out_label = label;
     }
     return btn;
 }
 
 static lv_obj_t *create_glow_rect(lv_obj_t *parent, int x, int y, int w, int h,
                                   uint32_t color, lv_opa_t opa, int radius)
 {
     lv_obj_t *obj = lv_obj_create(parent);
     lv_obj_remove_style_all(obj);
     lv_obj_set_pos(obj, x, y);
     lv_obj_set_size(obj, w, h);
     lv_obj_set_style_radius(obj, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN | LV_STATE_DEFAULT);
     return obj;
 }
 
 static lv_obj_t *create_robot_btn(lv_obj_t *parent, int x, int y, int w, int h,
                                   lv_event_cb_t cb)
 {
     lv_obj_t *btn = lv_btn_create(parent);
     lv_obj_set_pos(btn, x, y);
     lv_obj_set_size(btn, w, h);
     lv_obj_set_style_radius(btn, 13, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_color(btn, lv_color_hex(C_BTN_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_border_color(btn, lv_color_hex(C_ROBOT_ON), LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_border_opa(btn, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
 
     if (!s_low_render_mode) {
         /* Tiny robot head icon, built from solid rectangles/circles instead of
          * an image asset. Geometry is tuned for a low 34px pill. */
         create_glow_rect(btn, 9, 11, 18, 15, C_ROBOT_ON, LV_OPA_COVER, 4);
         create_glow_rect(btn, 17,  6,  2, 5, C_ROBOT_ON, LV_OPA_COVER, 1);
         create_glow_rect(btn, 14,  4,  8, 3, C_ROBOT_ON, LV_OPA_COVER, 2);
         create_glow_rect(btn, 13, 17,  3, 3, C_BG_BOTTOM, LV_OPA_COVER, 2);
         create_glow_rect(btn, 21, 17,  3, 3, C_BG_BOTTOM, LV_OPA_COVER, 2);
 
         lv_obj_t *line = lv_obj_create(btn);
         lv_obj_remove_style_all(line);
         lv_obj_set_pos(line, 36, 7);
         lv_obj_set_size(line, 1, h - 14);
         lv_obj_set_style_bg_color(line, lv_color_hex(C_ROBOT_ON), LV_PART_MAIN | LV_STATE_DEFAULT);
         lv_obj_set_style_bg_opa(line, LV_OPA_60, LV_PART_MAIN | LV_STATE_DEFAULT);
     }
 
     s_robot_label = lv_label_create(btn);
     lv_obj_set_width(s_robot_label, w);
     lv_label_set_long_mode(s_robot_label, LV_LABEL_LONG_DOT);
     lv_obj_set_pos(s_robot_label, 0, 8);
     lv_obj_set_style_text_font(s_robot_label, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_text_color(s_robot_label, lv_color_hex(C_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_text_align(s_robot_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
 
     if (cb != NULL) {
         lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
     }
    lv_obj_add_event_cb(btn, on_any_press, LV_EVENT_PRESSED, NULL);
     s_robot_btn = btn;
     return btn;
 }
 
 /* ---------------- dynamic refreshes ---------------- */
 
 static void refresh_status(void)
 {
     bool linked = bt_music_is_connected();
     if (s_ui_linked == (int)linked) {
         return;
     }
     s_ui_linked = (int)linked;
     if (s_link_dot != NULL && lv_obj_is_valid(s_link_dot)) {
         lv_obj_set_style_bg_color(s_link_dot,
                                   lv_color_hex(linked ? C_LINK_OK : C_LINK_OFF),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
     }
     if (s_link_label != NULL && lv_obj_is_valid(s_link_label)) {
         lv_label_set_text(s_link_label,
                           linked ? LV_SYMBOL_BLUETOOTH " Connected"
                                  : LV_SYMBOL_BLUETOOTH " Searching");
         lv_obj_set_style_text_color(s_link_label,
                                     lv_color_hex(linked ? C_TITLE : C_LINK_OFF),
                                     LV_PART_MAIN | LV_STATE_DEFAULT);
     }
 }
 
 static void refresh_hand(void)
 {
     bool dancing = bt_music_is_dancing();
     if (s_ui_dancing == (int)dancing) {
         return;
     }
     s_ui_dancing = (int)dancing;
     if (s_robot_btn != NULL && lv_obj_is_valid(s_robot_btn)) {
         lv_obj_set_style_border_color(s_robot_btn,
                                       lv_color_hex(dancing ? C_ROBOT_ON : C_ROBOT_OFF),
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
         lv_obj_set_style_border_opa(s_robot_btn, dancing ? LV_OPA_COVER : LV_OPA_60,
                                     LV_PART_MAIN | LV_STATE_DEFAULT);
     }
     if (s_robot_label != NULL && lv_obj_is_valid(s_robot_label)) {
         lv_label_set_text(s_robot_label, dancing ? "beken claw:on"
                                                  : "beken claw:off");
         lv_obj_set_style_text_color(s_robot_label,
                                     lv_color_hex(dancing ? C_TITLE : C_LINK_OFF),
                                     LV_PART_MAIN | LV_STATE_DEFAULT);
     }
 }
 
 static void set_play_symbol(bool playing)
 {
     if (s_ui_playing == (int)playing) {
         return;
     }
     s_ui_playing = (int)playing;
     if (s_play_label != NULL && lv_obj_is_valid(s_play_label)) {
         lv_label_set_text(s_play_label, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
     }
 }

/* Reposition/resize the SAME 6 bars + caps between the compact and the
 * full-screen "immersive" geometry, so immersive reuses the main visualizer
 * instead of building a second full set of objects. */
static void layout_bars(bool immersive)
{
    int bw   = immersive ? BTM_IMM_BAR_W   : BTM_BAR_W;
    int bh   = immersive ? BTM_IMM_BAR_H   : BTM_BAR_H;
    int btop = immersive ? BTM_IMM_BAR_TOP : BTM_BAR_TOP;
    int gap  = immersive ? BTM_IMM_BAR_GAP : BTM_BAR_GAP;
    int caph = immersive ? BTM_IMM_CAP_H   : BTM_CAP_H;
    int span = BTM_BARS * bw + (BTM_BARS - 1) * gap;
    int x0   = (LOGICAL_SCREEN_WIDTH - span) / 2;

    for (int i = 0; i < BTM_BARS; i++) {
        int x = x0 + i * (bw + gap);
        if (s_bar[i] != NULL && lv_obj_is_valid(s_bar[i])) {
            lv_obj_set_pos(s_bar[i], x, btop);
            lv_obj_set_size(s_bar[i], bw, bh);
        }
        if (s_cap[i] != NULL && lv_obj_is_valid(s_cap[i])) {
            lv_obj_set_size(s_cap[i], bw, caph);
            lv_obj_set_x(s_cap[i], x);   /* keep cap aligned with its bar */
        }
    }
}

static void set_immersive(bool enable)
{
    if (enable == s_immersive) {
        s_last_touch_ms = (uint32_t)rtos_get_time();
        return;
    }
    if (enable && (s_low_render_mode || s_screen == NULL || !lv_obj_is_valid(s_screen))) {
        return;
    }

    s_immersive = enable;
    layout_bars(enable);

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
 
 /* ---------------- button callbacks ---------------- */
 
 static void on_prev(lv_event_t *e)     { (void)e; bt_music_prev(); }
 static void on_next(lv_event_t *e)     { (void)e; bt_music_next(); }
 static void on_vol_down(lv_event_t *e) { (void)e; bt_music_vol_down(); }
 static void on_vol_up(lv_event_t *e)   { (void)e; bt_music_vol_up(); }
 
 static void on_play_pause(lv_event_t *e)
 {
     (void)e;
     bt_music_play_pause();
 }
 
 static void on_robot_toggle(lv_event_t *e)
 {
     (void)e;
     bt_music_dance_toggle();
     refresh_hand();
 }
 
 /* ---------------- meter ---------------- */
 
 static void meter_kick_cb(lv_timer_t *timer)
 {
     if (s_meter_timer != NULL) {
         lv_timer_resume(s_meter_timer);
     }
     bt_music_enable_speaker_after_first_paint();
     s_kick_timer = NULL;
     lv_timer_delete(timer);
 }
 
 /* ---------------- meter ---------------- */
 
 static void meter_timer_cb(lv_timer_t *timer)
 {
     (void)timer;
     /* Each bar follows one hand joint (5 fingers low->high + base). The source
      * is still the real pose level, but the display value is compressed so the
      * UI leaves headroom and doesn't look pinned near full. */
    uint8_t sp[BTM_BARS] = {0};
    bt_music_get_pose_levels(sp, BTM_BARS);
    uint32_t now = (uint32_t)rtos_get_time();
    bool playing = bt_music_is_playing();
 
     int base_y = s_immersive ? BTM_IMM_BASE_Y  : BTM_BASE_Y;
     int top_y  = s_immersive ? BTM_IMM_BAR_TOP : BTM_BAR_TOP;
     int bar_h  = s_immersive ? BTM_IMM_BAR_H   : BTM_BAR_H;
     int cap_h  = s_immersive ? BTM_IMM_CAP_H   : BTM_CAP_H;

     for (int i = 0; i < BTM_BARS; i++) {
         float target = pose_to_bar_value(sp[i]);
         float a = (target > s_disp[i]) ? 0.60f : 0.34f;   /* faster fall */
         s_disp[i] += (target - s_disp[i]) * a;
         if (s_bar[i] != NULL && lv_obj_is_valid(s_bar[i])) {
             lv_bar_set_value(s_bar[i], (int)(s_disp[i] + 0.5f), LV_ANIM_OFF);
         }
 
         if (s_disp[i] >= s_peak[i]) {
             s_peak[i] = s_disp[i];
         } else {
             s_peak[i] -= 2.4f;   /* peak cap drops faster too */
             if (s_peak[i] < s_disp[i]) {
                 s_peak[i] = s_disp[i];
             }
         }
         if (s_cap[i] != NULL && lv_obj_is_valid(s_cap[i])) {
             int ph = (int)(s_peak[i] / 100.0f * (float)bar_h + 0.5f);
             int cap_y = base_y - ph - cap_h;
             if (cap_y < top_y) {
                 cap_y = top_y;
             }
             lv_obj_set_y(s_cap[i], cap_y);
         }
     }

    if (!playing) {
        if (s_immersive) {
            set_immersive(false);
        }
        s_last_touch_ms = now;
    } else if (!s_immersive && !s_low_render_mode &&
        (uint32_t)(now - s_last_touch_ms) >= BTM_IMM_TIMEOUT_MS) {
        set_immersive(true);
    }
 
     if ((++s_tick % 12U) == 0U) {
        set_play_symbol(playing);
         refresh_hand();
         refresh_status();
     }
 }
 
 /* ---------------- navigation ---------------- */
 
 static void bt_music_page_close(void)
 {
    set_immersive(false);
     if (s_meter_timer != NULL) {
         lv_timer_delete(s_meter_timer);
         s_meter_timer = NULL;
     }
     if (s_kick_timer != NULL) {
         lv_timer_delete(s_kick_timer);
         s_kick_timer = NULL;
     }
     bt_music_stop();
 }
 
 static void clear_refs(void)
 {
     s_play_label = NULL;
     s_link_dot = NULL;
     s_link_label = NULL;
     s_robot_btn = NULL;
     s_robot_label = NULL;
    s_chrome = NULL;
    s_imm_catch = NULL;
    s_immersive = false;
     s_low_render_mode = false;
     s_tick = 0;
    s_last_touch_ms = 0;
    s_ui_linked = -1;
    s_ui_dancing = -1;
    s_ui_playing = -1;
     for (int i = 0; i < BTM_BARS; i++) {
         s_bar[i] = NULL;
         s_cap[i] = NULL;
         s_disp[i] = (float)BTM_BAR_MIN;
         s_peak[i] = (float)BTM_BAR_MIN;
     }
 }
 
 static void on_screen_prev(bk_lv_ui_t *ui)
 {
     if (ui == NULL) {
         return;
     }
     bt_music_page_close();
     (void)ui_demo_return_to_menu();
     if (s_screen != NULL && lv_obj_is_valid(s_screen)) {
         ui_nav_unregister_screen(s_screen);
         lv_obj_del(s_screen);
     }
     s_screen = NULL;
     clear_refs();
 }
 
 static void on_screen_next(bk_lv_ui_t *ui)
 {
     (void)ui;
     bt_music_play_pause();
 }
 
 static const ui_page_nav_ops_t s_bt_music_nav_ops = {
     .on_focus_prev = NULL,
     .on_focus_next = NULL,
     .on_screen_prev = on_screen_prev,
     .on_screen_next = on_screen_next,
 };
 
 /* ---------------- low-memory hint ---------------- */

 static void toast_close_cb(lv_timer_t *timer)
 {
     if (s_toast != NULL && lv_obj_is_valid(s_toast)) {
         lv_obj_del(s_toast);
     }
     s_toast = NULL;
     s_toast_timer = NULL;
     lv_timer_delete(timer);
 }

 /* Shown (on the top layer, so it floats over the menu) when the page is
  * refused for low memory, so the user knows it's busy, not broken. */
 void page_bt_music_show_low_mem_hint(void)
 {
     if (s_toast != NULL && lv_obj_is_valid(s_toast)) {
         return;   /* one at a time */
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
     lv_obj_set_style_text_font(lbl, LV_FONT_DEFAULT, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_text_color(lbl, lv_color_hex(C_TITLE), LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_label_set_text(lbl, LV_SYMBOL_WARNING " Memory busy now.\n"
                            "Please exit other demos, then open Bluetooth Music again.");
     lv_obj_center(lbl);

     s_toast_timer = lv_timer_create(toast_close_cb, 3500, NULL);
     if (s_toast_timer != NULL) {
         lv_timer_set_repeat_count(s_toast_timer, 1);
    } else {
        if (s_toast != NULL && lv_obj_is_valid(s_toast)) {
            lv_obj_del(s_toast);
        }
        s_toast = NULL;
     }
 }

 int page_bt_music_enter(void)
 {
     bk_lv_ui_t *ui = &bk_lv_tool_ui;
     (void)ui;
 
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
 
     uint32_t entry_heap = rtos_get_free_heap_size();
     if (entry_heap < BTM_ENTRY_BLOCK_HEAP) {
         LOGI("bt_music page blocked, iram free=%u min=%u need=%u\r\n",
              (unsigned)entry_heap,
              (unsigned)rtos_get_minimum_free_heap_size(),
              (unsigned)BTM_ENTRY_BLOCK_HEAP);
         page_bt_music_show_low_mem_hint();
         return -1;
     }
     s_low_render_mode = (entry_heap < BTM_LOW_RENDER_HEAP);
     LOGI("bt_music page render=%s iram free=%u min=%u psram=%u\r\n",
          s_low_render_mode ? "low" : "full",
          (unsigned)entry_heap,
          (unsigned)rtos_get_minimum_free_heap_size(),
          (unsigned)rtos_get_psram_free_heap_size());
 
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
    btn_style_init();

    /* All chrome (status + transport) lives in one transparent holder so
     * immersive mode just hides it and grows the bars -- no second object set. */
    s_chrome = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_chrome);
    lv_obj_set_size(s_chrome, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_pos(s_chrome, 0, 0);
    lv_obj_clear_flag(s_chrome, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

     int span = BTM_BARS * BTM_BAR_W + (BTM_BARS - 1) * BTM_BAR_GAP;
     int x0 = (LOGICAL_SCREEN_WIDTH - span) / 2;

     /* ---- top status bar (kept inside the real safe band) ---- */
     s_link_dot = lv_obj_create(s_chrome);
     lv_obj_remove_style_all(s_link_dot);
     lv_obj_set_size(s_link_dot, 9, 9);
     lv_obj_set_pos(s_link_dot, 28, 24);
     lv_obj_set_style_radius(s_link_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_opa(s_link_dot, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
 
     s_link_label = lv_label_create(s_chrome);
     lv_obj_set_style_text_font(s_link_label, BTM_TEXT_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_pos(s_link_label, 46, 18);
     lv_obj_set_width(s_link_label, LOGICAL_SCREEN_WIDTH - 72);
     lv_label_set_long_mode(s_link_label, LV_LABEL_LONG_DOT);

     lv_obj_t *divider = lv_obj_create(s_chrome);
     lv_obj_remove_style_all(divider);
     lv_obj_set_size(divider, LOGICAL_SCREEN_WIDTH - 32, 1);
     lv_obj_set_pos(divider, 16, 46);
     lv_obj_set_style_bg_color(divider, lv_color_hex(C_DIVIDER), LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
 
     refresh_status();
 
     /* ---- spectrum baseline glow (no surrounding panel: bars sit bare on the
      * dark background, matching the reference recording) ---- */
     lv_obj_t *base = lv_obj_create(s_chrome);
     lv_obj_remove_style_all(base);
     lv_obj_set_size(base, LOGICAL_SCREEN_WIDTH - 64, 2);
     lv_obj_set_pos(base, 32, BTM_BASE_Y + 3);
     lv_obj_set_style_bg_color(base, lv_color_hex(C_BASE_GLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_bg_opa(base, LV_OPA_40, LV_PART_MAIN | LV_STATE_DEFAULT);
     lv_obj_set_style_radius(base, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
 
     /* ---- joint bars (5 fingers low->high + base), bare on the background ----
      * Peak caps are cheap (one small object each) and are the main "wow" detail,
      * so we keep them even in low-render mode. */
     for (int i = 0; i < BTM_BARS; i++) {
         int x = x0 + i * (BTM_BAR_W + BTM_BAR_GAP);
         s_bar[i] = create_eq_bar(s_screen, x, i);
         s_cap[i] = create_cap(s_screen, x, i);
     }

     /* ---- music transport row: VOL- PREV PLAY NEXT VOL+ (compact, centered) ---- */
     {
         const int dn = 42, dp = 52, gap = 12;
         int row_w = dn * 4 + dp + gap * 4;
         int rx = (LOGICAL_SCREEN_WIDTH - row_w) / 2;
         int yn = 216, yp = 211;
         create_round_btn(s_chrome, LV_SYMBOL_VOLUME_MID, rx,                             yn, dn, false, on_vol_down, NULL);
         create_round_btn(s_chrome, LV_SYMBOL_PREV,       rx + (dn + gap),                yn, dn, false, on_prev, NULL);
         create_round_btn(s_chrome, LV_SYMBOL_PLAY,       rx + 2 * (dn + gap),            yp, dp, true,  on_play_pause, &s_play_label);
         create_round_btn(s_chrome, LV_SYMBOL_NEXT,       rx + 2 * (dn + gap) + dp + gap, yn, dn, false, on_next, NULL);
         create_round_btn(s_chrome, LV_SYMBOL_VOLUME_MAX, rx + 3 * (dn + gap) + dp + gap, yn, dn, false, on_vol_up, NULL);
     }
     create_robot_btn(s_chrome, 54, 274, 276, 34, on_robot_toggle);
     refresh_hand();

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
     LOGI("bt_music page entered\r\n");
     return 0;
 }
 
 #else  /* !ROBOT_TEST */
 
int page_bt_music_enter(void) { return 0; }
void page_bt_music_show_low_mem_hint(void) { }

#endif /* ROBOT_TEST */
 