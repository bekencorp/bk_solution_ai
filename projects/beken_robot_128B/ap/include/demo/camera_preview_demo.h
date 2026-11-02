/**
 * @file camera_preview_demo.h
 * @brief Camera preview demo (thin wrapper; the actual MIPI camera +
 *        GPU + JPEG + SD-NAND pipeline lives in src/demo/camera_preview.c).
 */
#ifndef __BK_DEMO_CAMERA_PREVIEW_H__
#define __BK_DEMO_CAMERA_PREVIEW_H__

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int camera_preview_demo_init(void);
int camera_preview_demo_start(void);
int camera_preview_demo_stop(void);

extern const bk_demo_iface_t g_demo_camera_preview;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_CAMERA_PREVIEW_H__ */
