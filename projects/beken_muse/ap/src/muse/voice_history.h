/**
 * @file voice_history.h
 * @brief Ring buffer of recent Muse turn replies for the history page.
 *
 * muse_voice pushes one entry per completed turn (voice note or photo);
 * the LVGL history page reads them back. Thread-safe: push happens on the
 * voice worker, reads on the LVGL task.
 */
#ifndef __BK_MUSE_VOICE_HISTORY_H__
#define __BK_MUSE_VOICE_HISTORY_H__

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_HISTORY_MAX        8
#define VOICE_HISTORY_TITLE_MAX  32
#define VOICE_HISTORY_BODY_MAX   384

/** Append one entry (oldest evicted when full). NULL/empty body is ignored. */
void voice_history_push(const char *title, const char *body);

/** Number of stored entries (0..VOICE_HISTORY_MAX). */
int voice_history_count(void);

/** Copy out entry idx (0 = newest). Returns false when idx out of range. */
bool voice_history_get(int idx, char *title, size_t tcap, char *body, size_t bcap);

/** Drop all entries. */
void voice_history_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK_MUSE_VOICE_HISTORY_H__ */
