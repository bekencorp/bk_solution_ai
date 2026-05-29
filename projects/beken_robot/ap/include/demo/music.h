/**
 * @file music.h
 * @brief Backend API for the page_9 music demo (audio_engine prompt
 *        tone wrapper). UI hooks live in beken_generated/page_music/.
 */
#ifndef __BK_DEMO_MUSIC_H__
#define __BK_DEMO_MUSIC_H__

#include <stdbool.h>

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int music_init(void);
int music_start(void);
int music_stop(void);

/** Play the bundled demo.mp3 (returns 0 on success). */
int music_play(void);
/** Stop playback (returns 0 on success). */
int music_pause(void);
/** Skip to the next track (currently re-plays demo.mp3). */
int music_next(void);

/** @return true when audio is currently playing. */
bool music_is_playing(void);
/** @return Current track display name (never NULL). */
const char *music_get_track_name(void);

extern const bk_demo_iface_t g_demo_music;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_MUSIC_H__ */
