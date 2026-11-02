#ifndef __AUDIO_PARA_H__
#define __AUDIO_PARA_H__

#include <stdint.h>

/*
 * Project ASR DOA decision: maps the AEC phase metric (phs_sm2) to the arrow
 * angle in degrees. Implemented in audio_para.c, registered by
 * sound_localization_init().
 */
int audio_param_doa_to_arrow(int32_t phs_sm2);

#endif /* __AUDIO_PARA_H__ */
