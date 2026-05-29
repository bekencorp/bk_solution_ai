/**
 * @file bk_aimi_palm_tracker.h
 * @brief Pure "palm position -> servo angle" math, one axis at a time.
 *
 * This module deliberately does NOT touch any hardware. It only converts
 * a palm center coordinate (on one axis) plus the current servo angle
 * into a new angle command. The caller pushes that angle to the actual
 * PWM line via bk_aimi_servo_set_angle() in bk_aimi_servo.h.
 *
 * Splitting the math out from the PWM driver gives us:
 *   - clean pan-tilt support (call step() twice per frame: once for H,
 *     once for V, each with its own tuning and direction sign);
 *   - testable, stateless math that doesn't depend on any board state;
 *   - servo driver that stays small and only worries about PWM duty.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== Default tuning knobs ===================== */
/* These are the algorithm-level defaults. Per-axis values are passed in
 * through bk_aimi_palm_tracker_axis_cfg_t, so the caller can override
 * any of them independently for horizontal / vertical. */

/* Gain that turns the normalized palm offset (in [-0.5, +0.5]) into a
 * per-frame angle offset. Larger -> turns more aggressively. */
#ifndef BK_AIMI_PALM_TRACKER_DEFAULT_GAIN
#define BK_AIMI_PALM_TRACKER_DEFAULT_GAIN      20.0f
#endif

/* Hard cap on |offset| each frame, to keep motion smooth. */
#ifndef BK_AIMI_PALM_TRACKER_DEFAULT_MAX_STEP
#define BK_AIMI_PALM_TRACKER_DEFAULT_MAX_STEP  5
#endif

/* Dead band on normalized palm offset; below this the palm is considered
 * already centered on this axis and the angle holds. */
#ifndef BK_AIMI_PALM_TRACKER_DEFAULT_DEADBAND
#define BK_AIMI_PALM_TRACKER_DEFAULT_DEADBAND  0.06f
#endif

/* ===================== Public API ===================== */

/**
 * @brief Per-axis tuning + mechanical limits for the tracker.
 *
 * One of these per physical motor. `dir` lets you flip the sign without
 * rewiring: set to -1 if the servo turns the wrong way relative to the
 * palm motion on this axis.
 */
typedef struct {
    float    gain;       /**< per-frame angle delta multiplier */
    int      max_step;   /**< clamp |delta| per frame, in degrees */
    float    deadband;   /**< |norm| below this => no update    */
    int      dir;        /**< +1 or -1; flips mounting direction */
    uint32_t min_angle;  /**< mechanical lower bound in degrees */
    uint32_t max_angle;  /**< mechanical upper bound in degrees */
} bk_aimi_palm_tracker_axis_cfg_t;

/**
 * @brief Compute a new servo angle for one axis from the palm position.
 *
 * Stateless. The whole pipeline is:
 *
 *   1. norm   = (palm_pos - dim/2) / dim   in [-0.5, +0.5]
 *      (negative = palm is on the "minus" side of the image center;
 *       positive = palm is on the "plus" side. Which physical side
 *       that means depends on the camera orientation; use `cfg->dir`
 *       to flip it.)
 *
 *   2. If |norm| < cfg->deadband: hold current angle, return false.
 *
 *   3. delta  = round(dir * norm * gain)
 *      delta  = clamp(delta, [-max_step, +max_step])
 *
 *   4. next   = clamp(current + delta, [min_angle, max_angle])
 *
 *   5. If next == current (saturated against either limit and no actual
 *      motion happens), return false. Otherwise write *next and return
 *      true.
 *
 * @param cfg         per-axis tuning + mechanical limits. Must not be NULL.
 * @param palm_pos    palm center on this axis in image coordinates (0..dim).
 * @param dim         image size on this axis (e.g. width or height).
 *                    Must be > 0; otherwise the function returns false.
 * @param current     the last commanded angle on this axis.
 * @param[out] next   receives the new angle if the function returns true.
 *                    Untouched on false.
 * @param axis_name   short label used in the debug print ("H" / "V" /
 *                    NULL to disable the print).
 *
 * @return true  if the caller should commit @p *next to the servo;
 *         false if the angle should be left alone (deadband or saturated
 *               or invalid input).
 */
bool bk_aimi_palm_tracker_step(const bk_aimi_palm_tracker_axis_cfg_t *cfg,
                               float palm_pos, float dim,
                               uint32_t current, uint32_t *next,
                               const char *axis_name);

#ifdef __cplusplus
}
#endif
