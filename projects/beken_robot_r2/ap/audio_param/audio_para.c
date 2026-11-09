#include <os/os.h>
#include <common/bk_include.h>

#include "audio_engine_hooks.h"
#include "audio_para.h"

#define TAG "audio_para"
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#if CONFIG_AE_AUDIO_PARAM && CONFIG_AUD_PARAM_CTRL
#include <components/audio_param_ctrl.h>
#include <components/bk_audio/audio_algorithms/aec_v3_algorithm_v2.h>
#endif

#if CONFIG_BK_AUDIO_ENGINE
#include "audio_engine.h"

#if !CONFIG_ADK_ONBOARD_MIC_STREAM_V2
#error "CONFIG_ADK_ONBOARD_MIC_STREAM_V2 must be enabled for onboard mic"
#endif

#define BOARD_AUDIO_MIC_TYPE        AUDIO_ENGINE_MIC_ANALOG_DIFF
#define BOARD_AUDIO_CH_BITMAP       0x7
#define BOARD_AUDIO_DMIC_MODE       0
#define BOARD_AUDIO_DMIC_CLK_GPIO   (-1)
#define BOARD_AUDIO_DMIC_DATA_GPIO  (-1)
#define BOARD_AUDIO_AEC_DIST        2

static const audio_engine_board_cfg_t s_board_cfg = {
    .version        = AUDIO_ENGINE_BOARD_CFG_VERSION,
    .size           = sizeof(audio_engine_board_cfg_t),
    .mic_type       = BOARD_AUDIO_MIC_TYPE,
    .ch_bitmap      = BOARD_AUDIO_CH_BITMAP,
    .dmic_mode      = BOARD_AUDIO_DMIC_MODE,
    .dmic_clk_gpio  = BOARD_AUDIO_DMIC_CLK_GPIO,
    .dmic_data_gpio = BOARD_AUDIO_DMIC_DATA_GPIO,
    .aec_dist       = BOARD_AUDIO_AEC_DIST,
};

int audio_board_config_init(void)
{
    return audio_engine_set_board_cfg(&s_board_cfg);
}
#endif

#if CONFIG_AE_AUDIO_PARAM && CONFIG_AUD_PARAM_CTRL

/* ============================ EQ (DL, 16K) ============================ */
#define VOC_EQ_TOTAL_NUM 2
#define VOC_EQGAIN 16384

//E0_freq_600_gain_n15_qval_1_type_1_LS
#define VOC_EQ0 1
#define VOC_EQ0A0 -1668051
#define VOC_EQ0A1 734106
#define VOC_EQ0B0 934084
#define VOC_EQ0B1 -1715175
#define VOC_EQ0B2 801474
#define VOC_EQ0FREQ 0x44160000
#define VOC_EQ0GAIN 0xc1700000
#define VOC_EQ0QVAL 0x3f800000
#define VOC_EQ0FTYPE 0x01

//E1_freq_700_gain_n1_qval_1_type_0_PK
#define VOC_EQ1 1
#define VOC_EQ1A0 -1764716
#define VOC_EQ1A1 784980
#define VOC_EQ1B0 1034243
#define VOC_EQ1B1 -1764716
#define VOC_EQ1B2 799312
#define VOC_EQ1FREQ 0x442f0000
#define VOC_EQ1GAIN 0xbf800000
#define VOC_EQ1QVAL 0x3f800000
#define VOC_EQ1FTYPE 0x00

#define VOC_EQSAMP   0x3e80
#define VOC_EQFGAIN  0x00000000

#define CUST_EQ_PARA_DL_VOICE()                                                 \
{                                                                               \
    .app_eq_en = 1,                                                             \
    .eq_en = 1,                                                                 \
    .filters = VOC_EQ_TOTAL_NUM,                                                      \
    .globle_gain = VOC_EQGAIN,                                                      \
    .eq_para[0].a[0] = -VOC_EQ0A0,                                                  \
    .eq_para[0].a[1] = -VOC_EQ0A1,                                                  \
    .eq_para[0].b[0] = VOC_EQ0B0,                                                   \
    .eq_para[0].b[1] = VOC_EQ0B1,                                                   \
    .eq_para[0].b[2] = VOC_EQ0B2,                                                   \
    .eq_para[1].a[0] = -VOC_EQ1A0,                                                  \
    .eq_para[1].a[1] = -VOC_EQ1A1,                                                  \
    .eq_para[1].b[0] = VOC_EQ1B0,                                                   \
    .eq_para[1].b[1] = VOC_EQ1B1,                                                   \
    .eq_para[1].b[2] = VOC_EQ1B2,                                                   \
    .eq_load.f_gain     = VOC_EQFGAIN,                                              \
    .eq_load.samplerate = VOC_EQSAMP,                                               \
    .eq_load.eq_load_para[0].freq   = VOC_EQ0FREQ,                                  \
    .eq_load.eq_load_para[0].gain   = VOC_EQ0GAIN,                                  \
    .eq_load.eq_load_para[0].q_val  = VOC_EQ0QVAL,                                  \
    .eq_load.eq_load_para[0].type   = VOC_EQ0FTYPE,                                 \
    .eq_load.eq_load_para[0].enable = VOC_EQ0,                                      \
    .eq_load.eq_load_para[1].freq   = VOC_EQ1FREQ,                                  \
    .eq_load.eq_load_para[1].gain   = VOC_EQ1GAIN,                                  \
    .eq_load.eq_load_para[1].q_val  = VOC_EQ1QVAL,                                  \
    .eq_load.eq_load_para[1].type   = VOC_EQ1FTYPE,                                 \
    .eq_load.eq_load_para[1].enable = VOC_EQ1,                                      \
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
