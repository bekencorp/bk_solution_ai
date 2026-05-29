/**
 * @file page_chat_anim.h
 * @brief LVGL animation widget for AI chat (page_6) and AI chat + vision
 *        recognition (page_7) demo pages.
 *
 * Renders a tech-abstract scene (glowing core, concentric breathing ripples,
 * bottom EQ bars; plus a viewfinder + scan-line + REC dot in vision mode) and
 * subscribes to app_event AGENT_* / RTC_* / AI_* messages to switch its
 * runtime state.
 *
 * Lifecycle:
 *   page_chat_anim_attach()     called from page init under display lock
 *   page_chat_anim_set_state()  optional explicit state push (lock taken
 *                               internally)
 *   page_chat_anim_detach()     called from page destroy under display lock
 */
#ifndef __PAGE_CHAT_ANIM_H__
#define __PAGE_CHAT_ANIM_H__

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PAGE_CHAT_ANIM_MODE_VOICE = 0,   /**< AI chat only (page_6) */
    PAGE_CHAT_ANIM_MODE_VISION,      /**< AI chat + vision recognition (page_7) */
} page_chat_anim_mode_t;

typedef enum {
    PAGE_CHAT_STATE_CONNECTING = 0,  /**< default after attach, slow breathing */
    PAGE_CHAT_STATE_IDLE,            /**< AGENT_JOINED, agent ready */
    PAGE_CHAT_STATE_LISTENING,       /**< user speaking (VAD active) */
    PAGE_CHAT_STATE_THINKING,        /**< model inference, between user-stop and TTS */
    PAGE_CHAT_STATE_SPEAKING,        /**< remote TTS playing */
    PAGE_CHAT_STATE_ERROR,           /**< RTC lost / agent offline / start fail */
} page_chat_anim_state_t;

/**
 * @brief Build the chat animation tree under @p parent and start animations.
 *
 * Idempotent: a previous attach is detached automatically. Caller MUST hold
 * the LVGL display lock (this is the case when called from page init paths
 * routed through ui_nav_dispatch_event).
 *
 * Initial state after attach is PAGE_CHAT_STATE_CONNECTING; no separate
 * set_state call is required.
 */
void page_chat_anim_attach(lv_obj_t *parent, page_chat_anim_mode_t mode);

/**
 * @brief Stop all animations / timers, delete LVGL objects, and stop
 *        forwarding app_event messages to the UI.
 *
 * Caller MUST hold the LVGL display lock (true on page destroy paths).
 * Safe to call when not attached.
 */
void page_chat_anim_detach(void);

/**
 * @brief Push a UI state explicitly. Takes the LVGL display lock internally,
 *        so caller MUST NOT already hold it. Intended for use from
 *        unrelated threads (e.g. app_event handlers).
 */
void page_chat_anim_set_state(page_chat_anim_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* __PAGE_CHAT_ANIM_H__ */
