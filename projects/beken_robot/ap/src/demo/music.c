/**
 * @file music.c
 * @brief Backend for the page_9 music demo. LVGL-free wrapper around
 *        audio_engine prompt-tone playback. The page UI hooks live in
 *        beken_generated/page_music/.
 */
#include "demo/music.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef ROBOT_TEST

#include "audio_engine_prompt_tone.h"
#include "test1_mp3_array.h"
#include <components/log.h>

#define TAG "music_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

extern audio_engine_prompt_tone_handle_t g_audio_engine_prompt_tone;

typedef enum {
    MUSIC_STATE_STOPPED = 0,
    MUSIC_STATE_PLAYING,
} music_state_t;

static music_state_t s_state = MUSIC_STATE_STOPPED;
static const char *s_track = "demo.mp3";

static int music_play_local_test1(void)
{
#if CONFIG_AE_SUPPORT_PROMPT_TONE && CONFIG_AE_PROMPT_TONE_SOURCE_ARRAY && CONFIG_AE_PROMPT_TONE_DECODER_MP3
    prompt_tone_uri_info_t info = {0};
    info.uri = (char *)test1_mp3;
    info.total_len = test1_mp3_len;
    int ret = audio_engine_prompt_tone_start(g_audio_engine_prompt_tone, &info);
    if (ret != 0) {
        LOGW("music play failed: %d\n", ret);
        return -1;
    }
    s_state = MUSIC_STATE_PLAYING;
    LOGI("music play demo.mp3\n");
    return 0;
#else
    LOGW("music requires prompt tone array + mp3 decoder\n");
    return -1;
#endif
}

int music_play(void)
{
    return music_play_local_test1();
}

int music_pause(void)
{
#if CONFIG_AE_SUPPORT_PROMPT_TONE
    int ret = audio_engine_prompt_tone_stop(g_audio_engine_prompt_tone);
    if (ret != 0) {
        LOGW("music stop failed: %d\n", ret);
        return -1;
    }
#else
    LOGW("prompt tone stop is disabled by config\n");
#endif
    s_state = MUSIC_STATE_STOPPED;
    LOGI("music stop\n");
    return 0;
}

int music_next(void)
{
    int ret = music_play_local_test1();
    if (ret == 0) {
        LOGI("music next(demo.mp3)\n");
    }
    return ret;
}

bool music_is_playing(void)
{
    return (s_state == MUSIC_STATE_PLAYING);
}

const char *music_get_track_name(void)
{
    return s_track;
}

int music_init(void) { return 0; }

extern int page_music_enter(void);

int music_start(void)
{
    return page_music_enter();
}

int music_stop(void)
{
    (void)music_pause();
    return 0;
}

#else  /* !ROBOT_TEST */

int music_play(void)  { return 0; }
int music_pause(void) { return 0; }
int music_next(void)  { return 0; }
bool music_is_playing(void) { return false; }
const char *music_get_track_name(void) { return ""; }
int music_init(void)  { return 0; }
int music_start(void) { return 0; }
int music_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_music = {
    .name  = "music",
    .init  = music_init,
    .start = music_start,
    .stop  = music_stop,
};
