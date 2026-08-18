/**
 * @file bt_music.h
 * @brief Demo-menu entry for the Bluetooth music page (bk_demo_iface_t).
 *
 * UI lives in beken_generated/page_bt_music/; classic BT/A2DP/AVRCP in
 * a2dp_sink.c; hand rhythm in bt_rhythm.c.
 */
#pragma once

#include "demo_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

int bt_music_init(void);
int bt_music_start(void);
int bt_music_stop(void);

extern const bk_demo_iface_t g_demo_bt_music;

#ifdef __cplusplus
}
#endif
