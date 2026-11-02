/**
 * @file page_doa_eyes.h
 * @brief Centered face for page_5: two eyes (sclera + pupil + blink +
 *        gaze + color that tracks sound direction) plus a smile arc
 *        (an "upwards bowl" opening).
 *
 * Built with LVGL v9 primitives only -- no PNG / font dependencies:
 *   1) Sclera: two white LV_RADIUS_CIRCLE lv_obj instances
 *   2) Pupil:  two dark LV_RADIUS_CIRCLE child lv_objs that translate
 *              within the sclera to convey gaze direction
 *   3) Smile:  three stacked lv_obj layers to form the "upwards arc"
 *              (avoiding lv_arc, whose angles render incorrectly under
 *              ROTATE_90 + vg_lite acceleration):
 *                - s_mouth_outer : outer white ellipse / circle
 *                - s_mouth_inner : inner dark ellipse / circle, inset
 *                                  by INSET px from outer
 *                - s_mouth_mask  : top-half mask (same color as the
 *                                  page background) that hides the
 *                                  upper half so only the bottom
 *                                  "smile" arc remains.
 *              This avoids any reliance on lv_arc angle rotation and
 *              keeps the rendered direction identical to the screen.
 *
 * Screen-coord caveat (affects every constant below):
 *   ap_main.c sets cfg.rotation = ROTATE_90, so LVGL swaps
 *   horizontal / vertical and the application sees LOGICAL_SCREEN_WIDTH x
 *   LOGICAL_SCREEN_HEIGHT landscape canvas. page_5 is also resized to match
 *   so page-local coordinates match the screen. All PAGE_5_FACE_NUDGE_X/_Y
 *   and PAGE_5_RING_W/_H are measured against that landscape canvas.
 *
 * Design tradeoffs:
 *   - Gaze offset is a small (<= EYE_GAZE_RADIUS px) pupil translation
 *     -- no whole-eye rotation or scaling. This keeps the refresh cost
 *     low and renders stably under LVGL software composition.
 *   - Blink is implemented by animating pupil height / sclera clip
 *     (the pupil "squashes" to fake an eyelid). Cheaper than a real
 *     eyelid lv_obj.
 *   - The smile is static stacked lv_objs with no animation, so it
 *     costs roughly 0 % CPU once laid out.
 *   - PAGE_5_FACE_NUDGE_X/Y let us shift the whole composition
 *     (eyes + ring + mouth) together to align with the chassis on the
 *     real board: one tweak, everything follows.
 */

#ifndef __PAGE_DOA_EYES_H__
#define __PAGE_DOA_EYES_H__

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------
 *  Whole-composition nudge on page_5 (eyes + ring + mouth move together)
 * -------------------------------------------------------------------- */
/*
 * Physical screen is ~4.6 cm x 3.9 cm (390 x 360 logical px, landscape):
 *   horizontal: ~8.5 px / mm
 *   vertical:   ~9.2 px / mm
 *
 * Empirically-tuned constants, verified on hardware (385x320 logical):
 *   X: shift right ~3 mm  -> +22 px
 *   Y: shift up    ~3 mm  -> -25 px  (scaled from legacy -28 @ 360h)
 * Board coords: +X = right, -Y = up.
 */
/* Face read slightly low-left on hardware; nudged ~1 mm up-left
 * (X 22->14 = left, Y -12->-20 = up). +X=right, -Y=up (board-verified). */
#define PAGE_5_FACE_NUDGE_X  14
#define PAGE_5_FACE_NUDGE_Y  (-20)

/* Ring-only vertical drop: the white arc (and its KNOB) move down by this
 * many px AFTER eyes/mouth are placed, so the face features stay put while
 * just the ring slides down. +Y = down on screen, ~9.2 px/mm vertical:
 *   was 4 mm down (+37); ring nudged up 2 mm -> net +19 px. */
#define PAGE_5_RING_EXTRA_Y  19

/* ----------------------------------------------------------------------
 *  Ring bounding box (the white arc the KNOB rides along)
 * -------------------------------------------------------------------- */
/*
 * Designer default was 248 x 220 (visibly smaller than the face). The
 * customer asked the whole face to scale up; we add ~8 mm of diameter
 * on each axis, then scale for 385x320 logical canvas:
 *   horizontal 316 * 385/390 -> 312
 *   vertical   294 * 320/360 -> 261
 * The ring and page_5_eyes share the same FACE_CENTER + FACE_NUDGE so
 * these two constants resize / shift the whole composition concentric.
 */
/* Restored to the original board ring size so eyes/mouth reproduce the
 * original layout 1:1 (ring == REF_RING_W/H in page_doa_eyes.c). */
#define PAGE_5_RING_W  316
#define PAGE_5_RING_H  294
/* Must match page_doa_init.c lv_arc MAIN arc_width */
#define PAGE_5_ARC_STROKE  12

/* ----------------------------------------------------------------------
 *  Public API
 * -------------------------------------------------------------------- */

/**
 * @brief Create the eye / smile primitives on @p parent.
 *
 * Precondition: caller already holds lv_vendor_disp_lock() (true for
 * init_page_page_5). This function does NOT take the lock itself.
 *
 * Idempotent: repeated calls first destroy the existing primitives and
 * then rebuild.
 *
 * Z-order: the smile mask is large enough to overlap the eyes, so the
 * three smile lv_objs are created BEFORE the eyes; the later-created
 * eyes naturally float on top.
 *
 * @param ring_arc  page_5_arc_1 after hooks resize / reposition it; eyes
 *                  and mouth are laid out inside this widget bounds.
 */
void page_5_eyes_create(lv_obj_t *parent, lv_obj_t *ring_arc);

/**
 * @brief Destroy all primitives and stop the blink animation.
 *
 * Must be called BEFORE lv_obj_del(page_5) in destroy_page_page_5,
 * otherwise the blink anim callback would write into already-freed
 * lv_obj memory.
 */
void page_5_eyes_destroy(void);

/**
 * @brief Show / hide the composition (toggles LV_OBJ_FLAG_HIDDEN
 *        only, does NOT destroy). Use page_5_eyes_destroy() when the
 *        page itself is leaving.
 */
void page_5_eyes_set_visible(bool visible);

/**
 * @brief Translate the pupils to fake a "looking at you" gaze toward
 *        the given direction.
 *
 * Angle convention follows lv_arc:
 *   0 deg   = screen right
 *   90 deg  = screen bottom
 *   180 deg = screen left
 *   270 deg = screen top
 *
 * The pupil color is also recolored based on the angle:
 *   - left side  (135 deg .. 225 deg) -> blue
 *   - right side (<= 45 deg or >= 315 deg) -> red
 *   - top / bottom -> default dark
 *
 * Calling thread: this function does NOT take any lock. Callers must
 * already hold lv_vendor_disp_lock (typical site:
 * page_doa_hooks.c::apply_arrow_state_locked).
 */
void page_5_eyes_set_gaze(int degrees);

/**
 * @brief Register the `eyes` debug CLI (sub-commands: blink / gaze / show).
 *
 *   eyes blink                trigger one blink
 *   eyes gaze <deg>           set the gaze direction (same as page_5_set_arrow_angle)
 *   eyes show 0|1             hide / show the composition
 *
 * Useful for diagnosing the eye state machine over the serial console
 * without going through the KWS pipeline.
 */
int page_5_eyes_cli_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __PAGE_DOA_EYES_H__ */
