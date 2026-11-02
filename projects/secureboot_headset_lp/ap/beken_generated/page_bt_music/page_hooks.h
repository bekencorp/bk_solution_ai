/**
 * @file page_hooks.h
 * @brief Headset stub: the robot build uses bk_page_attach_right_swipe_gesture
 *        to leave the music page back to the demo menu. The headset product
 *        keeps the music page forever, so the gesture is a no-op here.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void bk_page_attach_right_swipe_gesture(lv_obj_t *scr)
{
    (void)scr; /* single-page product: no menu to swipe back to */
}

#ifdef __cplusplus
}
#endif
