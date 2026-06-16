/**
 * @file ui_screenshot.c
 * @brief Save the active LVGL screen as a BMP file on SD-NAND.
 */
#include <common/sys_config.h>
#include <common/bk_err.h>
#include <components/log.h>
#include <os/os.h>
#include <os/mem.h>

#include "ui_screenshot.h"

#if CONFIG_LVGL
#include "lvgl.h"
#include "lv_vendor.h"
#endif

#if CONFIG_FATFS
#include "ff.h"
#endif

#include "board_usb_switch.h"

#if CONFIG_CLI
#include "cli.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TAG "ui_snap"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define SNAP_SDNAND_DRIVE_PATH      "1:"
#define SNAP_SDNAND_BASE_DIR        "1:/screenshots"
#define SNAP_DEFAULT_FILE_PREFIX    "lvgl"
#define SNAP_DEFAULT_PATH_BUF_LEN   64u
#define SNAP_MAX_FILE_INDEX         9999u
#define SNAP_SDNAND_POWER_SETTLE_MS 50u
#define SNAP_SDNAND_MKFS_BUF_SIZE   4096u
#define SNAP_WRITE_CHUNK            (16u * 1024u)

#define SNAP_BMP_FILE_HEADER_SIZE   14u
#define SNAP_BMP_INFO_HEADER_SIZE   40u
#define SNAP_BMP_MASK_SIZE          12u
#define SNAP_BMP_HEADER_SIZE        (SNAP_BMP_FILE_HEADER_SIZE + \
                                     SNAP_BMP_INFO_HEADER_SIZE + \
                                     SNAP_BMP_MASK_SIZE)
#define SNAP_BMP_BI_BITFIELDS       3u

#if CONFIG_LVGL && CONFIG_FATFS && LV_USE_SNAPSHOT

static FATFS *s_snap_fs;

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
    p[2] = (uint8_t)((v >> 16) & 0xffu);
    p[3] = (uint8_t)((v >> 24) & 0xffu);
}

static bk_err_t snap_write_all(FIL *fp, const void *buf, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t remaining = len;

    while (remaining > 0) {
        UINT to_write = (remaining > SNAP_WRITE_CHUNK) ? SNAP_WRITE_CHUNK : remaining;
        UINT bw = 0;
        FRESULT fr = f_write(fp, p, to_write, &bw);
        if (fr != FR_OK || bw != to_write) {
            LOGE("f_write failed fr=%d bw=%u/%u\n",
                 fr, (unsigned)bw, (unsigned)to_write);
            return BK_FAIL;
        }
        p += to_write;
        remaining -= to_write;
    }
    return BK_OK;
}

static bk_err_t snap_mount_if_needed(void)
{
    if (board_usb_switch_prepare_nand_access() != BK_OK) {
        LOGE("USB mux is still owned by MSC; cannot access SD-NAND\n");
        return BK_FAIL;
    }

    if (board_sd_nand_power_on() != BK_OK) {
        LOGE("board_sd_nand_power_on failed\n");
        return BK_FAIL;
    }
    rtos_delay_milliseconds(SNAP_SDNAND_POWER_SETTLE_MS);

    FRESULT fr = f_mkdir(SNAP_SDNAND_BASE_DIR);
    if (fr == FR_OK || fr == FR_EXIST) {
        return BK_OK;
    }

    if (s_snap_fs == NULL) {
        s_snap_fs = (FATFS *)os_malloc(sizeof(FATFS));
        if (s_snap_fs == NULL) {
            LOGE("FATFS alloc failed\n");
            return BK_FAIL;
        }
    }

    fr = f_mount(s_snap_fs, SNAP_SDNAND_DRIVE_PATH, 1);
    if (fr == FR_NO_FILESYSTEM) {
        LOGW("no FAT filesystem, running f_mkfs\n");
        uint8_t *mkfs_buf = (uint8_t *)os_malloc(SNAP_SDNAND_MKFS_BUF_SIZE);
        if (mkfs_buf == NULL) {
            LOGE("mkfs buffer alloc failed\n");
            return BK_FAIL;
        }
        fr = f_mkfs(SNAP_SDNAND_DRIVE_PATH, FM_ANY, 0,
                    mkfs_buf, SNAP_SDNAND_MKFS_BUF_SIZE);
        os_free(mkfs_buf);
        if (fr != FR_OK) {
            LOGE("f_mkfs failed fr=%d\n", fr);
            return BK_FAIL;
        }
        fr = f_mount(s_snap_fs, SNAP_SDNAND_DRIVE_PATH, 1);
    }
    if (fr != FR_OK) {
        LOGE("f_mount(%s) failed fr=%d\n", SNAP_SDNAND_DRIVE_PATH, fr);
        return BK_FAIL;
    }

    fr = f_mkdir(SNAP_SDNAND_BASE_DIR);
    if (fr != FR_OK && fr != FR_EXIST) {
        LOGE("f_mkdir(%s) failed fr=%d\n", SNAP_SDNAND_BASE_DIR, fr);
        return BK_FAIL;
    }
    return BK_OK;
}

static bk_err_t snap_open_default(FIL *fp, char *path, uint32_t path_len)
{
    for (uint32_t i = 1; i <= SNAP_MAX_FILE_INDEX; ++i) {
        int n = snprintf(path, path_len, "%s/%s_%04u.bmp",
                         SNAP_SDNAND_BASE_DIR, SNAP_DEFAULT_FILE_PREFIX,
                         (unsigned)i);
        if (n <= 0 || n >= (int)path_len) {
            LOGE("default path overflow n=%d\n", n);
            return BK_FAIL;
        }

        FRESULT fr = f_open(fp, path, FA_CREATE_NEW | FA_WRITE);
        if (fr == FR_OK) {
            return BK_OK;
        }
        if (fr != FR_EXIST) {
            LOGE("f_open(%s) failed fr=%d\n", path, fr);
            return BK_FAIL;
        }
    }

    LOGE("default screenshot index exhausted\n");
    return BK_FAIL;
}

static bk_err_t snap_write_bmp(FIL *fp, const lv_draw_buf_t *draw_buf)
{
    const uint32_t width = draw_buf->header.w;
    const uint32_t height = draw_buf->header.h;
    const uint32_t stride = draw_buf->header.stride;
    const uint32_t row_bytes = width * 2u;
    const uint32_t row_pad = (4u - (row_bytes & 3u)) & 3u;
    const uint32_t image_size = (row_bytes + row_pad) * height;
    const uint32_t file_size = SNAP_BMP_HEADER_SIZE + image_size;

    if (draw_buf->data == NULL || width == 0 || height == 0 || stride < row_bytes) {
        LOGE("invalid draw_buf data=%p %ux%u stride=%u\n",
             draw_buf->data, (unsigned)width, (unsigned)height, (unsigned)stride);
        return BK_FAIL;
    }
    if (width > 0x7fffu || height > 0x7fffu) {
        LOGE("snapshot too large for BMP: %ux%u\n",
             (unsigned)width, (unsigned)height);
        return BK_FAIL;
    }

    uint8_t file_header[SNAP_BMP_FILE_HEADER_SIZE] = {0};
    file_header[0] = 'B';
    file_header[1] = 'M';
    put_le32(&file_header[2], file_size);
    put_le32(&file_header[10], SNAP_BMP_HEADER_SIZE);

    uint8_t info_header[SNAP_BMP_INFO_HEADER_SIZE] = {0};
    put_le32(&info_header[0], SNAP_BMP_INFO_HEADER_SIZE);
    put_le32(&info_header[4], width);
    put_le32(&info_header[8], height);
    put_le16(&info_header[12], 1);
    put_le16(&info_header[14], 16);
    put_le32(&info_header[16], SNAP_BMP_BI_BITFIELDS);
    put_le32(&info_header[20], image_size);

    uint8_t masks[SNAP_BMP_MASK_SIZE] = {0};
    put_le32(&masks[0], 0x0000f800u);
    put_le32(&masks[4], 0x000007e0u);
    put_le32(&masks[8], 0x0000001fu);

    if (snap_write_all(fp, file_header, sizeof(file_header)) != BK_OK ||
        snap_write_all(fp, info_header, sizeof(info_header)) != BK_OK ||
        snap_write_all(fp, masks, sizeof(masks)) != BK_OK) {
        return BK_FAIL;
    }

    const uint8_t pad[3] = {0};
    for (uint32_t y = height; y > 0; --y) {
        const uint8_t *row = draw_buf->data + ((y - 1u) * stride);
        if (snap_write_all(fp, row, row_bytes) != BK_OK) {
            return BK_FAIL;
        }
        if (row_pad > 0 && snap_write_all(fp, pad, row_pad) != BK_OK) {
            return BK_FAIL;
        }
    }

    return BK_OK;
}

bk_err_t ui_screenshot_save_bmp(const char *path)
{
    lv_draw_buf_t *draw_buf = NULL;
    FIL *fp = NULL;
    char *default_path = NULL;
    const char *save_path = path;
    bool file_open = false;
    bk_err_t rc = BK_FAIL;

    lv_vendor_disp_lock();
    lv_obj_t *screen = lv_screen_active();
    if (screen != NULL) {
        lv_obj_invalidate(screen);
        lv_refr_now(NULL);
        draw_buf = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
    }
    lv_vendor_disp_unlock();

    if (draw_buf == NULL) {
        LOGE("lv_snapshot_take failed\n");
        goto out;
    }
    if (draw_buf->header.cf != LV_COLOR_FORMAT_RGB565) {
        LOGE("unexpected snapshot color format: %u\n",
             (unsigned)draw_buf->header.cf);
        goto out;
    }

    if (snap_mount_if_needed() != BK_OK) {
        goto out;
    }

    fp = (FIL *)os_malloc(sizeof(FIL));
    if (fp == NULL) {
        LOGE("FIL alloc failed\n");
        goto out;
    }

    if (save_path == NULL || save_path[0] == '\0') {
        default_path = (char *)os_malloc(SNAP_DEFAULT_PATH_BUF_LEN);
        if (default_path == NULL) {
            LOGE("default path alloc failed\n");
            goto out;
        }
        if (snap_open_default(fp, default_path, SNAP_DEFAULT_PATH_BUF_LEN) != BK_OK) {
            goto out;
        }
        save_path = default_path;
    } else {
        FRESULT fr = f_open(fp, save_path, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr != FR_OK) {
            LOGE("f_open(%s) failed fr=%d\n", save_path, fr);
            goto out;
        }
    }
    file_open = true;

    if (snap_write_bmp(fp, draw_buf) != BK_OK) {
        goto out;
    }
    (void)f_sync(fp);

    LOGI("saved %ux%u RGB565 BMP -> %s\n",
         (unsigned)draw_buf->header.w, (unsigned)draw_buf->header.h, save_path);
    rc = BK_OK;

out:
    if (file_open && fp != NULL) {
        (void)f_close(fp);
    }
    if (fp != NULL) {
        os_free(fp);
    }
    if (default_path != NULL) {
        os_free(default_path);
    }
    if (draw_buf != NULL) {
        lv_draw_buf_destroy(draw_buf);
    }
    return rc;
}

#else  /* !(CONFIG_LVGL && CONFIG_FATFS && LV_USE_SNAPSHOT) */

bk_err_t ui_screenshot_save_bmp(const char *path)
{
    (void)path;
#if !CONFIG_LVGL
    LOGE("LVGL is disabled\n");
#elif !CONFIG_FATFS
    LOGE("FATFS is disabled\n");
#else
    LOGE("LV_USE_SNAPSHOT is disabled\n");
#endif
    return BK_FAIL;
}

#endif /* CONFIG_LVGL && CONFIG_FATFS && LV_USE_SNAPSHOT */

#if CONFIG_CLI

static void snap_cli_help(void)
{
    LOGI("usage:\n");
    LOGI("  snap              - save to 1:/screenshots/lvgl_XXXX.bmp\n");
    LOGI("  snap <path.bmp>   - save to a FatFS path, e.g. 1:/screenshots/top.bmp\n");
}

static void snap_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen,
                         int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc > 1 &&
        (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "?") == 0)) {
        snap_cli_help();
        return;
    }

    const char *path = (argc > 1) ? argv[1] : NULL;
    if (ui_screenshot_save_bmp(path) != BK_OK) {
        LOGE("screenshot save failed\n");
    }
}

static const struct cli_command s_snap_cmds[] = {
    {"snap", "snap [1:/screenshots/name.bmp]", snap_cli_cmd},
};

void ui_screenshot_cli_init(void)
{
    static bool registered;
    if (registered) {
        return;
    }

    int ret = cli_register_commands(s_snap_cmds,
                                    sizeof(s_snap_cmds) / sizeof(s_snap_cmds[0]));
    if (ret == 0) {
        registered = true;
        LOGI("snap CLI registered\n");
    } else {
        LOGE("cli_register_commands failed: %d\n", ret);
    }
}

#else  /* !CONFIG_CLI */

void ui_screenshot_cli_init(void)
{
}

#endif /* CONFIG_CLI */
