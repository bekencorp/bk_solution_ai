/**
 * @file bk_aimi_palm_tracker.c
 * @brief Pure "palm position -> servo angle" math.
 *
 * See bk_aimi_palm_tracker.h for the contract. This file deliberately
 * does not touch any board hardware or any servo handle; bk_aimi_servo.c
 * remains the only place that programs the PWM IP.
 */

#include <common/bk_include.h>
#include <math.h>

#include "bk_aimi_palm_tracker.h"

#define TAG "palm_track"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

bool bk_aimi_palm_tracker_step(const bk_aimi_palm_tracker_axis_cfg_t *cfg,
                               float palm_pos, float dim,
                               uint32_t current, uint32_t *next,
                               const char *axis_name)
{
	if (cfg == NULL || next == NULL || dim <= 0.0f) {
		return false;
	}

	/* Clamp mechanical limits defensively so a misconfigured cfg can't
	 * push us out of bounds further down. */
	uint32_t lo = cfg->min_angle;
	uint32_t hi = cfg->max_angle;
	if (hi < lo) {
		return false;
	}

	const char *tag = axis_name ? axis_name : "?";
	int prev = (int)current;

	/* Step 1: signed normalized offset from image center.
	 *   norm < 0 : palm is on the "minus" side of the center.
	 *   norm > 0 : palm is on the "plus"  side of the center.
	 * Which physical side that maps to is up to cfg->dir. */
	float norm = (palm_pos - dim * 0.5f) / dim;

	/* Step 2: dead band -> hold last angle. */
	if (fabsf(norm) < cfg->deadband) {
		bk_printf("[track:%s] pos=%.1f/%u norm=%+.3f deadband, hold=%d\n",
		          tag, palm_pos, (unsigned)dim, norm, prev);
		return false;
	}

	/* Step 3: convert offset to a signed per-frame angle delta and
	 * clamp by max_step. */
	int delta = (int)lroundf((float)cfg->dir * norm * cfg->gain);
	if (delta >  cfg->max_step) delta =  cfg->max_step;
	if (delta < -cfg->max_step) delta = -cfg->max_step;

	/* Step 4: apply on top of `current`, clamp to mechanical limits. */
	int cand = prev + delta;
	if (cand < (int)lo) cand = (int)lo;
	if (cand > (int)hi) cand = (int)hi;

	bk_printf("[track:%s] pos=%.1f/%u norm=%+.3f delta=%+d  %d -> %d%s\n",
	          tag, palm_pos, (unsigned)dim, norm, delta, prev, cand,
	          (cand == prev) ? " (saturated)" : "");

	/* Step 5: tell the caller whether anything actually changed. Skip
	 * the servo write if not, so the PWM line stays quiet on a hold. */
	if ((uint32_t)cand == current) {
		return false;
	}

	*next = (uint32_t)cand;
	return true;
}
