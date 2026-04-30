// Copyright 2020-2021 Beken
// beken_robot media device controls -- panel/DPU + MIPI camera + GPU + H.264 encoder.
// See media_devices.h for the public API contract.

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <stdbool.h>
#include <driver/gpio.h>
#include "gpio_driver.h"
#include "sys_driver.h"
#include "sys_hal.h"
#include <components/bk_frame_buffer.h>
#include <common/avdk_pixel_types.h>

#include "media_devices.h"
#include "lcd_image.h"

#include "app_display.h"
#include "app_display_types.h"

#if CONFIG_ISP
#include "app_camera.h"
#include "app_camera_types.h"
#include <components/bk_flexa_bond.h>
#if CONFIG_GPU
#include "app_gpu.h"
#include "app_gpu_types.h"
#include <components/bk_gpu_ctlr.h>
#endif
#if CONFIG_MULTIMEDIA_H264_ENCODE
#include "app_codec.h"
#include "multimedia_img_manager.h"
#endif
#endif

#define TAG "media_dev"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)


/* beken_robot board GPIO map shared by all display flows */
#define ROBOT_GPIO_PERIPH_3V3      GPIO_53   /* board peripheral 3.3V power-on */
#define ROBOT_GPIO_PANEL_RESET     GPIO_5    /* jd9855 reset */
#define ROBOT_GPIO_PANEL_BACKLIGHT GPIO_7    /* active-low backlight enable */

/* MIPI CSI camera (gc2053) board pin map for beken_robot */
#define ROBOT_GPIO_PERIPH_2V8      GPIO_32
#define ROBOT_GPIO_CAM_SCL         GPIO_70
#define ROBOT_GPIO_CAM_SDA         GPIO_71
#define ROBOT_GPIO_CAM_RESET       GPIO_31
#define ROBOT_GPIO_CAM_XCLK        GPIO_59
#define ROBOT_CAM_I2C_ID           1

/* ===========================================================================
 * Module-private singletons
 *
 * The DSI panel + DPU lifecycle is owned entirely by app_display.c
 * (multimedia_device_service); we only call its turn_on/turn_off helpers.
 * ===========================================================================*/

#if CONFIG_ISP && CONFIG_MULTIMEDIA_H264_ENCODE
static void *s_isp_h264e_bond;   /* ISP MP NV12 -> H.264 encoder, hw flexa */
#endif

#if CONFIG_ISP && CONFIG_GPU
static void                *s_isp_gpu_bond;  /* ISP MP NV12 -> GPU, hw flexa */
static bk_gpu_ctlr_handle_t s_gpu_handle;
#endif

typedef struct
{
    uint8_t           enable;
    media_test_mode_t mode;
    beken_thread_t    thread;
    beken_semaphore_t sem;
} media_test_state_t;

static media_test_state_t s_test;

/* ===========================================================================
 * Hardware bring-up shared by all flows
 * ===========================================================================*/
void media_board_power_on(void)
{
    /* board LCD panel peripheral 3.3V power-on */
    gpio_dev_unmap(ROBOT_GPIO_PERIPH_3V3);
    BK_LOG_ON_ERR(bk_gpio_enable_output(ROBOT_GPIO_PERIPH_3V3));
    BK_LOG_ON_ERR(bk_gpio_pull_up(ROBOT_GPIO_PERIPH_3V3));
    bk_gpio_set_output_high(ROBOT_GPIO_PERIPH_3V3);

    /* board camera peripheral 2.8V power-on */
    gpio_dev_unmap(ROBOT_GPIO_PERIPH_2V8);
    BK_LOG_ON_ERR(bk_gpio_enable_output(ROBOT_GPIO_PERIPH_2V8));
    BK_LOG_ON_ERR(bk_gpio_pull_up(ROBOT_GPIO_PERIPH_2V8));
    bk_gpio_set_output_high(ROBOT_GPIO_PERIPH_2V8);
}

static void media_backlight_on(void)
{
    /* active-low backlight: drive LOW = ON. */
    gpio_dev_unmap(ROBOT_GPIO_PANEL_BACKLIGHT);
    BK_LOG_ON_ERR(bk_gpio_enable_output(ROBOT_GPIO_PANEL_BACKLIGHT));
    BK_LOG_ON_ERR(bk_gpio_pull_down(ROBOT_GPIO_PANEL_BACKLIGHT));
    bk_gpio_set_output_low(ROBOT_GPIO_PANEL_BACKLIGHT);
}

static void media_backlight_off(void)
{
    gpio_dev_unmap(ROBOT_GPIO_PANEL_BACKLIGHT);
    BK_LOG_ON_ERR(bk_gpio_enable_output(ROBOT_GPIO_PANEL_BACKLIGHT));
    bk_gpio_set_output_high(ROBOT_GPIO_PANEL_BACKLIGHT);
}

/* ===========================================================================
 * Panel + DPU -- thin wrapper over app_display.c
 *
 * app_mipi_lcd_turn_on() builds the entire DSI bus + panel + DPU stack and
 * votes display LDO; app_gpu.c's completion callback already routes its
 * compressed ARGB8888 frames into this DPU via app_mipi_lcd_flush(), so the
 * (ARGB8888, decompress=true) preview path needs no extra wiring here.
 * ===========================================================================*/

avdk_err_t media_lcd_panel_open(const bk_display_dsi_panel_t *panel,
                            bk_pixel_format_t format,
                            bool decompress)
{
    AVDK_RETURN_ON_FALSE(panel, AVDK_ERR_INVAL, TAG, "panel NULL");

    display_board_config_t db = {0};
    db.mipi.enable          = true;
    db.mipi.pin_reset       = ROBOT_GPIO_PANEL_RESET;
    db.mipi.pin_scl         = -1;   /* jd9855 has no MIPI bridge over I2C */
    db.mipi.pin_sda         = -1;
    db.mipi.panel           = panel;
    db.dpu_video.enable     = true;
    db.dpu_video.decompress = decompress;
    db.dpu_video.format     = format;

    if (app_display_board_config_set(&db) != AVDK_ERR_OK ||
        app_mipi_lcd_turn_on(app_display_board_config_get()) != BK_OK)
    {
        LOGE("app_mipi_lcd_turn_on failed\n");
        return AVDK_ERR_GENERIC;
    }

    media_backlight_on();
    LOGI("panel opened: fmt=%d decompress=%d\n", format, decompress);
    return AVDK_ERR_OK;
}

avdk_err_t media_lcd_panel_close(void)
{
    media_backlight_off();
    app_mipi_lcd_turn_off();
    LOGI("%s complete\n", __func__);
    return AVDK_ERR_OK;
}

bk_display_ctlr_handle_t media_panel_get_dpu_handle(void)
{
    return (bk_display_ctlr_handle_t)app_mipi_lcd_handle_get();
}

/* ===========================================================================
 * media_test_thread -- bring-up helper, mode picks the work loop
 * ===========================================================================*/

static avdk_err_t media_frame_free(void *args)
{
    if (args != NULL)
        bk_frame_buffer_free(args);
    return AVDK_ERR_OK;
}

static void media_test_splash_loop(void)
{
    const uint32_t frame_size = LCD_IMAGE_BK7259_SPLASH_RGB565_BYTES;

    while (s_test.enable)
    {
        if (!app_mipi_lcd_state_get())
        {
            break;
        }

        void *frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, frame_size);
        if (frame == NULL)
        {
            LOGE("splash frame alloc failed, size=%u\n", (unsigned)frame_size);
            rtos_delay_milliseconds(50);
            continue;
        }
        os_memcpy(frame, lcd_image_bk7259_splash_rgb565, frame_size);

        if (app_mipi_lcd_flush(frame, media_frame_free) != AVDK_ERR_OK)
        {
            bk_frame_buffer_free(frame);
        }
        rtos_delay_milliseconds(500);
    }
}

#if CONFIG_MULTIMEDIA_H264_ENCODE
static void media_test_h264_rx_loop(void)
{
    while (s_test.enable)
    {
        frame_buffer_t *fr = (frame_buffer_t *)bk_encoded_complete_data_request(100);
        if (fr == NULL) continue;       /* timeout -> no frame yet */

        /* Stand in for the WiFi/RTP transmitter: throttle the log to one line
         * every 100 encoded frames; encoder already stamps fr->sequence
         * (see h264e_driver.c). */
        if ((fr->sequence % 100) == 0)
        {
            LOGD("h264 frame: seq=%u ptr=%p len=%u type=%d (1=I,0=P)\n",
                 fr->sequence, (void *)fr->frame, fr->length, fr->h264_type);
        }
        bk_encoded_complete_data_free_request((uint8_t *)fr);
    }
}
#endif

static void media_test_thread_entry(void *args)
{
    (void)args;
    rtos_set_semaphore(&s_test.sem);

    switch (s_test.mode)
    {
    case MEDIA_TEST_MODE_SPLASH:
        media_test_splash_loop();
        break;
#if CONFIG_MULTIMEDIA_H264_ENCODE
    case MEDIA_TEST_MODE_H264_WIFI_TX:
        media_test_h264_rx_loop();
        break;
#endif
    default:
        LOGE("unsupported test mode: %d\n", (int)s_test.mode);
        break;
    }

    s_test.thread = NULL;
    rtos_set_semaphore(&s_test.sem);
    rtos_delete_thread(NULL);
}

avdk_err_t media_test_thread_start(media_test_mode_t mode)
{
    if (s_test.thread != NULL)
    {
        LOGW("test thread already running, ignore\n");
        return AVDK_ERR_OK;
    }

    s_test.mode   = mode;
    s_test.enable = 1;

    avdk_err_t ret = rtos_init_semaphore_ex(&s_test.sem, 1, 0);
    if (ret != BK_OK)
    {
        LOGE("init test sem failed\n");
        s_test.enable = 0;
        return ret;
    }

    ret = rtos_create_thread(&s_test.thread,
                             BEKEN_DEFAULT_WORKER_PRIORITY,
                             "media_test",
                             (beken_thread_function_t)media_test_thread_entry,
                             1024 * 8,
                             NULL);
    if (ret != BK_OK)
    {
        LOGE("create test thread failed\n");
        s_test.enable = 0;
        rtos_deinit_semaphore(&s_test.sem);
        return ret;
    }

    rtos_get_semaphore(&s_test.sem, BEKEN_WAIT_FOREVER);
    LOGI("media_test_thread started, mode=%d\n", (int)mode);
    return AVDK_ERR_OK;
}

avdk_err_t media_test_thread_stop(void)
{
    if (s_test.thread == NULL)
        return AVDK_ERR_OK;

    s_test.enable = 0;
    rtos_get_semaphore(&s_test.sem, BEKEN_WAIT_FOREVER);
    rtos_deinit_semaphore(&s_test.sem);
    return AVDK_ERR_OK;
}

/* ===========================================================================
 * MIPI camera + ISP MP (NV12, flexa). The GPU and H.264 encoder bind onto
 * this ISP MP through their own _open APIs.
 * ===========================================================================*/

avdk_err_t media_camera_open(uint16_t cam_w,
                             uint16_t cam_h,
                             uint16_t fps,
                             uint16_t isp_w,
                             uint16_t isp_h)
{
#if !CONFIG_ISP
    (void)cam_w; (void)cam_h; (void)fps; (void)isp_w; (void)isp_h;
    LOGE("CONFIG_ISP not enabled\n");
    return AVDK_ERR_UNSUPPORTED;
#else
    camera_board_config_t cfg = {0};
    cfg.mipi.enable             = true;
    cfg.mipi.pin_scl            = ROBOT_GPIO_CAM_SCL;
    cfg.mipi.pin_sda            = ROBOT_GPIO_CAM_SDA;
    cfg.mipi.i2c_id             = ROBOT_CAM_I2C_ID;
    cfg.mipi.pin_reset          = ROBOT_GPIO_CAM_RESET;
    cfg.mipi.pin_pwdn           = -1;
    cfg.mipi.pin_xclk           = ROBOT_GPIO_CAM_XCLK;
    cfg.mipi.sensor_max_width   = cam_w;
    cfg.mipi.sensor_max_height  = cam_h;
    cfg.mipi.sensor_fps         = (uint8_t)fps;

    cfg.isp.mp_enable = true;
    cfg.isp.mp_flexa  = true;
    cfg.isp.mp_width  = isp_w;
    cfg.isp.mp_height = isp_h;
    cfg.isp.mp_format = BK_PIXEL_FORMAT_NV12;

    if (app_camera_board_config_set(&cfg) != AVDK_ERR_OK)
    {
        LOGE("app_camera_board_config_set failed\n");
        return AVDK_ERR_GENERIC;
    }
    avdk_err_t ret = app_isp_mipi_camera_turn_on(app_camera_board_config_get());
    if (ret != BK_OK)
    {
        LOGE("app_isp_mipi_camera_turn_on failed: %d\n", ret);
        return ret;
    }

    LOGI("camera open: sensor %ux%u@%u -> ISP MP %ux%u\n",
         cam_w, cam_h, fps, isp_w, isp_h);
    return AVDK_ERR_OK;
#endif
}

avdk_err_t media_camera_close(void)
{
#if !CONFIG_ISP
    return AVDK_ERR_UNSUPPORTED;
#else
#if CONFIG_MULTIMEDIA_H264_ENCODE
    if (s_isp_h264e_bond != NULL)
    {
        bk_flexa_isp_h264e_bond_stop(s_isp_h264e_bond);
        s_isp_h264e_bond = NULL;
        app_h264e_turn_off();
    }
#endif

    app_isp_camera_turn_off();
    LOGI("camera closed\n");
    return AVDK_ERR_OK;
#endif
}

/* ===========================================================================
 * GPU: rotate (no scale) NV12 -> ARGB8888 compressed, hw flexa from ISP MP.
 * Does NOT touch panel/DPU. To preview on the panel, open the panel with
 * (ARGB8888, decompress=true); app_gpu.c's completion flushes to the DPU.
 * ===========================================================================*/

avdk_err_t media_gpu_open(uint16_t src_w, uint16_t src_h, uint16_t rotate_deg)
{
#if !(CONFIG_ISP && CONFIG_GPU)
    (void)src_w; (void)src_h; (void)rotate_deg;
    LOGE("CONFIG_ISP/CONFIG_GPU not enabled\n");
    return AVDK_ERR_UNSUPPORTED;
#else
    gpu_board_config_t gpu = {0};
    gpu.flexa.enable       = true;
    gpu.flexa.degree       = rotate_deg;
    gpu.flexa.src_width    = src_w;
    gpu.flexa.src_height   = src_h;
    gpu.flexa.dst_width    = src_w;
    gpu.flexa.dst_height   = src_h;
    gpu.flexa.src_format   = BK_PIXEL_FORMAT_NV12;
    gpu.flexa.dst_format   = BK_PIXEL_FORMAT_ARGB8888;
    gpu.flexa.dst_compress = true;
    gpu.flexa.scale        = false;

    if (app_gpu_board_config_set(&gpu) != AVDK_ERR_OK)
    {
        LOGE("app_gpu_board_config_set failed\n");
        return AVDK_ERR_GENERIC;
    }
    avdk_err_t ret = app_gpu_turn_on(app_gpu_board_config_get());
    if (ret != AVDK_ERR_OK)
    {
        LOGE("app_gpu_turn_on failed: %d\n", ret);
        return AVDK_ERR_GENERIC;
    }
    s_gpu_handle = app_gpu_handle_get();

    void *isp_handle = app_isp_handle_get();
    if (isp_handle == NULL || s_gpu_handle == NULL)
    {
        LOGE("isp_handle=%p gpu_handle=%p\n", isp_handle, s_gpu_handle);
        goto err_bond;
    }
    ret = bk_flexa_isp_gpu_bond_start(&s_isp_gpu_bond, isp_handle, s_gpu_handle);
    if (ret != BK_OK)
    {
        LOGE("bk_flexa_isp_gpu_bond_start failed: %d\n", ret);
        goto err_bond;
    }

    LOGI("GPU open: %ux%u rotate %u -> ARGB8888\n", src_w, src_h, rotate_deg);
    return AVDK_ERR_OK;

err_bond:
    if (s_gpu_handle)
    {
        app_gpu_turn_off(s_gpu_handle);
        s_gpu_handle = NULL;
    }
    return AVDK_ERR_GENERIC;
#endif
}

avdk_err_t media_gpu_close(void)
{
#if !(CONFIG_ISP && CONFIG_GPU)
    return AVDK_ERR_UNSUPPORTED;
#else
    if (s_isp_gpu_bond != NULL)
    {
        bk_flexa_isp_gpu_bond_stop(s_isp_gpu_bond);
        s_isp_gpu_bond = NULL;
    }
    if (s_gpu_handle != NULL)
    {
        app_gpu_turn_off(s_gpu_handle);
        s_gpu_handle = NULL;
    }
    LOGI("GPU closed\n");
    return AVDK_ERR_OK;
#endif
}

/* ===========================================================================
 * H.264 encoder bound to the live ISP MP NV12 stream via hw flexa.
 * ===========================================================================*/

avdk_err_t media_h264_encoder_start(void)
{
#if !CONFIG_MULTIMEDIA_H264_ENCODE
    LOGE("H264 encoder not built (CONFIG_MULTIMEDIA_H264_ENCODE=n)\n");
    return AVDK_ERR_UNSUPPORTED;
#else
    int rc = app_h264e_turn_on();
    if (rc != BK_OK)
    {
        LOGE("app_h264e_turn_on failed: %d\n", rc);
        return AVDK_ERR_GENERIC;
    }

    void *isp_handle = app_isp_handle_get();
    void *enc_handle = app_h264_encode_handle_get();
    if (isp_handle == NULL || enc_handle == NULL)
    {
        LOGE("isp_handle=%p enc_handle=%p\n", isp_handle, enc_handle);
        app_h264e_turn_off();
        return AVDK_ERR_GENERIC;
    }
    rc = bk_flexa_isp_h264e_bond_start(&s_isp_h264e_bond, isp_handle, enc_handle);
    if (rc != BK_OK)
    {
        LOGE("bk_flexa_isp_h264e_bond_start failed: %d\n", rc);
        s_isp_h264e_bond = NULL;
        app_h264e_turn_off();
        return AVDK_ERR_GENERIC;
    }

    LOGI("H264 encoder started\n");
    return AVDK_ERR_OK;
#endif
}

avdk_err_t media_h264_encoder_stop(void)
{
#if !CONFIG_MULTIMEDIA_H264_ENCODE
    return AVDK_ERR_UNSUPPORTED;
#else
    if (s_isp_h264e_bond != NULL)
    {
        bk_flexa_isp_h264e_bond_stop(s_isp_h264e_bond);
        s_isp_h264e_bond = NULL;
    }
    app_h264e_turn_off();
    return AVDK_ERR_OK;
#endif
}
