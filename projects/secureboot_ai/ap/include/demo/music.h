/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Public backend API for the page_9 music demo.
 *
 * Per the BK7259SW-1550 UI/Demo split convention: ap/src/demo/music.c is
 * the ONLY entry point that the UI layer (ap/beken_generated/page_music/)
 * may call. Everything below it (ui_music_storage, ui_music_pipeline,
 * audio_engine_play, FatFS, USB switching) is private to the demo and
 * stays inside ap/src/demo/.
 *
 * Lifecycle (driven by bk_demo_iface_t):
 *   music_init()  -> register CLI / lazy init only, no DAC use
 *   music_start() -> page_music_enter() (UI brings up the page)
 *   music_stop()  -> halt playback, free DAC
 *
 * Control surface (called from page_music_hooks.c):
 *   music_play / _pause / _next / _prev
 *
 * UI query surface (called from page_music_hooks.c lv_timer):
 *   music_is_playing
 *   music_get_track_name        -- name of CURRENT track
 *   music_get_track_count
 *   music_get_current_index
 *   music_get_track_name_at(idx)
 *   music_get_elapsed_sec / _total_sec
 *   music_get_spectrum_bins(out, n)   -- 16-bin VU bar source
 */
#ifndef __BK_DEMO_MUSIC_H__
#define __BK_DEMO_MUSIC_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int  music_init(void);
int  music_start(void);
int  music_stop(void);

int  music_play(void);
int  music_pause(void);
int  music_next(void);
int  music_prev(void);

bool        music_is_playing(void);
const char *music_get_track_name(void);

int         music_get_track_count(void);
int         music_get_current_index(void);
const char *music_get_track_name_at(int idx);

uint32_t    music_get_elapsed_sec(void);
uint32_t    music_get_total_sec(void);

/* Fill @out with @n bars (0..100) derived from the current speaker
 * envelope. Stable across calls when not playing (decays to 0). */
void        music_get_spectrum_bins(uint8_t *out, int n);

extern const bk_demo_iface_t g_demo_music;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_MUSIC_H__ */
