#include "audio_engine.h"
#include <os/os.h>
#include <common/bk_include.h>
#include <common/bk_err.h>
#include <network_transfer.h>
#include <string.h>
#if CONFIG_AE_SUPPORT_PROMPT_TONE
#include "audio_engine_prompt_tone.h"
#endif
#if CONFIG_ADK_ONBOARD_SPEAKER_STREAM_V2
#include <components/bk_audio/audio_streams/onboard_speaker_stream_v2.h>
#else
#include <components/bk_audio/audio_streams/onboard_speaker_stream.h>
#endif
#if CONFIG_BK_FACTORY_CONFIG
#include "bk_factory_config.h"
#endif

#if CONFIG_APP_EVT
#include "app_event.h"
#endif

#define TAG "audio_engine"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/*
 * ------------------------------------------------------------------------
 *  Sound-source-direction -> on-screen arrow angle (page_5)
 * ------------------------------------------------------------------------
 * The AEC reports a per-slot phase metric (phs_sm2). At wake-word hit
 * we classify the latest value into left / center / right and steer the
 * page_5 arrow accordingly. LVGL rotation is CW positive, 0 deg = designer
 * default orientation.
 *
 * Thresholds tuned from on-board captures:
 *   logs/225degree.log  (right, 225 deg): n=10, range=[-6173, -3234], median=-4512
 *   logs/315degree.log  (left,  315 deg): n=11, range=[ 7062, 13280], median= 8904
 *
 * Polarity on this hardware/AEC config:
 *   phs_sm2 < 0  -> sound source on the RIGHT  (screen arrow at 225 deg)
 *   phs_sm2 > 0  -> sound source on the LEFT   (screen arrow at 315 deg)
 * This is the inverse of the sign convention used in the legacy
 * GPIO-LED-blink debug code; the classifier below is adjusted accordingly.
 *
 * The two tuning clusters do not overlap (gap: -3234 .. 7062). We place
 * thresholds inside the gap with healthy margin so that samples near zero
 * (AEC failed to lock, or sound truly from the front) collapse into the
 * center dead-zone and the arrow stays at 270 deg:
 *     phs_sm2 <= RIGHT_MAX (0)      -> right, 225 deg
 *     phs_sm2 >= LEFT_MIN  (5000)   -> left,  315 deg
 *     otherwise                     -> center, 270 deg
 *
 * On the 21-sample tuning set this gives 100% direction accuracy with
 * ~3200 margin on the right side and ~2000 margin on the left side.
 * MIN/MAX are defensive clamps only; observed range [-6173, 13280] is
 * well inside them.
 */
#define AE_ASR_PHSM2_MIN         (-16384)
#define AE_ASR_PHSM2_MAX         ( 20000)
#define AE_ASR_PHSM2_RIGHT_MAX   (    0)   /* <= this -> right (225 deg) */
#define AE_ASR_PHSM2_LEFT_MIN    ( 5000)   /* >= this -> left  (315 deg) */

#define AE_ARROW_ANGLE_LEFT      (135)
#define AE_ARROW_ANGLE_RIGHT     (45)
#define AE_ARROW_ANGLE_CENTER    (90)

/*
 * Weak reference to page_5_set_arrow_angle(). Projects that ship the
 * beken_robot UI (which defines this symbol in page_5_init.c) get the
 * real implementation at link time; other projects link to NULL and we
 * skip the call, so no extra include path / CMake change is needed to
 * reach projects/beken_robot/ap/include/page_5_api.h from this shared
 * component.
 */
extern void page_5_set_arrow_angle(int degrees) __attribute__((weak));

#if CONFIG_AE_AUDIO_DECODER_G722 && (!CONFIG_ADK_G722_DECODER || !CONFIG_VOICE_SERVICE_G722_DECODER)
#error "CONFIG_AE_AUDIO_DECODER_G722 is enabled, but CONFIG_ADK_G722_DECODER or CONFIG_VOICE_SERVICE_G722_DECODER is not enabled"
#endif

#if CONFIG_AE_AUDIO_ENCODER_G722 && (!CONFIG_ADK_G722_ENCODER || !CONFIG_VOICE_SERVICE_G722_ENCODER)
#error "CONFIG_AE_AUDIO_ENCODER_G722 is enabled, but CONFIG_ADK_G722_ENCODER or CONFIG_VOICE_SERVICE_G722_ENCODER is not enabled"
#endif

#if CONFIG_AE_AUDIO_DECODER_OPUS && (!CONFIG_ADK_OPUS_DECODER || !CONFIG_VOICE_SERVICE_OPUS_DECODER)
#error "CONFIG_AE_AUDIO_DECODER_OPUS is enabled, but CONFIG_ADK_OPUS_DECODER or CONFIG_VOICE_SERVICE_OPUS_DECODER is not enabled"
#endif

#if CONFIG_AE_AUDIO_ENCODER_OPUS && (!CONFIG_ADK_OPUS_ENCODER || !CONFIG_VOICE_SERVICE_OPUS_ENCODER)
#error "CONFIG_AE_AUDIO_ENCODER_OPUS is enabled, but CONFIG_ADK_OPUS_ENCODER or CONFIG_VOICE_SERVICE_OPUS_ENCODER is not enabled"
#endif

#if (CONFIG_AE_AUDIO_ENCODER_G711A || CONFIG_AE_AUDIO_ENCODER_G711U) && (!CONFIG_ADK_G711_ENCODER || !CONFIG_VOICE_SERVICE_G711_ENCODER)
#error "(CONFIG_AE_AUDIO_ENCODER_G711A || CONFIG_AE_AUDIO_ENCODER_G711U) is enabled, but CONFIG_ADK_G711_ENCODER or CONFIG_VOICE_SERVICE_G711_ENCODER is not enabled"
#endif

#if (CONFIG_AE_AUDIO_DECODER_G711A || CONFIG_AE_AUDIO_DECODER_G711U) && (!CONFIG_ADK_G711_DECODER || !CONFIG_VOICE_SERVICE_G711_DECODER)
#error "(CONFIG_AE_AUDIO_DECODER_G711A || CONFIG_AE_AUDIO_DECODER_G711U) is enabled, but CONFIG_ADK_G711_DECODER or CONFIG_VOICE_SERVICE_G711_DECODER is not enabled"
#endif

/* Global handles for audio engine */
struct audio_engine_ctx g_audio_engine = {0};
audio_engine_cfg_t g_audio_engine_cfg = {0};

/* Latest AEC phase metric, refreshed per-slot by bk_aec_phase_callback(). */
static volatile int32_t s_wakeup_phsm2 = 0;

#if CONFIG_AE_SUPPORT_PROMPT_TONE
audio_engine_prompt_tone_handle_t g_audio_engine_prompt_tone = NULL;
#endif
uint8_t g_volume_level = 7;   // volume level, not gain.
static const float g_volume_gain[SPK_VOLUME_LEVEL] = {
	-36.00f, -33.06f, -29.82f, -26.22f, -22.26f, -17.88f,
	-13.03f, -7.68f, -1.76f, 4.77f, 12.00f
};

/*
 * Map the cached AEC phase metric to an on-screen arrow angle.
 * The input is clamped first to defend against upstream drift.
 */
static int audio_engine_phsm2_to_arrow_angle(int32_t phs_sm2)
{
    if (phs_sm2 < AE_ASR_PHSM2_MIN) {
        phs_sm2 = AE_ASR_PHSM2_MIN;
    } else if (phs_sm2 > AE_ASR_PHSM2_MAX) {
        phs_sm2 = AE_ASR_PHSM2_MAX;
    }

    if (phs_sm2 <= AE_ASR_PHSM2_RIGHT_MAX) {
        return AE_ARROW_ANGLE_RIGHT;
    } else if (phs_sm2 >= AE_ASR_PHSM2_LEFT_MIN) {
        return AE_ARROW_ANGLE_LEFT;
    }

    return AE_ARROW_ANGLE_CENTER;
}

/*
 * Called at wake-word hit: classify the latest phase metric and push
 * the resulting angle into the page_5 arrow, if the UI is linked in.
 */
static void audio_engine_update_arrow_on_wakeup(void)
{
    int32_t phsm2 = s_wakeup_phsm2;
    int deg = audio_engine_phsm2_to_arrow_angle(phsm2);

    LOGI("ASR wakeup phs_sm2=%d -> arrow=%d deg\r\n", (int)phsm2, deg);

    if (page_5_set_arrow_angle != NULL) {
        page_5_set_arrow_angle(deg);
    }
}

static uint8_t audio_engine_volume_get_diag_gain(void)
{
    if (g_volume_level > (SPK_VOLUME_LEVEL-1)) {
        g_volume_level = SPK_VOLUME_LEVEL-1;
    }
    return g_volume_gain[g_volume_level];
}

static int audio_engine_volume_init(void)
{
    LOGI("audio_engine_volume_init\n");

    #if CONFIG_BK_FACTORY_CONFIG
    int g_volume_level_size = 0;

    g_volume_level_size = bk_config_read("volume", (void *)&g_volume_level, 4);
    if (g_volume_level_size != 4)
    {
        LOGE("read volume config fail, use default config g_volume_level_size:%d\n", g_volume_level_size);
    }

    LOGI("Saved volume level: %d\n", g_volume_level);

    if (0 != bk_config_write("volume", (void *)&g_volume_level, 4))
    {
        LOGE("storage g_volume_level: %d fail\n", g_volume_level);
    }
    #else
    LOGI("not support factory config, use default config g_volume_level: %d\n", g_volume_level);
    #endif

    if (g_volume_level >= SPK_VOLUME_LEVEL) {
        g_volume_level = SPK_VOLUME_LEVEL - 1;
    }

    return AUDIO_ENGINE_SUCCESS;
}

static int audio_engine_adjust_voice_gain(float gain_db)
{
    voice_handle_t voice_handle = (voice_handle_t)g_audio_engine.voice_handle;
    audio_element_handle_t spk_str = NULL;
    spk_type_t spk_type = SPK_TYPE_INVALID;

    if (!voice_handle) {
        LOGE("audio engine adjust voice gain failed, voice_handle is NULL\n");
        return AUDIO_ENGINE_ERR_INVALID_PARAM;
    }

    bk_voice_get_spkstr(voice_handle, &spk_str);
    bk_voice_get_spkstr_type(voice_handle, &spk_type);

    if (spk_str && spk_type == SPK_TYPE_ONBOARD)
    {
        if (BK_OK != onboard_speaker_stream_set_digital_gain(spk_str, gain_db)) {
            LOGE("onboard_speaker_stream_set_digital_gain failed\n");
            return AUDIO_ENGINE_ERR_INVALID_PARAM;
        }
    }
    else
    {
        LOGE("audio engine adjust voice gain failed, spk_str is not onboard speaker stream\n");
        return AUDIO_ENGINE_ERR_INVALID_PARAM;
    }

    return AUDIO_ENGINE_SUCCESS;
}

void audio_engine_volume_increase(void)
{
    LOGI("volume up\r\n");

    if (g_volume_level >= (SPK_VOLUME_LEVEL - 1))
    {
        LOGI("volume have reached maximum: level %u, +12 dB\n", (SPK_VOLUME_LEVEL - 1));
        return;
    }

    if (BK_OK == audio_engine_adjust_voice_gain(g_volume_gain[g_volume_level + 1]))
    {
        g_volume_level += 1;
        #if CONFIG_BK_FACTORY_CONFIG
        if (0 != bk_config_write("volume", (void *)&g_volume_level, 4))
        {
            LOGE("storage volume: %d fail\n", g_volume_level);
        }
        #else
        LOGE("not support factory config, storage volume: %d fail\n", g_volume_level);
        #endif
        LOGI("current volume level: %u, %.1f dB\n", g_volume_level, g_volume_gain[g_volume_level]);
    }
    else
    {
        LOGI("set volume fail\n");
    }
}

void audio_engine_volume_decrease(void)
{
    LOGI("volume down\r\n");

    if (g_volume_level == 0)
    {
        LOGI("volume have reached minimum: level 0, %.1f dB\n", g_volume_gain[0]);
        return;
    }

    if (BK_OK == audio_engine_adjust_voice_gain(g_volume_gain[g_volume_level - 1]))
    {
        g_volume_level -= 1;
        #if CONFIG_BK_FACTORY_CONFIG
        if (0 != bk_config_write("volume", (void *)&g_volume_level, 4))
        {
            LOGE("storage volume: %d fail\n", g_volume_level);
        }
        #else
        LOGE("not support factory config, storage volume: %d fail\n", g_volume_level);
        #endif
        LOGI("current volume level: %u, %.1f dB\n", g_volume_level, g_volume_gain[g_volume_level]);
    }
    else
    {
        LOGI("set volume fail\n");
    }
}

uint8_t audio_engine_volume_get_level(void)
{
    if (g_volume_level >= SPK_VOLUME_LEVEL) {
        return SPK_VOLUME_LEVEL - 1;
    }

    return g_volume_level;
}

uint8_t audio_engine_volume_get_max_level(void)
{
    return SPK_VOLUME_LEVEL - 1;
}

#if (CONFIG_ASR_SERVICE)
#if CONFIG_WANSON_ARMINO_ASR
#include "bk_wanson_asr_intf.h"
#elif CONFIG_BEKEN_KWS
#include "bk_kws_asr.h"
#endif
static const char *g_audio_engine_asr_text = NULL;
static float g_audio_engine_asr_score = 0.0f;

void bk_audio_engine_asr_result_handle(void *p1, void *p2)
{
    const char *result = NULL;
    uint8_t asr_result = 0;

    /* BK7259 ASR callback passes result and score via p1/p2 pointers. */
    if (p1 != NULL) {
        result = *((char **)p1);
    }
    if (result == NULL) {
        LOGE("ASR result is NULL\n");
        return;
    }
    (void)p2;

#if CONFIG_BEKEN_KWS
    //LOGD("result : %s\n", result);
    if (os_strcmp(result, "nihaobotong") == 0)
    {
        LOGI("nihaobotong\r\n");
        asr_result = BK_KWS_ARMINO;
    } else if ((os_strcmp(result, "zaijianbotong") == 0))
    {
        LOGI("%s \n", "zaijianbotong");
        asr_result = BK_KWS_BYEBYE;
    } else if (os_strcmp(result, "Play Music") == 0)
    {
        LOGI("play music\r\n");
        asr_result = BK_KWS_PLAY_MUSIC;
    } else if (os_strcmp(result, "Stop Play") == 0)
    {
        LOGI("stop play\r\n");
        asr_result = BK_KWS_STOP_PLAY;
    }else if (os_strcmp(result, "Next song") == 0)
    {
        LOGI("next song\r\n");
        asr_result = BK_KWS_NEXT_SONG;
    } else if (os_strcmp(result, "Volume Up") == 0)
    {
        LOGI("volume up\r\n");
        asr_result = BK_KWS_VOLUME_UP;
    } else if (os_strcmp(result, "Volume Down") == 0)
    {
        LOGI("volume down\r\n");
        asr_result = BK_KWS_VOLUME_DOWN;
    } else {
        LOGE("Invalid asr result: %s\n", result);
        asr_result = BK_KWS_NONE;
    }
#endif

#if (CONFIG_WANSON_ASR_GROUP_VERSION_WORDS_V1)
    if (os_strcmp(result, "嗨阿米诺") == 0)
    {
        LOGI("%s \n", "hi armino, cmd: 0 ");
        asr_result = 1;
    }
    else if (os_strcmp(result, "嘿阿米楼") == 0)
    {
        LOGI("%s \n", "hi armino, cmd: 1 ");
        asr_result = 1;
    }
    else if (os_strcmp(result, "嘿儿米楼") == 0)
    {
        LOGI("%s \n", "hi armino, cmd: 2 ");
        asr_result = 1;
    }
    else if (os_strcmp(result, "嘿鹅迷楼") == 0)
    {
        LOGI("%s \n", "hi armino, cmd: 3 ");
        asr_result = 1;
    }
    else if (os_strcmp(result, "拜拜阿米诺") == 0)
    {
        LOGI("%s \n", "byebye armino, cmd: 0 ");
        asr_result = 2;
    }
    else if (os_strcmp(result, "拜拜阿米楼") == 0)
    {
        LOGI("%s \n", "byebye armino, cmd: 1 ");
        asr_result = 2;
    }
    else
    {
        //nothing
    }
#else
    if (os_strcmp(result, "你好阿米诺") == 0)
    {
        LOGI("%s \n", "nihao armino, cmd: 0 ");
        asr_result = 1;
    }
    else if (os_strcmp(result, "再见阿米诺") == 0)
    {
        LOGI("%s \n", "zaijian armino, cmd: 1 ");
        asr_result = 2;
    }
    else
    {
        //nothing
    }
#endif

#if CONFIG_BEKEN_KWS
    if ((asr_result <= BK_KWS_NONE) || (asr_result >= BK_KWS_MAX_WORDS)) {
        LOGE("Invalid asr_result: %d, valid range is %d-%d\n", asr_result, BK_KWS_NONE+1, BK_KWS_MAX_WORDS-1);
        return;
    }
#else
    if ((asr_result == 0) || (asr_result > 2)) {
        LOGE("Invalid asr_result: %d, valid range is 1-2\n", asr_result);
        return;
    }
#endif
    g_audio_engine.asr_result = asr_result;
    LOGD("ASR result set to: %d\n", g_audio_engine.asr_result);

#if 0
    audio_element_handle_t spk_element = bk_voice_get_spk_element(g_audio_engine.voice_handle);
    if (spk_element)
    {
        if (g_audio_engine.asr_result == 1) {
            onboard_speaker_stream_set_input_port_data_valid(spk_element, 0, true);
        } else if (g_audio_engine.asr_result == 2) {
            onboard_speaker_stream_set_input_port_data_valid(spk_element, 0, false);
        } else {
            //nothing todo
        }
    }
#endif

#if CONFIG_APP_EVT
#if CONFIG_BEKEN_KWS
    bk_err_t ret = BK_FAIL;
    if (g_audio_engine.asr_result == BK_KWS_ARMINO) {
        audio_engine_update_arrow_on_wakeup();
        ret = app_event_send_msg(APP_EVT_ASR_WAKEUP, 0);
        if (BK_OK != ret) {
            LOGE("Failed to send APP_EVT_ASR_WAKEUP event, ret: %d\n", ret);
        } else {
            LOGD("APP_EVT_ASR_WAKEUP event sent successfully\n");
        }
        ret = app_event_send_msg(APP_EVT_ASR_NIHAOBOTONG, 0);
        if (BK_OK != ret) {
            LOGE("Failed to send APP_EVT_ASR_NIHAOBOTONG event, ret: %d\n", ret);
        } else {
            LOGD("APP_EVT_ASR_NIHAOBOTONG event sent successfully\n");
        }
    }
    else if (g_audio_engine.asr_result == BK_KWS_BYEBYE) {
        ret = app_event_send_msg(APP_EVT_ASR_STANDBY, 0);
        if (BK_OK != ret) {
            LOGE("Failed to send APP_EVT_ASR_STANDBY event, ret: %d\n", ret);
        } else {
            LOGD("APP_EVT_ASR_STANDBY event sent successfully\n");
        }
        ret = app_event_send_msg(APP_EVT_ASR_ZAIJIANBOTONG, 0);
        if (BK_OK != ret) {
            LOGE("Failed to send APP_EVT_ASR_ZAIJIANBOTONG event, ret: %d\n", ret);
        } else {
            LOGD("APP_EVT_ASR_ZAIJIANBOTONG event sent successfully\n");
        }
    }
    else if (g_audio_engine.asr_result == BK_KWS_VOLUME_UP) {
        audio_engine_volume_increase();
    }
    else if (g_audio_engine.asr_result == BK_KWS_VOLUME_DOWN) {
        audio_engine_volume_decrease();
    }
    else if (g_audio_engine.asr_result >= BK_KWS_PLAY_MUSIC && g_audio_engine.asr_result <= BK_KWS_NEXT_SONG) {
        //nothing to do
    }
    else {
        LOGE("Unexpected asr_result: %d\n", g_audio_engine.asr_result);
    }
#else
    bk_err_t ret = BK_FAIL;
    if (g_audio_engine.asr_result == 1) {
        audio_engine_update_arrow_on_wakeup();
        ret = app_event_send_msg(APP_EVT_ASR_WAKEUP, 0);
        if (BK_OK != ret) {
            LOGE("Failed to send APP_EVT_ASR_WAKEUP event, ret: %d\n", ret);
        } else {
            LOGD("APP_EVT_ASR_WAKEUP event sent successfully\n");
        }
    }
    else if (g_audio_engine.asr_result == 2) {
        ret = app_event_send_msg(APP_EVT_ASR_STANDBY, 0);
        if (BK_OK != ret) {
            LOGE("Failed to send APP_EVT_ASR_STANDBY event, ret: %d\n", ret);
        } else {
            LOGD("APP_EVT_ASR_STANDBY event sent successfully\n");
        }
    }
 #endif
#else
    //LOGW("CONFIG_APP_EVT is not enabled, skipping event notification\n");
#endif
}

#endif

/**
 * @brief Initialize and start audio engine
 * 
 * @param cfg Pointer to audio engine configuration structure
 * @return int 
 *         - 0: Success
 *         - < 0: Error codes
 *             - -1: Invalid parameters
 *             - -2: Voice init failed
 *             - -3: Voice read init failed
 *             - -4: Voice write init failed
 *             - -5: Voice start failed
 *             - -6: Voice read start failed
 *             - -7: Voice write start failed
 */
#if CONFIG_AUDIO_PARA
extern app_aud_para_t *get_app_aud_cust_para(app_aud_service_type_t service_type);
extern void set_app_aud_cust_service_handle(void *service_handle, app_aud_service_type_t service_type);

void bk_audio_set_voc_cust_params(voice_cfg_t * voice_cfg, app_aud_service_type_t service_type)
{
	app_aud_para_t * cust_aud_para = NULL;
	{
		cust_aud_para = get_app_aud_cust_para(AUD_SERVICE_AI_VOC);
		if (cust_aud_para == NULL)
		{
			LOGE("get_app_aud_cust_para fail\n");
		} else
		{
			bk_aud_debug_get_audpara(cust_aud_para, AUD_SERVICE_AI_VOC);
		}

#if CONFIG_VOICE_SERVICE_EQ
		if (voice_cfg->eq_en)
		{
			if (cust_aud_para && cust_aud_para->eq_dl_config.app_eq_en)
			{
				if (1)//(spk_sample_rate == cust_aud_para->eq_dl_config.eq_load.samplerate)
				{
					voice_cfg->eq_en = cust_aud_para->eq_dl_config.eq_en;
					voice_cfg->eq_cfg.eq_alg_cfg.eq_cal_para.eq_en       = cust_aud_para->eq_dl_config.eq_en;
					voice_cfg->eq_cfg.eq_alg_cfg.eq_cal_para.filters     = cust_aud_para->eq_dl_config.filters;
					voice_cfg->eq_cfg.eq_alg_cfg.eq_cal_para.globle_gain = cust_aud_para->eq_dl_config.globle_gain;
					os_memcpy(&voice_cfg->eq_cfg.eq_alg_cfg.eq_cal_para.eq_para, &cust_aud_para->eq_dl_config.eq_para, sizeof(eq_para_t)*cust_aud_para->eq_dl_config.filters);
					os_memcpy(&voice_cfg->eq_cfg.eq_alg_cfg.eq_cal_para.eq_load, &cust_aud_para->eq_dl_config.eq_load, sizeof(app_eq_load_t));
				} else
				{
					voice_cfg->eq_en = 0;
					LOGE("voice dl eq init fail, spk_sample_rate not match\n");
				}
			}
		}
#endif

		if (voice_cfg->mic_type == MIC_TYPE_ONBOARD)
		{
			if (cust_aud_para && cust_aud_para->sys_config.app_sys_en)
			{
				// voice_cfg->mic_cfg.onboard_mic_cfg.adc_cfg.ana_gain = cust_aud_para->sys_config.mic0_analog_gain;
				// voice_cfg->mic_cfg.onboard_mic_cfg.adc_cfg.dig_gain = cust_aud_para->sys_config.mic0_digital_gain;
			}
		}
		if (voice_cfg->spk_type == SPK_TYPE_ONBOARD)
		{
			if (cust_aud_para && cust_aud_para->sys_config.app_sys_en)
			{
				// voice_cfg->spk_cfg.onboard_spk_cfg.ana_gain = cust_aud_para->sys_config.speaker_chan0_analog_gain;
				// voice_cfg->spk_cfg.onboard_spk_cfg.dig_gain = cust_aud_para->sys_config.speaker_chan0_digital_gain;
			}
		}
		if (voice_cfg->aec_en)
		{
			if (cust_aud_para && cust_aud_para->aec_v3_config.app_aec_en)
			{
				voice_cfg->aec_en = cust_aud_para->aec_v3_config.aec_enable;
				voice_cfg->aec_cfg.aec_alg_cfg.aec_cfg.delay_points = cust_aud_para->aec_v3_config.mic_delay;
				voice_cfg->aec_cfg.aec_alg_cfg.aec_cfg.ec_depth     = cust_aud_para->aec_v3_config.ec_depth;
				voice_cfg->aec_cfg.aec_alg_cfg.aec_cfg.ref_scale    = cust_aud_para->aec_v3_config.ref_scale;
				voice_cfg->aec_cfg.aec_alg_cfg.aec_cfg.ns_level     = cust_aud_para->aec_v3_config.ns_level;
				voice_cfg->aec_cfg.aec_alg_cfg.aec_cfg.ns_para      = cust_aud_para->aec_v3_config.ns_para;
			}
		}
	}
}
#endif

#if CONFIG_ADK_AEC_V3_ALGORITHM_COMPONENT_V2
static int bk_aec_phase_callback(int32_t phs_sm2, int vad_state)
{
    (void)vad_state;
    /* Cache latest estimate so the wake-word handler can steer the arrow.
     * The mic activity / level signals for the UI come from the dedicated
     * vad_state_cb and aec_level_cb hooks below, which are stable and
     * already hysteresis-filtered by the SDK. */
    s_wakeup_phsm2 = phs_sm2;
    return 1;
}
#endif /* CONFIG_ADK_AEC_V3_ALGORITHM_COMPONENT_V2 */

/* ============================================================================
 * Mic / spk PCM envelope and AI dialogue state machine.
 *
 * Three real SDK signal sources drive the UI's LISTENING/SPEAKING/IDLE state:
 *
 *   1) AEC V3 `vad_state_cb`       -> mic_active edge (user speaking, post-AEC)
 *   2) AEC V3 `aec_level_cb`       -> mic_level 0..100 (post-AEC envelope)
 *   3) onboard_speaker `status_cb` -> spk_active + spk_level (DAC output side,
 *                                     SDK already applies threshold+hysteresis)
 *
 * State priority (per product spec):
 *   - mic_active                                 -> LISTENING (user wins)
 *   - !mic_active && spk_active                  -> SPEAKING  (agent talking)
 *   - !mic_active && !spk_active                 -> IDLE
 *
 * Both callbacks may run on different SDK tasks (AEC vs speaker stream); we
 * use volatile single-byte storage plus a same-state short-circuit on the
 * emitted event to keep the implementation lock-free.
 * ==========================================================================*/

static volatile uint8_t s_mic_level;     /* 0..100, post-AEC, latest frame */
static volatile uint8_t s_spk_level;     /* 0..100, onboard speaker raw    */
static volatile uint8_t s_spk_level_smooth; /* EWMA of s_spk_level         */
static volatile uint8_t s_mic_active;    /* 1 = VAD_SPEECH_START else 0    */
static volatile uint8_t s_spk_active;    /* 1 = onboard spk is_playing     */

/* Minimum LISTENING dwell time before we treat the utterance as a real
 * query to the model. Anything shorter (door slam, throat-clearing, brief
 * environmental noise) is considered a false trigger and skips the
 * subsequent THINKING window. */
#define MIN_LISTENING_MS    1000u
static volatile uint32_t s_mic_start_at_ms; /* 0 = not currently listening */

/* SPEAKING linger: the SDK already applies threshold + hysteresis on
 * is_playing, but agent speech still has natural inter-phrase pauses of
 * 200-500ms which momentarily flip is_playing to false and cause the UI
 * to bounce between SPEAKING and IDLE. We add a configurable post-stop
 * linger window: when is_playing falls, we *do not* clear s_spk_active
 * immediately; instead we start a timestamp and only confirm the drop
 * once SPK_LINGER_MS has elapsed without is_playing coming back. */
#define SPK_LINGER_MS    700u
static volatile uint32_t s_spk_off_at_ms; /* 0 = not in linger window */

/* THINKING window: the gap between "user finished speaking" (VAD STOP) and
 * "agent starts speaking" (onboard speaker PLAYING) is the model's think /
 * RTT time. We surface this as APP_EVT_AI_THINKING so the UI can show the
 * orbit + status text. Capped at THINKING_TIMEOUT_MS to avoid getting
 * stuck if the agent never replies (network loss, etc.). */
#define THINKING_TIMEOUT_MS  8000u
static volatile uint32_t s_thinking_at_ms; /* 0 = not thinking */

uint8_t audio_engine_get_mic_level(void)
{
    /* While VAD reports silence, the SDK forces aec_level to 0; we keep
     * that semantics so the UI does not show fake mic activity. */
    return s_mic_level;
}

uint8_t audio_engine_get_spk_level(void)
{
    /* Return the EWMA-smoothed level so the EQ amplitude does not jitter
     * frame-by-frame; the SDK reports a fresh sample every speaker frame
     * (~10-20ms) and the raw value bounces wildly within an utterance. */
    return s_spk_active ? s_spk_level_smooth : 0u;
}

/* Debug accessors. Cheap, lock-free, intended for the `aiui state` CLI. */
uint8_t  audio_engine_get_mic_active(void)         { return s_mic_active; }
uint8_t  audio_engine_get_spk_active(void)         { return s_spk_active; }
uint32_t audio_engine_get_spk_off_pending_ms(void)
{
    uint32_t at = s_spk_off_at_ms;
    if (at == 0u) {
        return 0u;
    }
    uint32_t elapsed = rtos_get_time() - at;
    return (elapsed >= SPK_LINGER_MS) ? 0u : (SPK_LINGER_MS - elapsed);
}
/* audio_engine_get_last_ai_evt() is defined below, after s_last_ai_evt
 * (the static variable lives inside the CONFIG_APP_EVT block). */

#if CONFIG_APP_EVT
/* Last emitted AI_* event; protected only by the fact that updates are
 * idempotent (we just skip same-state emits, multi-task races at worst
 * cause a single duplicate which the UI already short-circuits). */
static volatile app_evt_type_t s_last_ai_evt = APP_EVT_AI_IDLE;

static void ai_state_evaluate(void)
{
    app_evt_type_t next;
    /* Priority: LISTENING wins over SPEAKING ("user barge-in" semantics).
     * As soon as the user starts talking we flip to LISTENING even if
     * the agent is still playing -- this gives immediate visual
     * acknowledgement that the device heard the user, and it matches
     * the typical voice-assistant idiom where speaking up interrupts
     * the agent. The 1s LISTENING dwell gate below still guards the
     * THINKING window so brief false VADs don't get rewarded. */
    if (s_mic_active) {
        /* User is talking -- cancel any pending think window. */
        s_thinking_at_ms = 0u;
        next = APP_EVT_AI_LISTENING;
    } else if (s_spk_active) {
        /* Mic silent, agent playing. */
        s_thinking_at_ms = 0u;
        next = APP_EVT_AI_SPEAKING;
    } else if (s_thinking_at_ms != 0u) {
        /* User has stopped, agent has not yet started: thinking. Expire
         * after THINKING_TIMEOUT_MS to avoid getting stuck. */
        uint32_t elapsed = rtos_get_time() - s_thinking_at_ms;
        if (elapsed >= THINKING_TIMEOUT_MS) {
            s_thinking_at_ms = 0u;
            LOGI("THINKING timeout (%ums) -> IDLE\n", (unsigned)elapsed);
            next = APP_EVT_AI_IDLE;
        } else {
            next = APP_EVT_AI_THINKING;
        }
    } else {
        next = APP_EVT_AI_IDLE;
    }
    if (next != s_last_ai_evt) {
        s_last_ai_evt = next;
        (void)app_event_send_msg(next, 0);
    }
}

int audio_engine_get_last_ai_evt(void)
{
    return (int)s_last_ai_evt;
}

/* ---- RTC hint surface (server-side authoritative signals) ----
 *
 * These are called from agora_rtc_engine when the Agora ConvoAI agent
 * publishes "state" frames on the data-stream. We use them to fix two
 * blind spots of the purely-local state machine:
 *
 *   audio_engine_hint_thinking()
 *     The local machine can only *guess* THINKING from "VAD STOP & spk
 *     idle"; the server knows for certain when the LLM is busy. Stamping
 *     s_thinking_at_ms here keeps the local 8s timeout + IDLE fallback,
 *     while letting evaluate() emit THINKING immediately.
 *
 *   audio_engine_hint_speaking_start()
 *     RTC reports "speaking" ~500ms before the first audio frame arrives
 *     at the local DAC. Promote s_spk_active so SPEAKING + EQ orange show
 *     up early; the eventual onboard_speaker_stream PLAYING is then
 *     short-circuited by the same-state check.
 *
 * LISTENING and SILENT hints from the server are intentionally NOT routed
 * here -- VAD is faster, and spk-linger is more accurate, respectively.
 */
void audio_engine_hint_thinking(void)
{
    uint32_t t = rtos_get_time();
    s_thinking_at_ms = (t == 0u) ? 1u : t;
    LOGI("THINKING hinted by RTC\n");
    ai_state_evaluate();
}

void audio_engine_hint_speaking_start(void)
{
    s_spk_off_at_ms = 0u;
    if (!s_spk_active) {
        s_spk_active = 1u;
        /* No PCM yet, so seed smooth at 0; EQ will rise once real frames
         * land in bk_onboard_spk_status_cb a few hundred ms later. */
        s_spk_level_smooth = 0u;
        LOGI("SPK PLAYING (hinted by RTC, pre-PCM)\n");
        ai_state_evaluate();
    }
}
#else  /* !CONFIG_APP_EVT */
int audio_engine_get_last_ai_evt(void)
{
    return -1;
}
void audio_engine_hint_thinking(void)        { /* no-op */ }
void audio_engine_hint_speaking_start(void)  { /* no-op */ }
#endif /* CONFIG_APP_EVT */

/* SPEAKING linger expiry check + THINKING expiry check. Cheap; called from
 * the 10ms AEC level callback (it runs even when the mic is silent, so the
 * timers reliably tick regardless of mic activity). */
static void spk_linger_tick(void)
{
    uint32_t now = rtos_get_time();

    uint32_t at = s_spk_off_at_ms;
    if (at != 0u && (now - at) >= SPK_LINGER_MS) {
        s_spk_off_at_ms = 0u;
        if (s_spk_active) {
            s_spk_active = 0u;
            LOGI("SPK STOP confirmed (linger %ums elapsed)\n",
                 (unsigned)SPK_LINGER_MS);
#if CONFIG_APP_EVT
            ai_state_evaluate();
#endif
        }
    }

#if CONFIG_APP_EVT
    /* Drive the THINKING -> IDLE timeout from the same 10ms tick. We only
     * call evaluate() at the boundary instant to avoid spamming. */
    uint32_t tat = s_thinking_at_ms;
    if (tat != 0u && !s_mic_active && !s_spk_active &&
        (now - tat) >= THINKING_TIMEOUT_MS) {
        ai_state_evaluate();
    }
#endif
}

#if CONFIG_ADK_AEC_V3_ALGORITHM_COMPONENT_V2
static int bk_aec_vad_state_cb(int32_t state)
{
    /* state == VAD_SPEECH_START / VAD_SPEECH_END / VAD_SILENCE / VAD_NONE.
     * Only SPEECH_START counts as "user is talking". */
    uint8_t active = (state == VAD_SPEECH_START) ? 1u : 0u;
    uint8_t was_active = s_mic_active;
    if (active != was_active) {
        s_mic_active = active;
        uint32_t now = rtos_get_time();

        if (active) {
            /* Mark the listening start so we can later judge whether the
             * utterance was long enough to be a real query. */
            s_mic_start_at_ms = (now == 0u) ? 1u : now;
            LOGI("VAD SPEECH_START\n");
        } else {
            uint32_t dur = (s_mic_start_at_ms != 0u)
                           ? (now - s_mic_start_at_ms) : 0u;
            s_mic_start_at_ms = 0u;
            /* Decide what comes after LISTENING:
             *   - Spk is playing  -> back to SPEAKING (LISTENING was a
             *                        barge-in interruption; agent kept
             *                        going underneath us).
             *   - Spk idle & dur >= 1s -> real query, start THINKING.
             *   - Spk idle & dur <  1s -> false VAD trigger, go IDLE
             *                             (don't reward it with THINKING).
             */
            if (s_spk_active) {
                LOGI("VAD STOP (listened %ums, spk active) "
                     "-> back to SPEAKING\n", (unsigned)dur);
            } else if (dur >= MIN_LISTENING_MS) {
                s_thinking_at_ms = (now == 0u) ? 1u : now;
                LOGI("VAD STOP (listened %ums) -> THINKING\n",
                     (unsigned)dur);
            } else {
                LOGI("VAD STOP (listened %ums, too short for a real "
                     "query) -> IDLE\n", (unsigned)dur);
            }
        }
#if CONFIG_APP_EVT
        ai_state_evaluate();
#endif
    }
    return 0;
}

static int bk_aec_level_cb(int32_t level)
{
    /* Piggyback the SPEAKING linger expiry check on this 10ms tick. */
    spk_linger_tick();

    /* aec_level_cb fires every AEC frame with the post-AEC envelope mapped
     * to 0..100 by the SDK; the SDK already forces level=0 when VAD is not
     * in SPEECH_START, so we can store it straight away. */
    uint32_t v = (level < 0) ? 0u : (uint32_t)level;
    if (v > 100u) {
        v = 100u;
    }
    s_mic_level = (uint8_t)v;
    return 0;
}
#endif /* CONFIG_ADK_AEC_V3_ALGORITHM_COMPONENT_V2 */

#if CONFIG_ADK_ONBOARD_SPEAKER_STREAM_V2
static void bk_onboard_spk_status_cb(audio_element_handle_t el,
                                     const onboard_speaker_stream_status_t *status,
                                     void *user_data)
{
    (void)el;
    (void)user_data;
    if (status == NULL) {
        return;
    }

    s_spk_level = status->energy_level;
    /* EWMA smoothing on the speaker meter (alpha = 1/4 = 0.25). This is
     * what feeds the EQ amplitude. Raw energy_level swings 2..30 within
     * a single phrase; smoothed value tracks the envelope with ~80ms TC
     * (4 frames @ 20ms), eliminating frame-to-frame jitter without
     * dragging on real loudness changes. */
    {
        uint32_t raw_v = status->energy_level;
        uint32_t sm   = s_spk_level_smooth;
        sm = (raw_v + sm * 3u) / 4u;
        s_spk_level_smooth = (uint8_t)sm;
    }
    uint8_t raw_playing = status->is_playing ? 1u : 0u;

    if (raw_playing) {
        /* Cancel any pending linger; we are clearly still in agent speech. */
        s_spk_off_at_ms = 0u;
        if (!s_spk_active) {
            s_spk_active = 1u;
            /* Prime the EWMA so the first frame of agent audio lifts the
             * EQ immediately instead of crawling up from 0 over 4 frames. */
            s_spk_level_smooth = status->energy_level;
            LOGI("SPK PLAYING (level=%u)\n", (unsigned)status->energy_level);
#if CONFIG_APP_EVT
            ai_state_evaluate();
#endif
        }
    } else {
        /* Start (or keep running) the linger window; do NOT clear s_spk_active
         * yet so brief inter-phrase pauses do not bounce the UI to IDLE. The
         * spk_linger_tick() running on the AEC 10ms tick will confirm the drop
         * once SPK_LINGER_MS has elapsed without a new is_playing=true. */
        if (s_spk_active && s_spk_off_at_ms == 0u) {
            s_spk_off_at_ms = rtos_get_time();
            /* DEBUG only: the SDK re-calls status_cb each time raw is_playing
             * flips during agent inter-frame jitter, which would spam this
             * line at ~5Hz inside a single utterance. Only PLAYING and
             * "STOP confirmed" edges stay at INFO. */
            LOGD("SPK STOP pending (level=%u, linger %ums)\n",
                 (unsigned)status->energy_level, (unsigned)SPK_LINGER_MS);
        }
    }
}
#endif /* CONFIG_ADK_ONBOARD_SPEAKER_STREAM_V2 */

#if (CONFIG_ASR_SERVICE)
static int audio_engine_asr_build_cfg(asr_cfg_t *asr_cfg)
{
    if (asr_cfg == NULL) {
        return AUDIO_ENGINE_ERR_INVALID_PARAM;
    }

    asr_cfg_t t_asr_cfg = ASR_BY_ONBOARD_MIC_CFG_DEFAULT();
    *asr_cfg = t_asr_cfg;
    asr_cfg->asr_en = true;

    if (g_audio_engine_cfg.mic_sample_rate != asr_cfg->asr_sample_rate) {
#if CONFIG_ADK_RSP_ALGORITHM
        asr_cfg->asr_rsp_en = true;
        asr_cfg->rsp_cfg.rsp_alg_cfg.rsp_cfg.src_rate = g_audio_engine_cfg.mic_sample_rate;
#else
        asr_cfg->asr_rsp_en = false;
        LOGE("Need Open the aud resample Macro\n");
        return AUDIO_ENGINE_ERR_ASR_INIT;
#endif
    } else {
        asr_cfg->asr_rsp_en = false;
    }

    asr_cfg->args = NULL;
    asr_cfg->event_handle = NULL;
    if (g_audio_engine_cfg.mic_sample_rate == 16000) {
        asr_cfg->read_pool_size = g_audio_engine_cfg.mic_sample_rate * 2 * 20 / 1000;
    } else if (g_audio_engine_cfg.mic_sample_rate == 8000) {
        asr_cfg->read_pool_size = 2 * g_audio_engine_cfg.mic_sample_rate * 2 * 20 / 1000;
    }

    return AUDIO_ENGINE_SUCCESS;
}

int audio_engine_asr_start(void)
{
    asr_cfg_t asr_cfg = {0};
    voice_cfg_t mic_cfg = {0};

    if (!g_audio_engine.is_started) {
        LOGE("audio engine not started, cannot start asr\n");
        return AUDIO_ENGINE_ERR_NOT_STARTED;
    }

    if (g_audio_engine.asr_started) {
        return AUDIO_ENGINE_SUCCESS;
    }

    if ((g_audio_engine.asr_handle == NULL) != (g_audio_engine.aud_asr_handle == NULL)) {
        /* Recover from a partial init state before recreating handles. */
        (void)audio_engine_asr_stop();
    }

    if (g_audio_engine.asr_handle == NULL || g_audio_engine.aud_asr_handle == NULL) {
        int cfg_ret = audio_engine_asr_build_cfg(&asr_cfg);
        if (cfg_ret != AUDIO_ENGINE_SUCCESS) {
            return cfg_ret;
        }

        g_audio_engine.asr_handle = bk_asr_create(&asr_cfg);
        if (g_audio_engine.asr_handle == NULL) {
            LOGE("asr create fail\n");
            return AUDIO_ENGINE_ERR_ASR_INIT;
        }

        mic_cfg.aec_en = g_audio_engine_cfg.aec_enable ? true : false;
        g_audio_engine.asr_handle->mic_str = (audio_element_handle_t)bk_voice_get_mic_str(g_audio_engine.voice_handle, &mic_cfg);

        if (BK_OK != bk_asr_init(&asr_cfg, g_audio_engine.asr_handle)) {
            LOGE("asr init fail\n");
            (void)audio_engine_asr_stop();
            return AUDIO_ENGINE_ERR_ASR_INIT;
        }

        aud_asr_cfg_t aud_asr_cfg = AUDIO_ASR_CFG_DEFAULT();
        aud_asr_cfg.asr_handle = g_audio_engine.asr_handle;
        aud_asr_cfg.aud_asr_result_handle = bk_audio_engine_asr_result_handle;
#if CONFIG_WANSON_ARMINO_ASR
        aud_asr_cfg.aud_asr_init   = bk_wanson_asr_common_init;
        aud_asr_cfg.aud_asr_deinit = bk_wanson_asr_common_deinit;
        aud_asr_cfg.aud_asr_recog  = bk_wanson_asr_recog;
        aud_asr_cfg.max_read_size  = 960;
#elif CONFIG_BEKEN_KWS
        aud_asr_cfg.aud_asr_init   = bk_tflite_asr_init;
        aud_asr_cfg.aud_asr_deinit = bk_tflite_asr_deinit;
        aud_asr_cfg.aud_asr_recog  = bk_tflite_asr_recog;
        aud_asr_cfg.max_read_size  = 1280;
        aud_asr_cfg.task_stack     = 25 * 1024;
        aud_asr_cfg.mem_type       = AUDIO_MEM_TYPE_SRAM;
#endif
        aud_asr_cfg.p1 = (void *)&g_audio_engine_asr_text;
        aud_asr_cfg.p2 = (void *)&g_audio_engine_asr_score;

        g_audio_engine.aud_asr_handle = bk_aud_asr_init(&aud_asr_cfg);
        if (g_audio_engine.aud_asr_handle == NULL) {
            LOGE("aud asr init fail\n");
            (void)audio_engine_asr_stop();
            return AUDIO_ENGINE_ERR_ASR_INIT;
        }
    }

    if (BK_OK != bk_asr_start(g_audio_engine.asr_handle)) {
        LOGE("asr start fail\n");
        (void)audio_engine_asr_stop();
        return AUDIO_ENGINE_ERR_ASR_START;
    }

    if (BK_OK != bk_aud_asr_start(g_audio_engine.aud_asr_handle)) {
        LOGE("aud asr start fail\n");
        (void)audio_engine_asr_stop();
        return AUDIO_ENGINE_ERR_ASR_START;
    }

#if CONFIG_BEKEN_KWS
    g_audio_engine.asr_result = BK_KWS_NONE;
#else
    g_audio_engine.asr_result = 0;
#endif
    g_audio_engine.asr_started = true;
    LOGI("asr started on demand\n");
    return AUDIO_ENGINE_SUCCESS;
}

int audio_engine_asr_stop(void)
{
    int ret = AUDIO_ENGINE_SUCCESS;

    if (g_audio_engine.asr_started && g_audio_engine.aud_asr_handle) {
        if (BK_OK != bk_aud_asr_stop(g_audio_engine.aud_asr_handle)) {
            LOGE("aud asr stop failed\n");
            ret = AUDIO_ENGINE_ERR_ASR_STOP;
        }
    }
    if (g_audio_engine.asr_started && g_audio_engine.asr_handle) {
        if (BK_OK != bk_asr_stop(g_audio_engine.asr_handle)) {
            LOGE("asr stop failed\n");
            ret = AUDIO_ENGINE_ERR_ASR_STOP;
        }
    }

    if (g_audio_engine.aud_asr_handle) {
        bk_aud_asr_deinit(g_audio_engine.aud_asr_handle);
        g_audio_engine.aud_asr_handle = NULL;
    }
    if (g_audio_engine.asr_handle) {
        bk_asr_deinit(g_audio_engine.asr_handle);
        g_audio_engine.asr_handle = NULL;
    }

#if CONFIG_BEKEN_KWS
    g_audio_engine.asr_result = BK_KWS_NONE;
#else
    g_audio_engine.asr_result = 0;
#endif
    g_audio_engine.asr_started = false;
    LOGI("asr stopped on demand\n");
    return ret;
}
#endif

int audio_engine_start(audio_engine_cfg_t *cfg)
{
    int ret = 0;

    if (!cfg) {
        LOGE("Invalid configuration pointer\n");
        return AUDIO_ENGINE_ERR_INVALID_PARAM;
    }

    /* Check if already started */
    if (g_audio_engine.is_started) {
        LOGD("Audio engine already started\n");
        return AUDIO_ENGINE_SUCCESS;
    }

    os_memcpy(&g_audio_engine_cfg, cfg, sizeof(audio_engine_cfg_t));

    /* Validate parameters */
    if (cfg->mic_sample_rate != 8000 && cfg->mic_sample_rate != 16000) {
        LOGE("Invalid mic sample rate: %d\n", cfg->mic_sample_rate);
        return AUDIO_ENGINE_ERR_INVALID_PARAM;
    }

    if (cfg->spk_sample_rate != 8000 && cfg->spk_sample_rate != 16000) {
        LOGE("Invalid spk sample rate: %d\n", cfg->spk_sample_rate);
        return AUDIO_ENGINE_ERR_INVALID_PARAM;
    }

    /* Initialize voice configuration */
    voice_cfg_t *voice_cfg = (voice_cfg_t *)os_malloc(sizeof(voice_cfg_t));
    if (!voice_cfg) {
        LOGE("Failed to allocate memory for voice_cfg\n");
        return AUDIO_ENGINE_ERR_INIT_FAILED;
    }
    os_memset(voice_cfg, 0x00, sizeof(voice_cfg_t));

    /* Configure microphone */
    voice_cfg->mic_type = MIC_TYPE_ONBOARD;
    onboard_mic_stream_cfg_t onboard_mic_cfg = ONBOARD_MIC_ADC_STREAM_CFG_DEFAULT();
    // onboard_mic_cfg.adc_cfg.dig_gain = 0x30;
    // onboard_mic_cfg.adc_cfg.ana_gain = 0x08;
    onboard_mic_cfg.adc_cfg.sample_rate = cfg->mic_sample_rate;
    /* one frame size, 20ms */
    if (cfg->mic_sample_rate == 8000) {
            onboard_mic_cfg.frame_size = 160;
    } else {
            onboard_mic_cfg.frame_size = 320*2;
    }

#if CONFIG_ADK_ONBOARD_MIC_STREAM_V2
    onboard_mic_cfg.ch_bitmap = ONBOARD_MIC_ADC_DEFAULT_ACTIVE_CH_BITS;
    onboard_mic_cfg.adc_cfg.chl_num = 0;
    for(uint32_t j = 0; j < AUD_ADC_CHL_MAX; j++)
    {
        if(onboard_mic_cfg.ch_bitmap & (1 << j))
        {
            onboard_mic_cfg.adc_cfg.chl_num++;
        }
    }
    onboard_mic_cfg.adc_cfg.aec_en = cfg->aec_enable;
    onboard_mic_cfg.dmic_cfg.dmic_clk_gpio  = GPIO_50;
    onboard_mic_cfg.dmic_cfg.dmic_data_gpio = GPIO_49;
    onboard_mic_cfg.dmic_en = 1;
#endif

    voice_cfg->mic_cfg.onboard_mic_cfg = onboard_mic_cfg;

    /* Configure AEC */
    voice_cfg->aec_en = cfg->aec_enable;
    aec_v3_algorithm_cfg_t aec_cfg;
    if (cfg->aec_enable) {
        aec_cfg = (aec_v3_algorithm_cfg_t)DEFAULT_AEC_V3_ALGORITHM_CONFIG();
        aec_cfg.aec_cfg.mode = AEC_MODE_HARDWARE;
        if (aec_cfg.aec_cfg.mode == AEC_MODE_HARDWARE)
        {
            //onboard_mic_cfg.adc_cfg.chl_num = 2;
        #if CONFIG_ADK_AEC_V3_ALGORITHM_COMPONENT_V2
            aec_cfg.aec_cfg.ns_type        = NS_TRADITION;
            aec_cfg.aec_phase_cb           = bk_aec_phase_callback;
            /* SDK already produces a hysteresis-filtered VAD edge and a
             * post-AEC mic envelope (0..100). Hook both for the UI:
             *   vad_state_cb -> mic_active edge -> AI_LISTENING
             *   aec_level_cb -> mic_level meter -> EQ amplitude
             * No DIY ec_out_cb integration is needed. */
            aec_cfg.vad_state_cb           = bk_aec_vad_state_cb;
            aec_cfg.aec_level_cb           = bk_aec_level_cb;
            aec_cfg.aec_cfg.ec_only_output = 1;
            aec_cfg.aec_cfg.multi_output_use_ec_out = 1;
            aec_cfg.dual_ch            = 1;
            aec_cfg.multi_in_port_num  = 0;
            aec_cfg.vad_cfg.vad_enable = 1;
            aec_cfg.vad_cfg.vad_eng_threshold = 500;
        #endif
            voice_cfg->mic_cfg.onboard_mic_cfg = onboard_mic_cfg;
        }

        voice_cfg->aec_cfg.aec_alg_cfg = aec_cfg;
    }

    /* Configure encoder */
    voice_cfg->enc_en = true;
    voice_cfg->enc_type = cfg->enc_type;
    if (cfg->enc_type == AUDIO_ENC_TYPE_G711A) {
        g711_encoder_cfg_t g711_enc_cfg = DEFAULT_G711_ENCODER_CONFIG();
        if (cfg->mic_sample_rate == 8000) {
            g711_enc_cfg.buf_sz = 160;
            g711_enc_cfg.out_block_size = 160;
        } else {
            g711_enc_cfg.buf_sz = 320;
            g711_enc_cfg.out_block_size = 320;
        }
        voice_cfg->enc_cfg.g711_enc_cfg = g711_enc_cfg;
        voice_cfg->read_pool_size = (cfg->mic_sample_rate == 8000) ? 160 : 320;
    }
    #if CONFIG_VOICE_SERVICE_G722_ENCODER
    else if (cfg->enc_type == AUDIO_ENC_TYPE_G722) {
        g722_encoder_cfg_t g722_enc_cfg = DEFAULT_G722_ENCODER_CONFIG();
        if (cfg->mic_sample_rate == 8000) {
            g722_enc_cfg.buf_sz = 160;
            g722_enc_cfg.out_block_size = 80;
        } else {
            g722_enc_cfg.buf_sz = 320;
            g722_enc_cfg.out_block_size = 160;
        }
        voice_cfg->enc_cfg.g722_enc_cfg = g722_enc_cfg;
        voice_cfg->read_pool_size = (cfg->mic_sample_rate == 8000) ? 160 : 320;
    }
    #endif
    #if CONFIG_VOICE_SERVICE_OPUS_ENCODER
    else if (cfg->enc_type == AUDIO_ENC_TYPE_OPUS)
    {
        opus_enc_cfg_t opus_enc_cfg = DEFAULT_OPUS_ENC_CONFIG();
        voice_cfg->enc_cfg.opus_enc_cfg = opus_enc_cfg;
    }
    #endif
    else if (cfg->enc_type == AUDIO_ENC_TYPE_PCM) {
        voice_cfg->read_pool_size = (cfg->mic_sample_rate == 8000) ? 320 : 640;
    }

    /* Configure decoder */
    voice_cfg->dec_en = true;
    voice_cfg->dec_type = cfg->dec_type;
    if (cfg->dec_type == AUDIO_DEC_TYPE_G711A) {
        g711_decoder_cfg_t g711_dec_cfg = DEFAULT_G711_DECODER_CONFIG();
        if (cfg->spk_sample_rate == 8000) {
            g711_dec_cfg.buf_sz = 160;
            g711_dec_cfg.out_block_size = 320;
        } else {
            g711_dec_cfg.buf_sz = 320;
            g711_dec_cfg.out_block_size = 640;
        }
        voice_cfg->dec_cfg.g711_dec_cfg = g711_dec_cfg;
        voice_cfg->write_pool_size = (cfg->spk_sample_rate == 8000) ? 160 : 320;
    }
    #if CONFIG_VOICE_SERVICE_G722_DECODER
    else if (cfg->dec_type == AUDIO_DEC_TYPE_G722) {
        g722_decoder_cfg_t g722_dec_cfg = DEFAULT_G722_DECODER_CONFIG();
        if (cfg->spk_sample_rate == 8000) {
            g722_dec_cfg.buf_sz = 80;
            g722_dec_cfg.out_block_size = 320;
        } else {
            g722_dec_cfg.buf_sz = 160;
            g722_dec_cfg.out_block_size = 640;
        }
        voice_cfg->dec_cfg.g722_dec_cfg = g722_dec_cfg;
        voice_cfg->write_pool_size = (cfg->spk_sample_rate == 8000) ? 80 : 160;
    }
    #endif
    #if CONFIG_VOICE_SERVICE_OPUS_DECODER
    else if (cfg->dec_type == AUDIO_DEC_TYPE_OPUS)
    {
        opus_dec_cfg_t opus_dec_cfg = DEFAULT_OPUS_DEC_CONFIG();
        voice_cfg->dec_cfg.opus_dec_cfg = opus_dec_cfg;
    }
#endif
    else if (cfg->dec_type == AUDIO_DEC_TYPE_PCM) {
        voice_cfg->write_pool_size = (cfg->spk_sample_rate == 8000) ? 320 : 640;
    }

    /* Configure speaker */
    voice_cfg->spk_type = SPK_TYPE_ONBOARD;
    onboard_speaker_stream_cfg_t onboard_spk_cfg = ONBOARD_SPEAKER_STREAM_CFG_DEFAULT();
#if CONFIG_ADK_ONBOARD_SPEAKER_STREAM_V2
    for (uint32_t i = 0; i < AUD_DAC_SOURCE_MAX; i++)
    {
        onboard_spk_cfg.sample_rate[i] = cfg->spk_sample_rate;
        onboard_spk_cfg.frame_size[i]  = (cfg->spk_sample_rate == 8000) ? 320 : 640;
    }
    onboard_spk_cfg.dac_source_bitmap = ONBOARD_SPEAKER_STREAM_DAC_SOURCE_A2DP_BIT | ONBOARD_SPEAKER_STREAM_DAC_SOURCE_CALL_BIT;
#else
    onboard_spk_cfg.sample_rate = cfg->spk_sample_rate;
    onboard_spk_cfg.frame_size = (cfg->spk_sample_rate == 8000) ? 320 : 640;
#endif

    onboard_spk_cfg.pa_ctrl_en   = cfg->pa_enable;
    onboard_spk_cfg.pa_ctrl_gpio = cfg->pa_gpio;
    onboard_spk_cfg.pa_on_level  = cfg->pa_on_level;
    onboard_spk_cfg.pa_on_delay  = cfg->pa_on_delay;
    onboard_spk_cfg.pa_off_delay = cfg->pa_off_delay;
    // onboard_spk_cfg.dig_gain = cfg->dig_gain;
    // onboard_spk_cfg.ana_gain = cfg->ana_gain;
#if CONFIG_AE_SUPPORT_PROMPT_TONE
    onboard_spk_cfg.multi_in_port_num++;
#endif

#if CONFIG_ADK_ONBOARD_SPEAKER_STREAM_V2
    /* DAC-side voice-activity meter. The SDK applies its own threshold +
     * hysteresis (configured below) before flipping is_playing, so the UI
     * never sees a flapping SPEAKING state caused by occasional silence
     * frames inside agent speech. */
    onboard_spk_cfg.play_energy_threshold  = 8;
    onboard_spk_cfg.play_energy_hysteresis = 4;
    onboard_spk_cfg.status_cb              = bk_onboard_spk_status_cb;
    onboard_spk_cfg.status_cb_user_data    = NULL;
#endif

    voice_cfg->spk_cfg.onboard_spk_cfg = onboard_spk_cfg;

    /* Configure EQ */
#if CONFIG_VOICE_SERVICE_EQ
    if (cfg->eq_enable > 0) {
        voice_cfg->eq_en = true;
        eq_algorithm_cfg_t eq_cfg = DEFAULT_EQ_ALGORITHM_CONFIG();
        eq_cfg.eq_chl_num = cfg->eq_enable;
        eq_cfg.eq_mode = EQ_MODE_HARDWARE;
        voice_cfg->eq_cfg.eq_alg_cfg = eq_cfg;
    } else {
        voice_cfg->eq_en = false;
    }
#endif

#if (CONFIG_ASR_SERVICE)
    if (voice_cfg->aec_en) {
        voice_cfg->aec_cfg.aec_alg_cfg.multi_out_port_num++;
    } else
    {
        voice_cfg->mic_cfg.onboard_mic_cfg.multi_out_port_num++;
    }
#endif

#if CONFIG_AUDIO_PARA
    bk_audio_set_voc_cust_params(voice_cfg, AUD_SERVICE_AI_VOC);
#endif

    /* Configure event callback */
    voice_cfg->event_handle = cfg->event_cb;
    voice_cfg->args = cfg->user_data;

    /* Initialize voice service */
    g_audio_engine.voice_handle = bk_voice_init(voice_cfg);
    if (!g_audio_engine.voice_handle) {
        LOGE("Voice init failed\n");
        ret = AUDIO_ENGINE_ERR_VOICE_INIT;
        goto cleanup;
    }

#if CONFIG_AUDIO_PARA
    bk_app_aud_get_service_handle((void *)g_audio_engine.voice_handle, AUD_SERVICE_AI_VOC);
    set_app_aud_cust_service_handle((void *)g_audio_engine.voice_handle, AUD_SERVICE_AI_VOC);
#endif

    /* Initialize voice read service */
    voice_read_cfg_t voice_read_cfg = VOICE_READ_CFG_DEFAULT();
    voice_read_cfg.voice_handle = g_audio_engine.voice_handle;
    voice_read_cfg.max_read_size = (cfg->mic_sample_rate * 2 * 20 / 1000) / 4; // one frame size(20ms)
    voice_read_cfg.voice_read_callback = cfg->read_cb;
    voice_read_cfg.args = cfg->user_data;

    g_audio_engine.read_handle = bk_voice_read_init(&voice_read_cfg);
    if (!g_audio_engine.read_handle) {
        LOGE("Voice read init failed\n");
        ret = AUDIO_ENGINE_ERR_READ_INIT;
        goto cleanup_voice;
    }

    /* Initialize voice write service */
    voice_write_cfg_t voice_write_cfg = VOICE_WRITE_CFG_DEFAULT();
    voice_write_cfg.voice_handle = g_audio_engine.voice_handle;
    voice_write_cfg.node_size = voice_cfg->write_pool_size;
    voice_write_cfg.node_num = 10;// 10 frames buffer

    #if CONFIG_VOICE_SERVICE_OPUS_DECODER
    if(cfg->enc_type == AUDIO_ENC_TYPE_OPUS)
    {
        voice_write_cfg.write_buf_type = PORT_TYPE_FB;
        voice_write_cfg.node_size = 160;
        voice_write_cfg.node_num = 16;
    }
    #endif
    g_audio_engine.write_handle = bk_voice_write_init(&voice_write_cfg);
    if (!g_audio_engine.write_handle) {
        LOGE("Voice write init failed\n");
        ret = AUDIO_ENGINE_ERR_WRITE_INIT;
        goto cleanup_read;
    }

#if CONFIG_ADK_ONBOARD_SPEAKER_STREAM_V2
    if (BK_OK != audio_engine_adjust_voice_gain(g_volume_gain[g_volume_level])) {
        LOGE("apply initial speaker digital gain failed\n");
    }
#endif

    /* Start voice service */
    if (BK_OK != bk_voice_start(g_audio_engine.voice_handle)) {
        LOGE("Voice start failed\n");
        ret = AUDIO_ENGINE_ERR_VOICE_START;
        goto cleanup_write;
    }

    /* Start voice read service */
    if (BK_OK != bk_voice_read_start(g_audio_engine.read_handle)) {
        LOGE("Voice read start failed\n");
        ret = AUDIO_ENGINE_ERR_READ_START;
        goto cleanup_voice_start;
    }

    /* Start voice write service */
    if (BK_OK != bk_voice_write_start(g_audio_engine.write_handle)) {
        LOGE("Voice write start failed\n");
        ret = AUDIO_ENGINE_ERR_WRITE_START;
        goto cleanup_read_start;
    }

    if(voice_cfg)
    {
        os_free(voice_cfg);
        voice_cfg = NULL;
    }
    g_audio_engine.is_started = true;
    LOGI("Audio engine started successfully\n");
    return AUDIO_ENGINE_SUCCESS;

cleanup_read_start:
    bk_voice_read_stop(g_audio_engine.read_handle);
cleanup_voice_start:
    bk_voice_stop(g_audio_engine.voice_handle);
cleanup_write:
    bk_voice_write_deinit(g_audio_engine.write_handle);
    g_audio_engine.write_handle = NULL;
cleanup_read:
    bk_voice_read_deinit(g_audio_engine.read_handle);
    g_audio_engine.read_handle = NULL;
cleanup_voice:
    bk_voice_deinit(g_audio_engine.voice_handle);
    g_audio_engine.voice_handle = NULL;

cleanup:
    if(voice_cfg)
    {
        os_free(voice_cfg);
        voice_cfg = NULL;
    }
    return ret;
}

/**
 * @brief Stop audio engine and cleanup resources
 * 
 * @return int 
 *         - 0: Success
 *         - -1: Audio engine not started
 *         - -2: Stop operations failed
 */
int audio_engine_stop(void)
{
    if (!g_audio_engine.is_started) {
        LOGD("Audio engine not started\n");
        return AUDIO_ENGINE_ERR_NOT_STARTED;
    }

    int ret = AUDIO_ENGINE_SUCCESS;

#if (CONFIG_ASR_SERVICE)
    if (AUDIO_ENGINE_SUCCESS != audio_engine_asr_stop()) {
        ret = AUDIO_ENGINE_ERR_ASR_STOP;
    }
#if CONFIG_BEKEN_KWS
    g_audio_engine.asr_result = BK_KWS_NONE;
#else
    g_audio_engine.asr_result = 0;
#endif
    g_audio_engine.asr_started = false;
#endif

#if CONFIG_AE_SUPPORT_PROMPT_TONE
    if (BK_OK != audio_engine_prompt_tone_deinit(g_audio_engine_prompt_tone))
    {
        LOGE("deinit audio engine prompt tone fail\n");
        ret = BK_FAIL;
    }
    g_audio_engine_prompt_tone = NULL;
#endif

    /* Stop voice write service */
    if (g_audio_engine.write_handle) {
        if (BK_OK != bk_voice_write_stop(g_audio_engine.write_handle)) {
            LOGE("Voice write stop failed\n");
            ret = AUDIO_ENGINE_ERR_WRITE_STOP;
        }
    }

    /* Stop voice read service */
    if (g_audio_engine.read_handle) {
        if (BK_OK != bk_voice_read_stop(g_audio_engine.read_handle)) {
            LOGE("Voice read stop failed\n");
            ret = AUDIO_ENGINE_ERR_READ_STOP;
        }
    }

    /* Stop voice service */
    if (g_audio_engine.voice_handle) {
        if (BK_OK != bk_voice_stop(g_audio_engine.voice_handle)) {
            LOGE("Voice stop failed\n");
            ret = AUDIO_ENGINE_ERR_VOICE_STOP;
        }
    }

    /* Deinit voice write service */
    if (g_audio_engine.write_handle) {
        bk_voice_write_deinit(g_audio_engine.write_handle);
        g_audio_engine.write_handle = NULL;
    }

    /* Deinit voice read service */
    if (g_audio_engine.read_handle) {
        bk_voice_read_deinit(g_audio_engine.read_handle);
        g_audio_engine.read_handle = NULL;
    }

    /* Deinit voice service */
    if (g_audio_engine.voice_handle) {
        bk_voice_deinit(g_audio_engine.voice_handle);
        g_audio_engine.voice_handle = NULL;
    }

    g_audio_engine.is_started = false;
    LOGI("Audio engine stopped successfully\n");
    return ret;
}

/**
 * @brief Check if audio engine is currently running
 * 
 * @return bool 
 *         - true: Audio engine is running
 *         - false: Audio engine is not running
 */
bool audio_engine_is_running(void)
{
    return g_audio_engine.is_started;
}
/**
 * @brief Get audio engine encoder type
 * 
 * @return audio_enc_type_t 
 */
audio_enc_type_t audio_engine_get_encoder_type(void)
{
    if (!g_audio_engine.is_started) {
        return AUDIO_ENC_TYPE_INVALID;
    }
    return g_audio_engine_cfg.enc_type;
}
/**
 * @brief Get audio engine decoder type
 * 
 * @return audio_dec_type_t 
 */
audio_dec_type_t audio_engine_get_decoder_type(void)
{
    if (!g_audio_engine.is_started) {
        return AUDIO_DEC_TYPE_INVALID;
    }
    return g_audio_engine_cfg.dec_type;
}
/**
 * @brief Write audio data to speaker
 * 
 * @param data Pointer to audio data
 * @param size Size of audio data in bytes
 * @param timeout_ms Timeout in milliseconds
 * @return int 
 *         - 0: Success
 *         - < 0: Error codes
 */
/* Voice read callback example */
static int voice_read_callback(unsigned char *data, unsigned int len, void *args)
{
    int ret = 0;

    #if CONFIG_BK_NETWORK_TRANSFER
    #if (CONFIG_ASR_SERVICE) && (!CONFIG_AE_SEND_AUDIO_WITHOUT_ASR_RESULT)
    #if CONFIG_BEKEN_KWS
        if (g_audio_engine.asr_result == BK_KWS_ARMINO)
    #else
        if (g_audio_engine.asr_result == 1)
    #endif
    #endif
    {
        ret = ntwk_trans_send_audio(data, len, g_audio_engine_cfg.enc_type);
    }
    #else
    ret = bk_voice_write_frame_data(g_audio_engine.write_handle, (char *)data, len);
    #endif

    if (ret != len)
    {
        //LOGE("%s, %d, bk_voice_write_frame_data: %d != %d\n", __func__, __LINE__, ret, len);
    }
    else
    {
        //LOGD("%s, %d, len: %d\n", __func__, __LINE__, len);
    }

    return ret;
}
int audio_engine_write_data(const uint8_t *data, uint32_t size, uint32_t timeout_ms)
{
    if (!g_audio_engine.is_started || !g_audio_engine.write_handle) {
        return AUDIO_ENGINE_ERR_NOT_STARTED;
    }

    #if (CONFIG_ASR_SERVICE)
    #if CONFIG_BEKEN_KWS
        if (g_audio_engine.asr_result == BK_KWS_BYEBYE)
    #else
        if (g_audio_engine.asr_result != 1)
    #endif
    {
        return AUDIO_ENGINE_ERR_ASR_STOP;
    }
    #endif
    return bk_voice_write_frame_data(g_audio_engine.write_handle, (char *)data, size);
}

/**
 * @brief Get audio engine error string
 * 
 * @param err Error code
 * @return const char* Error description string
 */
const char *audio_engine_err_to_str(int err)
{
    switch (err) {
        case AUDIO_ENGINE_SUCCESS:
            return "Success";
        case AUDIO_ENGINE_ERR_INVALID_PARAM:
            return "Invalid parameters";
        case AUDIO_ENGINE_ERR_NOT_STARTED:
            return "Audio engine not started";
        case AUDIO_ENGINE_ERR_VOICE_INIT:
            return "Voice init failed";
        case AUDIO_ENGINE_ERR_READ_INIT:
            return "Voice read init failed";
        case AUDIO_ENGINE_ERR_WRITE_INIT:
            return "Voice write init failed";
        case AUDIO_ENGINE_ERR_VOICE_START:
            return "Voice start failed";
        case AUDIO_ENGINE_ERR_READ_START:
            return "Voice read start failed";
        case AUDIO_ENGINE_ERR_WRITE_START:
            return "Voice write start failed";
        case AUDIO_ENGINE_ERR_VOICE_STOP:
            return "Voice stop failed";
        case AUDIO_ENGINE_ERR_READ_STOP:
            return "Voice read stop failed";
        case AUDIO_ENGINE_ERR_WRITE_STOP:
            return "Voice write stop failed";
        case AUDIO_ENGINE_ERR_ASR_INIT:
            return "ASR init failed";
        case AUDIO_ENGINE_ERR_ASR_START:
            return "ASR start failed";
        case AUDIO_ENGINE_ERR_ASR_STOP:
            return "ASR stop failed";
        default:
            return "Unknown error";
    }
}
/* Voice event callback example */
static bk_err_t voice_event_callback(voice_evt_t event, void *data, void *args)
{
    LOGI("Voice event: %d\n", event);
    return BK_OK;
}

/**
 * @brief Convert string to audio encoder type
 * 
 * @param enc_str Encoder type string ("PCM", "G711A", "G711U", "G722", "OPUS")
 * @return audio_enc_type_t Encoder type enum value
 */
audio_enc_type_t audio_engine_str_to_enc_type(const char *enc_str)
{
    if (!enc_str) {
        LOGE("Invalid encoder type string pointer\n");
        return AUDIO_ENC_TYPE_INVALID;
    }

    if (strcasecmp(enc_str, "PCM") == 0) {
        return AUDIO_ENC_TYPE_PCM;
    } else if (strcasecmp(enc_str, "G711A") == 0) {
        return AUDIO_ENC_TYPE_G711A;
    } else if (strcasecmp(enc_str, "G711U") == 0) {
        return AUDIO_ENC_TYPE_G711U;
    } else if (strcasecmp(enc_str, "G722") == 0) {
        return AUDIO_ENC_TYPE_G722;
    } else if (strcasecmp(enc_str, "OPUS") == 0) {
        return AUDIO_ENC_TYPE_OPUS;
    } else {
        LOGE("Unknown encoder type string: %s\n", enc_str);
        return AUDIO_ENC_TYPE_INVALID;
    }
}

/**
 * @brief Convert string to audio decoder type
 * 
 * @param dec_str Decoder type string ("PCM", "G711A", "G711U", "G722", "OPUS")
 * @return audio_dec_type_t Decoder type enum value
 */
audio_dec_type_t audio_engine_str_to_dec_type(const char *dec_str)
{
    if (!dec_str) {
        LOGE("Invalid decoder type string pointer\n");
        return AUDIO_DEC_TYPE_INVALID;
    }

    if (strcasecmp(dec_str, "PCM") == 0) {
        return AUDIO_DEC_TYPE_PCM;
    } else if (strcasecmp(dec_str, "G711A") == 0) {
        return AUDIO_DEC_TYPE_G711A;
    } else if (strcasecmp(dec_str, "G711U") == 0) {
        return AUDIO_DEC_TYPE_G711U;
    } else if (strcasecmp(dec_str, "G722") == 0) {
        return AUDIO_DEC_TYPE_G722;
    } else if (strcasecmp(dec_str, "OPUS") == 0) {
        return AUDIO_DEC_TYPE_OPUS;
    } else {
        LOGE("Unknown decoder type string: %s\n", dec_str);
        return AUDIO_DEC_TYPE_INVALID;
    }
}

int audio_engine_init(void)
{
    audio_engine_cfg_t cfg = {
        .mic_sample_rate = CONFIG_AE_AUDIO_ADC_SAMP_RATE,
        .spk_sample_rate = CONFIG_AE_AUDIO_DAC_SAMP_RATE,
        .aec_enable   = 3,
        .eq_enable    = 1,
        .enc_type     = AUDIO_ENC_TYPE_INVALID,
        .dec_type     = AUDIO_DEC_TYPE_INVALID,
        .event_cb     = voice_event_callback,
        .read_cb      = voice_read_callback,
        .user_data    = NULL,
        #if CONFIG_AE_ENABLE_PA_CNTRL
        .pa_enable    = true,
        .pa_gpio      = CONFIG_AE_PA_CNTRL_GPIO,
        .pa_on_level  = CONFIG_AE_PA_ON_LEVEL,
        .pa_on_delay  = CONFIG_AE_PA_ON_DELAY,
        .pa_off_delay = CONFIG_AE_PA_OFF_DELAY,
        #else
        .pa_enable    = false,
        .pa_gpio      = 0,
        .pa_on_level  = 0,
        .pa_on_delay  = 0,
        .pa_off_delay = 0,
        #endif
        // .dig_gain = CONFIG_AE_DEFAULT_DIG_GAIN,
        // .ana_gain = CONFIG_AE_DEFAULT_ANA_GAIN,
    };

    if (AUDIO_ENGINE_SUCCESS != audio_engine_volume_init()) {
        LOGE("Failed to init volume\n");
        return AUDIO_ENGINE_ERR_INIT_FAILED;
    }

    LOGD("audio encoder type: %s\n", CONFIG_AE_AUDIO_ENCODER_TYPE);
    LOGD("audio decoder type: %s\n", CONFIG_AE_AUDIO_DECODER_TYPE);
    cfg.enc_type = audio_engine_str_to_enc_type(CONFIG_AE_AUDIO_ENCODER_TYPE);
    cfg.dec_type = audio_engine_str_to_dec_type(CONFIG_AE_AUDIO_DECODER_TYPE);

    int ret = audio_engine_start(&cfg);
    if (ret != AUDIO_ENGINE_SUCCESS) {
        LOGE("Failed to start audio engine: %s\n", audio_engine_err_to_str(ret));
        return ret;
    }

#if CONFIG_AE_SUPPORT_PROMPT_TONE
    audio_engine_prompt_tone_cfg_t prompt_tone_cfg = DEFAULT_AUDIO_ENGINE_PROMPT_TONE_CONFIG();
    bk_voice_get_spkstr(g_audio_engine.voice_handle, &prompt_tone_cfg.spk_stream);
    g_audio_engine_prompt_tone = audio_engine_prompt_tone_init(&prompt_tone_cfg);
    if (!g_audio_engine_prompt_tone)
    {
        LOGE("Failed to init prompt tone\n");
        return AUDIO_ENGINE_ERR_INIT_FAILED;
    }

    extern int bk_aud_engine_cli_init(void);
    bk_aud_engine_cli_init();
#endif

    LOGI("audio engine started successfully\n");
    return AUDIO_ENGINE_SUCCESS;
}

int audio_engine_deinit(void)
{
    return audio_engine_stop();
}
