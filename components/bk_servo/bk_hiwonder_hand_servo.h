/**
 * @file bk_hiwonder_hand_servo.h
 * @brief Hiwonder 6-DOF hand servo control with smoothed hardware-PWM pulse updates.
 *
 * Servo ID to output mapping on the beken_robot board:
 *   1 -> GPIO_22 / PWM8
 *   2 -> GPIO_23 / PWM6
 *   3 -> GPIO_24 / PWM7
 *   4 -> GPIO_25 / PWM5
 *   5 -> GPIO_26 / PWM4
 *   6 -> GPIO_27 / PWM3
 */

#pragma once

#include <common/bk_include.h>
#include <stdint.h>
#include <stdbool.h>
#include <driver/pwm_types.h>
#include <driver/hal/hal_gpio_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BK_HIWONDER_HAND_SERVO_MAX_ID           6

#define BK_HIWONDER_HAND_SERVO_FRAME_US         2500U

/** Hardware mapping for a single servo channel. */
typedef struct {
    pwm_chan_t pwm_chan;   /**< PWM channel driving the servo */
    gpio_id_t  gpio;       /**< GPIO pad routed to that PWM channel */
} bk_hiwonder_hand_servo_hw_map_t;

/**
 * @brief Opaque handle owning one hand-servo group instance.
 *
 * All per-instance memory and state (hardware map copy, per-servo duty
 * tracking, ramp timer) live behind this handle, so multiple hands can be
 * driven independently and no global state is shared. Treat as opaque; the
 * struct layout is internal to bk_hiwonder_hand_servo.c.
 */
typedef struct bk_hiwonder_hand_servo_handle_s *bk_hiwonder_hand_servo_handle_t;

/**
 * @brief Bring up a hand-servo group from a caller-provided hardware map.
 *
 * The driver allocates the handle and an internal copy of the map, so the
 * passed array does not need to stay alive after the call.
 *
 * @param map   array of @p count entries; entry i describes servo id (i + 1).
 * @param count number of servos, 1..BK_HIWONDER_HAND_SERVO_MAX_ID.
 * @return a valid handle on success, or NULL on config / allocation error.
 *         Release it with bk_hiwonder_hand_servo_deinit().
 */
bk_hiwonder_hand_servo_handle_t bk_hiwonder_hand_servo_init(
        const bk_hiwonder_hand_servo_hw_map_t *map, uint8_t count);

/** @brief Stop all PWM outputs and free the handle. Safe with @p handle == NULL. */
void bk_hiwonder_hand_servo_deinit(bk_hiwonder_hand_servo_handle_t handle);

/**
 * @brief Set a servo target pulse and the move duration for the group.
 * @param id servo id, 1..count.
 */
void bk_hiwonder_hand_servo_set_pulse_and_time(bk_hiwonder_hand_servo_handle_t handle,
                                               uint8_t id, uint16_t pulse_us, uint16_t time_ms);

/** @brief Advance the smoothed ramp one step; driven by the internal timer. */
void bk_hiwonder_hand_servo_duty_compare(bk_hiwonder_hand_servo_handle_t handle);

/** @brief Cycle through the built-in gesture presets forever (debug helper). */
void bk_hiwonder_hand_servo_test(bk_hiwonder_hand_servo_handle_t handle);

/** @param preset_id 0-based index into the built-in gesture preset table (0..4). */
void bk_hiwonder_hand_servo_apply_preset(bk_hiwonder_hand_servo_handle_t handle, uint8_t preset_id);

#ifdef __cplusplus
}
#endif
