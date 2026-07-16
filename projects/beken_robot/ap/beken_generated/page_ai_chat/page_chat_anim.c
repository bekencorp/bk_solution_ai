/**
 * @file page_chat_anim.c
 * @brief LVGL animation widget for the AI chat (page_6) and AI chat + vision
 *        recognition (page_7) demo pages.
 *
 * The widget renders a tech-abstract scene anchored at (CORE_CX, CORE_CY):
 *
 *   - A glowing core dot whose colour reflects the current state.
 *   - Three concentric breathing ripples that pulse out from the core.
 *   - A row of five EQ bars at the bottom. Bar height is driven either
 *     by a fixed sine animation (CONNECTING / IDLE / THINKING) or by the
 *     real-time mic / spk PCM envelope coming from audio_engine
 *     (LISTENING / SPEAKING).
 *   - In vision mode only: 4 viewfinder corners + a sweeping scan line
 *     across the framed region, plus a blinking REC label.
 *
 * Lifecycle:
 *   page_chat_anim_attach()    builds the LVGL tree under the caller's
 *                              page object and arms the animations / event
 *                              handler. Initial state is CONNECTING.
 *   page_chat_anim_set_state() pushes a new state from any thread.
 *   page_chat_anim_detach()    unwinds everything.
 *
 * State transitions are normally driven by the app_event bus: the
 * audio_engine module publishes APP_EVT_AI_{LISTENING,THINKING,SPEAKING,
 * IDLE} based on local VAD / speaker meters and RTC hints; the page just
 * mirrors that into apply_state_locked().
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lvgl.h"
#include "lv_vendor.h"
#include "beken_ui.h"

#include <components/log.h>

#include "page_chat_anim.h"

#if CONFIG_APP_EVT
#include "app_event.h"
#endif

#if defined(__has_include)
#  if __has_include("network_engine.h")
#    include "network_engine.h"
#    define HAVE_NTWK_TRANS 1
#  endif
#endif

#include "audio_engine.h"

#define TAG "page_chat_anim"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

/* ============================================================================
 *  Layout constants (logical screen, post 90-degree rotation)
 * ==========================================================================*/

#define APP_LOGICAL_W     LOGICAL_SCREEN_WIDTH
#define APP_LOGICAL_H     LOGICAL_SCREEN_HEIGHT

#define CORE_CX           (APP_LOGICAL_W / 2)
#define CORE_CY           PAGE_VISION_CORE_CY
#define CORE_DIAM         50

/* Status line sits above the viewfinder. The vision preview now fills the whole
 * frame (top edge at FRAME_TOP = CORE_CY - FRAME_H/2 = 61), so keep the status
 * text clear of it: STATUS_Y + STATUS_FONT_H must stay below FRAME_TOP. */
#define STATUS_Y          34
#define STATUS_FONT_H     22

/* EQ bars: bottom-anchored, height grows upward. */
#define EQ_BAR_COUNT      5
#define EQ_BAR_W          22
#define EQ_BAR_GAP        14
#define EQ_BAR_BASE_H     14
/* Peak height is bounded by the gap between EQ_BOTTOM_Y (=332) and the
 * lower edge of the core (=193) plus a small visual margin. 130px keeps
 * roughly 10px clearance to the core while filling ~36% of the screen
 * vertically so the bars feel substantial rather than decorative. */
#define EQ_BAR_PEAK_H     115
#define EQ_BOTTOM_MARGIN  25
#define EQ_BOTTOM_Y       (APP_LOGICAL_H - EQ_BOTTOM_MARGIN)
#define EQ_TIMER_PERIOD_MS 33   /* ~30Hz refresh */
#define EQ_LERP_NUM        3    /* cur_h += (target - cur_h) * 3/10 */
#define EQ_LERP_DEN        10

/* Viewfinder frame (vision mode only) - geometry shared via page_chat_anim.h
 * so page_vision_preview.c can align the live camera image to these brackets. */
#define FRAME_W           PAGE_VISION_FRAME_W
#define FRAME_H           PAGE_VISION_FRAME_H
#define FRAME_LEFT        PAGE_VISION_FRAME_LEFT
#define FRAME_TOP         PAGE_VISION_FRAME_TOP
#define FRAME_BOTTOM      (FRAME_TOP + FRAME_H)
#define CORNER_LEN        24
#define CORNER_THICK      3

/* Ripple */
#define RIPPLE_MAX_DIAM   180

/* Orbit dot */
#define ORBIT_RADIUS      44
#define ORBIT_DOT_DIAM    10

/* Colours -- one base palette per state */
#define COLOR_BG_HEX          0x000000u
#define COLOR_CORE_CONN_HEX   0x00aaffu
#define COLOR_CORE_IDLE_HEX   0x00aaffu
#define COLOR_CORE_LISTEN_HEX 0x2ecc71u
#define COLOR_CORE_THINK_HEX  0x00aaffu
#define COLOR_CORE_SPEAK_HEX  0xffaa00u
#define COLOR_CORE_ERROR_HEX  0xff5050u
#define COLOR_EQ_HEX          0x00aaffu
#define COLOR_FRAME_HEX       0x00aaffu
#define COLOR_REC_HEX         0xff3030u
#define COLOR_TEXT_HEX        0xffffffu

#define COLOR_CORE_CONN     lv_color_hex(COLOR_CORE_CONN_HEX)
#define COLOR_CORE_IDLE     lv_color_hex(COLOR_CORE_IDLE_HEX)
#define COLOR_CORE_LISTEN   lv_color_hex(COLOR_CORE_LISTEN_HEX)
#define COLOR_CORE_THINK    lv_color_hex(COLOR_CORE_THINK_HEX)
#define COLOR_CORE_SPEAK    lv_color_hex(COLOR_CORE_SPEAK_HEX)
#define COLOR_CORE_ERROR    lv_color_hex(COLOR_CORE_ERROR_HEX)
#define COLOR_EQ            lv_color_hex(COLOR_EQ_HEX)
#define COLOR_FRAME         lv_color_hex(COLOR_FRAME_HEX)
#define COLOR_REC           lv_color_hex(COLOR_REC_HEX)
#define COLOR_TEXT          lv_color_hex(COLOR_TEXT_HEX)

extern const lv_font_t lv_font_ali_16;

/* ============================================================================
 *  Static state
 * ==========================================================================*/

static page_chat_anim_state_t s_state = PAGE_CHAT_STATE_CONNECTING;
static page_chat_anim_mode_t  s_mode  = PAGE_CHAT_ANIM_MODE_VOICE;
static volatile uint8_t       s_attached;

static lv_obj_t *s_root;
static lv_obj_t *s_status_label;

static lv_obj_t *s_core_anchor;          /* zero-size at (CORE_CX, CORE_CY) */
static lv_obj_t *s_core;
static lv_obj_t *s_ripple[3];
static lv_obj_t *s_orbit_dot;
static lv_timer_t *s_orbit_timer;

static lv_obj_t *s_eq[EQ_BAR_COUNT];
/* Cached top-left x for each EQ bar so the eq_height_cb / energy timer can
 * re-position the bar (top moves up while bottom stays at EQ_BOTTOM_Y)
 * without relying on align which would force a relayout. */
static int       s_eq_x[EQ_BAR_COUNT];
static int32_t   s_eq_cur_h[EQ_BAR_COUNT];
static int32_t   s_eq_t_deg;
static lv_timer_t *s_eq_energy_timer;

/* Vision mode overlay. Corner brackets + scan line are owned by
 * page_vision_preview.c (drawn over the live image); only REC lives here. */
static lv_obj_t *s_rec_label;

#if CONFIG_APP_EVT
static volatile uint8_t s_evt_registered;
#endif

/* ============================================================================
 *  Small helpers
 * ==========================================================================*/

static lv_obj_t *make_solid(lv_obj_t *parent, int w, int h,
                            lv_color_t color, int radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    return o;
}

static void set_status_text_locked(const char *txt)
{
    if (s_status_label == NULL || !lv_obj_is_valid(s_status_label)) {
        return;
    }
    lv_label_set_text(s_status_label, txt ? txt : "");
}

static void set_core_color_locked(lv_color_t color)
{
    if (s_core != NULL && lv_obj_is_valid(s_core)) {
        lv_obj_set_style_bg_color(s_core, color, LV_PART_MAIN);
    }
}

static void set_eq_color_locked(lv_color_t color)
{
    for (int i = 0; i < EQ_BAR_COUNT; i++) {
        if (s_eq[i] != NULL && lv_obj_is_valid(s_eq[i])) {
            lv_obj_set_style_bg_color(s_eq[i], color, LV_PART_MAIN);
        }
    }
}

/* ============================================================================
 *  Orbit dot (used in THINKING)
 * ==========================================================================*/

static void orbit_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (s_orbit_dot == NULL || !lv_obj_is_valid(s_orbit_dot)) {
        return;
    }
    static int32_t deg;
    deg = (deg + 6) % 360;
    int32_t s = lv_trigo_sin(deg);
    int32_t c = lv_trigo_cos(deg);
    int32_t dx = (ORBIT_RADIUS * c) / LV_TRIGO_SIN_MAX - ORBIT_DOT_DIAM / 2;
    int32_t dy = (ORBIT_RADIUS * s) / LV_TRIGO_SIN_MAX - ORBIT_DOT_DIAM / 2;
    lv_obj_set_pos(s_orbit_dot, dx, dy);
}

static void start_orbit_anim(void)
{
    if (s_orbit_dot == NULL) {
        return;
    }
    lv_obj_remove_flag(s_orbit_dot, LV_OBJ_FLAG_HIDDEN);
    if (s_orbit_timer == NULL) {
        s_orbit_timer = lv_timer_create(orbit_timer_cb, 30, NULL);
    }
}

static void stop_orbit_anim(void)
{
    if (s_orbit_timer != NULL) {
        lv_timer_delete(s_orbit_timer);
        s_orbit_timer = NULL;
    }
    if (s_orbit_dot != NULL && lv_obj_is_valid(s_orbit_dot)) {
        lv_obj_add_flag(s_orbit_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ============================================================================
 *  Ripple breathing (used in CONNECTING / LISTENING / SPEAKING)
 * ==========================================================================*/

static void ripple_progress_cb(void *var, int32_t v)
{
    lv_obj_t *o = (lv_obj_t *)var;
    if (o == NULL || !lv_obj_is_valid(o)) {
        return;
    }
    /* v: 0..1000 maps to diameter 0..RIPPLE_MAX_DIAM and opacity 200..0 */
    int32_t diam = (v * RIPPLE_MAX_DIAM) / 1000;
    if (diam < 2) diam = 2;
    lv_obj_set_size(o, diam, diam);
    lv_obj_set_style_radius(o, diam / 2, LV_PART_MAIN);
    lv_obj_set_pos(o, -diam / 2, -diam / 2);

    int32_t opa = 200 - (v * 200) / 1000;
    if (opa < 0) opa = 0;
    lv_obj_set_style_bg_opa(o, (lv_opa_t)opa, LV_PART_MAIN);
}

static void start_ripple_anim(uint32_t period_ms)
{
    for (int i = 0; i < 3; i++) {
        if (s_ripple[i] == NULL || !lv_obj_is_valid(s_ripple[i])) {
            continue;
        }
        lv_obj_remove_flag(s_ripple[i], LV_OBJ_FLAG_HIDDEN);

        lv_anim_delete(s_ripple[i], ripple_progress_cb);

        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_ripple[i]);
        lv_anim_set_exec_cb(&a, ripple_progress_cb);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_duration(&a, period_ms);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_delay(&a, (period_ms / 3) * i);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    }
}

static void stop_ripple_anim(void)
{
    for (int i = 0; i < 3; i++) {
        if (s_ripple[i] == NULL || !lv_obj_is_valid(s_ripple[i])) {
            continue;
        }
        lv_anim_delete(s_ripple[i], ripple_progress_cb);
        lv_obj_add_flag(s_ripple[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ============================================================================
 *  EQ bars (fixed-amplitude path)
 * ==========================================================================*/

static void eq_height_cb(void *var, int32_t v)
{
    lv_obj_t *o = (lv_obj_t *)var;
    if (o == NULL || !lv_obj_is_valid(o)) {
        return;
    }
    int idx = (int)(intptr_t)lv_obj_get_user_data(o);
    if (idx < 0 || idx >= EQ_BAR_COUNT) {
        return;
    }
    /* Bottom anchored at EQ_BOTTOM_Y; height grows upward. */
    lv_obj_set_size(o, EQ_BAR_W, v);
    lv_obj_set_pos(o, s_eq_x[idx], EQ_BOTTOM_Y - v);
}

static void start_eq_anim(int32_t hi, uint32_t period_base_ms)
{
    static const uint32_t period_jitter[EQ_BAR_COUNT] = {0, 80, 160, 60, 200};

    lv_anim_delete(NULL, eq_height_cb);
    if (s_eq[0] == NULL) {
        return;
    }
    for (int i = 0; i < EQ_BAR_COUNT; i++) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_eq[i]);
        lv_anim_set_exec_cb(&a, eq_height_cb);
        lv_anim_set_values(&a, EQ_BAR_BASE_H, hi);
        lv_anim_set_duration(&a, period_base_ms + period_jitter[i]);
        lv_anim_set_reverse_duration(&a, period_base_ms + period_jitter[i]);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_delay(&a, 60 * i);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
    }
}

/* ============================================================================
 *  EQ bars (energy-driven path)
 * ==========================================================================*/

/* Integer sqrt for the EQ non-linear amplifier (Newton, monotone). */
static uint32_t eq_isqrt_u32(uint32_t n)
{
    if (n < 2u) {
        return n;
    }
    uint32_t x = n;
    uint32_t y = (x + 1u) >> 1;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1;
    }
    return x;
}

/* Non-linear amplifier for the SDK-reported PCM envelope (0..100).
 *
 * Field measurement: the SDK aec_level / onboard_speaker energy_level (post
 * our EWMA in audio_engine) hovers in 10..20 even during clear speech,
 * with brief peaks at ~25-30 and isolated spikes to ~40. A direct linear
 * mapping leaves the bars sitting near the base line. The curve
 * y = sqrt(lvl*300) is aggressive enough that normal-volume speech pushes
 * effective level into 55..85 (bars at 60..95% of EQ_BAR_PEAK_H), while
 * still preserving headroom for the rare loudest peaks:
 *     lvl=5  -> 38    lvl=15 -> 67    lvl=25 -> 86
 *     lvl=10 -> 54    lvl=20 -> 77    lvl>=33-> 100
 */
static uint8_t eq_scale_level(uint8_t lvl)
{
    if (lvl == 0u) {
        return 0u;
    }
    uint32_t y = eq_isqrt_u32((uint32_t)lvl * 300u);
    if (y > 100u) {
        y = 100u;
    }
    return (uint8_t)y;
}

/* Active-state amplitude floor: SDK levels can drop to 1..3 between phonemes
 * yet the speech is clearly ongoing. Without a floor the bars visibly
 * collapse mid-utterance. 65 keeps a confident breathing minimum (bars
 * sweep 44..89px ~12-25% of screen height) while still letting louder
 * energy push the bars near EQ_BAR_PEAK_H. */
#define EQ_ACTIVE_LVL_FLOOR   65u

/* Energy-driven EQ timer callback. Runs at ~30Hz while in LISTENING /
 * SPEAKING. Per-bar height = base + span * energy * sine_modulation, then
 * exponentially smoothed toward target to avoid jitter. */
static void eq_energy_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_attached || s_eq[0] == NULL) {
        return;
    }

    uint8_t lvl_raw;
    if (s_state == PAGE_CHAT_STATE_LISTENING) {
        lvl_raw = audio_engine_get_mic_level();
    } else if (s_state == PAGE_CHAT_STATE_SPEAKING) {
        lvl_raw = audio_engine_get_spk_level();
    } else {
        /* Timer should have been stopped on state change; defensive bail. */
        return;
    }

    uint32_t lvl = eq_scale_level(lvl_raw);
    if (lvl < EQ_ACTIVE_LVL_FLOOR) {
        lvl = EQ_ACTIVE_LVL_FLOOR;
    }

    /* Throttled telemetry: ~1Hz peak-hold so we can verify the amplifier
     * curve against real speech without flooding the console. */
    static uint8_t  s_dbg_peak_raw;
    static uint8_t  s_dbg_peak_eff;
    static uint32_t s_dbg_tick;
    if (lvl_raw > s_dbg_peak_raw) s_dbg_peak_raw = lvl_raw;
    if (lvl     > s_dbg_peak_eff) s_dbg_peak_eff = (uint8_t)lvl;
    if (++s_dbg_tick >= 30u) {
        LOGI("EQ src=%s peak raw=%u -> eff=%u\n",
                (s_state == PAGE_CHAT_STATE_LISTENING) ? "MIC" : "SPK",
                (unsigned)s_dbg_peak_raw, (unsigned)s_dbg_peak_eff);
        s_dbg_peak_raw = 0;
        s_dbg_peak_eff = 0;
        s_dbg_tick = 0;
    }

    int32_t span = ((int32_t)lvl * (EQ_BAR_PEAK_H - EQ_BAR_BASE_H)) / 100;

    /* Slow global phase, ~ 540 deg/s at 30Hz (18 deg/tick). */
    s_eq_t_deg = (s_eq_t_deg + 18) % 360;
    static const int32_t phase_off[EQ_BAR_COUNT] = { 0, 50, 100, 30, 70 };

    for (int i = 0; i < EQ_BAR_COUNT; i++) {
        if (s_eq[i] == NULL) {
            continue;
        }
        int32_t deg = (s_eq_t_deg + phase_off[i]) % 360;
        int32_t s   = lv_trigo_sin(deg);   /* -LV_TRIGO_SIN_MAX..+MAX */
        /* Per-bar sine modulation. Range 40..100% keeps even the trough
         * of the wave above ~40% of the current span, so the bars feel
         * solid rather than visibly collapsing every half-cycle. */
        int32_t mod = 70 + (s * 30) / LV_TRIGO_SIN_MAX;
        int32_t target = EQ_BAR_BASE_H + (span * mod) / 100;
        if (target < EQ_BAR_BASE_H) {
            target = EQ_BAR_BASE_H;
        }
        if (target > EQ_BAR_PEAK_H) {
            target = EQ_BAR_PEAK_H;
        }
        int32_t cur = s_eq_cur_h[i];
        cur += ((target - cur) * EQ_LERP_NUM) / EQ_LERP_DEN;
        s_eq_cur_h[i] = cur;
        lv_obj_set_size(s_eq[i], EQ_BAR_W, cur);
        lv_obj_set_pos(s_eq[i], s_eq_x[i], EQ_BOTTOM_Y - cur);
    }
}

static void start_eq_energy(void)
{
    /* Energy mode takes over from the fixed lv_anim oscillation. */
    lv_anim_delete(NULL, eq_height_cb);
    for (int i = 0; i < EQ_BAR_COUNT; i++) {
        s_eq_cur_h[i] = EQ_BAR_BASE_H;
    }
    if (s_eq_energy_timer == NULL) {
        s_eq_energy_timer = lv_timer_create(eq_energy_timer_cb,
                                            EQ_TIMER_PERIOD_MS, NULL);
    }
}

static void stop_eq_energy(void)
{
    if (s_eq_energy_timer != NULL) {
        lv_timer_delete(s_eq_energy_timer);
        s_eq_energy_timer = NULL;
    }
}

/* ============================================================================
 *  Vision overlay (page_7 only)
 * ==========================================================================*/

static void rec_opa_cb(void *var, int32_t v)
{
    lv_obj_t *o = (lv_obj_t *)var;
    if (o == NULL || !lv_obj_is_valid(o)) {
        return;
    }
    lv_obj_set_style_text_color(o, COLOR_REC, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, (lv_opa_t)v, LV_PART_MAIN);
}

static void build_vision_overlay(lv_obj_t *root)
{
    /* NOTE: the corner brackets and the vertical scan line are NOT drawn here
     * anymore. They are now drawn by page_vision_preview.c on top of the live
     * camera image (a foreground panel), so they stay visible over the picture
     * instead of being hidden behind it. This overlay only keeps the blinking
     * REC label, which sits above the frame and never overlaps the image. */

    /* Blinking REC label below the frame top-right corner. */
    s_rec_label = lv_label_create(root);
    lv_label_set_text(s_rec_label, "REC");
    lv_obj_set_style_text_font(s_rec_label, &lv_font_ali_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_rec_label, COLOR_REC, LV_PART_MAIN);
    lv_obj_set_pos(s_rec_label, FRAME_LEFT + FRAME_W - 36, FRAME_TOP - 22);

    lv_anim_t ra;
    lv_anim_init(&ra);
    lv_anim_set_var(&ra, s_rec_label);
    lv_anim_set_exec_cb(&ra, rec_opa_cb);
    lv_anim_set_values(&ra, 80, 255);
    lv_anim_set_duration(&ra, 700);
    lv_anim_set_reverse_duration(&ra, 700);
    lv_anim_set_repeat_count(&ra, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&ra, lv_anim_path_ease_in_out);
    lv_anim_start(&ra);
}

/* ============================================================================
 *  Object construction
 * ==========================================================================*/

static void build_voice_layer(lv_obj_t *parent)
{
    /* Status text at the top, centered. */
    s_status_label = lv_label_create(parent);
    lv_label_set_text(s_status_label, "Connecting...");
    lv_obj_set_style_text_font(s_status_label, &lv_font_ali_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_status_label, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_obj_set_size(s_status_label, APP_LOGICAL_W, STATUS_FONT_H);
    lv_obj_set_pos(s_status_label, 0, STATUS_Y);

    /* Core anchor at (CORE_CX, CORE_CY); ripples + orbit + core are all
     * children so they move together. */
    s_core_anchor = lv_obj_create(parent);
    lv_obj_remove_style_all(s_core_anchor);
    lv_obj_set_size(s_core_anchor, 0, 0);
    lv_obj_set_pos(s_core_anchor, CORE_CX, CORE_CY);

    /* Ripples (start hidden until a state arms them). */
    for (int i = 0; i < 3; i++) {
        s_ripple[i] = make_solid(s_core_anchor, 2, 2, COLOR_CORE_CONN,
                                 1);
        lv_obj_set_pos(s_ripple[i], 0, 0);
        lv_obj_add_flag(s_ripple[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Glowing core. */
    s_core = make_solid(s_core_anchor, CORE_DIAM, CORE_DIAM,
                        COLOR_CORE_CONN, CORE_DIAM / 2);
    lv_obj_set_pos(s_core, -CORE_DIAM / 2, -CORE_DIAM / 2);

    /* Orbit dot (used in THINKING). */
    s_orbit_dot = make_solid(s_core_anchor, ORBIT_DOT_DIAM, ORBIT_DOT_DIAM,
                             COLOR_CORE_CONN, ORBIT_DOT_DIAM / 2);
    lv_obj_set_pos(s_orbit_dot, ORBIT_RADIUS - ORBIT_DOT_DIAM / 2,
                   -ORBIT_DOT_DIAM / 2);
    lv_obj_add_flag(s_orbit_dot, LV_OBJ_FLAG_HIDDEN);

    /* EQ bars row, centered horizontally, bottom anchored. The eq_height_cb
     * can move both size and y-position together to keep the bottom at
     * EQ_BOTTOM_Y. */
    int total_w = EQ_BAR_COUNT * EQ_BAR_W + (EQ_BAR_COUNT - 1) * EQ_BAR_GAP;
    int origin_x = (APP_LOGICAL_W - total_w) / 2; /* left edge of the EQ row */
    for (int i = 0; i < EQ_BAR_COUNT; i++) {
        s_eq_x[i] = origin_x + i * (EQ_BAR_W + EQ_BAR_GAP);
        s_eq[i]   = make_solid(parent, EQ_BAR_W, EQ_BAR_BASE_H, COLOR_EQ, 3);
        lv_obj_set_user_data(s_eq[i], (void *)(intptr_t)i);
        lv_obj_set_pos(s_eq[i], s_eq_x[i], EQ_BOTTOM_Y - EQ_BAR_BASE_H);
    }
}

/* ============================================================================
 *  State machine (UI side)
 * ==========================================================================*/

/* Only log state= / eq_color= on transitions; otherwise the line floods the
 * console at the rate the producer publishes events (could be every audio
 * frame if upstream loses its idempotency guard). */
static void apply_state_locked(page_chat_anim_state_t state)
{
    bool transition = (s_state != state);
    s_state = state;

    lv_color_t eq_color = COLOR_EQ;
    const char *name = NULL;

    switch (state) {
    case PAGE_CHAT_STATE_CONNECTING:
        start_ripple_anim(1400);
        stop_eq_energy();
        start_eq_anim(40, 360);
        stop_orbit_anim();
        set_status_text_locked("Connecting...");
        set_core_color_locked(COLOR_CORE_CONN);
        eq_color = COLOR_EQ;
        name = "CONNECTING";
        break;

    case PAGE_CHAT_STATE_IDLE:
        start_ripple_anim(1200);
        stop_eq_energy();
        start_eq_anim(45, 380);
        stop_orbit_anim();
        set_status_text_locked("Idle");
        set_core_color_locked(COLOR_CORE_IDLE);
        eq_color = COLOR_EQ;
        name = "IDLE";
        break;

    case PAGE_CHAT_STATE_LISTENING:
        start_ripple_anim(550);
        /* Energy-driven EQ: amplitude follows live mic_level. */
        start_eq_energy();
        stop_orbit_anim();
        set_status_text_locked("Listening...");
        set_core_color_locked(COLOR_CORE_LISTEN);
        eq_color = COLOR_CORE_LISTEN;
        name = "LISTENING";
        break;

    case PAGE_CHAT_STATE_THINKING:
        stop_ripple_anim();
        stop_eq_energy();
        /* No PCM energy in this transitional state; use a roomy fixed-amp
         * oscillation (peak 55px) so the EQ visibly "ponders" instead of
         * sitting flat. */
        start_eq_anim(55, 480);
        start_orbit_anim();
        set_status_text_locked("Thinking...");
        set_core_color_locked(COLOR_CORE_THINK);
        eq_color = COLOR_EQ;
        name = "THINKING";
        break;

    case PAGE_CHAT_STATE_SPEAKING:
        start_ripple_anim(800);
        /* Energy-driven EQ: amplitude follows live spk_level. */
        start_eq_energy();
        stop_orbit_anim();
        set_status_text_locked("Speaking...");
        set_core_color_locked(COLOR_CORE_SPEAK);
        eq_color = COLOR_CORE_SPEAK;
        name = "SPEAKING";
        break;

    case PAGE_CHAT_STATE_ERROR:
    default:
        stop_ripple_anim();
        stop_eq_energy();
        lv_anim_delete(NULL, eq_height_cb);
        stop_orbit_anim();
        set_status_text_locked("Disconnected");
        set_core_color_locked(COLOR_CORE_ERROR);
        eq_color = COLOR_EQ;
        name = "ERROR";
        break;
    }

    set_eq_color_locked(eq_color);
    if (transition) {
        LOGI("state=%s eq_color=0x%06lx\n", name,
             (unsigned long)(lv_color_to_u32(eq_color) & 0xFFFFFFu));
    }
}

/* ============================================================================
 *  app_event handler
 * ==========================================================================*/

#if CONFIG_APP_EVT
/* Set of events this widget is interested in. */
static const app_evt_type_t kEvents[] = {
    APP_EVT_AI_LISTENING,
    APP_EVT_AI_THINKING,
    APP_EVT_AI_SPEAKING,
    APP_EVT_AI_IDLE,
    APP_EVT_AGENT_JOINED,
    APP_EVT_AGENT_OFFLINE,
    APP_EVT_AGENT_START_FAIL,
    APP_EVT_RTC_CONNECTION_LOST,
};

static void page_chat_evt_cb(app_evt_msg_t *msg, void *user)
{
    (void)user;
    if (!s_attached || msg == NULL) {
        return;
    }
    uint32_t evt = (uint32_t)msg->event;

    page_chat_anim_state_t target;
    switch (evt) {
    case APP_EVT_AI_IDLE:        target = PAGE_CHAT_STATE_IDLE;      break;
    case APP_EVT_AI_LISTENING:   target = PAGE_CHAT_STATE_LISTENING; break;
    case APP_EVT_AI_THINKING:    target = PAGE_CHAT_STATE_THINKING;  break;
    case APP_EVT_AI_SPEAKING:    target = PAGE_CHAT_STATE_SPEAKING;  break;
    case APP_EVT_AGENT_JOINED:   target = PAGE_CHAT_STATE_IDLE;      break;
    case APP_EVT_AGENT_OFFLINE:
    case APP_EVT_AGENT_START_FAIL:
    case APP_EVT_RTC_CONNECTION_LOST: target = PAGE_CHAT_STATE_ERROR; break;
    default:                     return;
    }

    /* Only print the routing line when it actually changes state, so a
     * chatty producer (e.g. agora_rtc data-stream during a long agent reply)
     * does not flood the console. */
    if (target != s_state) {
        LOGI("evt=%lu -> state=%d\n", (unsigned long)evt, (int)target);
    }

    lv_vendor_disp_lock();
    apply_state_locked(target);
    lv_vendor_disp_unlock();
}
#endif /* CONFIG_APP_EVT */

/* ============================================================================
 *  Lifecycle
 * ==========================================================================*/

static void clear_all_animations(void)
{
    for (int i = 0; i < 3; i++) {
        if (s_ripple[i] != NULL && lv_obj_is_valid(s_ripple[i])) {
            lv_anim_delete(s_ripple[i], ripple_progress_cb);
        }
    }
    lv_anim_delete(NULL, eq_height_cb);
    if (s_orbit_timer != NULL) {
        lv_timer_delete(s_orbit_timer);
        s_orbit_timer = NULL;
    }
    if (s_eq_energy_timer != NULL) {
        lv_timer_delete(s_eq_energy_timer);
        s_eq_energy_timer = NULL;
    }
}

static void clear_all_objects(void)
{
    if (s_root != NULL && lv_obj_is_valid(s_root)) {
        lv_obj_delete(s_root);
    }
    s_root          = NULL;
    s_status_label  = NULL;
    s_core_anchor   = NULL;
    s_core          = NULL;
    s_orbit_dot     = NULL;
    s_rec_label     = NULL;
    for (int i = 0; i < 3; i++) s_ripple[i] = NULL;
    for (int i = 0; i < EQ_BAR_COUNT; i++) s_eq[i] = NULL;
}

void page_chat_anim_attach(lv_obj_t *parent, page_chat_anim_mode_t mode)
{
    if (parent == NULL) {
        return;
    }
    /* Idempotent: peel off a previous instance first. */
    if (s_attached) {
        clear_all_animations();
        clear_all_objects();
        s_attached = 0;
    }

    s_mode  = mode;
    s_state = PAGE_CHAT_STATE_CONNECTING;

    s_root = lv_obj_create(parent);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, APP_LOGICAL_W, APP_LOGICAL_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(COLOR_BG_HEX), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);

    build_voice_layer(s_root);
    if (mode == PAGE_CHAT_ANIM_MODE_VISION) {
        build_vision_overlay(s_root);
    }

    s_attached = 1;
    apply_state_locked(PAGE_CHAT_STATE_CONNECTING);

#if CONFIG_APP_EVT
    if (!s_evt_registered) {
        for (size_t i = 0; i < sizeof(kEvents) / sizeof(kEvents[0]); i++) {
            app_event_register_handler(kEvents[i], page_chat_evt_cb, NULL);
        }
        s_evt_registered = 1;
    }
#endif

    LOGI("attached mode=%d\n", (int)mode);

#if HAVE_NTWK_TRANS
    /* If we re-attach after a transient page swap and the agent is already
     * connected, skip the CONNECTING animation. */
    if (ntwk_eng_is_agent_connected()) {
        apply_state_locked(PAGE_CHAT_STATE_IDLE);
    }
#endif
}

void page_chat_anim_detach(void)
{
    if (!s_attached) {
        return;
    }
    s_attached = 0;
    clear_all_animations();
    clear_all_objects();
    s_state = PAGE_CHAT_STATE_ERROR;
    LOGI("detached\n");
}

void page_chat_anim_set_state(page_chat_anim_state_t state)
{
    if (!s_attached) {
        return;
    }
    lv_vendor_disp_lock();
    apply_state_locked(state);
    lv_vendor_disp_unlock();
}
