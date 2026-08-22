// Copyright 2020-2021 Beken
// Ported from doorbell reference project for beken_robot multimedia_device_service component.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "app_camera_types.h"
#include <common/avdk_pixel_types.h>
#include "avdk_error.h"

#define APP_ISP_MP_CHN_ID 0
#define APP_ISP_SP_CHN_ID 1

/* Ring (queued buffer) depth of the ISP SP channel opened by
 * app_isp_camera_sp_channel_turn_on(). The SP channel free-runs into this
 * ring; a consumer that reads it infrequently (e.g. the AI-camera shutter,
 * seconds apart) must first drain up to this many STALE frames before the
 * next read returns a fresh one -- otherwise it gets a frame captured right
 * after the PREVIOUS read (BK7259SW-3214). Kept in one place so the drain
 * count in camera_preview stays in lock-step with the actual buffer count. */
#define APP_ISP_SP_BUF_CNT 2

int app_isp_camera_turn_off(void);
bool app_isp_camera_state_get(void);
int app_isp_camera_soft_reset(void);
void *app_isp_handle_get(void);
int app_isp_mipi_camera_turn_on(const camera_board_config_t *config);
int app_isp_dvp_camera_turn_on(camera_parameters_ext_t *paramters);
int app_isp_camera_sp_channel_turn_on(const camera_board_config_t *config);
int app_isp_camera_channel_read(uint8_t channel, uint8_t *frame, uint32_t size, uint32_t timeout);

int app_camera_board_config_set(camera_board_config_t *config);
camera_board_config_t *app_camera_board_config_get(void);

/**
 * @brief Vote MIPI camera AuxLDOs (1.8V iovdd + 1.2V dvdd) on/off.
 *
 * Only MIPI sensors on this board need these two rails. DVP/UVC paths must NOT
 * call this helper. This is the single owner of PM_AUXLDO_USER_CAMERA; higher
 * layers MUST NOT vote PM_AUXLDO_USER_CAMERA themselves to avoid double voting.
 */
avdk_err_t app_mipi_camera_power_enable(bool enable);

#ifdef __cplusplus
}
#endif
