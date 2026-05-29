#ifndef __NETWORK_ENGINE_H__
#define __NETWORK_ENGINE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <components/media_types.h>
#include <components/bk_voice_service_types.h>

/**
 * @brief network type
 */
typedef enum {
    NETWORK_TYPE_VOLC_RTC = 0,    /**< 火山引擎RTC */
    NETWORK_TYPE_AGORA_RTC,       /**< 声网RTC */
    NETWORK_TYPE_BK_TRANS,        /**< BK SDK network transfer */
    NETWORK_TYPE_MAX
} network_type_t;

/**
 * @brief 音频发送回调函数类型
 * @param data_ptr 音频数据指针
 * @param data_len 数据长度
 * @param audio_type 音频编码类型
 * @return int 发送结果
 */
typedef int (*audio_tx_callback_t)(uint8_t *data_ptr, size_t data_len, audio_enc_type_t audio_type);

/**
 * @brief 视频发送回调函数类型
 * 
 * 该回调函数用于发送视频帧数据到网络。frame_buffer_t 结构包含了完整的帧信息，
 * 包括数据指针、数据长度、格式、时间戳等。
 * 
 * @param frame 视频帧缓冲区指针，包含以下信息：
 *              - frame->frame: 视频数据指针
 *              - frame->length: 数据长度
 *              - frame->fmt: 像素格式 (PIXEL_FMT_JPEG/PIXEL_FMT_H264/等)
 *              - frame->width: 帧宽度
 *              - frame->height: 帧高度
 *              - frame->sequence: 帧序号
 *              - frame->timestamp: 时间戳
 *              - frame->h264_type: H264帧类型（仅H264格式有效）
 * 
 * @return int 发送结果
 *         - BK_OK (0): 成功
 *         - BK_FAIL (-1): 失败
 */
typedef int (*video_tx_callback_t)(frame_buffer_t *frame);
/**
 * @brief 网络传输启动回调函数类型
 * @param user_data 用户数据
 * @return bk_err_t 启动结果
 */
typedef bk_err_t (*ntwk_eng_start_callback_t)(void *user_data);

/**
 * @brief 网络传输停止回调函数类型
 * @param user_data 用户数据
 * @return bk_err_t 停止结果
 */
typedef bk_err_t (*ntwk_eng_stop_callback_t)(void *user_data);

/**
 * @brief 网络传输预配置回调函数类型
 * @param user_data 用户数据
 * @return bk_err_t 预配置结果
 */
typedef bk_err_t (*ntwk_eng_pre_config_callback_t)(void *user_data);
/**
 * @brief 网络传输更新回调函数类型
 * @param user_data 用户数据
 * @return bk_err_t 更新结果
 */
typedef bk_err_t (*ntwk_eng_update_callback_t)(void *user_data, void *update_info);
/**
 * @brief 网络传输模块上下文结构体
 */
typedef struct {
    network_type_t network_type;           /**< network type */
    void *network_config;              /**< network特定配置 */
    audio_tx_callback_t audio_tx_cb; /**< 音频接收回调 */
    video_tx_callback_t video_tx_cb; /**< 视频接收回调 */
    ntwk_eng_start_callback_t start_cb; /**< 网络传输开始回调 */
    ntwk_eng_stop_callback_t stop_cb; /**< 网络传输停止回调 */
    ntwk_eng_pre_config_callback_t pre_config_cb; /**< 网络传输预配置回调 */
    ntwk_eng_update_callback_t update_cb; /**< 网络传输更新回调 */
    void *user_data;               /**< 用户数据 */
    bool is_started;               /**< 是否已启动 */
    bool initialized;                      /**< 初始化标志 */
} ntwk_eng_ctx_t;
/**
 * @brief 初始化 RTC 网络传输后端
 * @return int 初始化结果
 */
int ntwk_eng_rtc_init(void);

/**
 * @brief 初始化 BK network transfer 后端
 * @return int 初始化结果
 */
int ntwk_eng_bk_trans_init(void);

/**
 * @brief 反初始化网络传输模块
 * @return int 反初始化结果
 */
int ntwk_eng_deinit(void);

/**
 * @brief 更新网络传输
 * @param user_data 用户数据
 * @param update_info 更新信息
 * @return int 更新结果
 */
int ntwk_eng_update(void *user_data, void *update_info);
/**
 * @brief 启动网络传输
 * @param user_data 用户数据指针，传递给启动回调函数
 * @return int 0表示成功，负数表示失败
 */
int ntwk_eng_start(void *user_data);
bool ntwk_eng_is_started(void);
/**
 * @brief 停止网络传输
 * @param user_data 用户数据指针，传递给停止回调函数
 * @return int 0表示成功，负数表示失败
 */
int ntwk_eng_stop(void *user_data);
/**
 * @brief 发送音频数据
 * @param data 音频数据指针
 * @param size 数据大小
 * @param audio_type 音频编码类型
 * @return int 发送结果
 */
int ntwk_eng_send_audio(const uint8_t *data, size_t size, audio_enc_type_t audio_type);


/**
 * @brief 处理并发送视频帧
 * @param frame 视频帧缓冲区指针
 * @return int 0表示成功，负数表示失败
 */
int ntwk_eng_send_video(frame_buffer_t *frame); 
/**
 * @brief 获取当前network类型
 * @return network_type_t network类型
 */
network_type_t ntwk_eng_get_network_type(void);
/**
 * @brief 获取音频编码器类型
 * @return audio_enc_type_t 音频编码器类型
 */
audio_enc_type_t ntwk_eng_get_audio_encoder_type(void);

/**
 * @brief 接收音频数据
 * @param data 音频数据指针
 * @param size 数据大小
 * @return int 接收结果
 */
int ntwk_eng_recv_audio(const uint8_t *data, size_t size);

/**
 * @brief Whether the underlying RTC engine is currently joined to the agent.
 *
 * UI components that show per-agent state (e.g. page_chat_anim) can call this
 * on attach to pick a sensible initial state when the AGENT_JOINED app_event
 * was already broadcast before the page existed.
 *
 * @return true  if both volc/agora g_connected_flag is set,
 *         false otherwise (not initialized, or not yet joined).
 */
bool ntwk_eng_is_agent_connected(void);

/**
 * @brief Mute or unmute the uplink audio path to the agent / LLM.
 *
 * When muted, ntwk_eng_send_audio() drops every encoded frame on the floor
 * BEFORE calling the backend audio_tx callback, returning success to the
 * caller so the audio engine pipeline stays running.
 *
 * Use case (e.g. camera_preview's photo-recognition flow): the device still
 * wants the agent online to receive a freshly captured JPEG over RTM, but
 * does NOT want the user's voice to be transcribed in the same turn -- that
 * would race with the image and confuse the multimodal LLM. Muting the
 * uplink keeps the downlink agent voice working as usual.
 *
 * Idempotent. Default state is "not muted".
 *
 * @param muted  true to drop uplink audio; false to resume sending.
 */
void ntwk_eng_set_uplink_audio_muted(bool muted);

/**
 * @brief Current uplink audio mute state (see ntwk_eng_set_uplink_audio_muted).
 */
bool ntwk_eng_uplink_audio_is_muted(void);

/**
 * @brief Backend-agnostic shim: upload a JPEG to the active AI agent and
 *        trigger an LLM image-recognition turn in one call.
 *
 * Dispatches to the currently-selected RTC backend's
 * "send_image_with_query" implementation, with all the agora/volc
 * specific headers + types kept behind this component boundary. Use it
 * from app-level modules (e.g. camera_preview) that should not need to
 * pull in agora SDK headers (those live in a component-private include
 * path that's not exported app-wide).
 *
 * Currently only the Agora backend with CONFIG_AGORA_RTC_USE_STRING_UID
 * actually implements the RTM image-upload + user.transcription pair;
 * other backends return -1 so the caller can fail gracefully.
 *
 * See bk_agora_rtc_send_image_with_query() for the per-backend contract
 * (size limits, RTM login pre-req, default prompt).
 *
 * @param[in] jpeg      Raw JPEG bytes (NOT base64-encoded).
 * @param[in] jpeg_len  Length in bytes; backend may impose an upper bound
 *                      (Agora: <= 22 KB once base64-encoded into RTM).
 * @param[in] query     UTF-8 prompt fed to the LLM together with the
 *                      image; pass NULL to use the backend default.
 *
 * @return 0 on success; <0 on failure (no backend support, RTC/RTM not
 *         joined, oversized image, or SDK submit error).
 */
int ntwk_eng_send_image_with_query(const uint8_t *jpeg, size_t jpeg_len,
                                     const char *query);

#ifdef __cplusplus
}
#endif
#endif /* __NETWORK_ENGINE_H__ */
