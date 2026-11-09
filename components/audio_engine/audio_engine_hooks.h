#ifndef __AUDIO_ENGINE_HOOKS_H__
#define __AUDIO_ENGINE_HOOKS_H__

#include <stdint.h>

/*
 * Project contract of the audio engine. Everything here is implemented or
 * registered by the project; the engine only calls it when the related
 * feature is enabled.
 */

/* ==================== Beken tuning tool (audio_param) ================= */

/*
 * Called after the voice service starts and before it stops, so the project
 * can bind/unbind its audio parameter preset. Requires both
 * CONFIG_AE_AUDIO_PARAM and CONFIG_AUD_PARAM_CTRL.
 */
void media_audio_param_bind_voc_handle(void *voc_handle, uint32_t sample_rate);

void media_audio_param_unbind_voc_handle(void);

/* ============================ DOA to arrow ============================ */

/*
 * AEC only reports the raw direction metric (phs_sm2); the thresholds and the
 * decision logic itself (hysteresis, voting, 2mic/3mic fusion, ...) are board
 * specific. The engine always runs its default decision and a project installs
 * its own callback when its mapping differs.
 */
typedef int (*audio_engine_doa_to_arrow_callback_t)(int32_t phs_sm2);

/* Threshold bundle used by audio_engine_doa_to_arrow_default(). */
typedef struct {
    int32_t phs_sm2_min;        /* defensive lower clamp */
    int32_t phs_sm2_max;        /* defensive upper clamp */
    int32_t phs_sm2_right_max;  /* <= this -> right */
    int32_t phs_sm2_left_min;   /* >= this -> left  */

    int32_t arrow_angle_right;
    int32_t arrow_angle_center;
    int32_t arrow_angle_left;

    int32_t reserved[9];        /* keep zero; reserved for future use */
} audio_engine_doa_arrow_cfg_t;

/* Install the project callback; NULL restores the default decision. */
void audio_engine_register_doa_to_arrow_callback(audio_engine_doa_to_arrow_callback_t callback);

/*
 * Default decision: clamp phs_sm2 into [min, max] and classify it as
 * right/center/left. NULL cfg selects the built-in defaults. Exported so a
 * project callback can reuse it with its own thresholds.
 *
 * @return Arrow angle in degrees
 */
int audio_engine_doa_to_arrow_default(int32_t phs_sm2, const audio_engine_doa_arrow_cfg_t *cfg);

/* ============================ DOA metric ============================== */

#define AUDIO_ENGINE_DOA_METRIC_VERSION     (0x0001)

/*
 * Full direction metric set reported at wake-word hit. Projects with three
 * microphones get all three pair metrics; dual-mic projects only see phs_sm2
 * (phs02 / phs12 stay 0).
 */
typedef struct {
    uint16_t version;
    uint16_t size;
    uint8_t  mic_num;           /* 1..3, derived from the board ch_bitmap */
    uint8_t  vad_state;         /* 1 = SPEECH_START, 0 = otherwise */
    int8_t   reserved0[2];
    int32_t  phs_sm2;           /* mic0 - mic1 */
    int32_t  phs02;             /* mic0 - mic2, 0 when mic_num < 3 */
    int32_t  phs12;             /* mic1 - mic2, 0 when mic_num < 3 */
    uint8_t  reserved[8];       /* keep zero; reserved for future use */
} audio_engine_doa_metric_t;

typedef int (*audio_engine_doa_metric_callback_t)(const audio_engine_doa_metric_t *metric);

/*
 * Install the project metric callback; NULL restores the legacy path.
 * Takes priority over audio_engine_register_doa_to_arrow_callback().
 *
 * @return Arrow angle in degrees returned by the callback
 */
void audio_engine_register_doa_metric_callback(audio_engine_doa_metric_callback_t callback);

#endif /* __AUDIO_ENGINE_HOOKS_H__ */
