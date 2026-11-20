/**
 * @file pet_detection.h
 * @brief Pet (cat/dog) detection overlay demo
 *        (PetDetectionModel + MIPI camera + GPU OSD).
 */

#pragma once

#include "demo_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

int pet_detection_init(void);
int pet_detection_start(void);
int pet_detection_stop(void);
void pet_detection_set_return_to_edge_ai(bool enable);

extern const bk_demo_iface_t g_demo_pet_detection;

#ifdef __cplusplus
}
#endif
