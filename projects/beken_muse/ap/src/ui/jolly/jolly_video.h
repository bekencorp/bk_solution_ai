/**
 * @file jolly_video.h
 * @brief SD-NAND MP4(H.264) 动画播放器：bk_video_player 引擎 + HW H264
 *        解码（RGB565 直出），画面呈到 320x320 LVGL canvas（居中于
 *        385x320 逻辑屏，两侧留边）。
 *
 * 素材：1:/jolly/a.mp4（语音输入）与 1:/jolly/b.mp4（回复气泡），
 * 320x320 / H.264 / 无音轨。文件不存在时组件保持隐藏，上层页面
 * （voice_chat_page）自动落回普通布局。
 *
 * 帧缓冲来自 16MB UNCODED slab（不占 PSRAM 堆）；解码线程与 LVGL
 * 之间以 ping-pong 双缓冲零拷贝交换，无撕裂。播完自动无缝回卷。
 */
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JOLLY_CLIP_VOICE_INPUT = 0,   /* 1:/jolly/a.mp4  语音输入页（麦克风） */
    JOLLY_CLIP_REPLY_BUBBLE,      /* 1:/jolly/b.mp4  回复对话框页 */
} jolly_clip_t;

/**
 * @brief 创建播放器容器并启动管理线程（内部建引擎、分配 slab 帧缓冲）。
 * @return 0 成功；非 0 失败。
 */
int  jolly_video_create(lv_obj_t *parent);

/* 销毁：停播、停引擎、回收帧缓冲（LVGL 对象随父 screen 销毁）。 */
void jolly_video_destroy(void);

/**
 * @brief 循环播放某条 clip；切 clip 自动从头播。
 * 首次播放时挂载 SD-NAND（1:）；素材缺失时容器隐藏并返回 false，
 * 之后每 2s 自动重试一次。
 */
bool jolly_video_play(jolly_clip_t clip);

/* 停播：容器隐藏，引擎停止。 */
void jolly_video_stop(void);

/* 当前是否有素材且在播（上层据此决定显不显示后备 UI）。 */
bool jolly_video_available(void);

#ifdef __cplusplus
}
#endif

