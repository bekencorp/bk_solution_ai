/**
 * @file demo/a2dp_sink.h
 * @brief Robot-compatible a2dp_sink facade used by page_bt_music.c.
 *        On the headset these are implemented inside a2dp_sink_demo.c
 *        (thin wrappers over bt_manager / bk_avrcp_ct / the sink task).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int  a2dp_sink_demo_is_active(void);
int  a2dp_sink_demo_is_connected(void);
void a2dp_sink_demo_audio_spk_enable(uint8_t enable);

void a2dp_sink_demo_vol_up(void);
void a2dp_sink_demo_vol_down(void);
void a2dp_sink_demo_prev(void);
void a2dp_sink_demo_next(void);
void a2dp_sink_demo_play(void);
void a2dp_sink_demo_pause(void);
void a2dp_sink_demo_enter_pairing(void);

/* Playback (play/pause) and stream (a2dp streaming on/off) listeners. */
void a2dp_sink_demo_set_playback_listener(void (*on_start)(void), void (*on_stop)(void));
void a2dp_sink_demo_set_stream_listener(void (*on_start)(void), void (*on_stop)(void));

/* Robot defers BT teardown when leaving the page; single-page product never
 * leaves, so this is a no-op on the headset. */
void a2dp_sink_demo_begin_teardown(void);

#ifdef __cplusplus
}
#endif
