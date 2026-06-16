// Copyright 2025 Beken
// Lightweight camera preview: reuses palm_detection MIPI camera + GPU + DSI
// panel path without any NN model. ISP MP NV12 is rotated/converted to ARGB8888
// via GPU and displayed through FLEXA -> DPU.
//
// Usage (page_3 nav callbacks run on Tmr Svc with ~2 KB stack; APIs return
// immediately and heavy work runs on internal 16 KB workers):
//   - camera_preview_start(): lv_vendor_stop -> keep shared
//     ARGB8888+decompress panel -> open camera -> open GPU; rolls back on fail.
//   - camera_preview_stop(): close GPU -> close camera -> lv_vendor_start().
//
// DPU handle: the compressed DPU is shared with LVGL and kept alive across
// preview start/stop. ap_main LVGL flush fetches media_panel_get_dpu_handle()
// each frame, so callers need not sync LVGL state manually.
//
// State machine:
//   IDLE -> STARTING -> RUNNING --+-> STOPPING -> IDLE
//                                |
//                                +-> FROZEN --+-> RUNNING (resume_live)
//                                             +-> STOPPING -> IDLE (back key)
// RUNNING: take_photo / stop only. FROZEN: resume_live / stop only.
// Other states: all APIs are safe no-ops.
//
// Photo capture (dual path):
//   1) Display: MP 400x368 -> GPU -> DPU snapshot freeze (UI freeze).
//   2) HD: SP 1280x720 NV12 -> UNCODED PSRAM slab -> HW JPEG -> PSRAM_HEAP
//      for LVM / RTC.
//
// PSRAM layout (BK7259, 16 MB total, two mappings):
//   - UNCODED @ 0x60000000 / 16 MB: ISP/VCENC buffers. 720p NV12 ~1.32 MB/frame.
//   - PSRAM_HEAP @ 0x64000000 / 920 KB: JPEG output (cap 512 KB).
//
// MP and SP share the same sensor exposure (two scales); display and saved
// frames are content-aligned (may differ by up to one sensor period ~67 ms).

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter camera preview (async worker for LVGL/panel/camera/GPU switch).
 *
 * Returns immediately; is_running() becomes true when the worker finishes.
 * Duplicate calls while starting are ignored (return 0).
 *
 * @return 0 on success or already active; <0 if worker thread creation failed.
 */
int camera_preview_start(void);

/**
 * @brief Exit camera preview (async teardown + lv_vendor_start).
 *
 * Triggers worker only from RUNNING or FROZEN; other states are no-op.
 *
 * @return 0 on success or ignored; <0 if worker creation failed (sync rollback).
 */
int camera_preview_stop(void);

/** @brief True when live camera frames are displayed (RUNNING only). */
bool camera_preview_is_running(void);

/** @brief True in any non-IDLE state (blocks page_3 menu re-entry). */
bool camera_preview_is_active(void);

/** @brief True when display is frozen on a snapshot (FROZEN). */
bool camera_preview_is_frozen(void);

/**
 * @brief Take photo: freeze display + async HD NV12 capture.
 *
 * RUNNING only. Arms GPU snapshot (<= one frame) and starts SP worker for
 * 1280x720 NV12 + JPEG. State becomes FROZEN immediately.
 *
 * @return 0 if triggered; <0 on error.
 */
int camera_preview_take_photo(void);

/**
 * @brief Resume live preview from FROZEN; releases photo buffers.
 *
 * FROZEN only. Clears GPU freeze; next live frame replaces snapshot on DPU.
 *
 * @return 0 on success or ignored.
 */
int camera_preview_resume_live(void);

/**
 * @brief Get captured HD photo (NV12, uncompressed).
 *
 * Valid only in FROZEN after SP worker completes. Caller must not free; pointer
 * is invalid after resume_live() or stop().
 *
 * @return 0 on success; <0 if no photo available.
 */
int camera_preview_get_photo_nv12(void **buf, uint32_t *size,
                                  uint16_t *w, uint16_t *h);

/**
 * @brief Get captured HD photo (JPEG, HW-encoded for LVM).
 *
 * Same lifetime rules as get_photo_nv12(). May return -1 if JPEG encode failed
 * while NV12 is still available.
 *
 * @return 0 on success; <0 if no JPEG available.
 */
int camera_preview_get_photo_jpeg(void **buf, uint32_t *size);

/**
 * @brief Debug-only: power on SD-NAND and mount the FATFS volume once,
 *        for the rest of the app lifetime, so take_photo can persist
 *        each JPEG to disk with only mkdir + write per shot.
 *
 * Call this ONCE during app init (ap_main.c). After this returns:
 *   * NAND_VDD stays HIGH for the rest of the session (~mA of static
 *     current -- acceptable for a developer build).
 *   * "1:" stays mounted; each camera_preview_take_photo() lands a
 *     new "1:/photos/NNNN/photo.jpg" file when the photo worker
 *     completes its HW JPEG encode.
 *
 * There is intentionally NO matching deinit. An earlier design
 * unmounted + powered off SD-NAND on every camera_preview_stop(), but
 * that f_unmount + board_sd_nand_power_off pair reliably left the LCD
 * panel black after the LVGL panel reopen + lv_vendor_start. The root
 * cause is somewhere in {sdio_dwc::sdio_reset, gpio_dev_unmap(GPIO_53)
 * for the LCD_B2 second-func pin, NAND_VDD HIGH->LOW transient} and
 * has not been pinpointed; persisting NAND_VDD HIGH for the whole
 * session sidesteps the issue cleanly.
 *
 * Compiled out (to a no-op returning 0) when
 * CONFIG_CAM_PREVIEW_SDNAND_DEBUG=0 or when FATFS / SD-NAND power gate
 * are absent from defconfig. Failure is non-fatal: preview keeps
 * working without the debug persist (just no JPEGs land on disk).
 *
 * @return 0 on mount success or no-op build; <0 on mount failure
 *         (NAND_VDD is left LOW on failure to save idle current).
 */
int camera_preview_sdnand_debug_init(void);

#ifdef __cplusplus
}
#endif
