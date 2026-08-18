/**
 * @file asr.h
 * @brief Backend API for the page_8 ASR (speech-recognition) demo:
 *        audio_engine ASR control + APP_EVT_ASR_* -> phrase trigger
 *        bridge. The page UI hooks live in
 *        beken_generated/page_asr/.
 */
#ifndef __BK_DEMO_ASR_H__
#define __BK_DEMO_ASR_H__

#include <stdbool.h>

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int asr_init(void);
int asr_start(void);
int asr_stop(void);

/** Start audio_engine ASR (returns 0 on success). */
int asr_start_service(void);
/** Stop audio_engine ASR (returns 0 on success). */
int asr_stop_service(void);

/**
 * @brief Drain the latest APP_EVT_ASR_* trigger into @p buf.
 *
 * @return true  if a phrase was returned (buf contains a NUL-terminated string)
 * @return false if no new trigger was pending
 */
bool asr_consume_phrase_trigger(char *buf, int buf_len);

/** Reset any pending phrase trigger. */
void asr_reset_phrase_trigger(void);

extern const bk_demo_iface_t g_demo_asr;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_ASR_H__ */
