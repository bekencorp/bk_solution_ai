/**
 * @file jolly_video.c
 * @brief SD-NAND MP4 (H.264) 动画播放器（bk_video_player 引擎版）。
 *
 * 播放 1:/jolly/{a,b,question}.mp4（H.264，无音轨）：
 * - 容器解析走 mp4 parser，解码走 HW H264 frame decoder，PP 直接输出
 *   RGB565，免色彩转换；
 * - 两块 320x320 RGB565 帧缓冲来自 16MB UNCODED slab（不占 1.4MB PSRAM
 *   堆，无需开机预分配）；
 * - 零拷贝 ping-pong：buffer_alloc_cb 把空闲帧交给解码器，decode_complete_cb
 *   在 disp 锁内把 canvas 指到刚解码完的帧；解码与显示各用一块，无撕裂；
 * - 无音轨文件自动走视频时钟，引擎自带 24fps PTS 节奏；播完由管理线程
 *   通过 play_file 无缝回卷；
 * - 素材缺失时容器隐藏，2s 重试；上层（voice_chat_page）回退普通布局。
 */
#include "jolly_video.h"

#if CONFIG_MUSE_GADGET

#include <stdio.h>
#include <string.h>

#include <components/log.h>
#include <os/mem.h>
#include <os/os.h>

#include "bk_posix.h"
#include "bk_partition.h"
#include "board_usb_switch.h"
#include "lv_vendor.h"
#include "ui_theme.h"         /* ui_disp_is_lvgl_thread() */

#include <components/bk_frame_buffer.h>
#include <components/media_types.h>
#include <components/bk_video_player/bk_video_player_engine.h>
#include <components/bk_video_player/bk_video_player_types.h>
#include <components/bk_video_player/container_parser/bk_video_player_mp4_parser.h>
#include <components/bk_video_player/video_decoder/bk_video_player_hw_h264_decoder.h>

#define TAG "jolly_video"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define JV_SCREEN_W      385        /* 逻辑屏 385(宽)x320(高)：物理 320x385 竖屏 + ROTATE_90 */
#define JV_FRAME_W       384        /* 视频铺满逻辑屏宽（385 是奇数，H264 yuv420 不支持，384 只差 1px） */
#define JV_FRAME_H       320
#define JV_FRAME_X       ((JV_SCREEN_W - JV_FRAME_W) / 2)   /* 0 */
#define JV_FRAME_Y       0
#define JV_FRAME_BYTES   (JV_FRAME_W * JV_FRAME_H * 2U)     /* RGB565 = 245760 */

#define JV_SLOT_CNT      2          /* ping-pong：一块解码一块显示 */
#define JV_RETRY_MS      2000       /* 素材缺失重试间隔 */
/* 3072 会在首次起播时溢出：play_file → mp4 解析（mp4_stco_cache_get_rel
 * 栈上 buf[512] + mp4_stco_cache_t rel[64]≈556B）→ VFS → FATFS → SDIO
 * 的完整链条超过 3KB（2026-11-10 实机 UsageFault STKOF，内核报
 * "Current Task stack overflow: jolly_vid"）。 */
#define JV_CTRL_STACK    6144
#define JV_CTRL_PRIO     5

typedef enum {
    JV_SLOT_FREE = 0,   /* 可交给解码器 */
    JV_SLOT_DECODING,   /* 已交给引擎（alloc 后、complete/free 前） */
    JV_SLOT_DISPLAYED,  /* canvas 正在显示 */
} jv_slot_state_t;

typedef struct {
    uint8_t          *mem;          /* UNCODED slab 帧缓冲 */
    jv_slot_state_t   state;
} jv_slot_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *canvas;

    jv_slot_t slots[JV_SLOT_CNT];
    int       displayed;            /* canvas 当前指向的 slot */

    bk_video_player_engine_handle_t engine;
    bool      engine_open;

    beken_thread_t ctrl_task;
    beken_semaphore_t ctrl_sem;
    volatile bool task_exit;
    volatile bool task_running;
    volatile bool want_play;        /* 上层期望播放中 */
    volatile bool need_replay;      /* 播完一轮，需无缝回卷 */
    volatile bool clip_changed;
    jolly_clip_t clip;
    char        path[24];           /* 当前 clip 的文件路径 */

    bool   mounted;
    bool   assets_ok;               /* 上次播放尝试成功（素材存在） */
    uint32_t last_retry;
    bool   no_assets_logged;
} jv_ctx_t;

static jv_ctx_t s_jv;

/* 极小占位帧：destroy 时把 canvas 从 slab 帧上解绑，避免悬空指针。 */
static uint16_t s_dummy_fb[4 * 4];

/* clip -> 文件名。question.mp4 不是单字母命名，所以不能再用 %c。 */
static const char *const kJvClipFile[] = {
    "a.mp4",          /* JOLLY_CLIP_VOICE_INPUT */
    "b.mp4",          /* JOLLY_CLIP_REPLY_BUBBLE */
    "question.mp4",   /* JOLLY_CLIP_QUESTION */
};

static void jv_clip_path(char *out, size_t cap, jolly_clip_t clip)
{
    /* mp4lib 经 VFS open() 访问（video_osi_wrapper），必须用 VFS 挂载
     * 路径 /sd0，不是 FatFS 盘符 1:（field: f_opendir 能列出文件但
     * f_open "1:/jolly/a.mp4" 失败，换 /sd0 后正常）。 */
    if ((int)clip < 0 || (size_t)clip >= sizeof(kJvClipFile) / sizeof(kJvClipFile[0])) {
        clip = JOLLY_CLIP_VOICE_INPUT;
    }
    snprintf(out, cap, "/sd0/jolly/%s", kJvClipFile[clip]);
}

/* ---- SD-NAND ----------------------------------------------------------------*/

/*
 * 注意：mp4lib 的文件读写经 VFS POSIX open()（video_osi_wrapper），所以
 * 必须走 VFS 挂载点 /sd0，不能用 FatFS 盘符直挂（field: FatFS 层能看到
 * 文件、引擎 open "1:/jolly/a.mp4" 却失败）。VFS 的 FATFS_DEV_SDCARD
 * 在 devices[] 中索引为 1，正好映射到同一张 SD-NAND 的盘符 1:。
 * 为避免同一卷被两个 FATFS 对象重复挂载（缓存不一致），本模块只用
 * VFS mount/umount，不再自己持有 FATFS 句柄。
 */
static bool jv_mount(void)
{
    if (s_jv.mounted) {
        return true;
    }
    if (board_usb_switch_prepare_nand_access() != BK_OK) {
        return false;
    }
    if (board_sd_nand_power_on() != BK_OK) {
        return false;
    }
    rtos_delay_milliseconds(50);

    struct bk_fatfs_partition part;
    memset(&part, 0, sizeof(part));
    part.part_type = FATFS_DEVICE;
    part.part_dev.device_name = (void *)FATFS_DEV_SDCARD;
    part.mount_path = VFS_SD_0_PATITION_0;   /* "/sd0" */
    if (mount("SOURCE_NONE", part.mount_path, "fatfs", 0, &part) != 0) {
        return false;
    }
    s_jv.mounted = true;
    LOGI("SD-NAND mounted at %s\r\n", VFS_SD_0_PATITION_0);

    /* 诊断：列出素材目录内容（一次性，最多 8 条）。
     * VFS 挂载后若素材是经 USB MSC 从 PC 侧新拷入的，目录视图以本次
     * 挂载时刻为准——此处打印即为引擎真实所见。 */
    {
        DIR *dir = opendir("/sd0/jolly");
        if (dir != NULL) {
            int n = 0;
            struct dirent *ent;
            while (n < 8 && (ent = readdir(dir)) != NULL) {
                LOGI("  jolly/ %-12s %lu bytes\r\n", ent->d_name,
                     (unsigned long)ent->d_size);
                n++;
            }
            if (n == 0) {
                LOGW("  jolly/ is EMPTY\r\n");
            }
            closedir(dir);
        } else {
            LOGW("  /sd0/jolly dir missing\r\n");
        }
    }
    return true;
}

static void jv_unmount(void)
{
    if (s_jv.mounted) {
        (void)umount(VFS_SD_0_PATITION_0);
    }
    s_jv.mounted = false;
}

/* ---- LVGL --------------------------------------------------------------------*/

/*
 * g_disp_mutex 不可重入：disp 线程（页面 enter/teardown 都在其上）已经持锁，
 * 再调 lv_vendor_disp_lock() 会自我死锁——表现为 LVGL 任务冻结、tp 队列满、
 * 整机无响应（field 2026-11-04）。disp 线程上直接操作 LVGL 是安全的
 * （锁本来就是为了挡其它线程），其它线程（视频解码线程）则必须拿锁。
 */
static void jv_disp_lock(void)
{
    if (!ui_disp_is_lvgl_thread()) {
        lv_vendor_disp_lock();
    }
}

static void jv_disp_unlock(void)
{
    if (!ui_disp_is_lvgl_thread()) {
        lv_vendor_disp_unlock();
    }
}

static void jv_show(bool show)
{
    jv_disp_lock();
    if (s_jv.root != NULL && lv_obj_is_valid(s_jv.root)) {
        if (show) {
            lv_obj_clear_flag(s_jv.root, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_jv.root, LV_OBJ_FLAG_HIDDEN);
        }
    }
    jv_disp_unlock();
}

/* ---- 引擎 buffer 回调 ----------------------------------------------------------
 * 帧缓冲生命周期：alloc（FREE->DECODING）-> decode_complete（->DISPLAYED，
 * 旧 DISPLAYED->FREE）；引擎在自己持有失败帧时调 free_cb（DECODING->FREE）。
 * complete 之后引擎不再触碰该缓冲，由我们在切走时回收。
 * ------------------------------------------------------------------------------*/

static avdk_err_t jv_frame_alloc_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;
    if (buffer == NULL || buffer->length == 0) {
        return AVDK_ERR_INVAL;
    }
    if (buffer->length > JV_FRAME_BYTES) {
        LOGE("frame alloc too big: %lu\r\n", (unsigned long)buffer->length);
        return AVDK_ERR_NOMEM;
    }
    for (int i = 0; i < JV_SLOT_CNT; i++) {
        if (s_jv.slots[i].state == JV_SLOT_FREE) {
            s_jv.slots[i].state = JV_SLOT_DECODING;
            buffer->data = s_jv.slots[i].mem;
            buffer->frame_buffer = NULL;
            /* user_data 存 idx+1：idx=0 会被回调里的 NULL 校验误判为非法。 */
            buffer->user_data = (void *)(uintptr_t)(i + 1);
            static uint32_t s_alloc_log;
            if (s_alloc_log < 6U) {
                s_alloc_log++;
                LOGI("frame alloc idx=%d\r\n", i);
            }
            return AVDK_ERR_OK;
        }
    }
    /* 无空闲帧：返回 NOMEM，引擎会带背压重试（不丢包）。 */
    static uint32_t s_alloc_warn_ms;
    uint32_t now = rtos_get_time();
    if ((uint32_t)(now - s_alloc_warn_ms) >= 1000U) {
        s_alloc_warn_ms = now;
        LOGW("frame alloc NOMEM: slot0=%d slot1=%d displayed=%d\r\n",
             (int)s_jv.slots[0].state, (int)s_jv.slots[1].state, s_jv.displayed);
    }
    return AVDK_ERR_NOMEM;
}

static void jv_frame_free_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;
    if (buffer == NULL || buffer->user_data == NULL) {
        LOGW("frame_free_cb: buf=%p user_data=%p (no-op)\r\n",
             buffer, buffer ? buffer->user_data : NULL);
        return;
    }
    int idx = (int)(uintptr_t)buffer->user_data - 1;   
    LOGD("frame_free_cb: idx=%d state=%d\r\n",
         idx, (idx >= 0 && idx < JV_SLOT_CNT) ? (int)s_jv.slots[idx].state : -1);
    if (idx >= 0 && idx < JV_SLOT_CNT &&
        s_jv.slots[idx].state == JV_SLOT_DECODING) {
        s_jv.slots[idx].state = JV_SLOT_FREE;
    }
    buffer->data = NULL;
    buffer->length = 0;
}

static void jv_video_decode_complete_cb(void *user_data,
                                        const video_player_video_frame_meta_t *meta,
                                        video_player_buffer_t *buffer)
{
    (void)user_data;
    (void)meta;
    /* 诊断：cb 入口就记录，user_data 为空也能看见。 */
    static uint32_t s_cb_count;
    s_cb_count++;
    bool verbose = (s_cb_count <= 5U) || ((s_cb_count % 48U) == 0U);
    if (verbose) {
        LOGD("cb #%lu entry buf=%p data=%p user_data=%p len=%lu\r\n",
             (unsigned long)s_cb_count, buffer, buffer ? buffer->data : NULL,
             buffer ? buffer->user_data : NULL,
             buffer ? (unsigned long)buffer->length : 0UL);
    }
    if (buffer == NULL || buffer->data == NULL || buffer->user_data == NULL) {
        return;
    }
    int idx = (int)(uintptr_t)buffer->user_data - 1;   /* alloc 存的是 idx+1 */
    if (idx < 0 || idx >= JV_SLOT_CNT) {
        return;
    }

    jv_disp_lock();
    if (verbose) {
        LOGD("cb #%lu idx=%d got disp_lock canvas=%p\r\n",
             (unsigned long)s_cb_count, idx, s_jv.canvas);
    }
    if (s_jv.canvas != NULL && lv_obj_is_valid(s_jv.canvas)) {
        lv_canvas_set_buffer(s_jv.canvas, s_jv.slots[idx].mem,
                             JV_FRAME_W, JV_FRAME_H, LV_COLOR_FORMAT_RGB565);
        int old = s_jv.displayed;
        s_jv.slots[idx].state = JV_SLOT_DISPLAYED;
        s_jv.displayed = idx;
        if (old >= 0 && old != idx &&
            s_jv.slots[old].state == JV_SLOT_DISPLAYED) {
            s_jv.slots[old].state = JV_SLOT_FREE;
        }
        lv_obj_invalidate(s_jv.canvas);
    } else {
        /* canvas 已销毁：直接回收 */
        s_jv.slots[idx].state = JV_SLOT_FREE;
    }
    jv_disp_unlock();
}

/* 码流 packet 缓冲：CODED slab（参考 video_player_example 做法）。 */
static avdk_err_t jv_packet_alloc_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;
    if (buffer == NULL || buffer->length == 0) {
        return AVDK_ERR_INVAL;
    }
    const uint32_t alloc_size = (buffer->length + 2048U + 63U) & ~63U;
    void *p = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, alloc_size);
    if (p == NULL) {
        LOGE("packet alloc failed: len=%lu\r\n", (unsigned long)buffer->length);
        buffer->data = NULL;
        buffer->length = 0;
        return AVDK_ERR_NOMEM;
    }
    buffer->data = (uint8_t *)p;
    buffer->frame_buffer = p;
    buffer->user_data = NULL;
    return AVDK_ERR_OK;
}

static void jv_packet_free_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;
    if (buffer != NULL && buffer->frame_buffer != NULL) {
        bk_frame_buffer_free(buffer->frame_buffer);
        buffer->frame_buffer = NULL;
        buffer->data = NULL;
        buffer->length = 0;
    }
}

/* 音频桩：片源无音轨，这些回调不会被调用，但引擎校验要求非空。 */
static avdk_err_t jv_audio_alloc_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;
    if (buffer == NULL || buffer->length == 0) {
        return AVDK_ERR_INVAL;
    }
    buffer->data = (uint8_t *)os_malloc(buffer->length);
    if (buffer->data == NULL) {
        buffer->length = 0;
        return AVDK_ERR_NOMEM;
    }
    return AVDK_ERR_OK;
}

static void jv_audio_free_cb(void *user_data, video_player_buffer_t *buffer)
{
    (void)user_data;
    if (buffer != NULL && buffer->data != NULL) {
        os_free(buffer->data);
        buffer->data = NULL;
        buffer->length = 0;
    }
}

static void jv_audio_decode_complete_cb(void *user_data,
                                        const video_player_audio_packet_meta_t *meta,
                                        video_player_buffer_t *buffer)
{
    (void)user_data;
    (void)meta;
    (void)buffer;
    /* 无音轨：不会触发；即便触发也直接丢弃（保持静音）。 */
}

static void jv_finished_cb(void *user_data, const char *file_path)
{
    (void)user_data;
    (void)file_path;
    /* 播完一轮：通知管理线程无缝回卷（不能在此上下文直接调 play_file）。 */
    s_jv.need_replay = true;
    if (s_jv.ctrl_sem != NULL) {
        rtos_set_semaphore(&s_jv.ctrl_sem);
    }
}

static void jv_error_cb(void *user_data, avdk_err_t error)
{
    (void)user_data;
    LOGW("engine async error=%d\r\n", (int)error);
}

/* ---- 引擎 --------------------------------------------------------------------*/

static int jv_engine_create(void)
{
    bk_video_player_config_t cfg;
    os_memset(&cfg, 0, sizeof(cfg));

    cfg.video.parser_to_decode_buffer_count = 2;
    cfg.video.decode_to_output_buffer_count = 2;
    cfg.video.packet_buffer_alloc_cb = jv_packet_alloc_cb;
    cfg.video.packet_buffer_free_cb = jv_packet_free_cb;
    cfg.video.buffer_alloc_cb = jv_frame_alloc_cb;
    cfg.video.buffer_free_cb = jv_frame_free_cb;
    cfg.video.decode_complete_cb = jv_video_decode_complete_cb;
    cfg.video.output_format = PIXEL_FMT_RGB565;
    cfg.video.rotate_degree = 0;
    cfg.video.display_width = 0;    /* 保持片源 320x320，不做 PP 缩放 */
    cfg.video.display_height = 0;

    cfg.audio.parser_to_decode_buffer_count = 2;
    cfg.audio.decode_to_output_buffer_count = 2;
    cfg.audio.buffer_alloc_cb = jv_audio_alloc_cb;
    cfg.audio.buffer_free_cb = jv_audio_free_cb;
    cfg.audio.decode_complete_cb = jv_audio_decode_complete_cb;

    cfg.playback_finished_cb = jv_finished_cb;
    cfg.error_cb = jv_error_cb;
    cfg.user_data = NULL;

    avdk_err_t ret = bk_video_player_engine_new(&s_jv.engine, &cfg);
    if (ret != AVDK_ERR_OK || s_jv.engine == NULL) {
        LOGE("engine_new failed, ret=%d\r\n", (int)ret);
        return -1;
    }

    ret = bk_video_player_engine_register_container_parser(
        s_jv.engine, bk_video_player_get_mp4_parser_ops());
    if (ret != AVDK_ERR_OK) {
        LOGE("register mp4 parser failed, ret=%d\r\n", (int)ret);
        goto fail;
    }
    ret = bk_video_player_engine_register_video_decoder(
        s_jv.engine, bk_video_player_get_hw_h264_decoder_frame_ops());
    if (ret != AVDK_ERR_OK) {
        LOGE("register h264 frame decoder failed, ret=%d\r\n", (int)ret);
        goto fail;
    }

    ret = bk_video_player_engine_open(s_jv.engine);
    if (ret != AVDK_ERR_OK) {
        LOGE("engine_open failed, ret=%d\r\n", (int)ret);
        goto fail;
    }
    s_jv.engine_open = true;
    return 0;

fail:
    bk_video_player_engine_delete(s_jv.engine);
    s_jv.engine = NULL;
    return -1;
}

static void jv_engine_destroy(void)
{
    if (s_jv.engine == NULL) {
        return;
    }
    if (s_jv.want_play) {
        (void)bk_video_player_engine_stop(s_jv.engine);
    }
    if (s_jv.engine_open) {
        (void)bk_video_player_engine_close(s_jv.engine);
        s_jv.engine_open = false;
    }
    (void)bk_video_player_engine_delete(s_jv.engine);
    s_jv.engine = NULL;
}

/* ---- 管理线程：挂载、起播、回卷、缺素材重试 -------------------------------------*/

static bool jv_engine_start_clip(void)
{
    avdk_err_t ret = bk_video_player_engine_play_file(s_jv.engine, s_jv.path);
    if (ret != AVDK_ERR_OK) {
        return false;
    }
    s_jv.assets_ok = true;
    s_jv.no_assets_logged = false;
    return true;
}

static void jv_ctrl_main(void *arg)
{
    (void)arg;
    bool engine_playing = false;

    s_jv.task_running = true;
    LOGI("ctrl task started\r\n");

    while (!s_jv.task_exit) {
        (void)rtos_get_semaphore(&s_jv.ctrl_sem, 100);

        if (!s_jv.want_play) {
            if (engine_playing) {
                (void)bk_video_player_engine_stop(s_jv.engine);
                engine_playing = false;
            }
            continue;
        }

        if (!jv_mount()) {
            rtos_delay_milliseconds(200);
            continue;
        }

        /* 首次起播 / 切 clip / 播完回卷 / 失败后到点重试 */
        bool want_restart = false;
        if (!engine_playing) {
            if (s_jv.assets_ok || s_jv.last_retry == 0U ||
                (uint32_t)(rtos_get_time() - s_jv.last_retry) >= JV_RETRY_MS) {
                want_restart = true;
            }
        } else if (s_jv.clip_changed) {
            want_restart = true;   /* 换片：必须整轮重启（set_file_path） */
        } else if (s_jv.need_replay) {
            /* 同一片 EOF 回卷：seek(0) 走 SDK 的 seek_inplace 路径——
             * parser/decoder/线程全部保留，只做会话重置。此前用 play_file
             * 整轮拆建（每 10s 一次），重建期语音发送停摆 3-6 秒，泵积压
             * 导致 chunk pool 灌满、录音被截断（2026-11-06 实机）。 */
            s_jv.need_replay = false;
            avdk_err_t sret = bk_video_player_engine_seek(s_jv.engine, 0);
            if (sret == AVDK_ERR_OK) {
                continue;
            }
            LOGW("seek rewind failed (%d); fall back to full restart\r\n",
                 (int)sret);
            want_restart = true;
        }

        if (!want_restart) {
            continue;
        }
        s_jv.clip_changed = false;
        s_jv.need_replay = false;

        if (jv_engine_start_clip()) {
            if (!engine_playing) {
                jv_show(true);
            }
            engine_playing = true;
        } else {
            if (!s_jv.no_assets_logged) {
                LOGW("play %s failed (missing assets?)\r\n", s_jv.path);
                s_jv.no_assets_logged = true;
            }
            s_jv.assets_ok = false;
            s_jv.last_retry = rtos_get_time();
            engine_playing = false;
            jv_show(false);
            /* 卸载卷：素材可能是本卷挂载后经 USB MSC 从 PC 拷入的，
             * FatFS 的旧目录视图看不到新文件，下次重试强制重新挂载。 */
            jv_unmount();
        }
    }

    if (engine_playing) {
        (void)bk_video_player_engine_stop(s_jv.engine);
    }
    s_jv.task_running = false;
    rtos_delete_thread(NULL);
}

/* ---- 公共 API -----------------------------------------------------------------*/

int jolly_video_create(lv_obj_t *parent)
{
    if (s_jv.root != NULL) {
        return 0;
    }
    memset(&s_jv, 0, sizeof(s_jv));

    /* 帧缓冲来自 16MB UNCODED slab，不占 PSRAM 堆；失败整块清理。 */
    for (int i = 0; i < JV_SLOT_CNT; i++) {
        s_jv.slots[i].mem = (uint8_t *)bk_frame_buffer_malloc(
            MEM_SLAB_HEAP_UNCODED, JV_FRAME_BYTES);
        if (s_jv.slots[i].mem == NULL) {
            LOGE("slab alloc failed (slot %d)\r\n", i);
            for (int j = 0; j < i; j++) {
                bk_frame_buffer_free(s_jv.slots[j].mem);
                s_jv.slots[j].mem = NULL;
            }
            return -1;
        }
        memset(s_jv.slots[i].mem, 0, JV_FRAME_BYTES);
    }
    /* canvas 初始绑 slot0；slot1 空闲。 */
    s_jv.slots[0].state = JV_SLOT_DISPLAYED;
    s_jv.slots[1].state = JV_SLOT_FREE;
    s_jv.displayed = 0;
    s_jv.clip = JOLLY_CLIP_VOICE_INPUT;
    jv_clip_path(s_jv.path, sizeof(s_jv.path), s_jv.clip);

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, JV_SCREEN_W, JV_FRAME_H);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    s_jv.root = root;

    lv_obj_t *canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(canvas, s_jv.slots[0].mem, JV_FRAME_W, JV_FRAME_H,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, JV_FRAME_X, JV_FRAME_Y);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    s_jv.canvas = canvas;

    jv_show(false);   /* 默认隐藏，起播成功后再显示 */

    if (rtos_init_semaphore(&s_jv.ctrl_sem, 1) != BK_OK) {
        LOGE("sem init failed\r\n");
        goto fail_mem;
    }

    if (jv_engine_create() != 0) {
        goto fail_sem;
    }

    if (rtos_create_thread(&s_jv.ctrl_task, JV_CTRL_PRIO, "jolly_vid",
                           jv_ctrl_main, JV_CTRL_STACK, NULL) != BK_OK) {
        LOGE("ctrl task create failed\r\n");
        goto fail_engine;
    }
    return 0;

fail_engine:
    jv_engine_destroy();
fail_sem:
    rtos_deinit_semaphore(&s_jv.ctrl_sem);
    s_jv.ctrl_sem = NULL;
fail_mem:
    for (int i = 0; i < JV_SLOT_CNT; i++) {
        if (s_jv.slots[i].mem != NULL) {
            bk_frame_buffer_free(s_jv.slots[i].mem);
            s_jv.slots[i].mem = NULL;
        }
    }
    s_jv.root = NULL;
    s_jv.canvas = NULL;
    return -2;
}

void jolly_video_destroy(void)
{
    s_jv.want_play = false;
    s_jv.task_exit = true;
    if (s_jv.ctrl_sem != NULL) {
        rtos_set_semaphore(&s_jv.ctrl_sem);
    }
    for (int i = 0; i < 100 && s_jv.task_running; i++) {
        rtos_delay_milliseconds(20);
    }
    jv_engine_destroy();

    if (s_jv.ctrl_sem != NULL) {
        rtos_deinit_semaphore(&s_jv.ctrl_sem);
        s_jv.ctrl_sem = NULL;
    }
    if (s_jv.mounted) {
        jv_unmount();
    }

    /* 把 canvas 解绑到静态占位帧，再释放 slab；LVGL 对象随父 screen 销毁。 */
    jv_disp_lock();
    if (s_jv.canvas != NULL && lv_obj_is_valid(s_jv.canvas)) {
        lv_canvas_set_buffer(s_jv.canvas, s_dummy_fb, 4, 4,
                             LV_COLOR_FORMAT_RGB565);
    }
    jv_disp_unlock();

    bk_frame_buffer_free(s_jv.slots[0].mem);
    bk_frame_buffer_free(s_jv.slots[1].mem);
    memset(&s_jv, 0, sizeof(s_jv));
}

bool jolly_video_play(jolly_clip_t clip)
{
    if (s_jv.root == NULL) {
        return false;
    }
    if (s_jv.clip != clip) {
        s_jv.clip = clip;
        jv_clip_path(s_jv.path, sizeof(s_jv.path), clip);
        s_jv.clip_changed = true;
    }
    s_jv.want_play = true;
    if (s_jv.ctrl_sem != NULL) {
        rtos_set_semaphore(&s_jv.ctrl_sem);
    }
    return s_jv.assets_ok;
}

void jolly_video_stop(void)
{
    s_jv.want_play = false;
    s_jv.need_replay = false;
    jv_show(false);
}

bool jolly_video_available(void)
{
    return s_jv.root != NULL && s_jv.want_play && s_jv.assets_ok;
}

#else /* !CONFIG_MUSE_GADGET */

int  jolly_video_create(lv_obj_t *parent) { (void)parent; return -1; }
void jolly_video_destroy(void) {}
bool jolly_video_play(jolly_clip_t clip) { (void)clip; return false; }
void jolly_video_stop(void) {}
bool jolly_video_available(void) { return false; }

#endif /* CONFIG_MUSE_GADGET */
