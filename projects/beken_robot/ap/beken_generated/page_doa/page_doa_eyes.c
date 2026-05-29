/**
 * @file page_doa_eyes.c
 * @brief Implementation of the page_5 central face: two eyes (sclera
 *        + pupil + blink + gaze + color tracking the sound direction)
 *        and the "upwards bowl" smile arc. See page_doa_eyes.h for the
 *        full design rationale.
 */

#include "page_doa_eyes.h"

#include "lvgl.h"
#include "lv_vendor.h"
#include "beken_ui.h"

#include "components/log.h"
#include "bk_cli.h"
#include "cli.h"
#include "os/os.h"
#include "os/str.h"

#include <stdlib.h>

#define TAG "page5_eyes"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* ------------------------------------------------------------------------- */
/*  Geometry constants                                                        */
/* ------------------------------------------------------------------------- */
/*
 * Application-layer logical screen: cfg.rotation = ROTATE_90 swaps
 * horizontal / vertical so the app sees 390 x 360. Center = (195, 180).
 * Face center (ring / eyes / mouth share this anchor) =
 *   logical center + empirical nudge.
 */
#define APP_LOGICAL_W   390
#define APP_LOGICAL_H   360

#define FACE_CENTER_X   ((APP_LOGICAL_W / 2) + PAGE_5_FACE_NUDGE_X)
#define FACE_CENTER_Y   ((APP_LOGICAL_H / 2) + PAGE_5_FACE_NUDGE_Y)

/* ----- Eye geometry -----
 * Sclera radius 50 px; horizontal eye-to-eye gap 110 px (pupil center
 * to center). The whole eye row is shifted up by
 * EYE_OFFSET_FROM_FACE_Y (~ 4 mm) from the face center to leave room
 * for the smile underneath. Gaze offset is +/- EYE_GAZE_RADIUS px
 * within the sclera; the value must stay below
 * (EYE_RADIUS - PUPIL_RADIUS) so the pupil never pokes out.
 */
#define EYE_RADIUS               50
#define EYE_GAP                  110
#define EYE_OFFSET_FROM_FACE_Y   (-37)
#define PUPIL_RADIUS             24
#define EYE_GAZE_RADIUS          18

#define BLINK_PERIOD_MS          3500
#define BLINK_DURATION_MS        180   /* Single blink "squash + restore" duration. */

/* ----- Smile geometry (outer + inner + top-half mask, three layers) -----
 *   - MOUTH_W / MOUTH_H_TOTAL : outer ellipse/circle bounding box
 *   - MOUTH_INSET             : inner inset from outer on every side
 *   - MOUTH_GAP               : distance from the eye baseline (sclera
 *                               bottom) to the outer top edge
 *   - MOUTH_VISIBLE_H = (MOUTH_H_TOTAL/2) * 2/3 : visible arc height
 *                               (bottom 2/3 of the lower half)
 *   - MOUTH_MASK_H            : how far down from the outer top the
 *                               mask reaches; sized so only
 *                               MOUTH_VISIBLE_H remains visible
 */
#define MOUTH_W           160
#define MOUTH_H_TOTAL     160
#define MOUTH_INSET       10
/*
 * Smile fine-tune offsets (independent of PAGE_5_FACE_NUDGE_*: lets
 * us nudge just the mouth while keeping eyes / ring fixed):
 *   - MOUTH_GAP      : eye-baseline -> outer-top distance (px). May
 *                      be negative -- that just means the outer top
 *                      sits higher than the eye baseline. The eyes
 *                      are constructed AFTER the three smile objects
 *                      so the mask never overlaps them; the visible
 *                      "bowl" arc still sits below the eyes.
 *   - MOUTH_OFFSET_X : horizontal shift of the smile relative to
 *                      FACE_CENTER_X; only the three smile objects
 *                      move, eyes and ring do not follow.
 *
 * Current values (visually aligned on the board):
 *   - GAP        30 -> -53: total shift up ~ 9 mm (8 mm + 1 mm @
 *                          9.2 px/mm).
 *   - OFFSET_X   = -9    : shift left ~ 1 mm @ 8.5 px/mm.
 */
#define MOUTH_GAP         (-53)
#define MOUTH_OFFSET_X    (-9)
#define MOUTH_VISIBLE_H   ((MOUTH_H_TOTAL / 2) * 2 / 3)
#define MOUTH_MASK_H      (MOUTH_H_TOTAL - MOUTH_VISIBLE_H)

/* ----- Colors ----- */
#define COLOR_BG                lv_color_hex(0x000000)
#define COLOR_EYE_WHITE         lv_color_hex(0xFFFFFF)
#define COLOR_PUPIL_DARK        lv_color_hex(0x8B4513)
#define COLOR_PUPIL_LEFT_BLUE   lv_color_hex(0x2195F6)
#define COLOR_PUPIL_RIGHT_RED   lv_color_hex(0xFFBF00)
#define COLOR_MOUTH_OUTER       lv_color_hex(0xFFFFFF)

/* ------------------------------------------------------------------------- */
/*  State                                                                     */
/* ------------------------------------------------------------------------- */

static lv_obj_t *s_eye_left;
static lv_obj_t *s_eye_right;
static lv_obj_t *s_pupil_left;
static lv_obj_t *s_pupil_right;

static lv_obj_t *s_mouth_outer;
static lv_obj_t *s_mouth_inner;
static lv_obj_t *s_mouth_mask;

static lv_anim_t s_blink_anim;
static bool      s_blink_anim_running;

/* Default to gaze direction 90 deg (looking down) so the first frame
 * matches the blue KNOB position used by the page_5 init code. */
static int s_last_gaze_deg = 90;

/* ------------------------------------------------------------------------- */
/*  Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static lv_obj_t *make_solid(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                            lv_color_t color, lv_coord_t radius)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(o, radius, LV_PART_MAIN);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *make_circle(lv_obj_t *parent, lv_coord_t diameter, lv_color_t color)
{
    return make_solid(parent, diameter, diameter, color, LV_RADIUS_CIRCLE);
}

static lv_obj_t *make_rect(lv_obj_t *parent, lv_coord_t w, lv_coord_t h,
                           lv_color_t color)
{
    return make_solid(parent, w, h, color, 0);
}

/*
 * Pupil offset within the sclera.
 *
 * LVGL trigo: lv_trigo_sin(0)=0, lv_trigo_sin(90)=LV_TRIGO_SIN_MAX
 * (32767). Screen coords: +x right, +y down. The KNOB / arrow angle
 * convention follows lv_arc:
 *   0 deg = right, 90 deg = down, 180 deg = left, 270 deg = up.
 * Hence:
 *   dx = cos(deg) * R
 *   dy = sin(deg) * R   (already matches the screen y direction)
 */
static void compute_gaze_offset(int deg, lv_coord_t *dx, lv_coord_t *dy)
{
    int32_t s = lv_trigo_sin(deg);
    int32_t c = lv_trigo_cos(deg);
    *dx = (lv_coord_t)((c * EYE_GAZE_RADIUS) / LV_TRIGO_SIN_MAX);
    *dy = (lv_coord_t)((s * EYE_GAZE_RADIUS) / LV_TRIGO_SIN_MAX);
}

static lv_color_t deg_to_pupil_color(int deg)
{
    int d = ((deg % 360) + 360) % 360;
    if (d >= 135 && d <= 225) {
        return COLOR_PUPIL_LEFT_BLUE;     /* Screen left side: blue */
    }
    if (d <= 45 || d >= 315) {
        return COLOR_PUPIL_RIGHT_RED;     /* Screen right side: red */
    }
    return COLOR_PUPIL_DARK;              /* Top / bottom zone: default dark */
}

static void apply_gaze_locked(int deg)
{
    if (s_pupil_left == NULL || s_pupil_right == NULL) {
        return;
    }
    lv_coord_t dx, dy;
    compute_gaze_offset(deg, &dx, &dy);
    lv_obj_set_pos(s_pupil_left,  dx, dy);
    lv_obj_set_pos(s_pupil_right, dx, dy);

    lv_color_t c = deg_to_pupil_color(deg);
    lv_obj_set_style_bg_color(s_pupil_left,  c, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_pupil_right, c, LV_PART_MAIN);
}

/* ------------------------------------------------------------------------- */
/*  Blink animation                                                           */
/* ------------------------------------------------------------------------- */

static void blink_anim_set_h(void *var, int32_t v)
{
    (void)var;
    /* The callback may fire once more after destroy(); guard to avoid
     * touching freed lv_objs. */
    if (s_pupil_left == NULL || s_pupil_right == NULL) {
        return;
    }
    /* Animate the pupil height with LV_ALIGN_CENTER re-anchoring to
     * fake "squash + restore" blink. Cheaper than spawning a real
     * eyelid lv_obj and the invalidated area is smaller. */
    lv_obj_set_height(s_pupil_left,  v);
    lv_obj_set_height(s_pupil_right, v);
}

static void blink_anim_start(void)
{
    if (s_pupil_left == NULL) {
        return;
    }
    /* Cancel any stale animation in the LVGL anim list first. */
    lv_anim_delete(NULL, blink_anim_set_h);

    lv_anim_init(&s_blink_anim);
    /* `var` is used as an identity marker; the callback locates the
     * actual pupil lv_objs through the s_pupil_* globals. */
    lv_anim_set_var(&s_blink_anim, s_pupil_left);
    lv_anim_set_exec_cb(&s_blink_anim, blink_anim_set_h);
    lv_anim_set_values(&s_blink_anim, PUPIL_RADIUS * 2, 4);
    lv_anim_set_duration(&s_blink_anim, BLINK_DURATION_MS / 2);
    lv_anim_set_reverse_duration(&s_blink_anim, BLINK_DURATION_MS / 2);
    lv_anim_set_repeat_count(&s_blink_anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&s_blink_anim, BLINK_PERIOD_MS - BLINK_DURATION_MS);
    lv_anim_start(&s_blink_anim);
    s_blink_anim_running = true;
}

static void blink_anim_stop(void)
{
    if (!s_blink_anim_running) {
        return;
    }
    lv_anim_delete(NULL, blink_anim_set_h);
    s_blink_anim_running = false;
}

/* ------------------------------------------------------------------------- */
/*  Create / destroy                                                          */
/* ------------------------------------------------------------------------- */

void page_5_eyes_create(lv_obj_t *parent)
{
    if (parent == NULL) {
        return;
    }
    page_5_eyes_destroy();    /* Idempotent on repeated calls. */

    const lv_coord_t eye_y  = FACE_CENTER_Y + EYE_OFFSET_FROM_FACE_Y;
    const lv_coord_t eye_lx = FACE_CENTER_X - EYE_GAP / 2;
    const lv_coord_t eye_rx = FACE_CENTER_X + EYE_GAP / 2;

    /* mouth_top = eye baseline + MOUTH_GAP. Total outer height is
     * MOUTH_H_TOTAL, of which the lower MOUTH_VISIBLE_H stays visible
     * while the top is covered to BG by the mask. MOUTH_OFFSET_X only
     * shifts mouth_left / mask / inner -- eyes never follow. */
    const lv_coord_t mouth_top    = eye_y + EYE_RADIUS + MOUTH_GAP;
    const lv_coord_t mouth_left   = FACE_CENTER_X - MOUTH_W / 2 + MOUTH_OFFSET_X;
    const lv_coord_t mouth_in_top = mouth_top + MOUTH_INSET;
    const lv_coord_t mouth_in_lx  = mouth_left + MOUTH_INSET;

    /* ---------- Smile (created first) ----------
     * The mask is a BG-colored rectangle that sits above outer / inner
     * and is taller than EYE_RADIUS, so it would overlap the eyes. We
     * therefore MUST create the smile BEFORE the eyes so the eyes end
     * up on top in z-order.
     */
    s_mouth_outer = make_solid(parent, MOUTH_W, MOUTH_H_TOTAL,
                               COLOR_MOUTH_OUTER, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_mouth_outer, mouth_left, mouth_top);

    s_mouth_inner = make_solid(parent,
                               MOUTH_W - 2 * MOUTH_INSET,
                               MOUTH_H_TOTAL - 2 * MOUTH_INSET,
                               COLOR_BG, LV_RADIUS_CIRCLE);
    lv_obj_set_pos(s_mouth_inner, mouth_in_lx, mouth_in_top);

    s_mouth_mask = make_rect(parent, MOUTH_W, MOUTH_MASK_H, COLOR_BG);
    lv_obj_set_pos(s_mouth_mask, mouth_left, mouth_top);

    /* ---------- Sclera + pupil (child) ---------- */
    s_eye_left  = make_circle(parent, EYE_RADIUS * 2, COLOR_EYE_WHITE);
    lv_obj_set_pos(s_eye_left,  eye_lx - EYE_RADIUS, eye_y - EYE_RADIUS);

    s_eye_right = make_circle(parent, EYE_RADIUS * 2, COLOR_EYE_WHITE);
    lv_obj_set_pos(s_eye_right, eye_rx - EYE_RADIUS, eye_y - EYE_RADIUS);

    /* Pupils are sclera children: LV_ALIGN_CENTER plus
     * set_pos(dx, dy) provides the gaze offset. */
    s_pupil_left = make_circle(s_eye_left, PUPIL_RADIUS * 2, COLOR_PUPIL_DARK);
    lv_obj_set_align(s_pupil_left, LV_ALIGN_CENTER);

    s_pupil_right = make_circle(s_eye_right, PUPIL_RADIUS * 2, COLOR_PUPIL_DARK);
    lv_obj_set_align(s_pupil_right, LV_ALIGN_CENTER);

    apply_gaze_locked(s_last_gaze_deg);
    blink_anim_start();

    LOGI("eyes+mouth created: face=(%d,%d) eye_y=%d mouth_top=%d\r\n",
         FACE_CENTER_X, FACE_CENTER_Y, eye_y, mouth_top);
}

void page_5_eyes_destroy(void)
{
    blink_anim_stop();

    /* Pupils are children of the sclera and get freed automatically
     * by lv_obj_del; we only delete the five top-level objects. */
    lv_obj_t *objs[] = {
        s_eye_left, s_eye_right,
        s_mouth_outer, s_mouth_inner, s_mouth_mask,
    };
    for (size_t i = 0; i < sizeof(objs) / sizeof(objs[0]); ++i) {
        if (objs[i] != NULL && lv_obj_is_valid(objs[i])) {
            lv_obj_del(objs[i]);
        }
    }
    s_eye_left   = NULL;
    s_eye_right  = NULL;
    s_pupil_left = NULL;
    s_pupil_right = NULL;
    s_mouth_outer = NULL;
    s_mouth_inner = NULL;
    s_mouth_mask  = NULL;
}

void page_5_eyes_set_visible(bool visible)
{
    lv_obj_t *objs[] = {
        s_eye_left, s_eye_right,
        s_mouth_outer, s_mouth_inner, s_mouth_mask,
    };
    for (size_t i = 0; i < sizeof(objs) / sizeof(objs[0]); ++i) {
        if (objs[i] == NULL) {
            continue;
        }
        if (visible) {
            lv_obj_remove_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(objs[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void page_5_eyes_set_gaze(int degrees)
{
    s_last_gaze_deg = degrees;
    apply_gaze_locked(degrees);
}

/* ----------------------------------------------------------------------
 *  CLI: eyes blink|gaze|show
 * -------------------------------------------------------------------- */

static void eyes_cli_usage(void)
{
    LOGI("Usage:\r\n"
         "  eyes blink                trigger one blink animation cycle\r\n"
         "  eyes gaze <deg>           set pupil gaze direction (0~360)\r\n"
         "  eyes show 0|1             hide / show all expression objects\r\n");
}

static void eyes_cli_handler(char *out, int out_len, int argc, char **argv)
{
    (void)out;
    (void)out_len;

    if (argc < 2) {
        eyes_cli_usage();
        return;
    }

    if (os_strcmp(argv[1], "blink") == 0) {
        lv_vendor_disp_lock();
        blink_anim_start();
        lv_vendor_disp_unlock();
        LOGI("eyes blink (re)started\r\n");
        return;
    }

    if (os_strcmp(argv[1], "gaze") == 0 && argc >= 3) {
        int deg = atoi(argv[2]);
        lv_vendor_disp_lock();
        page_5_eyes_set_gaze(deg);
        lv_vendor_disp_unlock();
        LOGI("eyes gaze=%d\r\n", deg);
        return;
    }

    if (os_strcmp(argv[1], "show") == 0 && argc >= 3) {
        bool v = atoi(argv[2]) != 0;
        lv_vendor_disp_lock();
        page_5_eyes_set_visible(v);
        lv_vendor_disp_unlock();
        LOGI("eyes show=%d\r\n", (int)v);
        return;
    }

    eyes_cli_usage();
}

static const struct cli_command s_eyes_cli_cmd[] = {
    {
        "eyes",
        "eyes [blink|gaze <deg>|show 0|1]",
        eyes_cli_handler,
    },
};

int page_5_eyes_cli_init(void)
{
    static bool s_inited;
    if (s_inited) {
        return 0;
    }
    int ret = cli_register_commands(s_eyes_cli_cmd,
                                    sizeof(s_eyes_cli_cmd) / sizeof(s_eyes_cli_cmd[0]));
    if (ret == 0) {
        s_inited = true;
        LOGI("eyes CLI registered\r\n");
    } else {
        LOGW("eyes CLI register failed: ret=%d\r\n", ret);
    }
    return ret;
}
