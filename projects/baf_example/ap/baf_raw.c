/**
 * @file baf_raw.c
 *
 * Raw (LVGL-independent) multi-layer BAF playback backend for baf_example.
 *
 * Compiled when CONFIG_LVGL is off (the default RAW backend). It stacks up to
 * BAF_MAX_LAYERS BAF animations back-to-front and flushes straight to the jd9855
 * DPU panel, no LVGL:
 *   layer 0 (back)  : bk_baf_compose(over=false) -- opaque base, fills the frame
 *   layer 1..N (top): bk_baf_compose(over=true)  -- src-over, transparent regions
 *                                                   reveal the layers below
 * A render thread polls every layer's decoder (each paces itself to its own
 * per-frame durations) and re-composites the stack whenever any layer yields a
 * new frame.
 *
 * The stack is runtime-selectable ("scenes" of 1/2/3 compiled-in layers) and any
 * layer can be overridden with a .baf file from the SD card. Changing the scene
 * or an override raises s_reconfig; the render thread tears the decoders down and
 * rebuilds the stack (the GPU set up once by bk_baf_init() stays up).
 */

#include "baf_raw.h"

/* Set to 1 to log per-30-frame perf stats (poll+decode / compose / cadence fps).
 * Development instrumentation; off by default (no aon_rtc calls, no logging). */
#define BAF_RAW_PROFILE 0

#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#include <components/log.h>
#include <components/bk_frame_buffer.h>
#include <common/avdk_pixel_types.h>
#if BAF_RAW_PROFILE
#include <driver/aon_rtc.h>                 /* bk_aon_rtc_get_us(): perf stats */
#endif
#include <components/bk_display.h>          /* umbrella: bus + panel + display ctlr */
#include <avdk_error.h>
#include <lcd/lcd_mipi_jd9855_320x385.h>
#include <bk_baf.h>
#include "baf_file.h"                        /* load a .baf file off the SD card */
#include "tf_card.h"                         /* TF_PATH_MAX */

#define TAG "baf_raw"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* Same board bring-up as the LVGL path (Robot V1): panel reset GPIO_5, backlight
 * GPIO_7. The peripheral 3.3V rail and aux LDO are enabled in main() first. */
#define PANEL_RESET_PIN        GPIO_5
#define PANEL_BACKLIGHT_PIN    GPIO_7

/* jd9855 MIPI DSI panel geometry (portrait). */
#define PANEL_WIDTH            320
#define PANEL_HEIGHT           385

/* Opaque grey background (ARGB8888, A=0xFF) shown under/through the base layer. */
#define BAF_RAW_BG_ARGB        0xFFD0D0D0U
#define BAF_RAW_BG_BYTE        0xD0

#define BAF_RAW_TASK_PRIORITY   4
#define BAF_RAW_TASK_STACK_SIZE (1024 * 16)
#define BAF_RAW_WAIT_MS         2U

/* BAF_MAX_LAYERS is declared in baf_raw.h (shared with the CLI). */

/* Compiled-in BAF v1 containers as C byte arrays (identical bytes to the .baf
 * files in resources/). Fed straight to bk_baf_open(cfg.data) -- bk_baf parses
 * each container internally, so the built-in scenes use the exact same container
 * path as SD-card files; the project never parses. */
extern const unsigned char hello_baf[];      extern const unsigned int hello_baf_size;
extern const unsigned char background_baf[]; extern const unsigned int background_baf_size;
extern const unsigned char curtain_baf[];    extern const unsigned int curtain_baf_size;

/* A scene layer = raw container bytes + byte length (the length lives in another
 * TU as `extern const unsigned int`, so we point at it). */
typedef struct {
    const unsigned char *buf;
    const unsigned int  *size;
} baf_asset_t;

/* ---- Scenes: preset layer stacks (back -> front) ----
 *   1 = avatar only, 2 = background + avatar (default), 3 = + curtain on top. */
static const baf_asset_t k_scene1[] = { { hello_baf, &hello_baf_size } };
static const baf_asset_t k_scene2[] = {
    { background_baf, &background_baf_size }, { hello_baf, &hello_baf_size },
};
static const baf_asset_t k_scene3[] = {
    { background_baf, &background_baf_size }, { hello_baf, &hello_baf_size },
    { curtain_baf, &curtain_baf_size },
};

static const baf_asset_t *scene_sources(int scene, int *count)
{
    switch (scene) {
    case 1: *count = 1; return k_scene1;
    case 3: *count = 3; return k_scene3;
    case 2:
    default: *count = 2; return k_scene2;
    }
}

#define BAF_SCENE_DEFAULT   2

static bk_display_ctlr_handle_t   s_dpu_ctlr_handle = NULL;
static bk_display_bus_handle_t    s_dsi_bus_handle  = NULL;
static bk_avdk_lcd_panel_handle_t s_panel_handle    = NULL;
static beken_thread_t             s_render_thread   = NULL;
/* Top layer's decoder, exposed only so baf_raw_set_freerun() (CLI) can toggle
 * free-run on the topmost layer. NULL until the render thread opens the stack. */
static bk_baf_decoder_t          *s_decoder         = NULL;

/* Two mutually-exclusive playback modes, switched from the CLI and applied by the
 * render thread on s_reconfig:
 *   SCENE  - show one of the compiled-in presets (scene 1/2/3).
 *   CUSTOM - show only SD-card .baf files the user stacked onto layers; empty
 *            slots are skipped and an empty stack shows just the grey background.
 * Switching modes fully tears the old stack down, so no layer from the previous
 * mode ever lingers. */
typedef enum { BAF_MODE_SCENE, BAF_MODE_CUSTOM } baf_raw_mode_t;

static volatile baf_raw_mode_t s_mode     = BAF_MODE_SCENE;
static volatile int            s_scene    = BAF_SCENE_DEFAULT;
static volatile bool           s_reconfig = true;
static char                    s_custom_path[BAF_MAX_LAYERS][TF_PATH_MAX];

static void baf_raw_copy_path(char *dst, size_t dstsz, const char *src)
{
    size_t i = 0;
    if (dstsz == 0U) return;
    if (src != NULL) {
        for (; src[i] != '\0' && (i + 1U) < dstsz; i++) dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void panel_backlight_on(void)
{
    gpio_dev_unmap(PANEL_BACKLIGHT_PIN);
    BK_LOG_ON_ERR(bk_gpio_enable_output(PANEL_BACKLIGHT_PIN));
    BK_LOG_ON_ERR(bk_gpio_pull_down(PANEL_BACKLIGHT_PIN));
    bk_gpio_set_capacity(PANEL_BACKLIGHT_PIN, GPIO_DRIVER_CAPACITY_3);
    bk_gpio_set_output_low(PANEL_BACKLIGHT_PIN);   /* active-low: LOW = ON */
}

/* jd9855 MIPI DSI bring-up: LINEAR ARGB8888 DPU surface (no decompress). */
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

/* DPU frame-consumed callback: release the framebuffer after scanout. */
static avdk_err_t baf_raw_fb_free_cb(void *frame)
{
    if (frame != NULL) {
        bk_frame_buffer_free(frame);
    }
    return AVDK_ERR_OK;
}

/* ---- Layer stack lifecycle ---- */
static bk_baf_decoder_t   *s_dec[BAF_MAX_LAYERS];       /* open decoders, back->front */
static uint8_t            *s_file_buf[BAF_MAX_LAYERS];  /* CUSTOM: SD file buffers to psram_free */
static int                 s_nlayers;

static void baf_raw_teardown_stack(void)
{
    for (int i = 0; i < s_nlayers; i++) {
        if (s_dec[i] != NULL) { bk_baf_close(s_dec[i]); s_dec[i] = NULL; }  /* frees the parsed view */
        /* Free the SD buffer only AFTER bk_baf_close() (the decoder aliased it). */
        if (s_file_buf[i] != NULL) { psram_free(s_file_buf[i]); s_file_buf[i] = NULL; }
    }
    s_nlayers = 0;
    s_decoder = NULL;
}

/* Build the decoder stack for the active mode:
 *   SCENE  -> the scene's compiled-in preset layers.
 *   CUSTOM -> only the SD .baf files assigned to layer slots (empties skipped).
 * Returns the number of layers opened (may be 0 in CUSTOM = background only). */
static int baf_raw_build_stack(void)
{
    int n = 0;

    if (s_mode == BAF_MODE_SCENE) {
        int count = 0;
        const baf_asset_t *preset = scene_sources(s_scene, &count);
        if (count > BAF_MAX_LAYERS) count = BAF_MAX_LAYERS;
        for (int i = 0; i < count; i++) {
            /* Compiled-in container bytes: bk_baf parses & owns the view. */
            bk_baf_config_t cfg = { .data = preset[i].buf, .data_len = *preset[i].size, .loop_count = 0 };
            bk_baf_decoder_t *d = bk_baf_open(&cfg);
            if (d == NULL) { LOGE("layer %d open failed\n", i); continue; }
            s_dec[n] = d;
            s_file_buf[n] = NULL;   /* flash bytes, nothing to free */
            n++;
        }
    } else {   /* BAF_MODE_CUSTOM: SD-card files only */
        for (int i = 0; i < BAF_MAX_LAYERS; i++) {
            if (s_custom_path[i][0] == '\0') continue;   /* empty slot */
            uint32_t len = 0;
            uint8_t *buf = baf_file_read(s_custom_path[i], &len);
            if (buf == NULL) {
                LOGW("layer %d: load '%s' failed, skipped\n", i, s_custom_path[i]);
                continue;
            }
            /* Raw container bytes: bk_baf parses & owns the view (aliases buf). */
            bk_baf_config_t cfg = { .data = buf, .data_len = len, .loop_count = 0 };
            bk_baf_decoder_t *d = bk_baf_open(&cfg);
            if (d == NULL) {
                LOGE("layer %d open failed\n", i);
                psram_free(buf);
                continue;
            }
            s_dec[n] = d;
            s_file_buf[n] = buf;   /* freed on teardown after bk_baf_close */
            n++;
        }
    }

    s_nlayers = n;
    s_decoder = (n > 0) ? s_dec[n - 1] : NULL;
    return n;
}

/* Composite the whole layer stack into @fb: base layer fills (over grey), each
 * higher layer is src-over blended so its transparent regions reveal the ones
 * below. Returns true if at least the base was drawn. */
static bool baf_raw_compose_stack(uint8_t *fb, uint16_t w, uint16_t h)
{
    bk_baf_frame_desc_t fb_desc = {
        .data = fb, .format = BK_BAF_PIXEL_ARGB8888,
        .width = w, .height = h, .stride = (uint32_t)w * 4U,
    };
    bool base_done = false;

    for (int i = 0; i < s_nlayers; i++) {
        bk_baf_frame_desc_t canvas, alpha;
        bk_baf_get_frame_desc(s_dec[i], &canvas, &alpha);
        if (canvas.data == NULL) {
            continue;   /* this layer hasn't produced its first frame yet */
        }
        /* Compose blits canvas.width x canvas.height into @fb; a layer larger than
         * the base framebuffer (possible with an arbitrary SD .baf in CUSTOM mode)
         * would write past it. Skip such layers instead of overrunning @fb. */
        if (canvas.width > w || canvas.height > h) {
            LOGW("layer %d %ux%u exceeds fb %ux%u, skipped\n",
                 i, (unsigned)canvas.width, (unsigned)canvas.height,
                 (unsigned)w, (unsigned)h);
            continue;
        }
        const bk_baf_frame_desc_t *ap = alpha.data ? &alpha : NULL;
        if (!base_done) {
            bk_baf_compose(&fb_desc, &canvas, ap, BAF_RAW_BG_ARGB, false); /* opaque base */
            base_done = true;
        } else {
            bk_baf_compose(&fb_desc, &canvas, ap, 0U, true);              /* stack on top */
        }
    }
    return base_done;
}

static void baf_raw_render_thread(void *arg)
{
    (void)arg;

    /* One-time hardware setup: pick the compositor and (GPU) bring VG-Lite up. */
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

    const uint32_t fb_size = (uint32_t)PANEL_WIDTH * (uint32_t)PANEL_HEIGHT * 4U;
    uint32_t fb_parity = 0U;
    uint16_t w = 0, h = 0;
    bool bg_pending = false;   /* CUSTOM w/ no layers: flush one grey frame */

#if BAF_RAW_PROFILE
    /* Perf stats over 30 displayed frames. */
    uint32_t st_cnt = 0, poll_sum = 0, cmp_sum = 0, cad_sum = 0;
    uint32_t cmp_min = 0xFFFFFFFFU, cmp_max = 0, cad_min = 0xFFFFFFFFU, cad_max = 0;
    uint64_t last_disp_us = 0;
#endif

    for (;;) {
        if (s_reconfig) {
            s_reconfig = false;
            baf_raw_teardown_stack();
            int n = baf_raw_build_stack();
            if (n == 0) {
                /* Valid in CUSTOM mode (all slots empty): show grey background. */
                w = PANEL_WIDTH; h = PANEL_HEIGHT;
                bg_pending = true;
                LOGI("%s: 0 layer(s), background only\n",
                     s_mode == BAF_MODE_SCENE ? "scene" : "custom");
            } else {
                w = bk_baf_get_width(s_dec[0]);
                h = bk_baf_get_height(s_dec[0]);
                if (w != PANEL_WIDTH || h == 0U || h > PANEL_HEIGHT) {
                    LOGE("unsupported base size %ux%u (panel %ux%u)\n",
                         (unsigned)w, (unsigned)h, (unsigned)PANEL_WIDTH, (unsigned)PANEL_HEIGHT);
                    baf_raw_teardown_stack();
                    rtos_delay_milliseconds(200);
                    continue;
                }
                bg_pending = false;
                LOGI("%s: %d layer(s), frame %ux%u -> panel %ux%u\n",
                     s_mode == BAF_MODE_SCENE ? "scene" : "custom",
                     n, (unsigned)w, (unsigned)h, (unsigned)PANEL_WIDTH, (unsigned)PANEL_HEIGHT);
            }
#if BAF_RAW_PROFILE
            last_disp_us = 0; st_cnt = 0;
#endif
        }

        /* No layers (CUSTOM, all cleared): flush the grey background once, idle. */
        if (s_nlayers == 0) {
            if (bg_pending) {
                uint8_t *fb = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, fb_size);
                if (fb != NULL) {
                    os_memset(fb, BAF_RAW_BG_BYTE, fb_size);
                    if (bk_display_flush(s_dpu_ctlr_handle, fb, baf_raw_fb_free_cb) != AVDK_ERR_OK)
                        bk_frame_buffer_free(fb);
                    bg_pending = false;
                }
            }
            rtos_delay_milliseconds(BAF_RAW_WAIT_MS);
            continue;
        }

        /* Poll every layer (each paces itself). Recompose only when at least one
         * produced a new frame; otherwise idle. */
#if BAF_RAW_PROFILE
        uint64_t t_poll0 = bk_aon_rtc_get_us();
#endif
        bool any_new = false;
        for (int i = 0; i < s_nlayers; i++) {
            bk_baf_decoder_result_t r = bk_baf_poll(s_dec[i]);
            if (r == BK_BAF_DECODER_RESULT_FRAME) any_new = true;
            else if (bk_baf_result_is_error(r)) LOGW("layer %d decode err %d\n", i, (int)r);
        }
        if (!any_new) {
            rtos_delay_milliseconds(BAF_RAW_WAIT_MS);
            continue;
        }
#if BAF_RAW_PROFILE
        uint32_t poll_us = (uint32_t)(bk_aon_rtc_get_us() - t_poll0);
#endif

        frame_buffer_heap_type_t fb_heap = (fb_parity++ & 1U) ? MEM_SLAB_HEAP_CODED
                                                              : MEM_SLAB_HEAP_UNCODED;
        uint8_t *fb = bk_frame_buffer_malloc(fb_heap, fb_size);
        if (fb == NULL) {
            LOGW("frame buffer alloc failed (%u bytes)\n", (unsigned)fb_size);
            rtos_delay_milliseconds(BAF_RAW_WAIT_MS);
            continue;
        }

#if BAF_RAW_PROFILE
        uint64_t t_cmp0 = bk_aon_rtc_get_us();
#endif
        bool drawn = baf_raw_compose_stack(fb, w, h);
#if BAF_RAW_PROFILE
        uint32_t cmp_us = (uint32_t)(bk_aon_rtc_get_us() - t_cmp0);

        uint64_t now_us = bk_aon_rtc_get_us();
        uint32_t cad_us = last_disp_us ? (uint32_t)(now_us - last_disp_us) : 0U;
        last_disp_us = now_us;
        poll_sum += poll_us; cmp_sum += cmp_us;
        if (cmp_us < cmp_min) cmp_min = cmp_us;
        if (cmp_us > cmp_max) cmp_max = cmp_us;
        if (cad_us) {
            cad_sum += cad_us;
            if (cad_us < cad_min) cad_min = cad_us;
            if (cad_us > cad_max) cad_max = cad_us;
        }
        if (++st_cnt >= 30U) {
            uint32_t cad_avg = cad_sum / 29U;
            LOGI("layers=%d perf/30: poll+decode avg=%u us | compose min/avg/max=%u/%u/%u us | "
                 "cadence avg=%u us (~%u.%u fps)\n",
                 s_nlayers, poll_sum / 30U, cmp_min, cmp_sum / 30U, cmp_max,
                 cad_avg, cad_avg ? (1000000U / cad_avg) : 0U,
                 cad_avg ? ((10000000U / cad_avg) % 10U) : 0U);
            st_cnt = 0; poll_sum = 0; cmp_sum = 0; cad_sum = 0;
            cmp_min = 0xFFFFFFFFU; cmp_max = 0; cad_min = 0xFFFFFFFFU; cad_max = 0;
        }
#endif

        if (!drawn) {
            os_memset(fb, BAF_RAW_BG_BYTE, fb_size);
        } else if (h < PANEL_HEIGHT) {
            os_memset(fb + (size_t)PANEL_WIDTH * (size_t)h * 4U, BAF_RAW_BG_BYTE,
                      (size_t)PANEL_WIDTH * (size_t)(PANEL_HEIGHT - h) * 4U);
        }

        if (bk_display_flush(s_dpu_ctlr_handle, fb, baf_raw_fb_free_cb) != AVDK_ERR_OK) {
            LOGE("display flush failed\n");
            bk_frame_buffer_free(fb);
        }
    }
}

avdk_err_t baf_raw_set_scene(int scene)
{
    if (scene < 1 || scene > BAF_MAX_LAYERS) return AVDK_ERR_INVAL;
    if (s_render_thread == NULL) return AVDK_ERR_INVAL;
    /* Enter SCENE mode: drop any custom SD layers so nothing from CUSTOM lingers. */
    for (int i = 0; i < BAF_MAX_LAYERS; i++) s_custom_path[i][0] = '\0';
    s_mode = BAF_MODE_SCENE;
    s_scene = scene;
    s_reconfig = true;
    LOGI("scene %d requested\n", scene);
    return AVDK_ERR_OK;
}

/* A NULL/empty path or the keywords "clear"/"none"/"default" clear the slot. */
static bool baf_raw_is_clear(const char *path)
{
    return (path == NULL || path[0] == '\0' ||
            os_strcmp(path, "clear")   == 0 ||
            os_strcmp(path, "none")    == 0 ||
            os_strcmp(path, "default") == 0);
}

avdk_err_t baf_raw_set_layer_file(int index, const char *path)
{
    if (index < 0 || index >= BAF_MAX_LAYERS) return AVDK_ERR_INVAL;
    if (s_render_thread == NULL) return AVDK_ERR_INVAL;

    if (baf_raw_is_clear(path)) {
        /* Clearing only makes sense once in CUSTOM mode (SCENE has no SD layers). */
        if (s_mode != BAF_MODE_CUSTOM) return AVDK_ERR_INVAL;
        s_custom_path[index][0] = '\0';
        s_reconfig = true;
        LOGI("layer %d cleared\n", index);
        return AVDK_ERR_OK;
    }

    /* Assigning an SD file enters CUSTOM mode. Coming from SCENE, wipe every slot
     * first so only the file(s) the user places are shown (no preset leftovers). */
    if (s_mode != BAF_MODE_CUSTOM) {
        for (int i = 0; i < BAF_MAX_LAYERS; i++) s_custom_path[i][0] = '\0';
        s_mode = BAF_MODE_CUSTOM;
    }
    baf_raw_copy_path(s_custom_path[index], TF_PATH_MAX, path);
    s_reconfig = true;
    LOGI("layer %d file: '%s'\n", index, s_custom_path[index]);
    return AVDK_ERR_OK;
}

avdk_err_t baf_raw_set_freerun(bool enable)
{
    bk_baf_decoder_t *decoder = s_decoder;
    if (decoder == NULL) return AVDK_ERR_INVAL;
    return bk_baf_ioctl(decoder, BK_BAF_IOCTL_SET_FREERUN, &enable);
}

avdk_err_t baf_raw_start(void)
{
    if (baf_raw_panel_open() != AVDK_ERR_OK) {
        LOGE("open jd9855 MIPI panel failed\n");
        return AVDK_ERR_GENERIC;
    }

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
