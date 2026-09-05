/**
 * @file baf_file.c
 *
 * Read a BAF v1 container file straight off the filesystem into one PSRAM buffer
 * and return the raw bytes. Parsing/validation is NOT done here -- the caller
 * hands the buffer to bk_baf_open(cfg.data), and the bk_baf component parses the
 * container internally (identical to consuming a compiled-in C byte array). This
 * keeps all container knowledge inside bk_baf; the project only moves bytes.
 *
 * Container layout: see BAF_SPEC_CN.md (64B FileHeader + ChunkDirectory + IDX/DUR/DATA).
 */

#include "baf_file.h"

#include <os/mem.h>
#include <components/log.h>
#include "ff.h"

#define TAG "baf_file"

#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BAF_MIN_FILE_SIZE   64U                    /* container FileHeader is 64 bytes */
#define BAF_MAX_FILE_SIZE   (8U * 1024U * 1024U)   /* sanity cap */

uint8_t *baf_file_read(const char *path, uint32_t *out_size)
{
    if(path == NULL || out_size == NULL) return NULL;

    FIL fp;
    FRESULT fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK) {
        LOGE("open '%s' failed: FRESULT=%d\r\n", path, (int)fr);
        return NULL;
    }

    uint32_t size = (uint32_t)f_size(&fp);
    if (size < BAF_MIN_FILE_SIZE || size > BAF_MAX_FILE_SIZE) {
        LOGE("'%s' bad size %u\r\n", path, (unsigned)size);
        f_close(&fp);
        return NULL;
    }

    uint8_t *buf = (uint8_t *)psram_malloc(size);
    if (buf == NULL) {
        LOGE("psram_malloc %u failed\r\n", (unsigned)size);
        f_close(&fp);
        return NULL;
    }

    /* Read in chunks; a single huge f_read can trip some FATFS/SD stacks. */
    uint32_t done = 0;
    while (done < size) {
        UINT br = 0;
        uint32_t want = size - done;
        if (want > 32768U) want = 32768U;
        fr = f_read(&fp, buf + done, want, &br);
        if (fr != FR_OK || br == 0) {
            LOGE("read '%s' failed at %u: FRESULT=%d br=%u\r\n",
                 path, (unsigned)done, (int)fr, (unsigned)br);
            psram_free(buf);
            f_close(&fp);
            return NULL;
        }
        done += br;
    }
    f_close(&fp);

    *out_size = size;
    return buf;
}
