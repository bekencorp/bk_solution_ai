/**
 * @file voice_chat_page.h
 * @brief Voice-chat home tile page: big mic button + live PTT state +
 *        the latest Muse reply preview.
 */
#ifndef __BK_DEMO_VOICE_CHAT_PAGE_H__
#define __BK_DEMO_VOICE_CHAT_PAGE_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Create and show the voice-chat page (dynamic screen). */
int voice_chat_page_enter(void);

/** True while the voice-chat page is the active screen. muse_voice uses
 * this to keep replies inside the page's own preview label instead of
 * popping the fullscreen reply overlay on top of it. */
bool voice_chat_page_is_active(void);

/** Show a proactive (no-voice-turn) Muse push in the page's B-scene bubble:
 * appended to voice history and pinned visible for 30 s. Any task may call. */
void voice_chat_page_show_push(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_VOICE_CHAT_PAGE_H__ */
