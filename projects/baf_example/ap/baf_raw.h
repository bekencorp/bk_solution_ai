#ifndef BAF_RAW_H
#define BAF_RAW_H

#include <stdbool.h>
#include <avdk_error.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the raw (no-LVGL) BAF backend: bring up the DPU panel and the GPU, then
 * spawn a render thread that decodes hello_bk_baf_source, GPU-composes each
 * frame into a linear ARGB8888 framebuffer and flushes it straight to the panel.
 */
avdk_err_t baf_raw_start(void);

/* Toggle free-run (max-speed) playback on the topmost layer's decoder. Returns
 * AVDK_ERR_INVAL if the render thread has no open decoder yet. */
avdk_err_t baf_raw_set_freerun(bool enable);

/* Enter SCENE mode and select a preset layer stack: 1 = avatar only,
 * 2 = background + avatar (default), 3 = background + avatar + curtain. Any custom
 * SD-card layers are cleared. Returns AVDK_ERR_INVAL for an out-of-range scene or
 * if the render thread is not running. */
avdk_err_t baf_raw_set_scene(int scene);

/* CUSTOM mode: stack SD-card .baf files onto layer slots (0 = back).
 *  - A real @path assigns that file to layer @index and enters CUSTOM mode; the
 *    first assignment coming from SCENE mode wipes all slots so only SD files are
 *    shown (no preset leftovers).
 *  - @path NULL/""/"clear"/"none"/"default" clears layer @index; valid only in
 *    CUSTOM mode. When every slot is empty, only the grey background is shown.
 * Returns AVDK_ERR_INVAL for a bad index, a clear outside CUSTOM mode, or if the
 * render thread is not running. */
avdk_err_t baf_raw_set_layer_file(int index, const char *path);

/* Maximum number of stacked layers the compositor supports (capped by GPU/PSRAM
 * budget: each layer is an independent decoder ~1.5 MB PSRAM + one compose pass
 * per displayed frame). */
#define BAF_MAX_LAYERS 3

#ifdef __cplusplus
}
#endif

#endif /* BAF_RAW_H */
