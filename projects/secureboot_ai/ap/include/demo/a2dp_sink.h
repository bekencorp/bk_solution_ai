#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/** Bring up classic BT manager + A2DP/AVRCP sink and enter pairing/reconnect. */
int a2dp_sink_demo_start(uint8_t aac_supported, uint8_t auto_accept_conn);

/** Tear down audio, profiles, worker thread and classic BT manager. */
int a2dp_sink_demo_stop(void);

int32_t a2dp_sink_demo_wait_player_end(void);

void a2dp_sink_demo_audio_spk_enable(uint8_t enable);

/** Non-zero when the BT music stack (manager + sink) is active. */
uint8_t a2dp_sink_demo_is_active(void);

/** Non-zero when an A2DP source (phone) is currently connected. */
uint8_t a2dp_sink_demo_is_connected(void);

void a2dp_sink_demo_begin_teardown(void);

/** Queue AVRCP transport on the a2dp_sink worker (safe from LVGL / key task). */
void a2dp_sink_demo_play(void);
void a2dp_sink_demo_pause(void);
void a2dp_sink_demo_next(void);
void a2dp_sink_demo_prev(void);
void a2dp_sink_demo_vol_up(void);
void a2dp_sink_demo_vol_down(void);

/** AVRCP playback notifications for UI state. */
typedef void (*a2dp_sink_playback_fn_t)(void);
void a2dp_sink_demo_set_playback_listener(a2dp_sink_playback_fn_t on_start,
                                          a2dp_sink_playback_fn_t on_stop);

/** A2DP stream notifications for audio-driven rhythm state. */
void a2dp_sink_demo_set_stream_listener(a2dp_sink_playback_fn_t on_start,
                                        a2dp_sink_playback_fn_t on_stop);

int32_t a2dp_sink_demo_try_disconnect_current(void);

#ifdef __cplusplus
}
#endif
