#include "bk_private/bk_init.h"
#include <os/os.h>
#include "cli.h"
#if CONFIG_LVGL
#include "lvgl.h"
#include "lv_vendor.h"
#include "baf_page.h"
#else
#include "baf_raw.h"
#endif
#include "media_service.h"
#include "tf_card.h"
#include <components/bk_frame_buffer.h>
#include <common/avdk_pixel_types.h>
#include <components/bk_display.h>          /* umbrella: bus + panel + display ctlr */
#include <driver/gpio.h>
#include "gpio_driver.h"
#include <avdk_error.h>
#include <lcd/lcd_mipi_jd9855_320x385.h>


#define TAG "baf_example"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* jd9855 MIPI DSI panel, 320x385 (portrait), same board bring-up as
 * baf_display_example (Robot V1): panel reset GPIO_5, backlight GPIO_7 and the
 * panel/peripheral 3.3V rail on GPIO_53. */
#define PANEL_RESET_PIN        GPIO_5
#define PANEL_BACKLIGHT_PIN    GPIO_7
#define BOARD_PERIPH_3V3_PIN   GPIO_53

#define LVGL_DISP_WIDTH        320
#define LVGL_DISP_HEIGHT       385
#define LVGL_DRAW_BUFFER_LINES (64)

#define SYS_ANA_REG_BASE    (0x44010000)
#define LDO_ANA_REG         (0x69)

#if CONFIG_LVGL
static bk_display_ctlr_handle_t s_dpu_ctlr_handle = NULL;
static bk_display_bus_handle_t  s_dsi_bus_handle = NULL;
static bk_avdk_lcd_panel_handle_t s_panel_handle = NULL;
#endif

/* Bring up the panel/peripheral 3.3V rail (Robot V1 board). */
static void robot_board_power_on(void)
{
    gpio_dev_unmap(BOARD_PERIPH_3V3_PIN);
    (void)bk_gpio_enable_output(BOARD_PERIPH_3V3_PIN);
    (void)bk_gpio_pull_up(BOARD_PERIPH_3V3_PIN);
    bk_gpio_set_output_high(BOARD_PERIPH_3V3_PIN);
}

static void bk_auxldo_enable(void)
{
    uint32_t reg = REG_READ(SYS_ANA_REG_BASE + LDO_ANA_REG * 4);
    reg |= (0xFU << 28) | (0x2U << 23) | (0x7U << 19) | (0x7U << 15);
    reg &= ~(0xFU << 11);
    reg |= (0x8U << 11);
    REG_WRITE(SYS_ANA_REG_BASE + LDO_ANA_REG * 4, reg);
}

#if CONFIG_LVGL
static void panel_backlight_on(void)
{
    gpio_dev_unmap(PANEL_BACKLIGHT_PIN);
    BK_LOG_ON_ERR(bk_gpio_enable_output(PANEL_BACKLIGHT_PIN));
    BK_LOG_ON_ERR(bk_gpio_pull_down(PANEL_BACKLIGHT_PIN));
    bk_gpio_set_capacity(PANEL_BACKLIGHT_PIN, GPIO_DRIVER_CAPACITY_3);
    /* Robot V1 board backlight enable is active-low: drive LOW = ON
     * (matches baf_display_example / beken_robot). Driving it HIGH left the
     * panel dark even though LVGL + BAF were flushing valid frames. */
    bk_gpio_set_output_low(PANEL_BACKLIGHT_PIN);
}

/* Self-contained jd9855 MIPI DSI bring-up (mirrors baf_display_dpu.c): DSI bus
 * -> panel -> DPU controller (ARGB8888 + DEC to match the LVGL output-compress
 * framebuffer) -> init -> open -> backlight. */
static avdk_err_t lv_baf_panel_open(void)
{
    bk_err_t ret;
    bk_display_dpu_config_t dpu_cfg = {0};
    bk_lcd_panel_config_t panel_cfg = {0};

    dpu_cfg.video.enable = true;
    dpu_cfg.video.decompress = true;
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

/* Plain panel flush. The lv_baf widget composites its animation on the GPU at
 * flush time before this runs, so there is no compositing glue here -- this only
 * adapts bk_display_flush to the flush_cb signature. */
static void baf_example_flush_cb(void *args, void *frame_buffer, int (*cb)(void *args))
{
    bk_display_flush(args, frame_buffer, cb);
}

bk_err_t lvgl_app_baf_example_init(void)
{
    bk_err_t ret = BK_OK;
    lv_vnd_config_t lv_vnd_config = {0};
    uint32_t frame_buffer_size = 0;

    if (lv_baf_panel_open() != AVDK_ERR_OK) {
        LOGE("open jd9855 MIPI panel failed\n");
        return BK_FAIL;
    }

    lv_vnd_config.width = LVGL_DISP_WIDTH;
    lv_vnd_config.height = LVGL_DISP_HEIGHT;
    lv_vnd_config.render_mode = RENDER_PARTIAL_MODE;
    if (lv_vnd_config.render_mode == RENDER_PARTIAL_MODE) {
        lv_vnd_config.draw_pixel_size = LVGL_DISP_WIDTH * LVGL_DRAW_BUFFER_LINES * sizeof(bk_color_t);
    }
    lv_vnd_config.rotation = ROTATE_NONE;
    lv_vnd_config.disp_width = LVGL_DISP_WIDTH;
    lv_vnd_config.disp_height = LVGL_DISP_HEIGHT;
    lv_vnd_config.output_compress = true;
    if (lv_vnd_config.output_compress && lv_vnd_config.render_mode == RENDER_PARTIAL_MODE) {
        if (lv_vnd_config.disp_width % 16 || lv_vnd_config.disp_height % 4) {
            lv_vnd_config.disp_width = (lv_vnd_config.disp_width + 15) & ~15;
            lv_vnd_config.disp_height = (lv_vnd_config.disp_height + 3) & ~3;
        }
        LOGI("lv_vnd_config.disp_width:%d, lv_vnd_config.disp_height:%d\r\n", lv_vnd_config.disp_width, lv_vnd_config.disp_height);
    }

    if (lv_vnd_config.output_compress) {
        frame_buffer_size = lv_vnd_config.disp_width * lv_vnd_config.disp_height;
    } else {
        frame_buffer_size = lv_vnd_config.disp_width * lv_vnd_config.disp_height * sizeof(bk_color_t);
    }

    /* Alternate the display framebuffers across the two PSRAM controllers
     * (PSRAM0/UNCODED @0x60000000 and PSRAM1/CODED @0x64000000). This is the
     * hardware-intended balance: the DPU scans one framebuffer out on one
     * controller while the GPU composites into the other on the second
     * controller. (Pinning BOTH framebuffers onto a single controller overloads
     * it -- DPU scanout + GPU write collide -- and stalls the GPU.) */
    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
        if (i % 2) {
            lv_vnd_config.frame_buffer[i] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, frame_buffer_size);
        } else {
            lv_vnd_config.frame_buffer[i] = bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, frame_buffer_size);
        }
        if (lv_vnd_config.frame_buffer[i] == NULL) {
            LOGE("allocate LVGL frame buffer %d failed\n", i);
            ret = BK_FAIL;
            goto err;
        }
    }
    lv_vnd_config.args = s_dpu_ctlr_handle;
    /* GPU-overlay compositing is driven by the lv_baf widget via the generic
     * lv_vendor overlay hook, so the flush callback is just the plain panel
     * flush (plus fps logging) -- no composite glue here (matches lv_gif). */
    lv_vnd_config.flush_cb = baf_example_flush_cb;

    lv_vendor_init(&lv_vnd_config);

    lv_vendor_disp_lock();
    baf_page_create();
    lv_vendor_disp_unlock();

    lv_vendor_start();

    LOGI("baf_example started, panel=jd9855_mipi_320x385, %ux%u\n",
         LVGL_DISP_WIDTH, LVGL_DISP_HEIGHT);
    return BK_OK;

err:
    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
        if (lv_vnd_config.frame_buffer[i] != NULL) {
            bk_frame_buffer_free(lv_vnd_config.frame_buffer[i]);
            lv_vnd_config.frame_buffer[i] = NULL;
        }
    }
    return ret;
}

static bk_err_t baf_example_rotation_from_degrees(uint16_t degrees, rott_angle_t *rotation)
{
    if (rotation == NULL) {
        return BK_FAIL;
    }

    switch (degrees) {
    case 0:
        *rotation = ROTATE_NONE;
        return BK_OK;
    case 90:
        *rotation = ROTATE_90;
        return BK_OK;
    case 180:
        *rotation = ROTATE_180;
        return BK_OK;
    case 270:
        *rotation = ROTATE_270;
        return BK_OK;
    default:
        return BK_FAIL;
    }
}

#endif /* CONFIG_LVGL */

/* CLI: "baf_display <sub> ...", registered in both playback modes.
 *   rot <0|90|180|270>   - LVGL display rotation (LVGL mode only)
 *   freerun <0|1>        - toggle max-speed playback (RAW mode only; the LVGL
 *                          pipeline can't hit the source frame rate anyway)
 * Auto-registered via the linker .cli_cmdtabl section (COMPONENTS_CLI_CMD_EXPORT)
 * so bk_cli_init() picks it up regardless of CLI-subsystem init ordering. */
static void cli_baf_display_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

#if CONFIG_LVGL
    if (argc == 3 && os_strcmp(argv[1], "rot") == 0) {
        uint16_t degrees = (uint16_t)os_strtoul(argv[2], NULL, 10);
        rott_angle_t rotation = ROTATE_NONE;
        bk_err_t ret = baf_example_rotation_from_degrees(degrees, &rotation);
        if (ret == BK_OK) {
            ret = lv_vendor_set_dynamic_rotation(rotation);
        }
        LOGI("baf_display rot %u ret=%d\r\n", degrees, ret);
        return;
    }
    LOGI("usage: baf_display rot <0|90|180|270>\r\n");
#else
    if (argc == 3 && os_strcmp(argv[1], "scene") == 0) {
        int scene = (int)os_strtoul(argv[2], NULL, 10);
        avdk_err_t ret = baf_raw_set_scene(scene);
        LOGI("baf_display scene %d ret=%d\r\n", scene, (int)ret);
        return;
    }
    if (argc == 4 && os_strcmp(argv[1], "layer") == 0) {
        int idx = (int)os_strtoul(argv[2], NULL, 10);
        avdk_err_t ret = baf_raw_set_layer_file(idx, argv[3]);
        LOGI("baf_display layer %d '%s' ret=%d\r\n", idx, argv[3], (int)ret);
        return;
    }
    if (argc == 2 && os_strcmp(argv[1], "maxlayers") == 0) {
        LOGI("baf_display max layers = %d\r\n", BAF_MAX_LAYERS);
        return;
    }
    if (argc == 3 && os_strcmp(argv[1], "freerun") == 0) {
        bool enable = (os_strtoul(argv[2], NULL, 10) != 0);
        avdk_err_t ret = baf_raw_set_freerun(enable);
        LOGI("baf_display freerun=%d ret=%d\r\n", (int)enable, (int)ret);
        return;
    }
    LOGI("usage: baf_display scene <1|2|3> | layer <idx> <sdpath|clear> | "
         "maxlayers | freerun <0|1>\r\n");
#endif
}

COMPONENTS_CLI_CMD_EXPORT
static const struct cli_command s_baf_display_commands[] =
{
#if CONFIG_LVGL
    {"baf_display", "baf_display rot <0|90|180|270>", cli_baf_display_cmd},
#else
    {"baf_display", "baf_display scene <1|2|3> | layer <idx> <sdpath|clear> | maxlayers | freerun <0|1>", cli_baf_display_cmd},
#endif
};

int main(void)
{
    bk_init();

    media_service_init();

    robot_board_power_on();
    bk_auxldo_enable();

    bk_frame_buffer_init();

    /* On-board TF/SD card + `tf` CLI (list dirs/files). Independent of the
     * playback backend, so available in both RAW and LVGL builds. */
    tf_card_init();

#if CONFIG_LVGL
    lvgl_app_baf_example_init();
#else
    baf_raw_start();
#endif

    return 0;
}
