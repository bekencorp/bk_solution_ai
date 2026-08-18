/**
 * @file ai_chat.h
 * @brief Backend API for the AI chat demo (page_6). The chat-animation
 *        widget and page-hook bindings live in
 *        beken_generated/page_ai_chat/.
 */
#ifndef __BK_DEMO_AI_CHAT_H__
#define __BK_DEMO_AI_CHAT_H__

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the AI text-mode service (returns 0 on success). */
int ai_chat_start_service(void);
/** Dispatch async AI exit (returns 0 on success). */
int ai_chat_request_exit(void);

int ai_chat_init(void);
int ai_chat_start(void);
int ai_chat_stop(void);

extern const bk_demo_iface_t g_demo_ai_chat;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_AI_CHAT_H__ */
