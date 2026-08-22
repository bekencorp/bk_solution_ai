/**
 * @file baf_file.c
 *
 * Play a .baf file straight off the filesystem instead of a compiled-in C array.
 *
 * The .baf container (baf_tool/tools/to_baf.py) is a 1:1 on-disk image of the
 * public bk_baf_media_t model: little-endian header, then u32 durations, then
 * (u32 offset,u32 size) AU tables == bk_baf_au_t, then the RGB / Alpha H.264
 * Annex-B blobs. So we just read the whole file into one PSRAM buffer and point
 * a bk_baf_media_t at the sections in place -- the closed decoder consumes it
 * exactly like the generated *_baf_asset.c. No SDK modification required.
 *
 *   header (52B, little-endian):
 *     char magic[8] "BAFANIM1"; u32 version; u16 w,h,alpha_w,alpha_h;
 *     u32 frame_count; u32 flags(bit0=HAS_ALPHA); u32 rgb_size; u32 alpha_size;
 *     u32 reserved[4];
 *   body: durations[fc] | rgb_aus[fc] | alpha_aus[fc]? | rgb_data | alpha_data?
 */

#include "baf_file.h"

#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <components/log.h>
#include "ff.h"

#define TAG "baf_file"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BAF_HDR_SIZE        52U
#define BAF_FLAG_HAS_ALPHA  (1U << 0)
#define BAF_MAX_FILE_SIZE   (8U * 1024U * 1024U)   /* sanity cap */

/* Instance: the public source/media plus the backing file buffer, so the whole
 * thing frees as a unit. bk_baf_source_t is the first member => the returned
 * source pointer can be cast back to the instance in baf_file_unload(). */
typedef struct {
    bk_baf_source_t source;
    bk_baf_media_t  media;
    uint8_t        *buf;
} baf_file_inst_t;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t *read_whole_file(const char *path, uint32_t *out_size)
{
    FIL fp;
    FRESULT fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK) {
        LOGE("open '%s' failed: FRESULT=%d\r\n", path, (int)fr);
        return NULL;
    }

    uint32_t size = (uint32_t)f_size(&fp);
    if (size < BAF_HDR_SIZE || size > BAF_MAX_FILE_SIZE) {
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

const bk_baf_source_t *baf_file_load(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    uint32_t size = 0;
    uint8_t *buf = read_whole_file(path, &size);
    if (buf == NULL) {
        return NULL;
    }

    /* ---- header ---- */
    static const char k_magic[8] = { 'B', 'A', 'F', 'A', 'N', 'I', 'M', '1' };
    bool magic_ok = true;
    for (int i = 0; i < 8; i++) {
        if (buf[i] != (uint8_t)k_magic[i]) { magic_ok = false; break; }
    }
    if (!magic_ok) {
        LOGE("'%s' bad magic\r\n", path);
        goto fail;
    }
    uint32_t version      = rd32(buf + 8);
    uint16_t width        = rd16(buf + 12);
    uint16_t height       = rd16(buf + 14);
    uint16_t alpha_width  = rd16(buf + 16);
    uint16_t alpha_height = rd16(buf + 18);
    uint32_t frame_count  = rd32(buf + 20);
    uint32_t flags        = rd32(buf + 24);
    uint32_t rgb_size     = rd32(buf + 28);
    uint32_t alpha_size   = rd32(buf + 32);
    bool     has_alpha    = (flags & BAF_FLAG_HAS_ALPHA) != 0U;

    if (version != 1U || frame_count == 0U || rgb_size == 0U) {
        LOGE("'%s' unsupported (ver=%u fc=%u rgb=%u)\r\n",
             path, (unsigned)version, (unsigned)frame_count, (unsigned)rgb_size);
        goto fail;
    }

    /* ---- section offsets ---- */
    uint32_t off_dur      = BAF_HDR_SIZE;
    uint32_t off_rgb_aus  = off_dur + frame_count * 4U;
    uint32_t off_alpha_au = off_rgb_aus + frame_count * 8U;      /* if has_alpha */
    uint32_t off_rgb_data = off_rgb_aus + frame_count * 8U +
                            (has_alpha ? frame_count * 8U : 0U);
    uint32_t off_alpha_dt = off_rgb_data + rgb_size;             /* if has_alpha */

    uint32_t need = off_rgb_data + rgb_size + (has_alpha ? alpha_size : 0U);
    if (need > size) {
        LOGE("'%s' truncated (need %u > %u)\r\n", path, (unsigned)need, (unsigned)size);
        goto fail;
    }

    /* ---- wrap the buffer in a source/media (all pointers alias into buf) ---- */
    baf_file_inst_t *inst = (baf_file_inst_t *)os_malloc(sizeof(*inst));
    if (inst == NULL) {
        LOGE("no memory for baf instance\r\n");
        goto fail;
    }
    os_memset(inst, 0, sizeof(*inst));
    inst->buf = buf;

    inst->media.width        = width;
    inst->media.height       = height;
    inst->media.alpha_width  = alpha_width;
    inst->media.alpha_height = alpha_height;
    inst->media.frame_count  = frame_count;
    inst->media.rgb.data      = buf + off_rgb_data;
    inst->media.rgb.data_size = rgb_size;
    inst->media.rgb.aus       = (const bk_baf_au_t *)(buf + off_rgb_aus);
    inst->media.rgb.au_count  = frame_count;
    if (has_alpha) {
        inst->media.alpha.data      = buf + off_alpha_dt;
        inst->media.alpha.data_size = alpha_size;
        inst->media.alpha.aus       = (const bk_baf_au_t *)(buf + off_alpha_au);
        inst->media.alpha.au_count  = frame_count;
    }
    inst->media.durations_ms = (const uint32_t *)(buf + off_dur);

    inst->source.magic = BK_BAF_SOURCE_MAGIC;
    inst->source.ops   = &bk_baf_decoder_ops;
    inst->source.data  = &inst->media;

    LOGI("loaded '%s': %ux%u frames=%u alpha=%d (%u bytes)\r\n",
         path, width, height, (unsigned)frame_count, (int)has_alpha, (unsigned)size);
    return &inst->source;

fail:
    psram_free(buf);
    return NULL;
}

void baf_file_unload(const bk_baf_source_t *source)
{
    if (source == NULL) {
        return;
    }
    baf_file_inst_t *inst = (baf_file_inst_t *)source;   /* source is first member */
    if (inst->buf != NULL) {
        psram_free(inst->buf);
    }
    os_free(inst);
}
