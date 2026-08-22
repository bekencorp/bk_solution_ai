/**
 * @file baf_raw.c
 *
 * Raw (LVGL-independent) BAF playback backend for baf_example.
 *
 * Compiled when CONFIG_LVGL is off (the default RAW backend; see ap/CMakeLists.txt
 * and ap/Kconfig.projbuild). It drives the closed bk_baf decoder + GPU compositor
 * directly:
 *   decode frame -> baf_gpu_compose_frame() into a LINEAR ARGB8888 framebuffer
 *   -> bk_display_flush() straight to the jd9855 DPU panel.
 * A dedicated render thread just polls the decoder (which paces playback to the
 * source frame durations internally) and flushes each frame. No LVGL / lv_vendor.
 */

#include "baf_raw.h"

#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <common/avdk_pixel_types.h>
#include <components/bk_display.h>          /* umbrella: bus + panel + display ctlr */
#include <avdk_error.h>
#include <lcd/lcd_mipi_jd9855_320x385.h>
#include <bk_baf.h>
#include "baf_file.h"                       /* play a .baf file off the TF card */
#include "tf_card.h"                         /* enumerate the /baf playlist */

#define TAG "baf_raw"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Same board bring-up as the LVGL path (Robot V1): panel reset GPIO_5, backlight
 * GPIO_7. The peripheral 3.3V rail and aux LDO are enabled in main() before this
 * backend starts, so they are not repeated here. */
#define PANEL_RESET_PIN        GPIO_5
#define PANEL_BACKLIGHT_PIN    GPIO_7

/* jd9855 MIPI DSI panel geometry (portrait). */
#define PANEL_WIDTH            320
#define PANEL_HEIGHT           385

/* Opaque background for the frame's transparent regions, matching the LVGL
 * baf_page screen background (BAF_BG_COLOR 0xD0D0D0). ARGB8888 with alpha 0xFF so
 * the single-layer DPU scanout shows it (the raw path has no LVGL screen behind
 * the animation). */
#define BAF_RAW_BG_ARGB        0xFFD0D0D0U

#define BAF_RAW_TASK_PRIORITY   4
#define BAF_RAW_TASK_STACK_SIZE (1024 * 16)
/* Poll cadence while the decode pipeline reports "not ready yet". */
#define BAF_RAW_WAIT_MS         2U

/* Boot playlist: auto-play every "*.baf" under this directory, each looped this
 * many times, then advance to the next and wrap around forever. */
#define BAF_PLAYLIST_DIR        "1:/baf"
#define BAF_PLAYLIST_MAX        32
#define BAF_PLAYLIST_LOOPS      2

/* The animated asset compiled into the firmware (fallback when the TF card has
 * no playable .baf files). */
extern const bk_baf_source_t sample_bk_baf_source;

static bk_display_ctlr_handle_t   s_dpu_ctlr_handle = NULL;
static bk_display_bus_handle_t    s_dsi_bus_handle  = NULL;
static bk_avdk_lcd_panel_handle_t s_panel_handle    = NULL;
static beken_thread_t             s_render_thread   = NULL;
/* Open decoder owned by the render thread; exposed only so baf_raw_set_freerun()
 * (CLI task) can toggle free-run at runtime. NULL until the thread opens it. */
static bk_baf_decoder_t          *s_decoder         = NULL;

/* Auto-playlist of .baf paths discovered under BAF_PLAYLIST_DIR at boot; the
 * render thread walks it in order (BAF_PLAYLIST_LOOPS each) and wraps forever.
 * Owned by the render thread. */
static char s_playlist[BAF_PLAYLIST_MAX][TF_PATH_MAX];
static int  s_playlist_count;
static int  s_playlist_index;

/* CLI override: `baf_display play <path>` stages a path here and raises
 * s_switch_req; the render loop interrupts the current animation, plays this
 * file once through, then resumes the playlist. */
static char          s_pending_path[TF_PATH_MAX];
static volatile bool s_switch_req = false;

static void baf_raw_copy_path(char *dst, size_t dstsz, const char *src)
{
    size_t i = 0;
    if (dstsz == 0U) return;
    for (; src[i] != '\0' && (i + 1U) < dstsz; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void panel_backlight_on(void)
{
    gpio_dev_unmap(PANEL_BACKLIGHT_PIN);
    BK_LOG_ON_ERR(bk_gpio_enable_output(PANEL_BACKLIGHT_PIN));
    BK_LOG_ON_ERR(bk_gpio_pull_down(PANEL_BACKLIGHT_PIN));
    bk_gpio_set_capacity(PANEL_BACKLIGHT_PIN, GPIO_DRIVER_CAPACITY_3);
    /* Robot V1 backlight enable is active-low: drive LOW = ON. */
    bk_gpio_set_output_low(PANEL_BACKLIGHT_PIN);
}

/* jd9855 MIPI DSI bring-up. Unlike the LVGL path the DPU framebuffer is a plain
 * LINEAR ARGB8888 surface (decompress = false), because baf_gpu_compose_frame()
 * writes a linear, non-tiled ARGB8888 frame. */
static avdk_err_t baf_raw_panel_open(void)
{
    bk_err_t ret;
    bk_display_dpu_config_t dpu_cfg = {0};
    bk_lcd_panel_config_t panel_cfg = {0};

    dpu_cfg.video.enable = true;
    dpu_cfg.video.decompress = false;
    dpu_cfg.video.format = BK_PIXEL_FORMAT_ARGB8888;

    ret = bk_display_dsi_bus_new(&s_dsi_bus_handle, NULL);
    if (ret != BK_OK) { LOGE("dsi bus new err %d\n", (int)ret); goto err; }

    panel_cfg.reset_pin = PANEL_RESET_PIN;
    ret = bk_lcd_mipi_panel_new(s_dsi_bus_handle, &panel_cfg,
                                &lcd_device_jd9855_mipi_320x385, &s_panel_handle);
    if (ret != BK_OK) { LOGE("panel new err %d\n", (int)ret); goto err; }

    ret = bk_display_dpu_ctlr_new(&s_dpu_ctlr_handle, s_panel_handle, &dpu_cfg);
    if (ret != BK_OK) { LOGE("dpu ctlr new err %d\n", (int)ret); goto err; }
    ret = bk_display_init(s_dpu_ctlr_handle);
    if (ret != BK_OK) { LOGE("display init err %d\n", (int)ret); goto err; }
    ret = bk_display_open(s_dpu_ctlr_handle);
    if (ret != BK_OK) { LOGE("display open err %d\n", (int)ret); goto err; }

    panel_backlight_on();
    return AVDK_ERR_OK;

err:
    if (s_dpu_ctlr_handle) { bk_display_delete(s_dpu_ctlr_handle); s_dpu_ctlr_handle = NULL; }
    if (s_panel_handle)    { bk_lcd_panel_delete(s_panel_handle); s_panel_handle = NULL; }
    if (s_dsi_bus_handle)  { bk_display_bus_delete(s_dsi_bus_handle); s_dsi_bus_handle = NULL; }
    return AVDK_ERR_GENERIC;
}

/* DPU frame-consumed callback: release the framebuffer once scanout is done. */
static avdk_err_t baf_raw_fb_free_cb(void *frame)
{
    if (frame != NULL) {
        bk_frame_buffer_free(frame);
    }
    return AVDK_ERR_OK;
}

/* Why baf_raw_play_source() returned. */
typedef enum {
    BAF_PLAY_END,      /* configured loop_count reached -> advance the playlist */
    BAF_PLAY_SWITCH,   /* a `baf_display play` override was requested */
    BAF_PLAY_ERROR,    /* fatal decode error -> stop the render thread */
} baf_play_result_t;

/* Open a decoder for @source with @loop_count (0 = infinite, N = play N times
 * then END). Hardware (backend + GPU) is set up once in the render thread via
 * bk_baf_init(); opening a source never touches the GPU, so switching sources
 * does not power-cycle it. */
static bk_baf_decoder_t *baf_raw_open(const bk_baf_source_t *source, int32_t loop_count)
{
    bk_baf_config_t cfg = { .source = source, .loop_count = loop_count };
    return bk_baf_open(&cfg);
}

/* Play one opened source until it finishes its loop_count (END), a CLI override
 * is requested (SWITCH), or it errors (ERROR). */
static baf_play_result_t baf_raw_play_source(bk_baf_decoder_t *decoder, uint16_t w, uint16_t h)
{
    const uint32_t fb_size = (uint32_t)PANEL_WIDTH * (uint32_t)PANEL_HEIGHT * 4U;
    uint32_t fb_parity = 0U;   /* alternate the display FB across the two PSRAM controllers */

    for (;;) {
        if (s_switch_req) {
            return BAF_PLAY_SWITCH;   /* leave the frame in place; caller swaps the source */
        }

        bk_baf_decoder_result_t result = bk_baf_poll(decoder);
        if (result == BK_BAF_DECODER_RESULT_WAIT) {
            rtos_delay_milliseconds(BAF_RAW_WAIT_MS);
            continue;
        }
        if (result == BK_BAF_DECODER_RESULT_END) {
            return BAF_PLAY_END;   /* loop_count exhausted; advance to the next asset */
        }
        if (bk_baf_result_is_error(result)) {
            LOGE("BAF decode failed: %d\n", (int)result);
            return BAF_PLAY_ERROR;
        }

        /* result == BK_BAF_DECODER_RESULT_FRAME */
        bk_baf_frame_desc_t canvas_desc, alpha_desc;
        bk_baf_get_frame_desc(decoder, &canvas_desc, &alpha_desc);
        if (canvas_desc.data == NULL) {
            continue;
        }

        /* PSRAM load-balancing: alternate the display framebuffer between the two
         * controllers (even -> PSRAM0/UNCODED, odd -> PSRAM1/CODED) so the DPU
         * scanout of the previous frame and the GPU compose-write of the current
         * frame no longer collide on one controller. */
        frame_buffer_heap_type_t fb_heap = (fb_parity++ & 1U) ? MEM_SLAB_HEAP_CODED
                                                              : MEM_SLAB_HEAP_UNCODED;
        uint8_t *fb = bk_frame_buffer_malloc(fb_heap, fb_size);
        if (fb == NULL) {
            LOGW("frame buffer alloc failed (%u bytes)\n", (unsigned)fb_size);
            rtos_delay_milliseconds(BAF_RAW_WAIT_MS);
            continue;
        }
        /* bk_baf_compose() clears rows [0, h) to the grey background, so only the
         * unused tail rows [h, PANEL_HEIGHT) need a CPU grey-fill. */
        if (h < PANEL_HEIGHT) {
            os_memset(fb + (size_t)PANEL_WIDTH * (size_t)h * 4U, 0xD0,
                      (size_t)PANEL_WIDTH * (size_t)(PANEL_HEIGHT - h) * 4U);
        }
        bk_baf_frame_desc_t fb_desc = {
            .data = fb, .format = BK_BAF_PIXEL_ARGB8888,
            .width = w, .height = h, .stride = (uint32_t)w * 4U,
        };
        bk_baf_compose(&fb_desc, &canvas_desc,
                       alpha_desc.data ? &alpha_desc : NULL, BAF_RAW_BG_ARGB);

        if (bk_display_flush(s_dpu_ctlr_handle, fb, baf_raw_fb_free_cb) != AVDK_ERR_OK) {
            LOGE("display flush failed\n");
            bk_frame_buffer_free(fb);
        }
    }
}

/* Play one asset (a loaded file source, or the built-in fallback) through the
 * pipeline. @file_src is non-NULL for a TF-card file (unloaded here when done),
 * NULL for the compiled-in asset. Returns why playback stopped. */
static baf_play_result_t baf_raw_play_one(const bk_baf_source_t *source,
                                          const bk_baf_source_t *file_src,
                                          int32_t loop_count, const char *label)
{
    bk_baf_decoder_t *decoder = baf_raw_open(source, loop_count);
    if (decoder == NULL) {
        LOGE("bk_baf_open failed\n");
        if (file_src != NULL) baf_file_unload(file_src);
        return BAF_PLAY_ERROR;
    }
    s_decoder = decoder;

    uint16_t w = bk_baf_get_width(decoder);
    uint16_t h = bk_baf_get_height(decoder);
    /* baf_gpu_compose_frame() assumes dst stride == frame width, so the frame
     * must be exactly the panel width; height may be <= panel height. */
    if (w != PANEL_WIDTH || h == 0U || h > PANEL_HEIGHT) {
        LOGE("unsupported BAF size %ux%u (panel %ux%u): %s\n",
             (unsigned)w, (unsigned)h, (unsigned)PANEL_WIDTH, (unsigned)PANEL_HEIGHT, label);
        s_decoder = NULL;
        bk_baf_close(decoder);
        if (file_src != NULL) baf_file_unload(file_src);
        return BAF_PLAY_END;   /* treat as "done", so the caller just advances */
    }

    LOGI("baf raw playback start: frame %ux%u -> panel %ux%u [%s]\n",
         (unsigned)w, (unsigned)h, (unsigned)PANEL_WIDTH, (unsigned)PANEL_HEIGHT, label);

    baf_play_result_t r = baf_raw_play_source(decoder, w, h);

    s_decoder = NULL;
    bk_baf_close(decoder);
    if (file_src != NULL) baf_file_unload(file_src);
    return r;
}

static void baf_raw_render_thread(void *arg)
{
    (void)arg;

    /* One-time hardware setup: pick the compositor and (GPU mode) bring VG-Lite up
     * once. Kept for the whole thread so source switches never re-init the GPU. */
    bk_baf_hw_config_t hw = {
#if CONFIG_BAF_RAW_RENDER_CPU
        .backend  = BK_BAF_RENDER_CPU,
#else
        .backend  = BK_BAF_RENDER_GPU,
        .init_gpu = true,
#endif
    };
    if (bk_baf_init(&hw) != AVDK_ERR_OK) {
        LOGE("bk_baf_init failed\n");
        s_render_thread = NULL;
        rtos_delete_thread(NULL);
        return;
    }

    LOGI("render backend: %s\n",
#if CONFIG_BAF_RAW_RENDER_CPU
         "CPU (Helium)"
#else
         "GPU (VG-Lite)"
#endif
    );

    /* Build the boot playlist from the TF card. */
    s_playlist_count = tf_card_list_ext(BAF_PLAYLIST_DIR, ".baf", s_playlist, BAF_PLAYLIST_MAX);
    if (s_playlist_count > 0) {
        LOGI("playlist: %d .baf file(s) under %s, %d loop(s) each\n",
             s_playlist_count, BAF_PLAYLIST_DIR, BAF_PLAYLIST_LOOPS);
    } else {
        LOGW("no .baf under %s; playing the built-in asset\n", BAF_PLAYLIST_DIR);
        s_playlist_count = 0;
    }
    s_playlist_index = 0;

    for (;;) {   /* one iteration per asset played */
        baf_play_result_t r;

        if (s_switch_req) {
            /* CLI override: play the requested file once through, then fall back
             * into the playlist at the same index. */
            s_switch_req = false;
            const bk_baf_source_t *fsrc = baf_file_load(s_pending_path);
            if (fsrc == NULL) {
                LOGW("play '%s' failed; resuming playlist\r\n", s_pending_path);
                continue;
            }
            r = baf_raw_play_one(fsrc, fsrc, BAF_PLAYLIST_LOOPS, s_pending_path);
        } else if (s_playlist_count > 0) {
            const char *path = s_playlist[s_playlist_index];
            const bk_baf_source_t *fsrc = baf_file_load(path);
            if (fsrc == NULL) {
                LOGW("load '%s' failed; skipping\r\n", path);
                s_playlist_index = (s_playlist_index + 1) % s_playlist_count;
                rtos_delay_milliseconds(50);
                continue;
            }
            r = baf_raw_play_one(fsrc, fsrc, BAF_PLAYLIST_LOOPS, path);
            if (r == BAF_PLAY_END) {
                s_playlist_index = (s_playlist_index + 1) % s_playlist_count;
            }
        } else {
            /* No playable files: loop the built-in asset forever. */
            r = baf_raw_play_one(&sample_bk_baf_source, NULL, 0, "built-in");
        }

        if (r == BAF_PLAY_ERROR) {
            break;   /* fatal decode / open error */
        }
        /* BAF_PLAY_SWITCH: next iteration handles the pending override.
         * BAF_PLAY_END:    already advanced above (playlist) or replays (built-in). */
    }

    bk_baf_deinit();   /* tear the GPU back down (creator destroys) */
    s_render_thread = NULL;
    rtos_delete_thread(NULL);
}

avdk_err_t baf_raw_set_freerun(bool enable)
{
    bk_baf_decoder_t *decoder = s_decoder;
    if (decoder == NULL) return AVDK_ERR_INVAL;
    return bk_baf_ioctl(decoder, BK_BAF_IOCTL_SET_FREERUN, &enable);
}

avdk_err_t baf_raw_play_file(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return AVDK_ERR_INVAL;
    }
    if (s_render_thread == NULL) {
        LOGE("render thread not running\n");
        return AVDK_ERR_INVAL;
    }
    baf_raw_copy_path(s_pending_path, sizeof(s_pending_path), path);
    s_switch_req = true;   /* render loop picks it up and swaps the source */
    LOGI("play requested: %s\n", s_pending_path);
    return AVDK_ERR_OK;
}

avdk_err_t baf_raw_start(void)
{
    if (baf_raw_panel_open() != AVDK_ERR_OK) {
        LOGE("open jd9855 MIPI panel failed\n");
        return AVDK_ERR_GENERIC;
    }

    /* Backend selection + GPU bring-up + decoder open are done via bk_baf_open()
     * inside the render thread (see baf_raw_render_thread). */
    bk_err_t ret = rtos_create_thread(&s_render_thread,
                                      BAF_RAW_TASK_PRIORITY,
                                      "baf_raw",
                                      (beken_thread_function_t)baf_raw_render_thread,
                                      BAF_RAW_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        LOGE("create render thread failed %d\n", (int)ret);
        s_render_thread = NULL;
        return AVDK_ERR_GENERIC;
    }

    LOGI("baf_raw started, panel=jd9855_mipi_320x385\n");
    return AVDK_ERR_OK;
}
