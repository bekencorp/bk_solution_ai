/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Backend for the page_9 music demo.
 *
 * Per the BK7259SW-1550 UI/Demo split: this file is the SOLE entry
 * point that the UI layer (ap/beken_generated/page_music/) may call.
 * The whole U-disk playback stack therefore lives here, in three
 * well-separated sections inside the same TU:
 *
 *   Part 1 -- Audio pipeline
 *     A dedicated stereo audio_pipeline (vfs_stream -> mp3/aac/wav_decoder
 *     -> onboard_speaker_stream chl_num=2), replacing bk_player_service
 *     for music files. bk_player_service forces chl_num=1 and hangs on
 *     stereo MP3 with "set dest_data_width 32 fail". Pattern mirrors
 *     SDK example adk_mp3_decoder_test_case_1(); the listener consumes
 *     REPORT_MUSIC_INFO to call onboard_speaker_stream_set_param() with
 *     the real format, and computes a codec-specific total duration
 *     (MP3 sniff, WAV header, AAC ADTS walker + runtime SBR-tolerant
 *     refinement).
 *
 *   Part 2 -- Catalog (FatFS scan of "1:/music")
 *     Up to 16 tracks (*.mp3 / *.aac / *.wav). The PC populates the
 *     folder over USB MSC from page_3; before scanning we unmount-and-
 *     remount drive 1 via camera_preview_sdnand_remount() so newly
 *     copied files become visible without a reboot.
 *
 *   Part 3 -- Public API (music_*)
 *     Lifecycle (init / start / stop), control verbs (play / pause /
 *     next / prev), UI queries (track list / elapsed / total / 16-bin
 *     spectrum synthesized from speaker status_cb envelope).
 *
 * DAC ownership: before the music pipeline opens its own speaker,
 * audio_engine_play_stop() + _deinit() release the standalone bk_player
 * held by the audio_engine play layer. Stop/exit leaves it released so
 * returning to the menu does not keep a speaker stream alive.
 *
 * Required defconfig (kept unconditional in this file -- this demo cannot
 * work without them, so guarding adds noise without adding flexibility):
 *   CONFIG_FATFS, CONFIG_BK_AUDIO_ENGINE, CONFIG_ADK_VFS_STREAM,
 *   CONFIG_ADK_MP3_DECODER, CONFIG_ADK_WAV_DECODER,
 *   CONFIG_ADK_AAC_DECODER, CONFIG_ADK_ONBOARD_SPEAKER_STREAM_V2.
 */

#include "demo/music.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>             /* strcasecmp */
#include <sys/types.h>           /* ssize_t */
#include <time.h>                /* time_t, struct tm */

#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>

#include "ff.h"
#include "audio_engine.h"
#include "board_usb_switch.h"

/* bk_partition.h is self-contained. bk_vfs.h / bk_posix.h would drag
 * in their own `typedef struct __dir DIR`, which conflicts with FatFS
 * DIR used by the catalog section below in the same TU. So we include
 * only bk_partition.h here and forward-declare the four bk_vfs entry
 * points we actually call. MUSIC_O_RDONLY = 0 matches the POSIX /
 * newlib / bk_posix convention. */
#include "bk_partition.h"
extern int     bk_vfs_mount(const char *source, const char *target,
                            const char *fs_type, unsigned long mount_flags,
                            const void *data);
extern int     bk_vfs_open(const char *path, int oflag);
extern int     bk_vfs_close(int fd);
extern ssize_t bk_vfs_read(int fd, void *buf, size_t count);
#define MUSIC_O_RDONLY 0

#include <components/bk_audio/audio_pipeline/audio_element.h>
#include <components/bk_audio/audio_pipeline/audio_pipeline.h>
#include <components/bk_audio/audio_pipeline/audio_event_iface.h>
#include <components/bk_audio/audio_pipeline/audio_types.h>
#include <components/bk_audio/audio_streams/vfs_stream.h>
#include <components/bk_audio/audio_streams/onboard_speaker_stream_v2.h>
#include <components/bk_audio/audio_decoders/mp3_decoder.h>
#include <components/bk_audio/audio_decoders/wav_decoder.h>
#include <components/bk_audio/audio_decoders/aac_decoder.h>

#define TAG "music"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* FatFS drive 1 = SD-NAND. */
#define MUSIC_FAT_DIR             "1:/music"

/* bk_vfs mount + dir used by vfs_stream playback. */
#define MUSIC_VFS_MOUNT           "/sd0"
#define MUSIC_VFS_DIR             MUSIC_VFS_MOUNT "/music"
#define MUSIC_VFS_PATH_LEN        96

#define MUSIC_MAX_TRACKS          16
#define MUSIC_NAME_LEN            48

#define MUSIC_SPECTRUM_BINS_MAX   16

/* --------------------------------------------------------------------
 * Newlib mktime() workaround
 * --------------------------------------------------------------------
 *
 * The SDK's bk_vfs FatFS adapter calls mktime() from stat():
 *
 *   vfs_stream task -> stat() -> bk_vfs_stat() -> _bk_fatfs_stat()
 *   -> fatfs_time_to_time_t() -> mktime()
 *
 * On this FreeRTOS/newlib configuration, the libc mktime() path lazily
 * initializes timezone state through _tzset_unlocked_r and can trip
 * os_free_debug:155 (double free / poisoned block) when first reached
 * from an audio element task or from the Tmr Svc navigation callback.
 *
 * The music pipeline only needs stat().st_size; st_mtime is irrelevant.
 * Provide a tiny UTC-only mktime() in the application so the adapter can
 * fill st_mtime without entering newlib's timezone heap path. This
 * symbol intentionally has external linkage to satisfy fatfs_adapter.c.
 */
static bool is_leap_year(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

time_t mktime(struct tm *tm)
{
    static const uint16_t days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };

    if (!tm) {
        return (time_t)0;
    }

    int year = tm->tm_year + 1900;
    if (year < 1970 || tm->tm_mon < 0 || tm->tm_mon > 11 ||
        tm->tm_mday < 1 || tm->tm_mday > 31 ||
        tm->tm_hour < 0 || tm->tm_hour > 23 ||
        tm->tm_min < 0 || tm->tm_min > 59 ||
        tm->tm_sec < 0 || tm->tm_sec > 60) {
        return (time_t)0;
    }

    uint32_t days = 0;
    for (int y = 1970; y < year; y++) {
        days += is_leap_year(y) ? 366u : 365u;
    }
    days += days_before_month[tm->tm_mon];
    if (tm->tm_mon > 1 && is_leap_year(year)) {
        days++;
    }
    days += (uint32_t)(tm->tm_mday - 1);

    return (time_t)((uint64_t)days * 86400ULL +
                    (uint32_t)tm->tm_hour * 3600u +
                    (uint32_t)tm->tm_min * 60u +
                    (uint32_t)tm->tm_sec);
}

/* ====================================================================
 *  PART 1 -- Audio pipeline (stereo vfs->dec->speaker, status meter,
 *            codec-specific duration estimation)
 * ==================================================================== */

#define PIPE_LISTENER_STACK   2048
#define PIPE_LISTENER_PRIO    5

/* Defaults match the SDK adk_mp3 test case; runtime set_param updates
 * the speaker once REPORT_MUSIC_INFO arrives with real format. */
#define PIPE_INIT_RATE        44100
#define PIPE_INIT_BITS        16
#define PIPE_INIT_CH          2

static audio_pipeline_handle_t    s_pipeline;
static audio_element_handle_t     s_vfs;
static audio_element_handle_t     s_decoder;    /* mp3 / wav / aac */
static audio_element_handle_t     s_spk;
static audio_event_iface_handle_t s_evt;
static audio_dec_type_t           s_dec_type;

static beken_thread_t             s_listener;
static volatile bool              s_listener_running;
static volatile bool              s_playing;

/* UI meter state:
 *   s_energy_sm: EWMA-smoothed 0..100, returned to UI as the meter.
 *   s_play_start_ms: rtos_get_time() at audio_pipeline_run() success.
 *   s_total_ms: estimated stream length (codec-specific math; see below).
 */
static volatile uint8_t  s_energy;
static volatile uint8_t  s_energy_sm;
static volatile uint32_t s_play_start_ms;
static volatile uint32_t s_total_ms;

/* MP3 bitrate sniffed from the first Layer-3 frame header. */
static uint32_t          s_sniffed_kbps;

/* AAC ADTS pre-sniff (sample_rate + average frame size). */
static uint32_t          s_aac_sr;
static uint32_t          s_aac_avg_frame_b;

/* PCM bytes/sec at the DAC, cached from the first REPORT_MUSIC_INFO.
 * Used by the runtime AAC duration refinement loop. */
static uint32_t          s_pcm_byte_rate;
static bool              s_aac_runtime_locked;

/* WAV header pre-parse. The speaker must be initialized at the WAV's
 * real sample-rate/channels/bits BEFORE pipeline_run, because the
 * SDK runtime onboard_speaker_stream_set_param -> audio_dac_reconfig
 * is broken when A2DP transitions between passthrough (44.1k/48k) and
 * resample (everything else): bk_aud_rsp_init_multi_instance is called
 * with rsp_cfg.src_ch=0 (never set during init for passthrough rates),
 * the resampler init fails, the speaker hangs with
 * "[speaker] semaphore get timeout", and a later NEXT/STOP triggers
 * bk_heap_overflow_check::assert via a Tmr Svc count_util race. */
struct music_wav_hdr {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits;
    bool     valid;
};
static struct music_wav_hdr s_wav_hdr;

/* MPEG-1 Layer-3 bitrate table (kbps). Index = (header_byte_2 >> 4) & 0xf. */
static const uint16_t s_mp3_bitrate_kbps_v1l3[16] = {
    0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0
};
/* MPEG-2 / 2.5 Layer-3 bitrate table (kbps). */
static const uint16_t s_mp3_bitrate_kbps_v2l3[16] = {
    0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0
};

/* AAC sample-rate table (ADTS sampling_frequency_index). */
static const uint32_t s_aac_sr_table[16] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025,  8000,  7350,     0,     0,     0,
};

/* --- Codec sniff helpers ------------------------------------------- */

static bool sniff_wav_header(const char *path, struct music_wav_hdr *out)
{
    if (!path || !out) {
        return false;
    }
    out->valid = false;

    int fd = bk_vfs_open(path, MUSIC_O_RDONLY);
    if (fd < 0) {
        LOGW("wav sniff: open(%s) fail fd=%d\n", path, fd);
        return false;
    }
    uint8_t hdr[44];
    int rd = (int)bk_vfs_read(fd, (char *)hdr, (int)sizeof(hdr));
    bk_vfs_close(fd);
    if (rd != (int)sizeof(hdr)) {
        LOGW("wav sniff: short read %d (need %d)\n", rd, (int)sizeof(hdr));
        return false;
    }
    if (hdr[0] != 'R' || hdr[1] != 'I' || hdr[2] != 'F' || hdr[3] != 'F' ||
        hdr[8] != 'W' || hdr[9] != 'A' || hdr[10] != 'V' || hdr[11] != 'E') {
        LOGW("wav sniff: not RIFF/WAVE\n");
        return false;
    }
    out->channels    = (uint16_t)(hdr[22] | ((uint16_t)hdr[23] << 8));
    out->sample_rate = (uint32_t)(hdr[24]) |
                       ((uint32_t)hdr[25] <<  8) |
                       ((uint32_t)hdr[26] << 16) |
                       ((uint32_t)hdr[27] << 24);
    out->bits        = (uint16_t)(hdr[34] | ((uint16_t)hdr[35] << 8));
    if (out->sample_rate == 0 || out->channels == 0 || out->bits == 0) {
        LOGW("wav sniff: bad fields rate=%u ch=%u bits=%u\n",
             out->sample_rate, out->channels, out->bits);
        return false;
    }
    out->valid = true;
    LOGI("wav sniff: rate=%u ch=%u bits=%u\n",
         out->sample_rate, out->channels, out->bits);
    return true;
}

/* MP3 bitrate sniffer: scan first 4 KiB for a valid Layer-3 sync. */
static uint32_t sniff_mp3_bitrate(const char *path)
{
    #define MP3_SNIFF_BYTES 4096
    if (!path) {
        return 0;
    }
    int fd = bk_vfs_open(path, MUSIC_O_RDONLY);
    if (fd < 0) {
        LOGW("mp3 sniff: open(%s) fail fd=%d\n", path, fd);
        return 0;
    }
    uint8_t *buf = (uint8_t *)os_malloc(MP3_SNIFF_BYTES);
    if (!buf) {
        bk_vfs_close(fd);
        return 0;
    }
    int rd = (int)bk_vfs_read(fd, (char *)buf, MP3_SNIFF_BYTES);
    bk_vfs_close(fd);
    if (rd < 10) {
        os_free(buf);
        return 0;
    }

    int start = 0;
    /* Skip ID3v2 tag if present (synch-safe 28-bit length at offset 6). */
    if (buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
        uint32_t tag_len = ((uint32_t)(buf[6] & 0x7f) << 21) |
                           ((uint32_t)(buf[7] & 0x7f) << 14) |
                           ((uint32_t)(buf[8] & 0x7f) <<  7) |
                           ((uint32_t)(buf[9] & 0x7f));
        start = 10 + (int)tag_len;
    }

    uint32_t kbps = 0;
    for (int i = start; i + 3 < rd; i++) {
        if (buf[i] != 0xff || (buf[i + 1] & 0xe0) != 0xe0) {
            continue;
        }
        uint8_t ver_id  = (buf[i + 1] >> 3) & 0x03;  /* 3=v1 2=v2 0=v2.5 */
        uint8_t layer   = (buf[i + 1] >> 1) & 0x03;  /* 1=Layer-3 */
        uint8_t br_idx  = (buf[i + 2] >> 4) & 0x0f;
        uint8_t sr_idx  = (buf[i + 2] >> 2) & 0x03;
        if (layer != 1 || (ver_id != 3 && ver_id != 2 && ver_id != 0)) {
            continue;
        }
        if (br_idx == 0 || br_idx == 15 || sr_idx == 3) {
            continue;
        }
        const uint16_t *tbl = (ver_id == 3) ? s_mp3_bitrate_kbps_v1l3
                                            : s_mp3_bitrate_kbps_v2l3;
        kbps = tbl[br_idx];
        if (kbps > 0) {
            LOGI("mp3 sniff: %u kbps (offset=%d ver=%u)\n", kbps, i, ver_id);
            break;
        }
    }
    os_free(buf);
    return kbps;
}

/* ADTS sniffer: walk consecutive frames to extract sample-rate +
 * average frame size. Coupled with vfs total_bytes, the listener
 * turns this into ms via   ms = file_size * samples_per_frame * 1000
 *                               / (avg_frame * sample_rate)
 * with samples_per_frame = 1024 for AAC LC, 2048 for HE-AAC. */
#define AAC_SNIFF_BYTES        8192
#define AAC_SNIFF_MAX_FRAMES   64
#define AAC_SNIFF_MIN_FRAME    64
#define AAC_SNIFF_MIN_LOCK     2
#define AAC_SNIFF_PROBE_WIN    1024

static inline bool adts_decode(const uint8_t *buf, int off,
                               uint8_t *sr_idx, uint8_t *ch_cfg,
                               uint32_t *frame_len)
{
    uint8_t b0 = buf[off];
    uint8_t b1 = buf[off + 1];
    if (b0 != 0xFF || (b1 & 0xF6) != 0xF0) {
        return false;
    }
    *sr_idx = (uint8_t)((buf[off + 2] >> 2) & 0x0F);
    *ch_cfg = (uint8_t)(((buf[off + 2] & 0x01) << 2) |
                        ((buf[off + 3] >> 6) & 0x03));
    *frame_len = ((uint32_t)(buf[off + 3] & 0x03) << 11) |
                 ((uint32_t)buf[off + 4] << 3) |
                 ((uint32_t)(buf[off + 5] >> 5) & 0x07);
    return true;
}

static bool sniff_aac_adts(const char *path, uint32_t *out_sr,
                           uint32_t *out_avg_frame_bytes)
{
    if (!path || !out_sr || !out_avg_frame_bytes) {
        return false;
    }
    *out_sr = 0;
    *out_avg_frame_bytes = 0;

    int fd = bk_vfs_open(path, MUSIC_O_RDONLY);
    if (fd < 0) {
        LOGW("aac sniff: open(%s) fail fd=%d\n", path, fd);
        return false;
    }
    uint8_t *buf = (uint8_t *)os_malloc(AAC_SNIFF_BYTES);
    if (!buf) {
        bk_vfs_close(fd);
        return false;
    }
    int rd = (int)bk_vfs_read(fd, (char *)buf, AAC_SNIFF_BYTES);
    bk_vfs_close(fd);
    if (rd < 7) {
        os_free(buf);
        return false;
    }

    int skip = 0;
    if (rd >= 10 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
        uint32_t tag_len = ((uint32_t)(buf[6] & 0x7f) << 21) |
                           ((uint32_t)(buf[7] & 0x7f) << 14) |
                           ((uint32_t)(buf[8] & 0x7f) <<  7) |
                           ((uint32_t)(buf[9] & 0x7f));
        skip = 10 + (int)tag_len;
        if (skip < 0 || skip >= rd) {
            LOGW("aac sniff: ID3v2 size %d past sniff window\n", skip);
            os_free(buf);
            return false;
        }
    }

    int probe_end = skip + AAC_SNIFF_PROBE_WIN;
    if (probe_end > rd - 7) {
        probe_end = rd - 7;
    }

    bool found = false;
    for (int start = skip; start <= probe_end; start++) {
        uint8_t  sr0, ch0;
        uint32_t fl0;
        if (!adts_decode(buf, start, &sr0, &ch0, &fl0)) {
            continue;
        }
        uint32_t sr = s_aac_sr_table[sr0];
        if (sr == 0 || fl0 < AAC_SNIFF_MIN_FRAME) {
            continue;
        }

        int      off = start;
        uint64_t sum = 0;
        uint32_t cnt = 0;
        while (cnt < AAC_SNIFF_MAX_FRAMES && off + 7 <= rd) {
            uint8_t  sr_i, ch_i;
            uint32_t fl_i;
            if (!adts_decode(buf, off, &sr_i, &ch_i, &fl_i)) {
                break;
            }
            if (sr_i != sr0 || ch_i != ch0 || fl_i < AAC_SNIFF_MIN_FRAME) {
                break;
            }
            sum += fl_i;
            cnt++;
            off += (int)fl_i;
        }
        if (cnt >= AAC_SNIFF_MIN_LOCK) {
            uint32_t avg = (uint32_t)(sum / cnt);
            if (avg == 0) {
                continue;
            }
            *out_sr = sr;
            *out_avg_frame_bytes = avg;
            LOGI("aac sniff: sr=%u ch_cfg=%u frames=%u avg_frame=%u "
                 "(start=%d skip=%d)\n",
                 sr, ch0, cnt, avg, start, skip);
            found = true;
            break;
        }
    }
    os_free(buf);
    if (!found) {
        LOGW("aac sniff: no consistent ADTS run in first %d bytes\n", rd);
    }
    return found;
}

static audio_dec_type_t dec_type_from_path(const char *path)
{
    if (!path) {
        return AUDIO_DEC_TYPE_INVALID;
    }
    size_t n = strlen(path);
    if (n < 4) {
        return AUDIO_DEC_TYPE_INVALID;
    }
    const char *ext = path + n - 4;
    if (strcasecmp(ext, ".mp3") == 0) return AUDIO_DEC_TYPE_MP3;
    if (strcasecmp(ext, ".wav") == 0) return AUDIO_DEC_TYPE_WAV;
    if (strcasecmp(ext, ".aac") == 0) return AUDIO_DEC_TYPE_AAC;
    return AUDIO_DEC_TYPE_INVALID;
}

/* --- Speaker status callback -> UI envelope ----------------------- */

static void spk_status_cb(audio_element_handle_t self,
                          const onboard_speaker_stream_status_t *status,
                          void *user_data)
{
    (void)self; (void)user_data;
    if (!status) {
        return;
    }
    s_energy = status->energy_level;
    /* Cheap fixed-point EWMA, alpha = 1/4 (~80 ms time constant). */
    uint32_t sm  = s_energy_sm;
    uint32_t raw = status->energy_level;
    sm = (raw + sm * 3u) / 4u;
    if (sm > 100u) {
        sm = 100u;
    }
    s_energy_sm = (uint8_t)sm;
}

/* --- Listener thread: REPORT_MUSIC_INFO + REPORT_STATUS + AAC dur --- */

static void pipeline_listener(beken_thread_arg_t arg)
{
    (void)arg;
    audio_event_iface_msg_t msg;

    while (s_listener_running) {
        /* Runtime AAC duration refinement.
         *
         * The pre-build ADTS sniff cannot reliably distinguish AAC LC
         * (1024 samples/frame) from HE-AAC (2048 samples/frame); the
         * first few frames are rarely representative of the song bitrate
         * (quiet intros vs loud verses). Once playback has been running
         * for >= 3 s of speaker output we can compute the exact total
         * duration directly from runtime counters, sidestepping SBR
         * detection entirely:
         *
         *   audio_sec_played = speaker.byte_pos / pcm_byte_rate
         *   total_duration   = file_size * audio_sec_played / decoder.byte_pos
         *
         * Both byte_pos counters move forward as actual audio plays so
         * the ratio is invariant to startup buffering and SBR. Lock the
         * result on the first qualifying sample to avoid jitter. */
        if (s_dec_type == AUDIO_DEC_TYPE_AAC && !s_aac_runtime_locked &&
            s_pcm_byte_rate > 0 && s_decoder && s_spk && s_vfs) {
            audio_element_info_t dinfo = {0};
            audio_element_info_t sinfo = {0};
            audio_element_info_t vinfo = {0};
            (void)audio_element_getinfo(s_decoder, &dinfo);
            (void)audio_element_getinfo(s_spk,     &sinfo);
            (void)audio_element_getinfo(s_vfs,     &vinfo);
            if (sinfo.byte_pos >= (int64_t)(3u * s_pcm_byte_rate) &&
                dinfo.byte_pos > 0 && vinfo.total_bytes > 0) {
                uint64_t audio_ms = (uint64_t)sinfo.byte_pos * 1000ULL /
                                    (uint64_t)s_pcm_byte_rate;
                uint64_t total_ms = (uint64_t)vinfo.total_bytes * audio_ms /
                                    (uint64_t)dinfo.byte_pos;
                if (total_ms > 0xFFFFFFFFu) {
                    total_ms = 0xFFFFFFFFu;
                }
                s_total_ms = (uint32_t)total_ms;
                s_aac_runtime_locked = true;
                LOGI("aac runtime: refined duration %u ms "
                     "(spk=%lld dec=%lld file=%lld)\n",
                     s_total_ms,
                     (long long)sinfo.byte_pos,
                     (long long)dinfo.byte_pos,
                     (long long)vinfo.total_bytes);
            }
        }

        if (audio_event_iface_listen(s_evt, &msg, BK_MS_TO_TICKS(200)) != BK_OK) {
            continue;
        }

        if (msg.source == (void *)s_decoder &&
            msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
            audio_element_info_t info = {0};
            audio_element_getinfo(s_decoder, &info);
            LOGI("music info: rate=%d bits=%d ch=%d (type=%d)\n",
                 info.sample_rates, info.bits, info.channels, (int)s_dec_type);
            (void)onboard_speaker_stream_set_param(s_spk,
                info.sample_rates, info.bits, info.channels,
                AUD_DAC_SOURCE_A2DP);

            if (info.sample_rates > 0 && info.channels > 0 && info.bits > 0) {
                s_pcm_byte_rate = (uint32_t)info.sample_rates *
                                  (uint32_t)info.channels *
                                  ((uint32_t)info.bits / 8u);
            }

            /* Initial duration estimate. Math is type-specific:
             *   MP3: file_size * 8 / kbps  (kbps sniffed pre-build).
             *   WAV: file_size * 1000 / (sr * ch * bits/8).
             *   AAC: pre-sniff + HE-AAC detection (see below); a runtime
             *        refinement loop later locks the exact value. */
            if (s_total_ms == 0 && s_vfs) {
                audio_element_info_t vinfo = {0};
                audio_element_getinfo(s_vfs, &vinfo);
                if (vinfo.total_bytes > 0) {
                    uint64_t ms = 0;
                    if (s_dec_type == AUDIO_DEC_TYPE_MP3) {
                        uint32_t kbps = s_sniffed_kbps ? s_sniffed_kbps : 128u;
                        ms = (uint64_t)vinfo.total_bytes * 8u / kbps;
                    } else if (s_dec_type == AUDIO_DEC_TYPE_WAV &&
                               info.sample_rates > 0 &&
                               info.bits > 0 && info.channels > 0) {
                        uint32_t bytes_per_sec =
                            (uint32_t)info.sample_rates *
                            (uint32_t)info.channels *
                            ((uint32_t)info.bits / 8u);
                        if (bytes_per_sec > 0) {
                            ms = (uint64_t)vinfo.total_bytes * 1000u /
                                 bytes_per_sec;
                        }
                    } else if (s_dec_type == AUDIO_DEC_TYPE_AAC) {
                        /* Prefer decoder SR (post-SBR), fall back to sniff. */
                        uint32_t sr = (info.sample_rates > 0)
                                          ? (uint32_t)info.sample_rates
                                          : s_aac_sr;
                        if (sr > 0 && s_aac_avg_frame_b > 0) {
                            /* HE-AAC detection by reductio: compute the
                             * bitrate this avg_frame would imply if the
                             * stream were AAC LC. LC tops at ~320 kbps in
                             * practice, so anything implying >256 kbps is
                             * almost certainly HE-AAC where each frame
                             * covers 2048 samples instead of 1024. */
                            uint32_t samples_per_frame = 1024;
                            uint64_t implied_lc_bps =
                                (uint64_t)s_aac_avg_frame_b *
                                8ULL * (uint64_t)sr / 1024ULL;
                            if (implied_lc_bps > 256000ULL) {
                                samples_per_frame = 2048;
                                LOGI("aac: implied LC bps=%llu > 256k "
                                     "-> assume SBR (2048 sa/fr)\n",
                                     (unsigned long long)implied_lc_bps);
                            }
                            ms = (uint64_t)vinfo.total_bytes *
                                 (uint64_t)samples_per_frame * 1000ULL /
                                 ((uint64_t)s_aac_avg_frame_b * (uint64_t)sr);
                        }
                    }
                    if (ms > 0xFFFFFFFFu) {
                        ms = 0xFFFFFFFFu;
                    }
                    s_total_ms = (uint32_t)ms;
                    LOGI("est duration: %u ms (size=%lld bytes)\n",
                         s_total_ms, (long long)vinfo.total_bytes);
                }
            }
            continue;
        }

        if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
            int st = (int)(uintptr_t)msg.data;
            if (msg.source == (void *)s_spk &&
                (st == AEL_STATUS_STATE_FINISHED ||
                 st == AEL_STATUS_STATE_STOPPED)) {
                LOGI("playback finished\n");
                s_playing = false;
                continue;
            }
            /* SDK workaround: when vfs_stream / mp3_dec hits ERROR_*,
             * audio_element_on_cmd_error() does NOT call
             * audio_element_set_port_done() on the element's output rb,
             * so the downstream blocks forever in audio_element_input()
             * (logs spam with "read mp3 data timeout, retry, r_size:-4").
             * Manually flag the upstream's output ringbuffer as done so
             * the downstream can drain and finish cleanly. */
            if (st == AEL_STATUS_ERROR_INPUT ||
                st == AEL_STATUS_ERROR_OUTPUT ||
                st == AEL_STATUS_ERROR_PROCESS ||
                st == AEL_STATUS_ERROR_OPEN) {
                const char *who = (msg.source == (void *)s_vfs)     ? "vfs" :
                                  (msg.source == (void *)s_decoder) ? "dec" :
                                  (msg.source == (void *)s_spk)     ? "spk" : "?";
                LOGE("element [%s] ERROR status=%d -> propagate EOF\n", who, st);
                if (msg.source == (void *)s_vfs && s_vfs) {
                    (void)audio_element_set_port_done(s_vfs);
                } else if (msg.source == (void *)s_decoder && s_decoder) {
                    (void)audio_element_set_port_done(s_decoder);
                }
                s_playing = false;
            }
        }
    }

    s_listener = NULL;
    rtos_delete_thread(NULL);
}

/* --- Pipeline build / teardown ------------------------------------ */

static int pipeline_build(const char *vfs_path)
{
    audio_pipeline_cfg_t pip_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    s_pipeline = audio_pipeline_init(&pip_cfg);
    if (!s_pipeline) {
        LOGE("pipeline_init fail\n");
        return BK_FAIL;
    }

    vfs_stream_cfg_t vfs_cfg = DEFAULT_VFS_STREAM_CONFIG();
    vfs_cfg.type           = AUDIO_STREAM_READER;
    vfs_cfg.buf_sz         = 8192;
    vfs_cfg.out_block_size = MP3_DECODER_MAIN_BUFF_SIZE;
    s_vfs = vfs_stream_init(&vfs_cfg);
    if (!s_vfs) {
        LOGE("vfs_stream_init fail\n");
        goto fail;
    }
    if (audio_element_set_uri(s_vfs, (char *)vfs_path) != BK_OK) {
        LOGE("set_uri(%s) fail\n", vfs_path);
        goto fail;
    }

    /* Plan the speaker frame_size first; the WAV decoder must produce
     * at least one full speaker frame per process call, otherwise the
     * speaker's per-DMA-tick audio_element_input(frame_size, 0) reads
     * partial bytes and falls into the FILL_SILENCE path. The 1/50 s
     * window mirrors the speaker config below. */
    uint32_t wav_pcm_rate = (s_dec_type == AUDIO_DEC_TYPE_WAV && s_wav_hdr.valid)
                                ? s_wav_hdr.sample_rate
                                : PIPE_INIT_RATE;
    uint32_t wav_pcm_ch   = (s_dec_type == AUDIO_DEC_TYPE_WAV && s_wav_hdr.valid)
                                ? (s_wav_hdr.channels ? s_wav_hdr.channels : 1)
                                : PIPE_INIT_CH;
    uint32_t wav_pcm_bits = (s_dec_type == AUDIO_DEC_TYPE_WAV && s_wav_hdr.valid)
                                ? (s_wav_hdr.bits ? s_wav_hdr.bits : 16)
                                : PIPE_INIT_BITS;
    uint32_t spk_frame_size = wav_pcm_rate * wav_pcm_ch * (wav_pcm_bits / 8u) / 50u;
    if (spk_frame_size == 0) {
        spk_frame_size = 320;
    }

    /* Decoder selection by file extension. The pipeline_link tag string
     * differs per decoder, so we pick the matching tag here too. */
    const char *dec_tag = NULL;
    switch (s_dec_type) {
    case AUDIO_DEC_TYPE_MP3: {
        mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
        s_decoder = mp3_decoder_init(&mp3_cfg);
        dec_tag = "mp3_dec";
        break;
    }
    case AUDIO_DEC_TYPE_WAV: {
        wav_decoder_cfg_t wav_cfg = DEFAULT_WAV_DECODER_CONFIG();
        /* Default WAV_DECODER_OUT_BLOCK_SIZE = 320 bytes, smaller than
         * one speaker frame for any rate >= 16 kHz mono. Re-size the
         * decoder output so it always delivers a full speaker frame
         * (plus a little headroom). */
        uint32_t wav_block = spk_frame_size * 2u;
        wav_block = (wav_block + 3u) & ~3u;
        if (wav_block < 1024u) {
            wav_block = 1024u;
        }
        wav_cfg.buf_sz         = (int)wav_block;
        wav_cfg.out_block_size = (int)wav_block;
        wav_cfg.out_block_num  = 1;
        LOGI("wav dec init: buf_sz=%u out_block=%u (spk_frame=%u)\n",
             wav_block, wav_block, spk_frame_size);
        s_decoder = wav_decoder_init(&wav_cfg);
        dec_tag = "wav_dec";
        break;
    }
    case AUDIO_DEC_TYPE_AAC: {
        aac_decoder_cfg_t aac_cfg = DEFAULT_AAC_DECODER_CONFIG();
        s_decoder = aac_decoder_init(&aac_cfg);
        dec_tag = "aac_dec";
        break;
    }
    default:
        LOGE("unsupported dec_type=%d\n", (int)s_dec_type);
        goto fail;
    }
    if (!s_decoder) {
        LOGE("decoder init fail (tag=%s)\n", dec_tag ? dec_tag : "?");
        goto fail;
    }

    onboard_speaker_stream_cfg_t spk_cfg = DEFAULT_ONBOARD_SPEAKER_STREAM_CONFIG();
    /* Only A2DP supports 44.1k passthrough; CALL/HINT sources are capped
     * to 8k/16k/48k by aud_dac_driver. Narrow the bitmap to A2DP only. */
    spk_cfg.dac_source_bitmap = ONBOARD_SPEAKER_STREAM_DAC_SOURCE_A2DP_BIT;
    spk_cfg.main_dac_source   = AUD_DAC_SOURCE_A2DP;
    spk_cfg.sample_rate[AUD_DAC_SOURCE_A2DP] = PIPE_INIT_RATE;
    spk_cfg.frame_size[AUD_DAC_SOURCE_A2DP]  = 320;
    spk_cfg.chl_num = PIPE_INIT_CH;
    spk_cfg.bits    = PIPE_INIT_BITS;
    if (s_dec_type == AUDIO_DEC_TYPE_WAV && s_wav_hdr.valid) {
        spk_cfg.sample_rate[AUD_DAC_SOURCE_A2DP] = s_wav_hdr.sample_rate;
        spk_cfg.chl_num = (uint8_t)(s_wav_hdr.channels ? s_wav_hdr.channels : 1);
        if (spk_cfg.chl_num > 2) {
            spk_cfg.chl_num = 2;
        }
        spk_cfg.bits = (uint8_t)(s_wav_hdr.bits ? s_wav_hdr.bits : 16);
        spk_cfg.frame_size[AUD_DAC_SOURCE_A2DP] = spk_frame_size;
        LOGI("wav spk init: rate=%u ch=%u bits=%u frame=%u\n",
             spk_cfg.sample_rate[AUD_DAC_SOURCE_A2DP],
             spk_cfg.chl_num, spk_cfg.bits, spk_frame_size);
    }
    /* Inherit the user-selected Page 10 volume so each music start does
     * NOT reset the DAC dig_gain to the SDK default (-7 dB). */
    spk_cfg.dig_gain = audio_engine_volume_get_gain_db();
#if CONFIG_AE_ENABLE_PA_CNTRL
    spk_cfg.pa_ctrl_en   = true;
    spk_cfg.pa_ctrl_gpio = CONFIG_AE_PA_CNTRL_GPIO;
    spk_cfg.pa_on_level  = CONFIG_AE_PA_ON_LEVEL;
    spk_cfg.pa_on_delay  = CONFIG_AE_PA_ON_DELAY;
    spk_cfg.pa_off_delay = CONFIG_AE_PA_OFF_DELAY;
#endif
    /* Lower play_energy_threshold so quiet passages still register as
     * "playing" for the meter. */
    spk_cfg.play_energy_threshold  = 1;
    spk_cfg.play_energy_hysteresis = 0;
    spk_cfg.status_cb = spk_status_cb;
    s_spk = onboard_speaker_stream_init(&spk_cfg);
    if (!s_spk) {
        LOGE("onboard_speaker_stream_init fail\n");
        goto fail;
    }

    if (audio_pipeline_register(s_pipeline, s_vfs,     "vfs_stream") != BK_OK ||
        audio_pipeline_register(s_pipeline, s_decoder, dec_tag)      != BK_OK ||
        audio_pipeline_register(s_pipeline, s_spk,     "speaker")    != BK_OK) {
        LOGE("pipeline_register fail\n");
        goto fail;
    }
    {
        const char *link_tags[3] = {"vfs_stream", dec_tag, "speaker"};
        if (audio_pipeline_link(s_pipeline, link_tags, 3) != BK_OK) {
            LOGE("pipeline_link fail\n");
            goto fail;
        }
    }

    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    s_evt = audio_event_iface_init(&evt_cfg);
    if (!s_evt) {
        LOGE("event_iface_init fail\n");
        goto fail;
    }
    if (audio_pipeline_set_listener(s_pipeline, s_evt) != BK_OK) {
        LOGE("set_listener fail\n");
        goto fail;
    }
    return BK_OK;

fail:
    return BK_FAIL;
}

static void pipeline_teardown(void)
{
    if (s_pipeline) {
        (void)audio_pipeline_stop(s_pipeline);
        (void)audio_pipeline_wait_for_stop(s_pipeline);
        (void)audio_pipeline_terminate(s_pipeline);
    }

    s_listener_running = false;
    while (s_listener != NULL) {
        rtos_delay_milliseconds(20);
    }

    if (s_pipeline) {
        if (s_vfs)     (void)audio_pipeline_unregister(s_pipeline, s_vfs);
        if (s_decoder) (void)audio_pipeline_unregister(s_pipeline, s_decoder);
        if (s_spk)     (void)audio_pipeline_unregister(s_pipeline, s_spk);
        (void)audio_pipeline_remove_listener(s_pipeline);
    }
    if (s_evt) {
        (void)audio_event_iface_destroy(s_evt);
        s_evt = NULL;
    }
    if (s_pipeline) {
        (void)audio_pipeline_deinit(s_pipeline);
        s_pipeline = NULL;
    }
    if (s_vfs)     { (void)audio_element_deinit(s_vfs);     s_vfs     = NULL; }
    if (s_decoder) { (void)audio_element_deinit(s_decoder); s_decoder = NULL; }
    if (s_spk)     { (void)audio_element_deinit(s_spk);     s_spk     = NULL; }

    s_playing            = false;
    s_play_start_ms      = 0;
    s_total_ms           = 0;
    s_energy             = 0;
    s_energy_sm          = 0;
    s_sniffed_kbps       = 0;
    s_aac_sr             = 0;
    s_aac_avg_frame_b    = 0;
    s_pcm_byte_rate      = 0;
    s_aac_runtime_locked = false;
    s_dec_type           = AUDIO_DEC_TYPE_INVALID;
    s_wav_hdr.valid      = false;
}

static int pipeline_play(const char *vfs_path)
{
    if (!vfs_path || vfs_path[0] == '\0') {
        return BK_FAIL;
    }
    /* Tear down internally (skip pipeline_stop) so we don't lazy-init
     * audio_engine_play just to tear it down on the next line. */
    pipeline_teardown();

    /* Release the standalone bk_player held by audio_engine_play so we
     * can open our own onboard_speaker_stream (chl_num=2). */
    (void)audio_engine_play_stop();
    (void)audio_engine_play_deinit();

    s_dec_type = dec_type_from_path(vfs_path);
    if (s_dec_type == AUDIO_DEC_TYPE_INVALID) {
        LOGE("unknown audio extension: %s\n", vfs_path);
        return BK_FAIL;
    }

    /* Codec-specific pre-build sniffing. Must run BEFORE vfs_stream
     * opens the file (FatFS may refuse a second open on the same path
     * in some configs). Sniff failure is non-fatal for playback; only
     * the UI progress bar degrades. */
    s_sniffed_kbps    = (s_dec_type == AUDIO_DEC_TYPE_MP3)
                            ? sniff_mp3_bitrate(vfs_path) : 0u;
    s_aac_sr          = 0;
    s_aac_avg_frame_b = 0;
    if (s_dec_type == AUDIO_DEC_TYPE_AAC) {
        (void)sniff_aac_adts(vfs_path, &s_aac_sr, &s_aac_avg_frame_b);
    }
    s_total_ms      = 0;
    s_play_start_ms = 0;
    s_energy        = 0;
    s_energy_sm     = 0;
    s_wav_hdr.valid = false;
    if (s_dec_type == AUDIO_DEC_TYPE_WAV) {
        (void)sniff_wav_header(vfs_path, &s_wav_hdr);
    }

    if (pipeline_build(vfs_path) != BK_OK) {
        LOGE("build fail (%s)\n", vfs_path);
        pipeline_teardown();
        return BK_FAIL;
    }

    s_listener_running = true;
    if (rtos_create_thread(&s_listener, PIPE_LISTENER_PRIO,
                           "music_evt",
                           (beken_thread_function_t)pipeline_listener,
                           PIPE_LISTENER_STACK, NULL) != kNoErr) {
        LOGE("create listener fail\n");
        s_listener_running = false;
        pipeline_teardown();
        return BK_FAIL;
    }

    s_play_start_ms = (uint32_t)rtos_get_time();
    if (audio_pipeline_run(s_pipeline) != BK_OK) {
        LOGE("pipeline_run fail\n");
        s_listener_running = false;
        s_play_start_ms = 0;
        pipeline_teardown();
        return BK_FAIL;
    }
    s_playing = true;
    LOGI("playing %s\n", vfs_path);
    return BK_OK;
}

static int pipeline_stop(void)
{
    if (!s_pipeline && !s_listener) {
        return BK_OK;
    }
    pipeline_teardown();
    LOGI("stopped\n");
    return BK_OK;
}

static bool     pipeline_is_playing(void)        { return s_playing; }
static uint8_t  pipeline_get_energy(void)        { return s_playing ? s_energy_sm : 0u; }
static uint32_t pipeline_get_total_ms(void)      { return s_total_ms; }
static uint32_t pipeline_get_elapsed_ms(void)
{
    uint32_t start = s_play_start_ms;
    if (!start) {
        return 0u;
    }
    uint32_t now     = (uint32_t)rtos_get_time();
    uint32_t elapsed = now - start;  /* unsigned wrap is fine */
    uint32_t total   = s_total_ms;
    if (total != 0u && elapsed > total) {
        elapsed = total;
    }
    return elapsed;
}

/* ====================================================================
 *  PART 2 -- Catalog (FatFS scan of "1:/music")
 * ==================================================================== */

/* Track name table (~768B); allocate from PSRAM so it does not eat AP .bss. */
static char (*s_track)[MUSIC_NAME_LEN] = NULL;
static int   s_track_count;
static int   s_current_idx;

static int catalog_ensure_track_buf(void)
{
    if (s_track != NULL) {
        return 0;
    }
    s_track = (char (*)[MUSIC_NAME_LEN])psram_malloc(
        (size_t)MUSIC_MAX_TRACKS * MUSIC_NAME_LEN);
    if (s_track == NULL) {
        LOGE("track catalog psram_malloc fail\n");
        return -1;
    }
    os_memset(s_track, 0, (size_t)MUSIC_MAX_TRACKS * MUSIC_NAME_LEN);
    return 0;
}

static bool s_vfs_mounted;

static int vfs_mount_sd0(void)
{
    if (s_vfs_mounted) {
        return 0;
    }
    struct bk_fatfs_partition partition;
    partition.part_type            = FATFS_DEVICE;
    partition.part_dev.device_name = FATFS_DEV_SDCARD;
    partition.mount_path           = MUSIC_VFS_MOUNT;

    int ret = bk_vfs_mount("SOURCE_NONE", partition.mount_path,
                           "fatfs", 0, &partition);
    if (ret != 0) {
        LOGE("vfs mount %s fail ret=%d\n", MUSIC_VFS_MOUNT, ret);
        return ret;
    }
    s_vfs_mounted = true;
    LOGI("vfs mount %s ok\n", MUSIC_VFS_MOUNT);
    return 0;
}

static const char *const s_audio_exts[] = { ".mp3", ".aac", ".wav" };

static bool name_is_audio(const char *name)
{
    if (!name || name[0] == '\0') {
        return false;
    }
    size_t len = strlen(name);
    if (len < 4) {
        return false;
    }
    for (size_t i = 0; i < sizeof(s_audio_exts) / sizeof(s_audio_exts[0]); i++) {
        if (os_strcasecmp(name + len - 4, s_audio_exts[i]) == 0) {
            return true;
        }
    }
    return false;
}

static int prepare_storage(void)
{
    /* Flip Type-C off USB MSC so AP-side FatFS regains exclusive
     * SD-NAND access. */
    (void)board_usb_switch_prepare_nand_access();
    rtos_delay_milliseconds(50);

    /* TODO: re-mount FATFS volume 1 after USB MSC writes so files
     * newly copied from the PC become visible without a reboot.
     * Deferred until camera_preview.c is updated; the SD-NAND
     * FATFS instance is currently owned there. Until then, freshly
     * copied U-disk files require a power-cycle to appear. */
    return 0;
}

static int catalog_refresh(void)
{
    DIR *dir     = NULL;
    FILINFO *fno = NULL;

    s_track_count = 0;
    s_current_idx = 0;

    if (catalog_ensure_track_buf() != 0) {
        return 0;
    }

    if (prepare_storage() != 0) {
        return 0;
    }
    (void)f_mkdir(MUSIC_FAT_DIR);          /* idempotent */
    (void)vfs_mount_sd0();

    dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    if (!dir || !fno) {
        LOGE("scan alloc fail\n");
        goto out;
    }

    FRESULT fr = f_opendir(dir, MUSIC_FAT_DIR);
    if (fr != FR_OK) {
        LOGW("f_opendir(%s) fr=%d -- put audio files there from PC via USB MSC\n",
             MUSIC_FAT_DIR, fr);
        goto out;
    }
    while (s_track_count < MUSIC_MAX_TRACKS) {
        fr = f_readdir(dir, fno);
        if (fr != FR_OK || fno->fname[0] == '\0') {
            break;
        }
        if (fno->fname[0] == '.' || (fno->fattrib & AM_DIR)) {
            continue;
        }
        if (!name_is_audio(fno->fname)) {
            continue;
        }
        os_strncpy(s_track[s_track_count], fno->fname, MUSIC_NAME_LEN - 1);
        s_track[s_track_count][MUSIC_NAME_LEN - 1] = '\0';
        LOGI("track[%d]: %s\n", s_track_count, s_track[s_track_count]);
        s_track_count++;
    }
    f_closedir(dir);

out:
    if (dir) os_free(dir);
    if (fno) os_free(fno);
    LOGI("scan: %d audio file(s) in %s\n", s_track_count, MUSIC_FAT_DIR);
    return s_track_count;
}

static int catalog_play_index(int idx)
{
    if (idx < 0 || idx >= s_track_count) {
        return -1;
    }
    if (vfs_mount_sd0() != 0) {
        return -1;
    }

    char path[MUSIC_VFS_PATH_LEN];
    snprintf(path, sizeof(path), "%s/%s", MUSIC_VFS_DIR, s_track[idx]);

    int ret = pipeline_play(path);
    if (ret != 0) {
        LOGE("pipeline_play(%s) ret=%d\n", path, ret);
        return ret;
    }
    s_current_idx = idx;
    LOGI("playing %s\n", path);
    return 0;
}

static int catalog_play_current(void)
{
    if (s_track_count <= 0) {
        (void)catalog_refresh();
    }
    if (s_track_count <= 0) {
        return -1;
    }
    if (s_current_idx < 0 || s_current_idx >= s_track_count) {
        s_current_idx = 0;
    }
    return catalog_play_index(s_current_idx);
}

static int catalog_play_next(void)
{
    if (s_track_count <= 0) {
        (void)catalog_refresh();
    }
    if (s_track_count <= 0) return -1;
    return catalog_play_index((s_current_idx + 1) % s_track_count);
}

static int catalog_play_prev(void)
{
    if (s_track_count <= 0) {
        (void)catalog_refresh();
    }
    if (s_track_count <= 0) return -1;
    int idx = (s_current_idx - 1 + s_track_count) % s_track_count;
    return catalog_play_index(idx);
}

static void catalog_stop(void)
{
    (void)pipeline_stop();
}

/* ====================================================================
 *  PART 3 -- Public API (UI surface)
 * ==================================================================== */

int music_play(void)  { return catalog_play_current(); }
int music_pause(void) { catalog_stop(); return 0; }
int music_next(void)  { return catalog_play_next(); }
int music_prev(void)  { return catalog_play_prev(); }

bool music_is_playing(void)
{
    return pipeline_is_playing();
}

const char *music_get_track_name(void)
{
    if (s_track_count <= 0) return "No Track";
    if (s_current_idx < 0 || s_current_idx >= s_track_count) return "?";
    return s_track[s_current_idx];
}

int music_get_track_count(void)   { return s_track_count; }
int music_get_current_index(void) { return s_current_idx; }

const char *music_get_track_name_at(int idx)
{
    if (idx < 0 || idx >= s_track_count) return "";
    return s_track[idx];
}

uint32_t music_get_elapsed_sec(void) { return pipeline_get_elapsed_ms() / 1000u; }
uint32_t music_get_total_sec(void)   { return pipeline_get_total_ms()   / 1000u; }

/* --- Spectrum synthesis from the single status_cb envelope ---------
 *
 * No real-time FFT tap on the speaker stream, so synthesize a
 * plausible 16-bin meter from the scalar energy: a soft parabolic
 * falloff from the centre bins outward, drifted by a rotating phase
 * offset and dithered with low-amplitude xorshift jitter so steady
 * passages still look alive. When the pipeline reports 0 we decay the
 * last-seen pattern over ~500 ms (at the page's 80 ms lv_timer tick)
 * so the UI fades to black instead of snapping flat at EOF.
 */
static uint8_t  s_bins[MUSIC_SPECTRUM_BINS_MAX];
static uint32_t s_phase;
static uint32_t s_rng_state = 0x12345678u;

static inline uint32_t spectrum_rand(void)
{
    uint32_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x <<  5;
    s_rng_state = x;
    return x;
}

void music_get_spectrum_bins(uint8_t *out, int n)
{
    if (!out || n <= 0) {
        return;
    }
    if (n > MUSIC_SPECTRUM_BINS_MAX) {
        n = MUSIC_SPECTRUM_BINS_MAX;
    }
    uint8_t energy = pipeline_get_energy();   /* 0..100, EWMA-smoothed */

    if (energy == 0) {
        for (int i = 0; i < n; i++) {
            uint8_t v = s_bins[i];
            v = (v > 12) ? (uint8_t)(v - 12) : 0;
            s_bins[i] = v;
            out[i] = v;
        }
        return;
    }

    s_phase++;
    int centre = n / 2;
    for (int i = 0; i < n; i++) {
        int dist = (i - centre); if (dist < 0) dist = -dist;
        int drop = (dist * dist * 6) / n;             /* parabolic falloff */
        int base = (int)energy - drop;
        if (base < 0) base = 0;

        int phased = base + (int)((s_phase >> (i & 3)) & 0x07);
        int jit    = (int)(spectrum_rand() & 0x0F) - 8;
        int v      = phased + jit;

        if (v < 0)   v = 0;
        if (v > 100) v = 100;
        s_bins[i] = (uint8_t)v;
        out[i] = s_bins[i];
    }
}

/* ====================================================================
 *  PART 4 -- Lifecycle iface
 * ==================================================================== */

int music_init(void) { return 0; }

extern int page_music_enter(void);

int music_start(void)
{
    /* Tear down any active voice session so the stereo music pipeline
     * can claim the DAC + DMA channels later. Symptom when omitted: the
     * voice mic+spk streams hold all 8 DMA channels, the music
     * onboard_speaker_stream_init fails at chnl_id=8 with "malloc dma
     * fail" / "dac_dma_init fail", and the half-built speaker then
     * triggers a MemFault during teardown (the speaker semaphore times
     * out 2 s later: "[spk] semaphore get timeout 2000ms"). No-ops when
     * voice was not running, so safe to call unconditionally. The next
     * page's _start() restores its own audio session. */
    (void)audio_engine_stop();

    /* Refresh catalog at page enter so newly copied U-disk files are
     * visible without requiring an explicit rescan. */
    (void)catalog_refresh();
    return page_music_enter();
}

int music_stop(void)
{
    catalog_stop();
    return 0;
}

const bk_demo_iface_t g_demo_music = {
    .name  = "music",
    .init  = music_init,
    .start = music_start,
    .stop  = music_stop,
};
