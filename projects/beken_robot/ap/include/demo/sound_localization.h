/**
 * @file sound_localization.h
 * @brief Backend API for the page_5 sound source localization demo:
 *        thread-safe angle setter (consumed by audio_engine DOA) and
 *        audio_engine ASR start/stop wrappers. UI hooks live in
 *        beken_generated/page_doa/.
 */
#ifndef __BK_DEMO_SOUND_LOCALIZATION_H__
#define __BK_DEMO_SOUND_LOCALIZATION_H__

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int sound_localization_init(void);
int sound_localization_start(void);
int sound_localization_stop(void);

/**
 * @brief UI sink: the page hook in beken_generated/page_doa registers a
 *        function that applies @p degrees to the lv_arc widget. May be
 *        called from any task, so the sink must take the LVGL lock.
 */
typedef void (*sound_loc_ui_sink_t)(int degrees);
void sound_localization_register_ui_sink(sound_loc_ui_sink_t sink);

/**
 * @brief Set the arrow/KNOB angle. Updates the cached value and (if the
 *        UI is alive) pushes the new value into the LVGL widget.
 *
 * @note  Thread-safe (acquires lv_vendor_disp_lock internally when the
 *        UI is alive).
 */
void sound_localization_set_angle(int degrees);
int  sound_localization_get_angle(void);

/** Start the audio_engine ASR pipeline that feeds the DOA estimator. */
int sound_localization_start_service(void);
/** Stop the audio_engine ASR pipeline. */
int sound_localization_stop_service(void);

extern const bk_demo_iface_t g_demo_sound_localization;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_SOUND_LOCALIZATION_H__ */
