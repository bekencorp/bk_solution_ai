/**
 * @file bk_hiwonder_hand_servo.c
 * @brief Hiwonder 6-DOF hand servo control with smoothed hardware-PWM pulse updates.
 *
 * Handle-based: all per-instance memory and state live in the opaque handle
 * struct allocated by bk_hiwonder_hand_servo_init(). The hardware map is
 * supplied by the caller and never hard-coded here, so multiple hands can run
 * independently without shared globals. Servo ids are 1-based at the API
 * boundary (1..count); internally they index 0-based arrays of length count.
 */

#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <driver/gpio.h>
#include <driver/pwm.h>
#include <driver/pwm_types.h>
#include "gpio_driver.h"
#include "bk_hiwonder_hand_servo.h"

#define TAG "hiwonder-hand"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BK_HIWONDER_HAND_SERVO_RAMP_PERIOD_MS     20
#define BK_HIWONDER_HAND_SERVO_HW_CLK_SRC         320000000U
#define BK_HIWONDER_HAND_SERVO_HW_PSC             249U
#define BK_HIWONDER_HAND_SERVO_HW_EFFECTIVE_CLK     (BK_HIWONDER_HAND_SERVO_HW_CLK_SRC / (BK_HIWONDER_HAND_SERVO_HW_PSC + 1U))
#define BK_HIWONDER_HAND_SERVO_HW_FREQ_HZ         50U
#define BK_HIWONDER_HAND_SERVO_HW_PERIOD_CYCLE    (BK_HIWONDER_HAND_SERVO_HW_EFFECTIVE_CLK / BK_HIWONDER_HAND_SERVO_HW_FREQ_HZ)

#define BK_HIWONDER_HAND_SERVO_DEFAULT_PULSE_US   1500U
#define BK_HIWONDER_HAND_SERVO_DEFAULT_MOVE_MS    2000U

/* ===================== Internal handle layout ===================== */
/* Opaque to callers; all per-instance memory and state lives here. Arrays are
 * length `count` and indexed 0-based (servo id - 1). */
struct bk_hiwonder_hand_servo_handle_s {
    uint8_t   count;                            /**< number of servos */
    bk_hiwonder_hand_servo_hw_map_t *hw_map;    /**< owned copy of the caller map */
    uint16_t *duty_set;                         /**< target pulse (us) per servo */
    uint16_t *duty;                             /**< current pulse (us) per servo */
    float    *duty_inc;                         /**< per-step delta toward target */
    bool     *hw_inited;                        /**< per-channel PWM init flag */
    uint16_t  move_time_ms;                     /**< current move duration */
    bool      duty_changed;                     /**< a new target was requested */
    uint16_t  ramp_remaining;                   /**< ramp steps left */
    bool      ramp_running;                      /**< ramp in progress */
    beken_timer_t ramp_timer;
};

/* ===================== Handle allocation ===================== */

static void bk_hiwonder_hand_servo_free_handle(bk_hiwonder_hand_servo_handle_t h)
{
    if (h == NULL) {
        return;
    }
    if (h->hw_map) {
        os_free(h->hw_map);
    }
    if (h->duty_set) {
        os_free(h->duty_set);
    }
    if (h->duty) {
        os_free(h->duty);
    }
    if (h->duty_inc) {
        os_free(h->duty_inc);
    }
    if (h->hw_inited) {
        os_free(h->hw_inited);
    }
    os_free(h);
}

static bk_hiwonder_hand_servo_handle_t bk_hiwonder_hand_servo_alloc_handle(uint8_t count)
{
    bk_hiwonder_hand_servo_handle_t h =
        (bk_hiwonder_hand_servo_handle_t)os_malloc(sizeof(*h));
    if (h == NULL) {
        return NULL;
    }
    os_memset(h, 0, sizeof(*h));
    h->count = count;
    h->hw_map    = (bk_hiwonder_hand_servo_hw_map_t *)os_malloc(sizeof(h->hw_map[0]) * count);
    h->duty_set  = (uint16_t *)os_malloc(sizeof(h->duty_set[0]) * count);
    h->duty      = (uint16_t *)os_malloc(sizeof(h->duty[0]) * count);
    h->duty_inc  = (float *)os_malloc(sizeof(h->duty_inc[0]) * count);
    h->hw_inited = (bool *)os_malloc(sizeof(h->hw_inited[0]) * count);
    if (h->hw_map == NULL || h->duty_set == NULL || h->duty == NULL ||
        h->duty_inc == NULL || h->hw_inited == NULL) {
        bk_hiwonder_hand_servo_free_handle(h);
        return NULL;
    }
    return h;
}

/* ===================== Hardware (PWM) layer ===================== */

static uint32_t bk_hiwonder_hand_servo_pulse_us_to_duty_cycle(uint16_t pulse_us)
{
    return (uint32_t)(((uint64_t)BK_HIWONDER_HAND_SERVO_HW_EFFECTIVE_CLK * pulse_us) / 1000000ULL);
}

static bk_err_t bk_hiwonder_hand_servo_hw_set_duty(pwm_chan_t chan, uint16_t pulse_us)
{
    pwm_period_duty_config_t cfg = {0};

    cfg.period_cycle = BK_HIWONDER_HAND_SERVO_HW_PERIOD_CYCLE;
    cfg.psc = BK_HIWONDER_HAND_SERVO_HW_PSC;
    cfg.duty_cycle = bk_hiwonder_hand_servo_pulse_us_to_duty_cycle(pulse_us);
    return bk_pwm_set_period_duty(chan, &cfg);
}

static void bk_hiwonder_hand_servo_hw_apply(bk_hiwonder_hand_servo_handle_t h, uint8_t idx)
{
    if (idx >= h->count || !h->hw_inited[idx]) {
        return;
    }

    bk_err_t ret = bk_hiwonder_hand_servo_hw_set_duty(h->hw_map[idx].pwm_chan, h->duty[idx]);
    if (ret != BK_OK) {
        LOGE("hw apply idx=%u chan=%u failed %d\n", idx, h->hw_map[idx].pwm_chan, ret);
    }
}

static bk_err_t bk_hiwonder_hand_servo_hw_channel_init(bk_hiwonder_hand_servo_handle_t h, uint8_t idx)
{
    pwm_init_config_t init_cfg = {0};
    bk_err_t ret;

    init_cfg.period_cycle = BK_HIWONDER_HAND_SERVO_HW_PERIOD_CYCLE;
    init_cfg.duty_cycle = bk_hiwonder_hand_servo_pulse_us_to_duty_cycle(h->duty[idx]);
    init_cfg.psc = BK_HIWONDER_HAND_SERVO_HW_PSC;

    /* The PWM channel's output pad is owned by the project GPIO config table
     * (GPIO_DEFAULT_DEV_CONFIG in usr_gpio_cfg.h); bk_pwm_init() routes it via
     * gpio_dev_map_by_func(GPIO_DEV_PWM<chan>). hw_map[idx].gpio is kept only
     * for logging/diagnostics here. */
    ret = bk_pwm_init(h->hw_map[idx].pwm_chan, &init_cfg);
    if (ret != BK_OK) {
        LOGE("bk_pwm_init idx=%u chan=%u gpio=%u failed %d\n",
             idx, h->hw_map[idx].pwm_chan, h->hw_map[idx].gpio, ret);
        return ret;
    }

    ret = bk_pwm_start(h->hw_map[idx].pwm_chan);
    if (ret != BK_OK) {
        LOGE("bk_pwm_start idx=%u chan=%u failed %d\n", idx, h->hw_map[idx].pwm_chan, ret);
        bk_pwm_deinit(h->hw_map[idx].pwm_chan);
        return ret;
    }

    h->hw_inited[idx] = true;
    LOGI("hw output id=%u gpio=%u pwm_ch=%u pulse=%u us\n",
         idx + 1, h->hw_map[idx].gpio, h->hw_map[idx].pwm_chan, h->duty[idx]);
    return BK_OK;
}

static void bk_hiwonder_hand_servo_hw_deinit_all(bk_hiwonder_hand_servo_handle_t h)
{
    for (uint8_t idx = 0; idx < h->count; idx++) {
        if (!h->hw_inited[idx]) {
            continue;
        }

        bk_pwm_stop(h->hw_map[idx].pwm_chan);
        bk_pwm_deinit(h->hw_map[idx].pwm_chan);
        h->hw_inited[idx] = false;
    }

    bk_pwm_driver_deinit();
}

static bk_err_t bk_hiwonder_hand_servo_hw_init_all(bk_hiwonder_hand_servo_handle_t h)
{
    bk_err_t ret = bk_pwm_driver_init();
    if (ret != BK_OK) {
        LOGE("bk_pwm_driver_init failed %d\n", ret);
        return ret;
    }

    for (uint8_t idx = 0; idx < h->count; idx++) {
        ret = bk_hiwonder_hand_servo_hw_channel_init(h, idx);
        if (ret != BK_OK) {
            bk_hiwonder_hand_servo_hw_deinit_all(h);
            return ret;
        }
    }

    return BK_OK;
}

/* ===================== Smoothed motion ===================== */

static void bk_hiwonder_hand_servo_ramp_timer_cb(void *arg)
{
    bk_hiwonder_hand_servo_duty_compare((bk_hiwonder_hand_servo_handle_t)arg);
}

void bk_hiwonder_hand_servo_set_pulse_and_time(bk_hiwonder_hand_servo_handle_t handle,
                                               uint8_t id, uint16_t pulse_us, uint16_t time_ms)
{
    if (handle == NULL || id == 0 || id > handle->count ||
        pulse_us < 500 || pulse_us > 2500) {
        return;
    }

    if (time_ms < 20) {
        time_ms = 20;
    }
    if (time_ms > 30000) {
        time_ms = 30000;
    }

    if (id == 6) {
        if (pulse_us > 2500) {
            pulse_us = 2500;
        } else if (pulse_us < 500) {
            pulse_us = 500;
        }
    } else {
        if (pulse_us > 2200) {
            pulse_us = 2200;
        } else if (pulse_us < 900) {
            pulse_us = 900;
        }
    }

    handle->duty_set[id - 1] = pulse_us;
    handle->move_time_ms = time_ms;
    handle->duty_changed = true;
}

void bk_hiwonder_hand_servo_duty_compare(bk_hiwonder_hand_servo_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    if (handle->duty_changed) {
        handle->duty_changed = false;
        handle->ramp_remaining = handle->move_time_ms / BK_HIWONDER_HAND_SERVO_RAMP_PERIOD_MS;
        if (handle->ramp_remaining == 0) {
            handle->ramp_remaining = 1;
        }

        for (uint8_t idx = 0; idx < handle->count; idx++) {
            if (handle->duty_set[idx] > handle->duty[idx]) {
                handle->duty_inc[idx] = -(float)(handle->duty_set[idx] - handle->duty[idx]);
            } else {
                handle->duty_inc[idx] = (float)(handle->duty[idx] - handle->duty_set[idx]);
            }
            handle->duty_inc[idx] /= (float)handle->ramp_remaining;
        }
        handle->ramp_running = true;
    }

    if (!handle->ramp_running) {
        return;
    }

    handle->ramp_remaining--;
    for (uint8_t idx = 0; idx < handle->count; idx++) {
        if (handle->ramp_remaining == 0) {
            handle->duty[idx] = handle->duty_set[idx];
            handle->ramp_running = false;
        } else {
            handle->duty[idx] = handle->duty_set[idx] +
                (int16_t)(handle->duty_inc[idx] * (float)handle->ramp_remaining);
        }

        bk_hiwonder_hand_servo_hw_apply(handle, idx);
    }
}

/* ===================== Lifecycle ===================== */

bk_hiwonder_hand_servo_handle_t bk_hiwonder_hand_servo_init(
        const bk_hiwonder_hand_servo_hw_map_t *map, uint8_t count)
{
    bk_err_t ret;

    if (map == NULL) {
        LOGE("map is NULL\n");
        return NULL;
    }
    if (count == 0 || count > BK_HIWONDER_HAND_SERVO_MAX_ID) {
        LOGE("invalid servo count %u (max %u)\n", count, BK_HIWONDER_HAND_SERVO_MAX_ID);
        return NULL;
    }

    bk_hiwonder_hand_servo_handle_t h = bk_hiwonder_hand_servo_alloc_handle(count);
    if (h == NULL) {
        LOGE("alloc handle failed\n");
        return NULL;
    }

    for (uint8_t idx = 0; idx < count; idx++) {
        h->hw_map[idx]   = map[idx];
        h->duty_set[idx] = BK_HIWONDER_HAND_SERVO_DEFAULT_PULSE_US;
        h->duty[idx]     = BK_HIWONDER_HAND_SERVO_DEFAULT_PULSE_US;
        h->duty_inc[idx] = 0.0f;
        h->hw_inited[idx] = false;
    }
    h->move_time_ms = BK_HIWONDER_HAND_SERVO_DEFAULT_MOVE_MS;

    ret = rtos_init_timer(&h->ramp_timer, BK_HIWONDER_HAND_SERVO_RAMP_PERIOD_MS,
                          bk_hiwonder_hand_servo_ramp_timer_cb, h);
    if (ret != BK_OK) {
        LOGE("ramp timer init failed %d\n", ret);
        goto fail;
    }

    ret = rtos_start_timer(&h->ramp_timer);
    if (ret != BK_OK) {
        LOGE("ramp timer start failed %d\n", ret);
        rtos_deinit_timer(&h->ramp_timer);
        goto fail;
    }

    ret = bk_hiwonder_hand_servo_hw_init_all(h);
    if (ret != BK_OK) {
        rtos_stop_timer(&h->ramp_timer);
        rtos_deinit_timer(&h->ramp_timer);
        goto fail;
    }

    LOGI("hiwonder hand servo inited (%u servos)\n", h->count);
    return h;

fail:
    bk_hiwonder_hand_servo_free_handle(h);
    return NULL;
}

void bk_hiwonder_hand_servo_deinit(bk_hiwonder_hand_servo_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    rtos_stop_timer(&handle->ramp_timer);
    rtos_deinit_timer(&handle->ramp_timer);
    bk_hiwonder_hand_servo_hw_deinit_all(handle);
    bk_hiwonder_hand_servo_free_handle(handle);
}

/* ===================== Gesture presets ===================== */

#define BK_HIWONDER_HAND_SERVO_TEST_INTERVAL_MS    5000
#define BK_HIWONDER_HAND_SERVO_TEST_SERVO_COUNT     6

typedef struct {
    uint8_t id;
    uint16_t pulse_us;
} bk_hiwonder_hand_servo_target_t;

typedef struct {
    uint16_t time_ms;
    bk_hiwonder_hand_servo_target_t servos[BK_HIWONDER_HAND_SERVO_TEST_SERVO_COUNT];
} bk_hiwonder_hand_servo_preset_t;

static const bk_hiwonder_hand_servo_preset_t s_gesture_presets[] = {
    {
        .time_ms = 100,
        .servos = {{1, 2000}, {2, 900}, {3, 900}, {4, 900}, {5, 900}, {6, 1388}},
    },
    {
        .time_ms = 100,
        .servos = {{1, 925}, {2, 900}, {3, 900}, {4, 900}, {5, 900}, {6, 1388}},
    },
    {
        .time_ms = 100,
        .servos = {{1, 925}, {2, 1753}, {3, 1728}, {4, 1802}, {5, 1778}, {6, 1388}},
    },
    {
        .time_ms = 100,
        .servos = {{1, 2000}, {2, 1753}, {3, 1728}, {4, 900}, {5, 900}, {6, 1388}},
    },
    {
        .time_ms = 100,
        .servos = {{1, 2000}, {2, 1753}, {3, 900}, {4, 900}, {5, 900}, {6, 1388}},
    },
};

static void bk_hiwonder_hand_servo_apply_preset_internal(bk_hiwonder_hand_servo_handle_t handle,
                                                         const bk_hiwonder_hand_servo_preset_t *preset)
{
    LOGI("preset move_time=%u ms\n", preset->time_ms);

    for (uint8_t i = 0; i < BK_HIWONDER_HAND_SERVO_TEST_SERVO_COUNT; i++) {
        bk_hiwonder_hand_servo_set_pulse_and_time(handle,
                                                  preset->servos[i].id,
                                                  preset->servos[i].pulse_us,
                                                  preset->time_ms);
    }
}

void bk_hiwonder_hand_servo_apply_preset(bk_hiwonder_hand_servo_handle_t handle, uint8_t preset_id)
{
    if (handle == NULL ||
        preset_id >= (sizeof(s_gesture_presets) / sizeof(s_gesture_presets[0]))) {
        return;
    }

    bk_hiwonder_hand_servo_apply_preset_internal(handle, &s_gesture_presets[preset_id]);
}

void bk_hiwonder_hand_servo_test(bk_hiwonder_hand_servo_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    while (1) {
        for (uint32_t i = 0; i < sizeof(s_gesture_presets) / sizeof(s_gesture_presets[0]); i++) {
            bk_hiwonder_hand_servo_apply_preset_internal(handle, &s_gesture_presets[i]);
            rtos_delay_milliseconds(BK_HIWONDER_HAND_SERVO_TEST_INTERVAL_MS);
        }
    }
}
