#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <components/system.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include "network_engine.h"
#include "cli.h"
#if CONFIG_VOLC_RTC_EN
#include "bk_volc_api.h"
#elif CONFIG_AGORA_IOT_SDK
#include "bk_agora_api.h"
#endif
#if CONFIG_BK_TRANS_EN
#include "bk_trans_api.h"
#endif
#if CONFIG_BK_AUDIO_ENGINE
#include "audio_engine.h"
#endif
#if CONFIG_BK_VIDEO_ENGINE
#include "video_engine.h"
#endif

#define TAG "ntwk_eng"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)

/**
 * @brief 网络传输模块全局上下文实例
 */
static ntwk_eng_ctx_t g_ntwk_eng_ctx = {0};

/* Uplink audio mute flag. Set by ntwk_eng_set_uplink_audio_muted() from any
 * task (UI / camera_preview workers) and read by the audio engine task that
 * pushes encoded frames into ntwk_eng_send_audio(). volatile is enough here
 * -- a single-byte flag flip is racy at worst by one frame, which is fine.
 *
 * Downlink (ntwk_eng_recv_audio) is intentionally NOT gated by this flag:
 * the agent's voice still needs to reach the speaker. */
static volatile bool s_uplink_audio_muted = false;

#if CONFIG_VOLC_RTC_EN || CONFIG_AGORA_IOT_SDK
extern bool g_connected_flag;
#endif

static const char *ntwk_eng_network_type_name(network_type_t network_type)
{
    switch (network_type) {
    case NETWORK_TYPE_VOLC_RTC:
        return "volc_rtc";
    case NETWORK_TYPE_AGORA_RTC:
        return "agora_rtc";
    case NETWORK_TYPE_BK_TRANS:
        return "bk_trans";
    default:
        return "unknown";
    }
}

static int ntwk_eng_register_volc_rtc(ntwk_eng_ctx_t *ctx)
{
    if (!ctx) {
        return BK_FAIL;
    }

#if CONFIG_VOLC_RTC_EN
    ctx->network_type = NETWORK_TYPE_VOLC_RTC;
    ctx->audio_tx_cb = bk_byte_rtc_audio_data_send;
    ctx->video_tx_cb = bk_byte_rtc_video_data_send;
    ctx->start_cb = bk_byte_start;
    ctx->stop_cb = bk_byte_stop;
    ctx->pre_config_cb = bk_byte_pre_config;
    ctx->update_cb = bk_byte_update_agent;
    return BK_OK;
#else
    LOGE("Volc RTC backend is not enabled\n");
    return -1;
#endif
}

static int ntwk_eng_register_agora_rtc(ntwk_eng_ctx_t *ctx)
{
    if (!ctx) {
        return BK_FAIL;
    }

#if CONFIG_AGORA_IOT_SDK
    ctx->network_type = NETWORK_TYPE_AGORA_RTC;
    ctx->audio_tx_cb = bk_agora_rtc_audio_data_send;
    ctx->video_tx_cb = bk_agora_rtc_video_data_send;
    ctx->start_cb = bk_agora_start;
    ctx->stop_cb = bk_agora_stop;
    ctx->pre_config_cb = bk_agora_pre_config;
    ctx->update_cb = bk_agora_update_agent;
    return BK_OK;
#else
    LOGE("Agora RTC backend is not enabled\n");
    return -1;
#endif
}

static int ntwk_eng_register_bk_trans(ntwk_eng_ctx_t *ctx)
{
    if (!ctx) {
        return BK_FAIL;
    }

#if CONFIG_BK_TRANS_EN
    ctx->network_type = NETWORK_TYPE_BK_TRANS;
    ctx->audio_tx_cb = bk_trans_audio_data_send;
    ctx->video_tx_cb = bk_trans_video_data_send;
    ctx->start_cb = bk_trans_start;
    ctx->stop_cb = bk_trans_stop;
    ctx->pre_config_cb = bk_trans_pre_config;
    ctx->update_cb = bk_trans_update;
    return BK_OK;
#else
    LOGE("BK transfer backend is not enabled\n");
    return -1;
#endif
}

static int ntwk_eng_init_backend(network_type_t network_type)
{
    switch (network_type) {
    case NETWORK_TYPE_VOLC_RTC:
        return ntwk_eng_register_volc_rtc(&g_ntwk_eng_ctx);
    case NETWORK_TYPE_AGORA_RTC:
        return ntwk_eng_register_agora_rtc(&g_ntwk_eng_ctx);
    case NETWORK_TYPE_BK_TRANS:
        return ntwk_eng_register_bk_trans(&g_ntwk_eng_ctx);
    default:
        LOGE("Invalid ntwk engine backend %s(%d)\n",
             ntwk_eng_network_type_name(network_type), network_type);
        return -1;
    }
}

static int ntwk_eng_deinit_backend(network_type_t network_type, void *user_data)
{
    switch (network_type) {
    case NETWORK_TYPE_VOLC_RTC:
    case NETWORK_TYPE_AGORA_RTC:
        (void)user_data;
        return BK_OK;
    case NETWORK_TYPE_BK_TRANS:
#if CONFIG_BK_TRANS_EN
        return bk_trans_deinit(user_data);
#else
        break;
#endif
    default:
        LOGE("Invalid ntwk engine backend %s(%d)\n",
             ntwk_eng_network_type_name(network_type), network_type);
        return -1;
    }

    LOGE("ntwk engine backend %s(%d) is not enabled\n",
         ntwk_eng_network_type_name(network_type), network_type);
    return -1;
}

int ntwk_eng_update(void *user_data, void *update_info)
{
    int ret = 0;

    if (!g_ntwk_eng_ctx.initialized) {
        LOGW("%s: NTWK engine not initialized\n", __func__);
        return -1;
    }
    if (g_ntwk_eng_ctx.update_cb) {
        ret = g_ntwk_eng_ctx.update_cb(user_data, update_info);
    }
    if (ret != 0) {
        LOGE("Failed to update ntwk engine\n");
        return ret;
    }
    LOGI("ntwk engine updated\n");
    return 0;
}
/**
 * @brief 启动网络传输
 * @param user_data 用户数据指针，传递给启动回调函数
 * @return int 0表示成功，负数表示失败
 */
int ntwk_eng_start(void *user_data)
{
    int ret = 0;
    if (!g_ntwk_eng_ctx.initialized) {
        LOGW("%s: NTWK engine not initialized\n", __func__);
        return -1;
    }

    if (g_ntwk_eng_ctx.is_started) {
        LOGW("ntwk engine already started\n");
        return 0;
    }

    // 调用启动回调函数
    if (g_ntwk_eng_ctx.start_cb) {
        ret = g_ntwk_eng_ctx.start_cb(user_data);
    }
    if (ret != 0) {
        LOGE("Failed to start ntwk engine\n");
        return ret;
    }

    g_ntwk_eng_ctx.is_started = true;
    LOGI("ntwk engine started\n");

    return 0;
}

bool ntwk_eng_is_started(void)
{
    return g_ntwk_eng_ctx.initialized && g_ntwk_eng_ctx.is_started;
}

/**
 * @brief 停止网络传输
 * @param user_data 用户数据指针，传递给停止回调函数
 * @return int 0表示成功，负数表示失败
 */
int ntwk_eng_stop(void *user_data)
{
    int ret = 0;

    if (!g_ntwk_eng_ctx.initialized) {
        LOGW("%s: NTWK engine not initialized\n", __func__);
        return -1;
    }

    if (!g_ntwk_eng_ctx.is_started) {
        LOGW("ntwk engine not started\n");
        return -1;
    }

    g_ntwk_eng_ctx.is_started = false;

    // 调用停止回调函数
    if (g_ntwk_eng_ctx.stop_cb) {
        ret = g_ntwk_eng_ctx.stop_cb(user_data);
    }

    if (ret != 0) {
        LOGE("Failed to stop ntwk engine\n");
        return ret;
    }

    LOGI("ntwk engine stopped\n");

    return 0;
}
/**
 * @brief 初始化网络传输模块
 * @return int 0表示成功，负数表示失败
 */
static int ntwk_eng_init(network_type_t network_type)
{
    int ret = 0;

    if (g_ntwk_eng_ctx.initialized) {
        if (g_ntwk_eng_ctx.network_type == network_type) {
            LOGW("ntwk engine already initialized with %s\n",
                 ntwk_eng_network_type_name(network_type));
            return 0;
        }

        LOGE("ntwk engine already initialized with %s, deinit before switching to %s\n",
             ntwk_eng_network_type_name(g_ntwk_eng_ctx.network_type),
             ntwk_eng_network_type_name(network_type));
        return -1;
    }

    ret = ntwk_eng_init_backend(network_type);
    if (ret != 0) {
        return ret;
    }

    // 执行预配置回调
    if (g_ntwk_eng_ctx.pre_config_cb) {
        ret = g_ntwk_eng_ctx.pre_config_cb(g_ntwk_eng_ctx.user_data);
    }

    if (ret != 0) {
        LOGE("Failed to initialize ntwk engine\n");
        os_memset(&g_ntwk_eng_ctx, 0, sizeof(ntwk_eng_ctx_t));
        return ret;
    }

    g_ntwk_eng_ctx.initialized = true;
    LOGI("ntwk engine initialized with backend: %s(%d)\n",
         ntwk_eng_network_type_name(g_ntwk_eng_ctx.network_type),
         g_ntwk_eng_ctx.network_type);

    return 0;
}

int ntwk_eng_rtc_init(void)
{
#if CONFIG_VOLC_RTC_EN
    return ntwk_eng_init(NETWORK_TYPE_VOLC_RTC);
#elif CONFIG_AGORA_IOT_SDK
    return ntwk_eng_init(NETWORK_TYPE_AGORA_RTC);
#else
    LOGE("RTC backend is not enabled\n");
    return -1;
#endif
}

int ntwk_eng_bk_trans_init(void)
{
    return ntwk_eng_init(NETWORK_TYPE_BK_TRANS);
}

/**
 * @brief 反初始化网络传输模块
 * @return int 0表示成功，负数表示失败
 */
int ntwk_eng_deinit(void)
{
    int ret = 0;

    if (!g_ntwk_eng_ctx.initialized) {
        LOGW("%s: NTWK engine not initialized\n", __func__);
        return 0;
    }

    // 先停止网络传输
    if (g_ntwk_eng_ctx.is_started) {
        ret = ntwk_eng_stop(g_ntwk_eng_ctx.user_data);
        if (ret != 0) {
            LOGE("Failed to deinitialize network transfer\n");
            return ret;
        }
    }

    ret = ntwk_eng_deinit_backend(g_ntwk_eng_ctx.network_type,
                                  g_ntwk_eng_ctx.user_data);
    if (ret != 0) {
        LOGE("Failed to deinitialize backend %s\n",
             ntwk_eng_network_type_name(g_ntwk_eng_ctx.network_type));
        return ret;
    }

    // 清空全局上下文
    os_memset(&g_ntwk_eng_ctx, 0, sizeof(ntwk_eng_ctx_t));
    LOGI("Network transfer deinitialized\n");

    return 0;
}

/**
 * @brief 发送音频数据到网络
 * @param data 音频数据指针
 * @param size 音频数据大小
 * @param audio_type 音频编码类型
 * @return int 0表示成功，负数表示失败
 */
int ntwk_eng_send_audio(const uint8_t *data, size_t size, audio_enc_type_t audio_type)
{
    int ret = 0;

    if (!g_ntwk_eng_ctx.initialized) {
        //LOGW("%s: NTWK engine not initialized\n", __func__);
        return -1;
    }

    // 参数校验
    if (!data || size == 0) {
        LOGE("Invalid audio data parameters\n");
        return -2;
    }

    if (audio_type == AUDIO_ENC_TYPE_INVALID) {
        LOGE("Invalid audio type\n");
        return -3;
    }

    /* Uplink muted (e.g. while camera_preview is recognizing a JPEG): drop
     * the frame quietly and pretend we sent it. The audio engine pipeline
     * keeps running so unmuting later resumes instantly, and the downlink
     * recv path is untouched -- agent voice still reaches the speaker. */
    if (s_uplink_audio_muted) {
        return (int)size;
    }

    // 调用音频发送回调函数
    if (g_ntwk_eng_ctx.audio_tx_cb) {
        ret = g_ntwk_eng_ctx.audio_tx_cb((uint8_t *)data, size, audio_type);
    }

    if (ret < 0) {
        //LOGE("Failed to send audio data, ret:%d\n", ret);
        return ret;
    }

    return ret;
}

/**
 * @brief 发送控制通道数据
 * @param data 控制数据指针
 * @param size 控制数据大小
 * @return int 发送结果
 */
int ntwk_eng_send_ctrl(const uint8_t *data, size_t size)
{
    if (!g_ntwk_eng_ctx.initialized) {
        LOGE("%s: NTWK engine not initialized\n", __func__);
        return -1;
    }

    if (!data || size == 0) {
        LOGE("Invalid ctrl data parameters\n");
        return -2;
    }

    if (g_ntwk_eng_ctx.network_type != NETWORK_TYPE_BK_TRANS) {
        LOGW("ctrl channel is only supported by bk_trans backend\n");
        return -3;
    }

#if CONFIG_BK_TRANS_EN
    return bk_trans_ctrl_send((uint8_t *)data, size);
#else
    return BK_FAIL;
#endif
}

/**
 * @brief 发送视频数据到网络
 * @param data 视频数据指针
 * @param size 视频数据大小
 * @param video_type 视频类型
 * @return int 0表示成功，负数表示失败
 */
int ntwk_eng_send_video(frame_buffer_t *frame)
{
    int ret = 0;

    if (!g_ntwk_eng_ctx.initialized) {
        LOGE("%s: NTWK engine not initialized\n", __func__);
        return -1;
    }

    // 参数校验
    if (!frame || !frame->frame || frame->length == 0) {
        LOGE("Invalid video frame parameters\n");
        return -2;
    }

    // 调用视频发送回调函数
    if (g_ntwk_eng_ctx.video_tx_cb) {
        ret = g_ntwk_eng_ctx.video_tx_cb(frame);
    } else {
        LOGW("video_tx_cb not set, video data dropped\n");
        return -4;
    }

    if (ret < 0) {
        //LOGE("Failed to send video data, ret:%d\n", ret);
        return ret;
    }

    return ret;
}

int ntwk_eng_abort_video_send(bool abort)
{
    if (!g_ntwk_eng_ctx.initialized) {
        return -1;
    }

    switch (g_ntwk_eng_ctx.network_type) {
#if CONFIG_BK_TRANS_EN
    case NETWORK_TYPE_BK_TRANS:
        return (bk_trans_abort_video_send(abort) == BK_OK) ? 0 : -1;
#endif
    default:
        /* RTC backend 的视频发送不会长时间阻塞，无需 abort */
        (void)abort;
        return 0;
    }
}

int ntwk_eng_abort_audio_send(bool abort)
{
    if (!g_ntwk_eng_ctx.initialized) {
        return -1;
    }

    switch (g_ntwk_eng_ctx.network_type) {
#if CONFIG_BK_TRANS_EN
    case NETWORK_TYPE_BK_TRANS:
        return (bk_trans_abort_audio_send(abort) == BK_OK) ? 0 : -1;
#endif
    default:
        /* RTC backend 的音频发送不会长时间阻塞，无需 abort */
        (void)abort;
        return 0;
    }
}

/**
 * @brief 获取当前网络传输类型
 * @return network_type_t 网络类型枚举值
 */
network_type_t ntwk_eng_get_network_type(void)
{
    return g_ntwk_eng_ctx.network_type;
}

bool ntwk_eng_is_agent_connected(void)
{
    if (!g_ntwk_eng_ctx.initialized) {
        return false;
    }

    switch (g_ntwk_eng_ctx.network_type) {
    case NETWORK_TYPE_VOLC_RTC:
    case NETWORK_TYPE_AGORA_RTC:
#if CONFIG_VOLC_RTC_EN || CONFIG_AGORA_IOT_SDK
        return g_connected_flag;
#else
        return false;
#endif
    case NETWORK_TYPE_BK_TRANS:
#if CONFIG_BK_TRANS_EN
        return bk_trans_is_connected();
#else
        return false;
#endif
    default:
        return false;
    }
}

void ntwk_eng_set_uplink_audio_muted(bool muted)
{
    if (s_uplink_audio_muted == muted) {
        return;
    }
    s_uplink_audio_muted = muted;
    LOGI("uplink audio %s\n", muted ? "muted" : "unmuted");
}

bool ntwk_eng_uplink_audio_is_muted(void)
{
    return s_uplink_audio_muted;
}

int ntwk_eng_send_image_with_query(const uint8_t *jpeg, size_t jpeg_len,
                                     const char *query)
{
#if CONFIG_AGORA_IOT_SDK && CONFIG_AGORA_RTC_USE_STRING_UID
    return (BK_OK == bk_agora_rtc_send_image_with_query(jpeg, jpeg_len, query))
               ? 0 : -1;
#else
    (void)jpeg;
    (void)jpeg_len;
    (void)query;
    LOGW("send_image_with_query: no RTM-capable RTC backend in this build\n");
    return -1;
#endif
}

/**
 * @brief 接收音频数据并写入音频引擎
 * @param data 音频数据指针
 * @param size 音频数据大小
 * @return int 0表示成功，负数表示失败
 */
int ntwk_eng_recv_audio(const uint8_t *data, size_t size)
{
    if (!g_ntwk_eng_ctx.initialized) {
        LOGE("%s: NTWK engine not initialized\n", __func__);
        return -1;
    }

    // 参数校验
    if (!data || size == 0) {
        LOGE("Invalid audio data parameters\n");
        return -2;
    }

    /* No SPEAKING heartbeat or spk-level estimator is needed here anymore:
     * the audio_engine now hooks onboard_speaker_stream::status_cb, which
     * fires on real DAC-side activity and drives both the AI_SPEAKING/IDLE
     * state edge and the spk_level meter used by the EQ bars. */

    // 如果音频引擎已启用，则将数据写入音频引擎
    #if CONFIG_BK_AUDIO_ENGINE
    return audio_engine_write_data(data, size, 0);
    #else
    LOGE("Audio engine not enabled\n");
    return 0;
    #endif
}
/**
 * @brief 获取音频编码器类型
 * @return audio_enc_type_t 音频编码器类型枚举值
 */
audio_enc_type_t ntwk_eng_get_audio_encoder_type(void)
{
    // 如果音频引擎已启用，则从音频引擎获取编码器类型
    #if CONFIG_BK_AUDIO_ENGINE
    return audio_engine_get_encoder_type();
    #else
    LOGW("Audio engine not enabled\n");
    return AUDIO_ENC_TYPE_INVALID;
    #endif
}