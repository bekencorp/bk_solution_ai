#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <common/bk_include.h>

#include "audio_engine_hooks.h"
#include "audio_para.h"

#define TAG "audio_para"
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#if CONFIG_AE_AUDIO_PARAM && CONFIG_AUD_PARAM_CTRL
#include <components/audio_param_ctrl.h>
#include <components/bk_audio/audio_algorithms/aec_v3_algorithm_v2.h>
#endif


/* ============ Customer audio parameters (tuning tool only) ============= */
#if CONFIG_AE_AUDIO_PARAM && CONFIG_AUD_PARAM_CTRL

/* ============================ EQ (DL, 16K) ============================ */
#define EQTotalNum 2
#define FILTER_PREGAIN_FRA_BITS (14)

//E0_freq_600_gain_n15_qval_1_type_1_LS
#define EQ0 1
#define EQ0A0 -1668051
#define EQ0A1 734106
#define EQ0B0 934084
#define EQ0B1 -1715175
#define EQ0B2 801474
#define EQ0FREQ 0x44160000
#define EQ0GAIN 0xc1700000
#define EQ0QVAL 0x3f800000
#define EQ0FTYPE 0x01

//E1_freq_700_gain_n1_qval_1_type_0_PK
#define EQ1 1
#define EQ1A0 -1764716
#define EQ1A1 784980
#define EQ1B0 1034243
#define EQ1B1 -1764716
#define EQ1B2 799312
#define EQ1FREQ 0x442f0000
#define EQ1GAIN 0xbf800000
#define EQ1QVAL 0x3f800000
#define EQ1FTYPE 0x00

#define EQSAMP   0x3e80
#define EQFGAIN  0x00000000

/* With CONFIG_AE_AUDIO_PARAM on, this preset replaces the engine's
 * DEFAULT_EQ_ALGORITHM_CONFIG() coefficients when the voice service binds. */
#define CUST_EQ_PARA_DL_VOICE()                                                 \
{                                                                               \
    .app_eq_en = 1,                                                             \
    .eq_en = 1,                                                                 \
    .filters = EQTotalNum,                                                      \
    .globle_gain = (int32_t)(1.12f * (1 << FILTER_PREGAIN_FRA_BITS)),           \
    .eq_para[0].a[0] = -EQ0A0,                                                  \
    .eq_para[0].a[1] = -EQ0A1,                                                  \
    .eq_para[0].b[0] = EQ0B0,                                                   \
    .eq_para[0].b[1] = EQ0B1,                                                   \
    .eq_para[0].b[2] = EQ0B2,                                                   \
    .eq_para[1].a[0] = -EQ1A0,                                                  \
    .eq_para[1].a[1] = -EQ1A1,                                                  \
    .eq_para[1].b[0] = EQ1B0,                                                   \
    .eq_para[1].b[1] = EQ1B1,                                                   \
    .eq_para[1].b[2] = EQ1B2,                                                   \
    .eq_load.f_gain     = EQFGAIN,                                              \
    .eq_load.samplerate = EQSAMP,                                               \
    .eq_load.eq_load_para[0].freq   = EQ0FREQ,                                  \
    .eq_load.eq_load_para[0].gain   = EQ0GAIN,                                  \
    .eq_load.eq_load_para[0].q_val  = EQ0QVAL,                                  \
    .eq_load.eq_load_para[0].type   = EQ0FTYPE,                                 \
    .eq_load.eq_load_para[0].enable = EQ0,                                      \
    .eq_load.eq_load_para[1].freq   = EQ1FREQ,                                  \
    .eq_load.eq_load_para[1].gain   = EQ1GAIN,                                  \
    .eq_load.eq_load_para[1].q_val  = EQ1QVAL,                                  \
    .eq_load.eq_load_para[1].type   = EQ1FTYPE,                                 \
    .eq_load.eq_load_para[1].enable = EQ1,                                      \
}

/* ======================== SYS (mic/spk gains) ========================= */
#define CUST_SYS_CONFIG_VOICE()                                                 \
{                                                                               \
    .app_sys_en = 1,                                                            \
    .mic0_digital_gain = 16,                                                    \
    .mic0_analog_gain  = 20,                                                    \
    .mic1_digital_gain = 16,                                                    \
    .mic1_analog_gain  = 20,                                                    \
    .mic2_digital_gain = 16,                                                    \
    .mic2_analog_gain  = 20,                                                    \
    .spk0_digital_gain = -7,                                                    \
    .spk0_analog_gain  = 4,                                                     \
}

/* ============================== AEC V3 ================================ */
#define CUST_AEC_V3_CONFIG_VOICE()                                              \
{                                                                               \
    .app_aec_en = 1,                                                            \
    .aec_enable = 1,                                                            \
    .init_flags = 0x1F,                                                         \
    .ec_filter  = 0x7,                                                          \
    .ec_depth   = 0xa,                                                          \
    .mic_delay  = 16,                                                           \
    .drc_gain   = 0,                                                            \
    .voice_vol  = 0xd,                                                          \
    .ref_scale  = 0,                                                            \
    .ns_level   = 7,                                                            \
    .ns_para    = 2,                                                            \
    .ns_filter  = 0x7,                                                          \
    .ns_type    = NS_TRADITION,                                                 \
    .vad_enable = 1,                                                            \
    .vad_start_threshold   = 480,                                               \
    .vad_stop_threshold    = 960,                                               \
    .vad_silence_threshold = 320,                                               \
    .vad_eng_threshold     = 500,                                               \
    .dual_mic_enable   = 1,                                                     \
}

static app_aud_para_t app_aud_cust_voice_16k_para =
{
    .service_type  = AUD_SERVICE_AI_VOC,
    .sys_config    = CUST_SYS_CONFIG_VOICE(),
    .aec_v3_config = CUST_AEC_V3_CONFIG_VOICE(),
    .eq_dl_config  = CUST_EQ_PARA_DL_VOICE(),
};

static app_aud_para_t *get_app_aud_cust_para(app_aud_service_type_t service_type, uint32_t sample_rate)
{
    if (service_type != AUD_SERVICE_AI_VOC)
    {
        return NULL;
    }

    /* AI audio engine runs at 16K; the DL EQ preset is a 16K curve. */
    if (sample_rate != 16000)
    {
        LOGE("unexpected sample rate %u, use 16K EQ preset\n", sample_rate);
    }

    return &app_aud_cust_voice_16k_para;
}

void media_audio_param_bind_voc_handle(void *voc_handle, uint32_t sample_rate)
{
    app_aud_para_t *para = get_app_aud_cust_para(AUD_SERVICE_AI_VOC, sample_rate);

    if (!voc_handle || !para)
    {
        LOGE("audio param bind: invalid handle or para\n");
        return;
    }

    if (BK_OK != bk_app_aud_service_bind(AUD_SERVICE_AI_VOC, voc_handle, NULL, NULL, para))
    {
        LOGE("audio param bind failed\n");
    }
}

void media_audio_param_unbind_voc_handle(void)
{
    bk_app_aud_service_unbind(AUD_SERVICE_AI_VOC);
}

#endif /* CONFIG_AE_AUDIO_PARAM && CONFIG_AUD_PARAM_CTRL */

/* ======================= ASR DOA / arrow mapping ====================== */
/*
 * Board-dependent thresholds: the AEC only reports the raw phase metric
 * (phs_sm2), turning it into an arrow angle is product specific. The values
 * below currently equal the audio_engine defaults and must be replaced with
 * whole-product measurements.
 *
 * NOTE: registration is done by sound_localization_init(), so this preset only
 * takes effect while the demo/UI tree (CONFIG_ROBOT_TEST / CONFIG_LVGL) is
 * built in; otherwise the engine keeps its own default decision.
 */
#if CONFIG_BK_AUDIO_ENGINE
static const audio_engine_doa_arrow_cfg_t s_doa_arrow_cfg =
{
    .phs_sm2_min        = -16384,
    .phs_sm2_max        =  20000,
    .phs_sm2_right_max  =      0,
    .phs_sm2_left_min   =   5000,
    .arrow_angle_right  =     45,
    .arrow_angle_center =     90,
    .arrow_angle_left   =    135,
};

int audio_param_doa_to_arrow(int32_t phs_sm2)
{
    return audio_engine_doa_to_arrow_default(phs_sm2, &s_doa_arrow_cfg);
}
#endif /* CONFIG_BK_AUDIO_ENGINE */

/* ============================ A2DP hooks ============================== */
/*
 * The SDK A2DP downlink path (bk_bluetooth/service/dm/a2dp/a2dp_sink_audio.c)
 * calls these project hooks whenever CONFIG_AUD_PARAM_CTRL is enabled. The AI
 * solution only tunes the AI voice service, so keep the A2DP music path on its
 * create-time default EQ (fill_eq returns 1) and leave the service unbound.
 */
#if CONFIG_AUD_PARAM_CTRL
struct audio_play;
struct _app_eq_t;

void bt_a2dp_audio_bind(struct audio_play *play, uint32_t sample_rate)
{
    (void)play;
    (void)sample_rate;
}

void bt_a2dp_audio_unbind(void)
{
}

int bt_a2dp_audio_fill_eq(struct _app_eq_t *eq_cal_para, uint32_t sample_rate)
{
    (void)eq_cal_para;
    (void)sample_rate;
    return 1;
}
#endif /* CONFIG_AUD_PARAM_CTRL */
