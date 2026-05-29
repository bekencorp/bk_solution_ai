/**
 * @file bk_aimi_servo.h
 * @brief Handle-based servo PWM driver.
 *
 * This module is intentionally minimal: it programs a PWM channel and
 * remembers the latest commanded angle in the returned opaque handle.
 * It does NOT contain any palm-tracking math; that lives in
 * bk_aimi_palm_tracker.h and is composed by the application layer on
 * top of one handle per physical motor.
 *
 * Pan-tilt usage: call bk_aimi_servo_init() once for the horizontal
 * motor and once for the vertical motor, then drive each independently
 * via bk_aimi_servo_set_angle().
 *
 * NOTE on PWM-to-GPIO mapping: this component assumes the BK SDK
 * convention where PWM channel N is mux-ed onto an alternate function
 * named `GPIO_DEV_PWM<N>`. The supplied `chan` is therefore used both
 * to drive the PWM IP and to compute the GPIO alternate function for
 * `gpio`.
 */

#pragma once

#include <common/bk_include.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <driver/pwm.h>
#include <driver/pwm_types.h>
#include <driver/gpio.h>
#include <driver/hal/hal_gpio_types.h>
#include "gpio_driver.h"

/* ===================== Low-level PWM parameters ===================== */
#define SERVO_CLK_SRC         320000000
#define SERVO_PSC             249
#define SERVO_EFFECTIVE_CLK   (SERVO_CLK_SRC / (SERVO_PSC + 1))
#define SERVO_FREQ            50
#define SERVO_PERIOD_CYCLE    (SERVO_EFFECTIVE_CLK / SERVO_FREQ)
#define SERVO_PULSE_MIN       (SERVO_EFFECTIVE_CLK * 5 / 10000)
#define SERVO_PULSE_MAX       (SERVO_EFFECTIVE_CLK * 25 / 10000)

/* ===================== Servo mechanical limits ===================== */
/* Initial / neutral angle (also used as the "centered" position). */
#define SERVO_CENTER_ANGLE    90
#define SERVO_MIN_ANGLE       0
#define SERVO_MAX_ANGLE       180

/* ===================== Public API ===================== */

/**
 * @brief Opaque handle representing one initialised servo channel.
 *
 * Carries the PWM channel, GPIO pin, and the latest commanded angle so
 * incremental motion (e.g. by the palm tracker) can do
 * `next = last + delta` without any global state. Treat as opaque; the
 * struct layout is internal to bk_aimi_servo.c.
 */
typedef struct bk_aimi_servo_handle_s *bk_aimi_servo_handle_t;

/**
 * @brief Per-instance configuration for bk_aimi_servo_init().
 *
 * `initial_angle` is both the initial PWM duty cycle programmed on the
 * line AND the starting value of the handle's internal angle state.
 * Setting it to SERVO_CENTER_ANGLE (90) avoids the "jump to 0 then to
 * 90 on first track" double-move that a hardcoded 0 would cause.
 */
typedef struct {
    pwm_chan_t chan;          /**< PWM channel to drive (e.g. PWM_ID_0). */
    gpio_id_t  gpio;          /**< GPIO pin to output the PWM signal on. */
    uint32_t   initial_angle; /**< Starting angle in degrees, [0, 180]. */
} bk_aimi_servo_config_t;

/**
 * @brief Bring up a servo channel and return a handle owning it.
 *
 * @param cfg   Configuration (channel, GPIO, initial angle). Must be non-NULL.
 * @return      A valid handle on success, or NULL on allocation / config error.
 *              Caller releases it with bk_aimi_servo_deinit().
 */
bk_aimi_servo_handle_t bk_aimi_servo_init(const bk_aimi_servo_config_t *cfg);

/**
 * @brief Stop the servo's PWM output and free the handle.
 *
 * Safe to call with @p handle == NULL.
 */
void bk_aimi_servo_deinit(bk_aimi_servo_handle_t handle);

/**
 * @brief Snap the servo to an absolute angle and remember it inside @p handle.
 *
 * @param handle  Handle previously returned by bk_aimi_servo_init().
 * @param angle   Target angle in degrees, will be clamped to [0, 180].
 */
void bk_aimi_servo_set_angle(bk_aimi_servo_handle_t handle, uint32_t angle);

/**
 * @brief Read the last angle commanded via init() / set_angle().
 *
 * @return Last commanded angle in degrees, or 0 if @p handle is NULL.
 */
uint32_t bk_aimi_servo_get_angle(bk_aimi_servo_handle_t handle);

#ifdef __cplusplus
}
#endif
