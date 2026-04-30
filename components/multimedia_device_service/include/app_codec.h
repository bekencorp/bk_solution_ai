// Copyright 2020-2021 Beken
// Ported from doorbell reference project: H.264 hardware-flexa encoder bound to
// the ISP MP channel, used to feed the WiFi/RTP transmitter for "img_xfer".

#pragma once

#include <common/bk_err.h>
#include <components/bk_encode/bk_h264_encode_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Bring up the H.264 hardware-flexa encoder and bind it to the currently
 * running ISP MP channel (NV12, width/height taken from the ISP channel).
 *
 * Pre-conditions:
 *   - app_isp_mipi_camera_turn_on() (or equivalent) MUST have been called first
 *     so app_isp_handle_get() returns a valid handle and the ISP MP channel is
 *     producing NV12 with enable_flexa = 1.
 *
 * On success, encoded H.264 frames are pushed into the encoded-frame pool
 * (see multimedia_img_manager.h) and can be consumed by the transmitter via
 * bk_encoded_complete_data_request() / bk_encoded_complete_data_free_request().
 *
 * @return BK_OK on success, error code otherwise.
 */
int app_h264e_turn_on(void);

/** Tear down the H.264 encoder created by app_h264e_turn_on(). */
int app_h264e_turn_off(void);

/** @return The HW H.264 encoder handle, or NULL if not running. */
void *app_h264_encode_handle_get(void);

#ifdef __cplusplus
}
#endif
