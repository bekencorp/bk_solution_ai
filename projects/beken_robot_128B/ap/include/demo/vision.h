/**
 * @file vision.h
 * @brief Backend API for the vision recognition demo (page_7). The page
 *        UI hooks live in beken_generated/page_vision/.
 */
#ifndef __BK_DEMO_VISION_H__
#define __BK_DEMO_VISION_H__

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the vision recognition service (returns 0 on success). */
int vision_start_service(void);
/** Dispatch async vision exit (returns 0 on success). */
int vision_request_exit(void);

int vision_init(void);
int vision_start(void);
int vision_stop(void);

extern const bk_demo_iface_t g_demo_vision;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_VISION_H__ */
