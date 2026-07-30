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

static volatile bool s_phrase_pending;
static char s_phrase_text[32];

#if CONFIG_BEKEN_KWS
/* Keep this display-only table aligned with bk_kws_word_t in bk_kws_asr.h.
 * The UI demo intentionally avoids including the ASR service private header. */
enum {
    ASR_KWS_NONE = 0,
    ASR_KWS_ARMINO,
    ASR_KWS_BYEBYE,
    ASR_KWS_JINRUBIAODING,
    ASR_KWS_WANCHENGBIAODING,
    ASR_KWS_DAKASHEXIANG,
    ASR_KWS_GUANBISHEXIANG,
    ASR_KWS_SHANGXIADUNQI,
    ASR_KWS_ZUOYOUYAOBAI,
    ASR_KWS_QIANHOUBAIDONG,
    ASR_KWS_YAOTOUHUANGNAO,
    ASR_KWS_SHENLANYAO,
    ASR_KWS_DAZHAOHU,
    ASR_KWS_NAOYANGYANG,
    ASR_KWS_ZUOZHUANWAN,
    ASR_KWS_YOUZHUANWAN,
    ASR_KWS_QIANJIN,
    ASR_KWS_HOUTUI,
    ASR_KWS_ZUOXIA,
    ASR_KWS_AONAO,
    ASR_KWS_BAOBAO,
    ASR_KWS_SAJIAO,
    ASR_KWS_YOUYONG,
    ASR_KWS_SHENGQI,
    ASR_KWS_QIQIU,
    ASR_KWS_MAX_WORDS,
};

static const char *asr_kws_word_label(uint32_t word)
{
    switch (word) {
    case ASR_KWS_ARMINO: return "nihaobotong";
    case ASR_KWS_BYEBYE: return "zaijianbotong";
    case ASR_KWS_JINRUBIAODING: return "jinrubiaoding";
    case ASR_KWS_WANCHENGBIAODING: return "wanchengbiaoding";
    case ASR_KWS_DAKASHEXIANG: return "dakaShexiang";
    case ASR_KWS_GUANBISHEXIANG: return "guanbishexiang";
    case ASR_KWS_SHANGXIADUNQI: return "shangxiadunqi";
    case ASR_KWS_ZUOYOUYAOBAI: return "zuoyouyaobai";
    case ASR_KWS_QIANHOUBAIDONG: return "qianhoubaidong";
    case ASR_KWS_YAOTOUHUANGNAO: return "yaotouhuangnao";
    case ASR_KWS_SHENLANYAO: return "shenlanyao";
    case ASR_KWS_DAZHAOHU: return "dazhaohu";
    case ASR_KWS_NAOYANGYANG: return "naoyangyang";
    case ASR_KWS_ZUOZHUANWAN: return "zuozhuanwan";
    case ASR_KWS_YOUZHUANWAN: return "youzhuanwan";
    case ASR_KWS_QIANJIN: return "qianjin";
    case ASR_KWS_HOUTUI: return "houtui";
    case ASR_KWS_ZUOXIA: return "zuoxia";
    case ASR_KWS_AONAO: return "aonao";
    case ASR_KWS_BAOBAO: return "baobao";
    case ASR_KWS_SAJIAO: return "sajiao";
    case ASR_KWS_YOUYONG: return "youyong";
    case ASR_KWS_SHENGQI: return "shengqi";
    case ASR_KWS_QIQIU: return "qiqiu";
    default: return NULL;
    }
}
#endif

static void asr_set_phrase_text(const char *text)
{
    if (text == NULL) {
        return;
    }
    snprintf(s_phrase_text, sizeof(s_phrase_text), "%s", text);
    s_phrase_pending = true;
}

bool asr_consume_phrase_trigger(char *buf, int buf_len)
{
    if (buf == NULL || buf_len <= 0 || !s_phrase_pending) {
        return false;
    }
    snprintf(buf, buf_len, "%s", s_phrase_text);
    s_phrase_pending = false;
    return true;
}

void asr_reset_phrase_trigger(void)
{
    s_phrase_pending = false;
    s_phrase_text[0] = '\0';
}

int asr_start_service(void)
{
#if (CONFIG_ASR_SERVICE)
    if (!audio_engine_is_running() && audio_engine_init() != AUDIO_ENGINE_SUCCESS) {
        LOGI("ASR restore audio engine failed\r\n");
        return -1;
    }
#if CONFIG_BEKEN_KWS
    if (AUDIO_ENGINE_SUCCESS != audio_engine_asr_switch_model(AUDIO_ENGINE_KWS_MODEL_CMDS)) {
        LOGI("page8 switch kws cmd model failed\r\n");
        return -1;
    }
#endif
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
    int ret = (AUDIO_ENGINE_SUCCESS == audio_engine_stop()) ? 0 : -1;
#if CONFIG_BEKEN_KWS
    (void)audio_engine_asr_switch_model(AUDIO_ENGINE_KWS_MODEL_WAKEUP);
#endif
    return ret;
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
        asr_set_phrase_text("nihaobotong");
    } else if (msg->event == APP_EVT_ASR_ZAIJIANBOTONG) {
        asr_set_phrase_text("zaijianbotong");
#if CONFIG_BEKEN_KWS
    } else if (msg->event == APP_EVT_ASR_KWS_LABEL) {
        asr_set_phrase_text(asr_kws_word_label(msg->param));
#endif
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
    (void)app_event_register_handler(APP_EVT_ASR_KWS_LABEL,
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
