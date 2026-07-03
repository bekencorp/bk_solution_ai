#pragma once

#include <common/bk_include.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_hiwonder_car_init(void);
bk_err_t bk_hiwonder_car_run(float vx_mm_s, float angular_rate);
bk_err_t bk_hiwonder_car_set_pwm_servo(uint8_t servo_id,
                                       uint16_t pulse,
                                       uint16_t duration_ms);
bk_err_t bk_hiwonder_car_send_raw(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif
