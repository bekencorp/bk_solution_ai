/**
 * @file asr.c
 * @brief Backend for the page_8 ASR (speech-recognition) demo. LVGL-free:
 *        starts/stops audio_engine ASR and exposes the latest
 *        APP_EVT_ASR_* trigger (set by an app_event subscription) for the
 *        UI hooks to poll. All LVGL work happens in
 *        beken_generated/page_asr/page_asr_hooks.c.
 */
#include "demo/asr.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef ROBOT_TEST

#include "audio_engine.h"
#include <components/log.h>

#if CONFIG_APP_EVT
#include "app_event.h"
#endif

#define TAG "asr_demo"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

/* 0:none, 1:nihaobotong, 2:zaijianbotong */
static volatile int s_phrase_trigger;

bool asr_consume_phrase_trigger(char *buf, int buf_len)
{
    int trigger = s_phrase_trigger;
    if (buf == NULL || buf_len <= 0 || trigger == 0) {
        return false;
    }
    if (trigger == 1) {
        snprintf(buf, buf_len, "nihaobotong");
    } else if (trigger == 2) {
        snprintf(buf, buf_len, "zaijianbotong");
    } else {
        return false;
    }
    s_phrase_trigger = 0;
    return true;
}

void asr_reset_phrase_trigger(void)
{
    s_phrase_trigger = 0;
}

int asr_start_service(void)
{
#if (CONFIG_ASR_SERVICE)
    if (!audio_engine_is_running() && audio_engine_init() != AUDIO_ENGINE_SUCCESS) {
        LOGI("ASR restore audio engine failed\r\n");
        return -1;
    }
    return (AUDIO_ENGINE_SUCCESS == audio_engine_asr_start()) ? 0 : -1;
#else
    return 0;
#endif
}

int asr_stop_service(void)
{
#if (CONFIG_ASR_SERVICE)
    if (!audio_engine_is_running()) {
        return 0;
    }
    return (AUDIO_ENGINE_SUCCESS == audio_engine_stop()) ? 0 : -1;
#else
    return 0;
#endif
}

#if CONFIG_APP_EVT
static void asr_phrase_evt_cb(app_evt_msg_t *msg, void *user_data)
{
    (void)user_data;
    if (msg == NULL) {
        return;
    }
    if (msg->event == APP_EVT_ASR_NIHAOBOTONG) {
        s_phrase_trigger = 1;
    } else if (msg->event == APP_EVT_ASR_ZAIJIANBOTONG) {
        s_phrase_trigger = 2;
    }
}
#endif

int asr_init(void)
{
#if CONFIG_APP_EVT
    (void)app_event_register_handler(APP_EVT_ASR_NIHAOBOTONG,
                                     asr_phrase_evt_cb, NULL);
    (void)app_event_register_handler(APP_EVT_ASR_ZAIJIANBOTONG,
                                     asr_phrase_evt_cb, NULL);
#endif
    return 0;
}

extern int page_asr_enter(void);

int asr_start(void)
{
    LOGI("Speech recognition -> page_8\r\n");
    if (asr_start_service() != 0) {
        LOGI("page8 start asr failed\r\n");
        return -1;
    }
    LOGI("page8 start asr\r\n");
    if (page_asr_enter() != 0) {
        LOGI("page8 init failed, rollback asr\r\n");
        (void)asr_stop_service();
        return -1;
    }
    return 0;
}

int asr_stop(void)
{
    (void)asr_stop_service();
    return 0;
}

#else  /* !ROBOT_TEST */

bool asr_consume_phrase_trigger(char *buf, int buf_len)
{
    (void)buf; (void)buf_len;
    return false;
}
void asr_reset_phrase_trigger(void) {}
int  asr_start_service(void) { return 0; }
int  asr_stop_service(void)  { return 0; }
int  asr_init(void)  { return 0; }
int  asr_start(void) { return 0; }
int  asr_stop(void)  { return 0; }

#endif /* ROBOT_TEST */

const bk_demo_iface_t g_demo_asr = {
    .name  = "asr",
    .init  = asr_init,
    .start = asr_start,
    .stop  = asr_stop,
};
