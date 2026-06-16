/**
 * @file hand_gesture.h
 * @brief Hand-gesture overlay demo (HandGestureDetectionModel + Hiwonder hand servos).
 */

#pragma once

#include "demo_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

int hand_gesture_init(void);
int hand_gesture_start(void);
int hand_gesture_stop(void);
void hand_gesture_set_return_to_edge_ai(bool enable);

extern const bk_demo_iface_t g_demo_hand_gesture;

#ifdef __cplusplus
}
#endif
