/**
 * @file page_vision_preview.h
 * @brief LVGL camera preview widget for page_7.
 */
#ifndef __PAGE_VISION_PREVIEW_H__
#define __PAGE_VISION_PREVIEW_H__

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void page_vision_preview_attach(lv_obj_t *parent);
void page_vision_preview_detach(void);

#ifdef __cplusplus
}
#endif

#endif /* __PAGE_VISION_PREVIEW_H__ */
