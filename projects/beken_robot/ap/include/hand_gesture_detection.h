#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool hand_gesture_detection_is_active(void);
int hand_gesture_detection_exit_to_menu(void);

#ifdef __cplusplus
}
#endif
