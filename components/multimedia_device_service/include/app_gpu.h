// Copyright 2020-2021 Beken
// Ported from doorbell reference project for beken_robot multimedia_device_service component.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <components/bk_gpu_types.h>
#include "app_gpu_types.h"
#include "avdk_error.h"

avdk_err_t app_gpu_turn_on(gpu_board_config_t *config);
avdk_err_t app_gpu_turn_off(bk_gpu_ctlr_handle_t ctlr);

int app_gpu_board_config_set(gpu_board_config_t *config);
gpu_board_config_t *app_gpu_board_config_get(void);
bk_gpu_ctlr_handle_t app_gpu_handle_get(void);
avdk_err_t app_gpu_frame_free(void *ptr);
/* Shared VG-Lite HW lock (bk_gpu_global_*), independent of bk_gpu ctlr handle. */
avdk_err_t app_gpu_lock(void);
avdk_err_t app_gpu_unlock(void);

/* ---------------------------------------------------------------------------
 * Snapshot / freeze hooks for camera preview "take photo" feature.
 *
 *   app_gpu_arm_snapshot():
 *     Set a one-shot flag so that the next GPU output frame is copied into
 *     an independent PSRAM buffer, the copy is flushed to the DPU (so the
 *     screen freezes on this frame), and all subsequent GPU frames are
 *     silently dropped. Returns 0 immediately; the actual capture happens
 *     when the next frame arrives.
 *
 *   app_gpu_resume_live():
 *     Clear the freeze flag. The next live GPU frame is flushed normally to
 *     the DPU; when the DPU swaps to it, the snapshot buffer it has been
 *     scanning out is released back to the frame pool via the same
 *     bkmm_frame_free callback that owns regular GPU frames -- no manual
 *     free is needed and there is no double-free race.
 *
 *   app_gpu_drop_snapshot():
 *     Force-cancel any pending arm / freeze and release the snapshot buffer
 *     immediately. Intended for the stop path: panel/DPU are about to be
 *     torn down and we cannot rely on a future GPU frame to release the
 *     buffer for us.
 *
 *   app_gpu_is_frozen():
 *     true while the DPU is locked on the snapshot frame.
 *
 *   app_gpu_get_snapshot_buffer() / _size():
 *     Pointer/size of the photo currently held in PSRAM (NULL/0 if no
 *     snapshot active). Format matches the GPU output (compressed ARGB8888).
 * ------------------------------------------------------------------------- */
int      app_gpu_arm_snapshot(void);
int      app_gpu_resume_live(void);
int      app_gpu_drop_snapshot(void);
bool     app_gpu_is_frozen(void);
void    *app_gpu_get_snapshot_buffer(void);
uint32_t app_gpu_get_snapshot_size(void);

#ifdef __cplusplus
}
#endif
