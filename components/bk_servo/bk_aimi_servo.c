/**
 * @file bk_aimi_servo.c
 * @brief Handle-based servo PWM driver.
 *
 * See bk_aimi_servo.h for the public API. This file is HW-agnostic: the
 * PWM channel and GPIO pin are passed in by the caller via
 * bk_aimi_servo_config_t and never hard-coded here. All per-instance
 * state (channel, gpio, current angle) lives in the handle struct, so
 * multiple servos (e.g. one for pan and one for tilt) can run side by
 * side without sharing globals.
 *
 * The "palm position -> new angle" math lives in a separate module
 * (bk_aimi_palm_tracker.{h,c}). This file is intentionally restricted
 * to driving the PWM line, so it can be reused by any motion-control
 * logic, not just palm tracking.
 */

#include <common/bk_include.h>
#include <os/mem.h>
#include "gpio_driver.h"
#include <driver/gpio.h>
#include <driver/pwm.h>
#include "bk_aimi_servo.h"

#define TAG "servo"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* ===================== Internal handle layout ===================== */
/* Opaque to callers; defined in the .c so we can grow it freely without
 * forcing recompiles of every translation unit that #include's the header. */
struct bk_aimi_servo_handle_s {
	pwm_chan_t chan;
	gpio_id_t  gpio;
	uint32_t   angle; /**< Last commanded angle (also "current physical
	                   *   angle" for the purposes of incremental tracking). */
};

/* ===================== Helpers ===================== */
/* BK SDK convention: PWM channel N is reached by configuring the GPIO
 * alternate function `GPIO_DEV_PWM<N>`. The values GPIO_DEV_PWM0..PWM11
 * are sequential in the BK hal_gpio_types.h enum, so channel N maps to
 * PWM0 + N with simple arithmetic. */
static inline gpio_dev_t servo_chan_to_gpio_dev(pwm_chan_t chan)
{
	return (gpio_dev_t)((uint32_t)GPIO_DEV_PWM0 + (uint32_t)chan);
}

static uint32_t servo_angle_to_duty(uint32_t angle)
{
	if (angle > SERVO_MAX_ANGLE)
		angle = SERVO_MAX_ANGLE;
	return SERVO_PULSE_MIN + angle * (SERVO_PULSE_MAX - SERVO_PULSE_MIN) / SERVO_MAX_ANGLE;
}

static void servo_remap_gpio(pwm_chan_t chan, gpio_id_t gpio)
{
	gpio_dev_t dev = servo_chan_to_gpio_dev(chan);
	gpio_dev_unmap(gpio);
	gpio_dev_map(gpio, dev);
	bk_gpio_pull_up(gpio);
	bk_printf("[servo] GPIO remapped: chan=%d -> GPIO_%d (dev=%d)\r\n",
	          chan, gpio, dev);
}

/* Push the angle stored in @p h onto the PWM line. No state mutation. */
static void servo_drive_pwm(struct bk_aimi_servo_handle_s *h)
{
	pwm_period_duty_config_t duty_cfg = {0};
	duty_cfg.period_cycle = SERVO_PERIOD_CYCLE;
	duty_cfg.psc          = SERVO_PSC;
	duty_cfg.duty_cycle   = servo_angle_to_duty(h->angle);
	BK_LOG_ON_ERR(bk_pwm_set_period_duty(h->chan, &duty_cfg));
}

/* ===================== Public API ===================== */

bk_aimi_servo_handle_t bk_aimi_servo_init(const bk_aimi_servo_config_t *cfg)
{
	if (cfg == NULL) {
		LOGE("init: cfg is NULL\n");
		return NULL;
	}

	uint32_t angle = cfg->initial_angle;
	if (angle > SERVO_MAX_ANGLE)
		angle = SERVO_MAX_ANGLE;

	struct bk_aimi_servo_handle_s *h =
	    (struct bk_aimi_servo_handle_s *)os_malloc(sizeof(*h));
	if (h == NULL) {
		LOGE("init: out of memory\n");
		return NULL;
	}
	h->chan  = cfg->chan;
	h->gpio  = cfg->gpio;
	h->angle = angle;

	bk_printf("[servo] init, chan=%d, gpio=%d, initial_angle=%u\r\n",
	          (int)h->chan, (int)h->gpio, (unsigned)h->angle);

	BK_LOG_ON_ERR(bk_pwm_driver_init());

	/* Program the PWM with the *initial* angle, not a hardcoded 0, so the
	 * first physical position matches what the caller asked for and the
	 * handle->angle state stays consistent with the wire. */
	pwm_init_config_t init_cfg = {0};
	init_cfg.period_cycle = SERVO_PERIOD_CYCLE;
	init_cfg.duty_cycle   = servo_angle_to_duty(h->angle);
	init_cfg.psc          = SERVO_PSC;
	BK_LOG_ON_ERR(bk_pwm_init(h->chan, &init_cfg));

	servo_remap_gpio(h->chan, h->gpio);

	BK_LOG_ON_ERR(bk_pwm_start(h->chan));

	return (bk_aimi_servo_handle_t)h;
}

void bk_aimi_servo_deinit(bk_aimi_servo_handle_t handle)
{
	struct bk_aimi_servo_handle_s *h = (struct bk_aimi_servo_handle_s *)handle;
	if (h == NULL)
		return;

	bk_printf("[servo] deinit, chan=%d, gpio=%d\r\n", (int)h->chan, (int)h->gpio);
	BK_LOG_ON_ERR(bk_pwm_stop(h->chan));
	BK_LOG_ON_ERR(bk_pwm_deinit(h->chan));
	/* Leave the GPIO pin in its current mux/pull state. The board can
	 * decide whether to re-purpose the pin afterwards; we don't know what
	 * is safe by default. */
	os_free(h);
}

void bk_aimi_servo_set_angle(bk_aimi_servo_handle_t handle, uint32_t angle)
{
	struct bk_aimi_servo_handle_s *h = (struct bk_aimi_servo_handle_s *)handle;
	if (h == NULL)
		return;

	if (angle > SERVO_MAX_ANGLE)
		angle = SERVO_MAX_ANGLE;

	h->angle = angle;
	servo_drive_pwm(h);
}

uint32_t bk_aimi_servo_get_angle(bk_aimi_servo_handle_t handle)
{
	struct bk_aimi_servo_handle_s *h = (struct bk_aimi_servo_handle_s *)handle;
	if (h == NULL)
		return 0;
	return h->angle;
}
