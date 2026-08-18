/**
 * @file volume.c
 * @brief Backend for the page_10 volume demo. LVGL-free wrapper around
 *        audio_engine volume controls. UI hooks live in
 *        beken_generated/page_volume/.
 */
#include "demo/volume.h"

#include <stddef.h>
#include <stdint.h>

#ifdef ROBOT_TEST

#include "audio_engine.h"
#include <components/log.h>

#define TAG "vol_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

int volume_increase(void)
{
    audio_engine_volume_increase();
    return 0;
}

int volume_decrease(void)
{
    audio_engine_volume_decrease();
    return 0;
}

int volume_set_level(uint8_t target)
{
    uint8_t max = audio_engine_volume_get_max_level();
    if (target > max) {
        target = max;
    }
    while (audio_engine_volume_get_level() < target) {
        uint8_t before = audio_engine_volume_get_level();
        audio_engine_volume_increase();
        if (audio_engine_volume_get_level() == before) {
            break;
        }
    }
    while (audio_engine_volume_get_level() > target) {
        uint8_t before = audio_engine_volume_get_level();
        audio_engine_volume_decrease();
        if (audio_engine_volume_get_level() == before) {
            break;
        }
    }
    return 0;
}

uint8_t volume_get_level(void) { return audio_engine_volume_get_level(); }
uint8_t volume_get_max(void)   { return audio_engine_volume_get_max_level(); }

int volume_init(void) { return 0; }

extern int page_volume_enter(void);

int volume_start(void)
{
    return page_volume_enter();
}

int volume_stop(void) { return 0; }

#else  /* !ROBOT_TEST */

int     volume_increase(void) { return 0; }
int     volume_decrease(void) { return 0; }
int     volume_set_level(uint8_t v) { (void)v; return 0; }
uint8_t volume_get_level(void) { return 0; }
uint8_t volume_get_max(void)   { return 0; }
int volume_init(void)  { return 0; }
int volume_start(void) { return 0; }
int volume_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_volume = {
    .name  = "volume",
    .init  = volume_init,
    .start = volume_start,
    .stop  = volume_stop,
};
