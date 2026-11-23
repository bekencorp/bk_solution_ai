#include "os/os.h"
#include "os/str.h"
#include "lcd_panel_devices.h"
#include "components/bk_display.h"
#include "avi_player.h"
#include "gpio_driver.h"
#include <driver/gpio.h>
#include "bk_posix.h"
#include "driver/flash_partition.h"
#include "frame_buffer.h"
#include "bk_dual_screen_avi_player.h"
#if CONFIG_LVGL
#include "lvgl.h"
#include "lv_vendor.h"
#endif


#define TAG "dual_avi"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD(TAG, ##__VA_ARGS__)
#define LOGV(...) BK_LOGV(TAG, ##__VA_ARGS__)


#define LCD_BL_IO GPIO_25
#define AVI_PLAYER_PAUSE_WAIT_MS 2000
#define AVI_PLAYER_FILE_PATH_MAX_LEN 128

// Backlight initialization flag
static bool lcd_backlight_initialized = false;

static beken_thread_t g_dual_screen_avi_player_thread;
static beken_semaphore_t g_dual_screen_avi_player_sem;
static bool g_dual_screen_avi_player_is_running = false;
static bk_avi_player_t *handle = NULL;
static bk_display_ctlr_handle_t lcd_display_handle = NULL;
static frame_buffer_t *lcd_frame_buffer = NULL;
static char s_avi_file_path[AVI_PLAYER_FILE_PATH_MAX_LEN] = {0};

#if CONFIG_LVGL
static lv_obj_t *label1 = NULL;
static lv_obj_t *label2 = NULL;
static frame_buffer_t *lvgl_frame_buffer[CONFIG_LVGL_FRAME_BUFFER_NUM] = {0};
static bool lvgl_ui_is_running = false;
static bool lvgl_vendor_is_initialized = false;
static volatile bool lvgl_stop_pending_on_first_avi_frame = false;

LV_FONT_DECLARE(lv_font);
#endif

static bk_err_t bk_dual_screen_avi_player_create_thread(void);
#if CONFIG_LVGL
static void bk_dual_screen_avi_player_stop_lvgl_on_first_avi_frame(void);
#endif

bk_display_dual_spi_ctlr_config_t dual_spi_ctlr_config = {
    .lcd0_config.lcd_device = &lcd_device_gc9d01,
    .lcd0_config.spi_id = 0,
    .lcd0_config.dc_pin = GPIO_7,
    .lcd0_config.reset_pin = GPIO_6,
    .lcd0_config.te_pin = 0,
    .lcd1_config.lcd_device = &lcd_device_gc9d01,
    .lcd1_config.spi_id = 1,
    .lcd1_config.dc_pin = GPIO_5,
    .lcd1_config.reset_pin = GPIO_45,
    .lcd1_config.te_pin = 0,
};


static avdk_err_t lcd_backlight_open(uint8_t bl_io)
{
    // Initialize only once, skip if already initialized
    if (lcd_backlight_initialized)
    {
        return AVDK_ERR_OK;
    }

    gpio_dev_unmap(bl_io);
    BK_LOG_ON_ERR(bk_gpio_enable_output(bl_io));
    BK_LOG_ON_ERR(bk_gpio_pull_up(bl_io));
    bk_gpio_set_output_high(bl_io);

    lcd_backlight_initialized = true;

    return AVDK_ERR_OK;
}

static avdk_err_t lcd_backlight_close(uint8_t bl_io)
{
    // Only close if already initialized
    if (!lcd_backlight_initialized)
    {
        return AVDK_ERR_OK;
    }

    BK_LOG_ON_ERR(bk_gpio_pull_down(bl_io));
    bk_gpio_set_output_low(bl_io);

    lcd_backlight_initialized = false;

    return AVDK_ERR_OK;
}

static int _fs_mount(void)
{
    struct bk_fatfs_partition partition;
    char *fs_name = NULL;
    int ret;

    fs_name = "fatfs";
    partition.part_type = FATFS_DEVICE;

    partition.part_dev.device_name = FATFS_DEV_SDCARD;
    partition.mount_path = VFS_SD_0_PATITION_0;

    ret = mount("SOURCE_NONE", partition.mount_path, fs_name, 0, &partition);

    return ret;
}

static bk_err_t bk_avi_player_vfs_init(void)
{
    bk_err_t ret = BK_FAIL;

    do {
        ret = _fs_mount();
        if (BK_OK != ret)
        {
            BK_LOGD(NULL, "[%s][%d] mount fail:%d\r\n", __FUNCTION__, __LINE__, ret);
            break;
        }

        BK_LOGD(NULL, "[%s][%d] mount success\r\n", __FUNCTION__, __LINE__);
    } while(0);

    return ret;
}

static bk_err_t bk_avi_player_vfs_deinit(void)
{
    bk_err_t ret = BK_FAIL;

    ret = umount(VFS_SD_0_PATITION_0);
    if (BK_OK != ret) {
        BK_LOGD(NULL, "[%s][%d] unmount fail:%d\r\n", __FUNCTION__, __LINE__, ret);
        return ret;
    }

    BK_LOGD(NULL, "[%s][%d] unmount success\r\n", __FUNCTION__, __LINE__);

    return ret;
}

static avdk_err_t display_frame_free_cb(void *frame)
{
    lcd_backlight_open(LCD_BL_IO);
    return AVDK_ERR_OK;
}

static void dual_screen_avi_player_thread(beken_thread_arg_t data)
{
    bk_err_t ret;
    uint32_t delay_time = 0;
    uint32_t start_time, end_time;

    g_dual_screen_avi_player_is_running = true;
    rtos_set_semaphore(&g_dual_screen_avi_player_sem);

    handle->pos = 0;
    delay_time = 1000 / (uint32_t)handle->avi->fps;

    while (g_dual_screen_avi_player_is_running)
    {
        if (handle->pos == handle->video_num) {
            handle->pos = 0;
        }

        start_time = rtos_get_time();
        ret = bk_avi_player_video_parse();
        if (ret != BK_OK) {
            LOGE("%s %d bk_avi_video_prase_to_rgb565 failed\r\n", __func__, __LINE__);
            handle->pos++;
            continue;
        }
        handle->pos++;

        if (handle->segment_flag) {
            lcd_frame_buffer->frame = handle->segmentbuffer;
        } else {
            lcd_frame_buffer->frame = handle->framebuffer;
        }

#if CONFIG_LVGL
        bk_dual_screen_avi_player_stop_lvgl_on_first_avi_frame();
#endif

        ret = bk_display_flush(lcd_display_handle, lcd_frame_buffer, display_frame_free_cb);
        if (ret != BK_OK) {
            LOGE("%s %d bk_display_flush failed, ret=%d\r\n", __func__, __LINE__, ret);
        }
        end_time = rtos_get_time();
        LOGV("bk_avi_player_video_parse time: %d ms\n", end_time - start_time);

        if (end_time - start_time > delay_time) {
            LOGI("bk_avi_player_video_parse time is too long, just delay 2ms, %dms\n", end_time - start_time);
            rtos_delay_milliseconds(2);
        } else {
            rtos_delay_milliseconds(delay_time);
        }
    }

    g_dual_screen_avi_player_is_running = false;
    g_dual_screen_avi_player_thread = NULL;
    rtos_set_semaphore(&g_dual_screen_avi_player_sem);
    rtos_delete_thread(NULL);
}

static bk_err_t bk_dual_screen_avi_player_create_thread(void)
{
    bk_err_t ret = BK_OK;

    ret = rtos_create_thread(&g_dual_screen_avi_player_thread,
                             BEKEN_DEFAULT_WORKER_PRIORITY - 1,
                             "dual_screen_avi_player_thread",
                             (beken_thread_function_t)dual_screen_avi_player_thread,
                             1024 * 4,
                             NULL);

    if (ret != BK_OK) {
        LOGE("%s, init thread failed\r\n", __func__);
        g_dual_screen_avi_player_thread = NULL;
        return ret;
    }

    rtos_get_semaphore(&g_dual_screen_avi_player_sem, BEKEN_NEVER_TIMEOUT);

    return BK_OK;
}

#if CONFIG_LVGL
static bk_err_t bk_dual_screen_avi_player_stop_thread(uint32_t timeout_ms)
{
    bk_err_t ret = BK_OK;

    if (!g_dual_screen_avi_player_is_running) {
        return BK_OK;
    }

    g_dual_screen_avi_player_is_running = false;
    ret = rtos_get_semaphore(&g_dual_screen_avi_player_sem, timeout_ms);
    if (ret != BK_OK) {
        LOGW("%s wait thread exit timeout\n", __func__);
        return ret;
    }

    return BK_OK;
}

static bk_err_t bk_dual_screen_avi_player_lvgl_start(void)
{
    lv_vnd_config_t lv_vnd_config = {0};

    if (lvgl_ui_is_running) {
        return BK_OK;
    }

    if (!lcd_display_handle || !lcd_frame_buffer) {
        LOGE("%s display resource is not ready\n", __func__);
        return BK_FAIL;
    }

    if (!lvgl_vendor_is_initialized) {
        lv_vnd_config.width = dual_spi_ctlr_config.lcd0_config.lcd_device->width;
        lv_vnd_config.height = dual_spi_ctlr_config.lcd0_config.lcd_device->height * 2;
        lv_vnd_config.render_mode = RENDER_PARTIAL_MODE;
        lv_vnd_config.rotation = ROTATE_NONE;
        lv_vnd_config.handle = lcd_display_handle;

        for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
            lvgl_frame_buffer[i] = frame_buffer_display_malloc(lv_vnd_config.width * lv_vnd_config.height * sizeof(bk_color_t));
            if (lvgl_frame_buffer[i] == NULL) {
                LOGE("%s lvgl frame buffer malloc failed, index=%d\n", __func__, i);
                for (int j = 0; j < i; j++) {
                    frame_buffer_display_free(lvgl_frame_buffer[j]);
                    lvgl_frame_buffer[j] = NULL;
                }
                return BK_FAIL;
            }
            lv_vnd_config.frame_buffer[i] = lvgl_frame_buffer[i];
        }

        if (lv_vendor_init(&lv_vnd_config) != BK_OK) {
            LOGE("%s lv_vendor_init failed\n", __func__);
            for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
                if (lvgl_frame_buffer[i]) {
                    frame_buffer_display_free(lvgl_frame_buffer[i]);
                    lvgl_frame_buffer[i] = NULL;
                }
            }
            return BK_FAIL;
        }

        lvgl_vendor_is_initialized = true;
    }

    lv_vendor_disp_lock();
    label1 = lv_label_create(lv_scr_act());
    lv_label_set_text(label1, "图 像");
    lv_obj_set_style_text_font(label1, &lv_font, 0);
    lv_obj_align(label1, LV_ALIGN_TOP_MID, 0, 60);

    label2 = lv_label_create(lv_scr_act());
    lv_label_set_text(label2, "识 别");
    lv_obj_set_style_text_font(label2, &lv_font, 0);
    lv_obj_align(label2, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_vendor_disp_unlock();

    lv_vendor_start();
    lvgl_ui_is_running = true;

    return BK_OK;
}

static void bk_dual_screen_avi_player_lvgl_delete_label(void)
{
    if (!lvgl_ui_is_running) {
        return;
    }

    lv_vendor_disp_lock();
    if (label1) {
        lv_obj_del(label1);
        label1 = NULL;
    }

    if (label2) {
        lv_obj_del(label2);
        label2 = NULL;
    }

    lv_vendor_disp_unlock();

    lvgl_ui_is_running = false;
}

static void bk_dual_screen_avi_player_lvgl_stop(void)
{
    lvgl_stop_pending_on_first_avi_frame = false;

    if (!lvgl_vendor_is_initialized) {
        return;
    }

    bk_dual_screen_avi_player_lvgl_delete_label();
    lv_vendor_stop();
}

static void bk_dual_screen_avi_player_stop_lvgl_on_first_avi_frame(void)
{
    if (!lvgl_stop_pending_on_first_avi_frame) {
        return;
    }

    lvgl_stop_pending_on_first_avi_frame = false;
    LOGI("%s first avi frame is ready, stop lvgl before display\n", __func__);
    bk_dual_screen_avi_player_lvgl_stop();
}

static void bk_dual_screen_avi_player_lvgl_deinit(void)
{
    if (!lvgl_vendor_is_initialized) {
        return;
    }

    lv_vendor_deinit();
    for (int i = 0; i < CONFIG_LVGL_FRAME_BUFFER_NUM; i++) {
        if (lvgl_frame_buffer[i]) {
            frame_buffer_display_free(lvgl_frame_buffer[i]);
            lvgl_frame_buffer[i] = NULL;
        }
    }

    lvgl_vendor_is_initialized = false;
}
#endif

static void bk_dual_screen_avi_player_release_resource(void)
{
#if CONFIG_LVGL
    lvgl_stop_pending_on_first_avi_frame = false;
    bk_dual_screen_avi_player_lvgl_stop();
#endif

    if (g_dual_screen_avi_player_is_running) {
        g_dual_screen_avi_player_is_running = false;
    }

    if (g_dual_screen_avi_player_thread) {
        bk_err_t ret = rtos_get_semaphore(&g_dual_screen_avi_player_sem, BEKEN_NEVER_TIMEOUT);
        if (ret != BK_OK) {
            LOGE("%s g_dual_screen_avi_player_sem get failed\n", __func__);
        }
    }

    if (lcd_display_handle) {
        bk_display_close(lcd_display_handle);
        bk_display_delete(lcd_display_handle);
        lcd_display_handle = NULL;
    }

#if CONFIG_LVGL
    bk_dual_screen_avi_player_lvgl_deinit();
#endif

    if (handle) {
        bk_avi_player_close();
        handle = NULL;
    }

    bk_avi_player_vfs_deinit();

    if (lcd_frame_buffer) {
        os_free(lcd_frame_buffer);
        lcd_frame_buffer = NULL;
    }

    if (g_dual_screen_avi_player_sem) {
        bk_err_t ret = rtos_deinit_semaphore(&g_dual_screen_avi_player_sem);
        if (ret != BK_OK) {
            LOGE("%s g_dual_screen_avi_player_sem deinit failed\n", __func__);
        }
        g_dual_screen_avi_player_sem = NULL;
    }
}

static bk_err_t bk_dual_screen_avi_player_reopen_avi(void)
{
    bk_err_t ret = BK_OK;
    bk_avi_player_config_t avi_player_config = {0};

    if (s_avi_file_path[0] == '\0') {
        LOGE("%s avi file path is empty\n", __func__);
        return BK_FAIL;
    }

    if (!lcd_frame_buffer) {
        LOGE("%s lcd frame buffer is not ready\n", __func__);
        return BK_FAIL;
    }

    if (handle) {
        bk_avi_player_close();
        handle = NULL;
    }

    avi_player_config.file_path = s_avi_file_path;
    avi_player_config.output_format = AVI_PLAYER_OUTPUT_FORMAT_RGB565;
    avi_player_config.segment_flag = true;
    avi_player_config.rgb565_byte_swap_flag = true;

    ret = bk_avi_player_open(&avi_player_config);
    if (ret != BK_OK) {
        LOGE("%s %d bk_avi_player_open failed\r\n", __func__, __LINE__);
        return ret;
    }

    handle = bk_avi_player_get_handle();
    if (handle == NULL) {
        LOGE("%s %d bk_avi_player_get_handle failed\r\n", __func__, __LINE__);
        bk_avi_player_close();
        return BK_FAIL;
    }

    lcd_frame_buffer->size = handle->frame_size;
    if (handle->segment_flag) {
        lcd_frame_buffer->width = handle->avi->height;
        lcd_frame_buffer->height = handle->avi->width;
    } else {
        lcd_frame_buffer->width = handle->avi->width;
        lcd_frame_buffer->height = handle->avi->height;
    }
    lcd_frame_buffer->fmt = PIXEL_FMT_RGB565;

    LOGI("%s complete\n", __func__);

    return BK_OK;
}

bk_err_t bk_dual_screen_avi_player_start(char *file_path)
{
    bk_err_t ret = BK_OK;
    bk_avi_player_config_t avi_player_config = {0};

    if (g_dual_screen_avi_player_is_running) {
        LOGW("%s already running\n", __func__);
        return BK_OK;
    }

    if (file_path) {
        os_snprintf(s_avi_file_path, sizeof(s_avi_file_path), "%s", file_path);
    }

    ret = bk_avi_player_vfs_init();
    if (ret != BK_OK) {
        LOGE("%s %d bk_avi_player_vfs_init failed\r\n", __func__, __LINE__);
        return ret;
    }

    avi_player_config.file_path = file_path;
    avi_player_config.output_format = AVI_PLAYER_OUTPUT_FORMAT_RGB565;
    avi_player_config.segment_flag = true;
    avi_player_config.rgb565_byte_swap_flag = true;
    ret = bk_avi_player_open(&avi_player_config);
    if (ret != BK_OK) {
        LOGE("%s %d bk_avi_player_open failed\r\n", __func__, __LINE__);
        bk_avi_player_vfs_deinit();
        return ret;
    }

    handle = bk_avi_player_get_handle();
    if (handle == NULL) {
        LOGE("%s %d bk_avi_player_get_handle failed\r\n", __func__, __LINE__);
        bk_avi_player_close();
        bk_avi_player_vfs_deinit();
        return BK_FAIL;
    }

    ret = rtos_init_semaphore_ex(&g_dual_screen_avi_player_sem, 1, 0);
    if (ret != BK_OK) {
        LOGE("%s g_dual_screen_avi_player_sem init failed\n", __func__);
        bk_avi_player_close();
        bk_avi_player_vfs_deinit();
        return ret;
    }

    ret = bk_display_dual_spi_new(&lcd_display_handle, &dual_spi_ctlr_config);
    if (ret != BK_OK) {
        LOGE("%s %d bk_display_dual_spi_new failed\r\n", __func__, __LINE__);
        bk_avi_player_close();
        bk_avi_player_vfs_deinit();
        rtos_deinit_semaphore(&g_dual_screen_avi_player_sem);
        return ret;
    }

    ret = bk_display_open(lcd_display_handle);
    if (ret != BK_OK) {
        LOGE("%s %d bk_display_open failed\r\n", __func__, __LINE__);
        bk_avi_player_close();
        bk_avi_player_vfs_deinit();
        bk_display_delete(lcd_display_handle);
        rtos_deinit_semaphore(&g_dual_screen_avi_player_sem);
        return ret;
    }

    lcd_frame_buffer = os_malloc(sizeof(frame_buffer_t));
    if (lcd_frame_buffer == NULL) {
        LOGE("%s %d os_malloc failed\r\n", __func__, __LINE__);
        bk_avi_player_close();
        bk_avi_player_vfs_deinit();
        bk_display_close(lcd_display_handle);
        bk_display_delete(lcd_display_handle);
        rtos_deinit_semaphore(&g_dual_screen_avi_player_sem);
        return BK_FAIL;
    }

    lcd_frame_buffer->size = handle->frame_size;
    if (handle->segment_flag) {
        lcd_frame_buffer->width = handle->avi->height;
        lcd_frame_buffer->height = handle->avi->width;
    } else {
        lcd_frame_buffer->width = handle->avi->width;
        lcd_frame_buffer->height = handle->avi->height;
    }
    lcd_frame_buffer->fmt = PIXEL_FMT_RGB565;

    ret = bk_dual_screen_avi_player_create_thread();
    if (ret != BK_OK) {
        bk_avi_player_close();
        bk_avi_player_vfs_deinit();
        bk_display_close(lcd_display_handle);
        bk_display_delete(lcd_display_handle);
        rtos_deinit_semaphore(&g_dual_screen_avi_player_sem);
        os_free(lcd_frame_buffer);
        lcd_frame_buffer = NULL;
        return ret;
    }

    LOGI("%s complete\n", __func__);

    return BK_OK;
}

bk_err_t bk_dual_screen_avi_player_switch_ui(bk_dual_screen_avi_player_ui_t ui_index)
{
    bk_err_t ret = BK_OK;

#if !CONFIG_LVGL
    LOGE("%s CONFIG_LVGL is not enabled\n", __func__);
    return BK_FAIL;
#else
    if (ui_index == BK_DUAL_SCREEN_AVI_PLAYER_UI_TEXT_AND_IMAGE) {
        lvgl_stop_pending_on_first_avi_frame = false;

        ret = bk_dual_screen_avi_player_stop_thread(AVI_PLAYER_PAUSE_WAIT_MS);
        if (ret != BK_OK) {
            return ret;
        }

        ret = bk_dual_screen_avi_player_lvgl_start();
        if (ret != BK_OK) {
            LOGE("%s lvgl start failed, ret=%d\n", __func__, ret);
            bk_dual_screen_avi_player_create_thread();
            return ret;
        }

        lcd_backlight_open(LCD_BL_IO);
        LOGI("%s switch to text and image complete\n", __func__);
        return BK_OK;
    }

    if (ui_index == BK_DUAL_SCREEN_AVI_PLAYER_UI_TEXT) {
        char file_path[AVI_PLAYER_FILE_PATH_MAX_LEN] = {0};

        if (s_avi_file_path[0] == '\0') {
            LOGE("%s avi file path is empty\n", __func__);
            return BK_FAIL;
        }

        os_snprintf(file_path, sizeof(file_path), "%s", s_avi_file_path);
        lvgl_stop_pending_on_first_avi_frame = true;

        if (g_dual_screen_avi_player_is_running) {
            return BK_OK;
        }

        if (!handle || !lcd_display_handle || !lcd_frame_buffer) {
            LOGE("%s player resource is not ready\n", __func__);
            lvgl_stop_pending_on_first_avi_frame = false;
            return BK_FAIL;
        }

        ret = bk_dual_screen_avi_player_reopen_avi();
        if (ret != BK_OK) {
            LOGE("%s avi player reopen failed, ret=%d\n", __func__, ret);
            lvgl_stop_pending_on_first_avi_frame = false;
            bk_dual_screen_avi_player_release_resource();
            ret = bk_dual_screen_avi_player_start(file_path);
            if (ret != BK_OK) {
                LOGE("%s avi player restart failed, ret=%d\n", __func__, ret);
                return ret;
            }
            return BK_OK;
        }

        ret = bk_dual_screen_avi_player_create_thread();
        if (ret != BK_OK) {
            LOGE("%s avi thread start failed, ret=%d\n", __func__, ret);
            lvgl_stop_pending_on_first_avi_frame = false;
            bk_dual_screen_avi_player_release_resource();
            ret = bk_dual_screen_avi_player_start(file_path);
            if (ret != BK_OK) {
                LOGE("%s avi player restart failed, ret=%d\n", __func__, ret);
                return ret;
            }
        }

        LOGI("%s switch to text complete\n", __func__);
        return BK_OK;
    }

    LOGE("%s invalid ui index: %d\n", __func__, ui_index);
    return BK_FAIL;
#endif
}

bk_err_t bk_dual_screen_avi_player_stop(void)
{
    bk_err_t ret = BK_OK;

#if CONFIG_LVGL
    bk_dual_screen_avi_player_lvgl_stop();
#endif

    if (!g_dual_screen_avi_player_is_running) {
        if (!handle && !lcd_display_handle && !lcd_frame_buffer) {
            LOGW("%s already stopped\n", __func__);
            return BK_OK;
        }
    } else {
        g_dual_screen_avi_player_is_running = false;
    }

    if (g_dual_screen_avi_player_thread) {
        ret = rtos_get_semaphore(&g_dual_screen_avi_player_sem, BEKEN_NEVER_TIMEOUT);
        if (ret != BK_OK) {
            LOGE("%s g_dual_screen_avi_player_sem get failed\n", __func__);
            return ret;
        }
    }

    ret = bk_display_close(lcd_display_handle);
    if (ret != BK_OK) {
        LOGE("%s %d bk_display_close failed\r\n", __func__, __LINE__);
        return ret;
    }
    LOGD("bk_display_close success!\n");

    ret = bk_display_delete(lcd_display_handle);
    if (ret != BK_OK) {
        LOGE("%s %d bk_display_delete failed\r\n", __func__, __LINE__);
        return ret;
    }
    lcd_display_handle = NULL;
    LOGD("bk_display_delete success!\n");

#if CONFIG_LVGL
    bk_dual_screen_avi_player_lvgl_deinit();
#endif

    bk_avi_player_close();
    bk_avi_player_vfs_deinit();

    if (lcd_frame_buffer) {
        os_free(lcd_frame_buffer);
        lcd_frame_buffer = NULL;
    }

    ret = rtos_deinit_semaphore(&g_dual_screen_avi_player_sem);
    if (ret != BK_OK) {
        LOGE("%s g_dual_screen_avi_player_sem deinit failed\n", __func__);
        return ret;
    }
    g_dual_screen_avi_player_sem = NULL;
    lcd_backlight_close(LCD_BL_IO);
    LOGI("%s complete\n", __func__);

    return BK_OK;
}
