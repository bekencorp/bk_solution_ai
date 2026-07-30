/**
 * @file bt_music.h
 * @brief Bluetooth A2DP music + hand-rhythm demo backend.
 *
 * Brings the phone-connect-and-play-music (A2DP sink + AVRCP) feature into
 * beken_robot and links it to the bt_rhythm engine so the on-board hand dances
 * to whatever the phone plays. Follows the bk_demo_iface_t plug-in contract.
 *
 * The control helpers (play/pause, prev/next, volume, dance toggle)
 * are non-blocking: they post to an internal worker thread and are safe to call
 * from the LVGL UI thread.
 */
#pragma once

#include "demo_registry.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int bt_music_init(void);
int bt_music_start(void);
int bt_music_stop(void);

void bt_music_play_pause(void);
void bt_music_next(void);
void bt_music_prev(void);
void bt_music_vol_up(void);
void bt_music_vol_down(void);

void bt_music_dance_toggle(void);
bool bt_music_is_dancing(void);
bool bt_music_is_playing(void);

/** Latest per-joint pose levels (0..100 each): 5 fingers + 1 base. */
void bt_music_get_pose_levels(uint8_t *levels, uint8_t count);

/** True when a phone is connected over A2DP. */
bool bt_music_is_connected(void);

/** Called by the A2DP worker when the remote stream starts/stops. */
void bt_music_on_stream_start(void);
void bt_music_on_stream_stop(void);

/** Opens the speaker after LVGL has completed the first page paint. */
void bt_music_enable_speaker_after_first_paint(void);

extern const bk_demo_iface_t g_demo_bt_music;

#ifdef __cplusplus
}
#endif
