/**
 * @file volume.h
 * @brief Backend API for the page_10 volume demo. UI hooks live in
 *        beken_generated/page_volume/.
 */
#ifndef __BK_DEMO_VOLUME_H__
#define __BK_DEMO_VOLUME_H__

#include <stdint.h>

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

int volume_init(void);
int volume_start(void);
int volume_stop(void);

int     volume_increase(void);
int     volume_decrease(void);
int     volume_set_level(uint8_t target);
uint8_t volume_get_level(void);
uint8_t volume_get_max(void);

extern const bk_demo_iface_t g_demo_volume;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_VOLUME_H__ */
