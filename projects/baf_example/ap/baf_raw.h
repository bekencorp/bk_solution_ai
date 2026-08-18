#ifndef BAF_RAW_H
#define BAF_RAW_H

#include <stdbool.h>
#include <avdk_error.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the raw (no-LVGL) BAF backend: bring up the DPU panel and the GPU, then
 * spawn a render thread that decodes sample_bk_baf_source, GPU-composes each
 * frame into a linear ARGB8888 framebuffer and flushes it straight to the panel.
 */
avdk_err_t baf_raw_start(void);

/* Toggle free-run (max-speed) playback on the running RAW decoder. Returns
 * AVDK_ERR_INVAL if the render thread has no open decoder yet. */
avdk_err_t baf_raw_set_freerun(bool enable);

/* Switch playback to a .baf file on the mounted filesystem (e.g.
 * "1:/baf/dizzy.baf"). The render thread loads the file and swaps sources at the
 * next frame; on failure it keeps the current animation. Returns AVDK_ERR_INVAL
 * for a bad path or if the RAW render thread is not running. */
avdk_err_t baf_raw_play_file(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* BAF_RAW_H */
