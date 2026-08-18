// Copyright 2020-2021 Beken
// beken_robot media device controls -- DSI panel + DPU, MIPI camera + ISP,
// GPU rotate, H.264 encoder, plus a bring-up test thread.
//
// Independent sub-modules; each does one thing only. Caller composes them.
//
//   1) DSI panel + DPU            : media_lcd_panel_open / _close
//   2) MIPI camera + ISP          : media_camera_open / _close
//   3) GPU rotate (NV12->ARGB8888): media_gpu_open  / _close
//                                   (binds ISP MP -> GPU; needs (2))
//   4) H.264 hardware encoder     : media_h264_encoder_start / _stop
//                                   (binds to ISP MP; needs (2))
//
// Each open/close pair is called at most once per lifecycle; no idempotent
// re-open / repeated-call protection.
//
// To preview the camera on the panel: open the panel with
// (BK_PIXEL_FORMAT_ARGB8888, decompress=true), then bring up camera + GPU.
// The GPU completion callback in app_gpu.c flushes its compressed ARGB8888
// frames into the panel DPU automatically -- no extra wiring needed here.

#pragma once

#include <avdk_check.h>
#include <common/avdk_pixel_types.h>
#include <components/bk_display.h>
#include <os/os.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- board lcd and camera power on ---------- */
void media_board_power_on(void);

/* ---------- Panel + DPU ---------- */

/** Open the DSI panel + DPU. Backlight + 3.3V peripheral rail are turned on. */
avdk_err_t media_lcd_panel_open(const bk_display_dsi_panel_t *panel,
                            bk_pixel_format_t format,
                            bool decompress);

/** Close the panel + DPU opened by media_lcd_panel_open(). */
avdk_err_t media_lcd_panel_close(void);

/** @return DPU handle from the most recent media_lcd_panel_open(); NULL if not open. */
bk_display_ctlr_handle_t media_panel_get_dpu_handle(void);

/* ---------- MIPI camera + ISP ----------
 *
 * Brings up the MIPI CSI sensor + ISP MP channel only (NV12, flexa enabled).
 * Downstream consumers (H.264 encoder, GPU) attach onto this ISP MP through
 * their own _open APIs.
 */
avdk_err_t media_camera_open(uint16_t cam_w,
                             uint16_t cam_h,
                             uint16_t fps,
                             uint16_t isp_w,
                             uint16_t isp_h);

/** Close the camera + ISP pipeline. */
avdk_err_t media_camera_close(void);

/* ---------- GPU rotate (NV12 -> ARGB8888 compressed, hw flexa from ISP MP) ----------
 *
 * @p src_w / @p src_h must equal the ISP MP isp_w / isp_h passed to
 * media_camera_open() (no scaling). Does NOT touch the panel/DPU; to route
 * the output onto the panel, open the panel with
 * (BK_PIXEL_FORMAT_ARGB8888, decompress=true).
 *
 * Pre-condition: media_camera_open() was called.
 */
avdk_err_t media_gpu_open(uint16_t src_w, uint16_t src_h, uint16_t rotate_deg);
avdk_err_t media_gpu_close(void);

/* ---------- One-shot NV12 -> JPEG (HW VCENC, synchronous) ----------
 * Full encoder lifecycle per call (~50-150ms). Caller supplies output buffer.
 * quality: 0..10 (SDK jpeg_param.quality).
 */
avdk_err_t media_jpeg_encode_nv12_oneshot(const void *nv12_buf,
                                          uint16_t w,
                                          uint16_t h,
                                          uint8_t quality,
                                          void *out_jpeg_buf,
                                          uint32_t out_capacity,
                                          uint32_t *out_len);

/* ---------- ISP SP channel (HD photo capture) ----------
 * Second ISP channel, frame-mode NV12. Opened with preview; read on take_photo.
 * Pre-condition: media_camera_open() (MP running).
 */
avdk_err_t media_camera_sp_open(uint16_t sp_w, uint16_t sp_h);
avdk_err_t media_camera_sp_close(void);

/** @brief Blocking read of one SP NV12 frame into caller buffer. */
avdk_err_t media_camera_sp_read(uint8_t *buf, uint32_t size, uint32_t timeout_ms);

/* ---------- GPU snapshot / freeze (photo still on DPU) ---------- */
avdk_err_t media_gpu_arm_snapshot(void);
avdk_err_t media_gpu_resume_live(void);
avdk_err_t media_gpu_drop_snapshot(void);
bool       media_gpu_is_frozen(void);

/* ---------- H.264 encoder (img_xfer) ----------
 *
 * Binds the running ISP MP NV12 stream to the hardware H.264 encoder via
 * flexa. Encoded frames are pushed into the pool exposed by
 * <multimedia_img_manager.h> and consumed via
 * bk_encoded_complete_data_request() / bk_encoded_complete_data_free_request().
 * Encoded resolution/FPS == ISP MP resolution/FPS.
 *
 * Pre-condition: media_camera_open() has been called.
 */
avdk_err_t media_h264_encoder_start(void);
avdk_err_t media_h264_encoder_stop(void);

/* ---------- Bring-up test thread ----------
 *
 *   MEDIA_TEST_MODE_SPLASH       : legacy RGB565 splash test; panel must be
 *                                  opened as RGB565, decompress=false.
 *   MEDIA_TEST_MODE_H264_WIFI_TX : stand in for the WiFi transmitter -- pop
 *                                  encoded H.264 frames and free them.
 */
typedef enum {
    MEDIA_TEST_MODE_SPLASH       = 0,
    MEDIA_TEST_MODE_H264_WIFI_TX = 1,
} media_test_mode_t;

avdk_err_t media_test_thread_start(media_test_mode_t mode);
avdk_err_t media_test_thread_stop(void);

#ifdef __cplusplus
}
#endif
