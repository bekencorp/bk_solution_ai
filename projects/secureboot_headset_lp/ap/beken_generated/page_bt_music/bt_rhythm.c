/**
 * @file bt_rhythm.c
 * @brief Music-to-motion rhythm engine (SBC subband band-energy -> hand servos).
 *
 * Audio analysis (bt_rhythm_feed_sbc, called from the A2DP audio task):
 *   SBC encodes audio into N (4 or 8) frequency subbands. Each subband carries a
 *   4-bit scale factor that is essentially log2 of that band's peak magnitude --
 *   i.e. a ready-made, per-frequency-bin energy estimate. We parse only the SBC
 *   frame header + scale-factor field (no costly PCM decode), fold the subbands
 *   into low/mid/high groups, and EMA-smooth them. SBC subbands ARE frequency
 *   bins, so this is faithful to "drive the hand from the audio frequency points"
 *   while costing almost no CPU.
 *
 * Motion control (bt_rhythm_task, 25 ms tick):
 *   The band envelope is mapped *continuously* onto the 6 servos (id 1..5 are
 *   the fingers, id 6 is the wrist): overall loudness sets the finger-wave
 *   amplitude and speed, the treble-vs-bass balance sets how open or clenched
 *   the hand sits, and detected beats add a short decaying "pop". Each tick the
 *   pose is low-pass filtered toward its target, then pushed through the
 *   existing bk_hiwonder_hand_servo PWM ramp API. While music plays each finger
 *   also keeps a small always-on breathing wiggle so a momentarily quiet band
 *   never freezes it. When the A2DP stream is paused/stopped (no recent SBC
 *   payload), the hand parks at neutral and stays still.
 */
#include "demo/bt_rhythm.h"

#include <math.h>
#include <stddef.h>
#include <os/os.h>
#include <components/log.h>

#define TAG "bt_rhythm"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* bk_a2dp codec type for SBC (matches CODEC_AUDIO_SBC in a2dp_sink.c). */
#define BT_RHYTHM_CODEC_SBC          0x00U

/* Headset port: servo/motion part removed (no robot hand on this board).
 * This file now only keeps the SBC band-energy analyzer + UI spectrum feed.
 * The spectrum decay runs lazily inside get_spectrum() (no control thread). */
#define BT_RHYTHM_TICK_MS      25U    /* decay time base, see get_spectrum() */

/* ---- Decision thresholds (band energies are 0..100) ---- */
#define BAND_SILENCE_LEVEL    8U    /* below this overall -> treated as silence */
#define BAND_ACTIVE_LEVEL    14U    /* above this overall -> music is playing */
/* ---- Shared analysis state (written by audio task, read by control task) ---- */
static volatile uint8_t s_band_low;    /* 0..100 EMA */
static volatile uint8_t s_band_mid;
static volatile uint8_t s_band_high;
static volatile uint32_t s_last_audio_ms;

/* ---- UI spectrum (visualizer-only, independent of the motion path) ---- *
 * Treble carries far smaller SBC scale factors than bass. A single global AGC
 * makes bass/mid slam to full while the last high bins barely move; pure
 * per-bin AGC makes every bin look equally loud. This uses a hybrid:
 *   - stronger high-bin pre-emphasis,
 *   - one global reference to preserve overall shape,
 *   - a slow per-bin reference blended in so high bins still animate.
 * The output is capped below 100 to leave visual headroom. */
#define BT_RHYTHM_AGC_FLOOR   11.0f
/* "Soft" AGC: track the *song's* overall level slowly, NOT each transient.
 * A fast AGC flattens loudness (a chorus normalizes back to the same height);
 * by reacting slowly the reference behaves like a long-term average, so a louder
 * chorus/drop genuinely shoots the bars up and quiet verses sit low. */
#define BT_RHYTHM_AGC_ATTACK   0.12f
#define BT_RHYTHM_AGC_RELEASE  0.02f
#define BT_RHYTHM_BIN_FLOOR    5.0f
#define BT_RHYTHM_VISUAL_MAX  86.0f
static const float s_preemph[BT_RHYTHM_SPECTRUM_BINS] = {
    0.85f, 0.95f, 1.05f, 1.20f, 1.45f, 1.90f, 2.55f, 3.35f,
};
static volatile uint8_t s_spectrum[BT_RHYTHM_SPECTRUM_BINS];
static float s_agc = BT_RHYTHM_AGC_FLOOR;
static float s_bin_ref[BT_RHYTHM_SPECTRUM_BINS] = {
    BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR,
    BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR,
};
/* Slow per-bin level for spectral onset detection: a sharp jump of a bin above
 * its own recent level == a transient (note attack / hit / "drop"). */
static float s_bin_slow[BT_RHYTHM_SPECTRUM_BINS] = {
    BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR,
    BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR, BT_RHYTHM_BIN_FLOOR,
};
/* Peak-held spectral transient strength (0..255 == 0..1); the motion task
 * consumed it on the robot build; kept for the analyzer internals here. */
static volatile uint8_t s_onset_q8;

static volatile bool s_enabled;
static uint8_t s_inited;
static uint32_t s_last_decay_ms;

/* ============================ SBC bitstream parse ============================ */

typedef struct {
    const uint8_t *buf;
    uint16_t len;       /* bytes */
    uint32_t bitpos;    /* current bit offset */
} sbc_bitreader_t;

static uint32_t sbc_read_bits(sbc_bitreader_t *r, uint8_t nbits)
{
    uint32_t val = 0;
    for (uint8_t i = 0; i < nbits; i++) {
        uint32_t byte_idx = r->bitpos >> 3;
        if (byte_idx >= r->len) {
            return val << (nbits - i); /* ran out; pad zeros */
        }
        uint8_t bit = (r->buf[byte_idx] >> (7 - (r->bitpos & 7))) & 0x1;
        val = (val << 1) | bit;
        r->bitpos++;
    }
    return val;
}

/* Map a 0..15 scale-factor sum over `n` subbands (max 15*n) to 0..100. */
static uint8_t scale_to_100(uint32_t sum, uint32_t nbands)
{
    uint32_t max = 15U * nbands;
    if (max == 0) {
        return 0;
    }
    uint32_t v = (sum * 100U) / max;
    return (v > 100U) ? 100U : (uint8_t)v;
}

static inline uint8_t ema_u8(uint8_t prev, uint8_t now)
{
    /* prev*3/4 + now*1/4 */
    return (uint8_t)(((uint32_t)prev * 3U + now) >> 2);
}

/*
 * Parse the first SBC frame in `payload` (which starts with a 1-byte A2DP
 * frame-count header) and update the low/mid/high band EMAs.
 */
static void sbc_analyze_frame(const uint8_t *payload, uint16_t len)
{
    if (payload == NULL || len < 6) {
        return;
    }

    /* Skip the 1-byte A2DP SBC media payload header (fragment/frame count). */
    const uint8_t *fb = payload + 1;
    uint16_t rem = len - 1;

    /* Locate the SBC syncword (0x9C) within the first few bytes. */
    int sync_off = -1;
    for (uint16_t i = 0; i < rem && i < 4; i++) {
        if (fb[i] == 0x9C) {
            sync_off = (int)i;
            break;
        }
    }
    if (sync_off < 0) {
        return;
    }
    fb += sync_off;
    rem -= (uint16_t)sync_off;
    if (rem < 4) {
        return;
    }

    uint8_t b1 = fb[1];
    uint8_t channel_mode = (b1 >> 2) & 0x03; /* 0 mono,1 dual,2 stereo,3 joint */
    uint8_t subbands = (b1 & 0x01) ? 8U : 4U;
    uint8_t nch = (channel_mode == 0) ? 1U : 2U;

    /* Worst-case bytes we will touch: header(4) + join(joint) + scalefactors. */
    uint32_t join_bits = (channel_mode == 3) ? subbands : 0U;
    uint32_t sf_bits = (uint32_t)nch * subbands * 4U;
    uint32_t need_bytes = 4U + ((join_bits + sf_bits + 7U) >> 3);
    if (need_bytes > rem) {
        return;
    }

    sbc_bitreader_t r = { .buf = fb, .len = rem, .bitpos = 4U * 8U };
    if (join_bits) {
        (void)sbc_read_bits(&r, (uint8_t)join_bits);
    }

    /* Scale factors: ch-major, sb-minor, 4 bits each. Sum across channels. */
    uint32_t e[8] = {0};
    for (uint8_t ch = 0; ch < nch; ch++) {
        for (uint8_t sb = 0; sb < subbands; sb++) {
            e[sb] += sbc_read_bits(&r, 4);
        }
    }

    uint32_t lo_sum, mid_sum, hi_sum;
    uint32_t lo_n, mid_n, hi_n;
    if (subbands == 8) {
        lo_sum  = e[0] + e[1];
        mid_sum = e[2] + e[3] + e[4];
        hi_sum  = e[5] + e[6] + e[7];
        lo_n = 2 * nch; mid_n = 3 * nch; hi_n = 3 * nch;
    } else { /* 4 subbands */
        lo_sum  = e[0];
        mid_sum = e[1] + e[2];
        hi_sum  = e[3];
        lo_n = 1 * nch; mid_n = 2 * nch; hi_n = 1 * nch;
    }

    uint8_t lo = scale_to_100(lo_sum, lo_n);
    uint8_t mid = scale_to_100(mid_sum, mid_n);
    uint8_t hi = scale_to_100(hi_sum, hi_n);

    s_band_low  = ema_u8(s_band_low, lo);
    s_band_mid  = ema_u8(s_band_mid, mid);
    s_band_high = ema_u8(s_band_high, hi);

    /* ---- Per-bin spectrum for the UI visualizer ----
     * Pre-emphasis (treble lift) + one slow global reference. The motion path
     * above keeps using the raw lo/mid/hi values, untouched. */
    float wv[BT_RHYTHM_SPECTRUM_BINS];
    if (subbands == 8) {
        for (uint8_t i = 0; i < 8; i++) {
            wv[i] = ((float)e[i] / (float)nch) * s_preemph[i];
        }
    } else { /* 4 subbands: fan each out to two visual bins */
        for (uint8_t i = 0; i < 4; i++) {
            float v = (float)e[i] / (float)nch;
            wv[2 * i]     = v * s_preemph[2 * i];
            wv[2 * i + 1] = v * s_preemph[2 * i + 1];
        }
    }

    float maxwv = 0.0f;
    for (uint8_t i = 0; i < BT_RHYTHM_SPECTRUM_BINS; i++) {
        if (wv[i] > maxwv) {
            maxwv = wv[i];
        }
    }
    float rate = (maxwv > s_agc) ? BT_RHYTHM_AGC_ATTACK : BT_RHYTHM_AGC_RELEASE;
    s_agc += (maxwv - s_agc) * rate;
    if (s_agc < BT_RHYTHM_AGC_FLOOR) {
        s_agc = BT_RHYTHM_AGC_FLOOR;
    }

    float onset = 0.0f;   /* strongest per-bin transient this frame (0..1) */
    for (uint8_t i = 0; i < BT_RHYTHM_SPECTRUM_BINS; i++) {
        float brate = (wv[i] > s_bin_ref[i]) ? 0.25f : 0.025f;
        s_bin_ref[i] += (wv[i] - s_bin_ref[i]) * brate;
        if (s_bin_ref[i] < BT_RHYTHM_BIN_FLOOR) {
            s_bin_ref[i] = BT_RHYTHM_BIN_FLOOR;
        }

        /* Onset: how far this bin shot above its own slow level (relative jump),
         * which captures tone changes / hits the steady AGC would smear out. */
        float over = wv[i] - s_bin_slow[i] * 1.35f;
        if (over > 0.0f) {
            float strength = over / (s_bin_slow[i] + 6.0f);
            if (strength > onset) {
                onset = strength;
            }
        }
        float srate = (wv[i] > s_bin_slow[i]) ? 0.20f : 0.08f;
        s_bin_slow[i] += (wv[i] - s_bin_slow[i]) * srate;

        /* global keeps low/mid/his proportional; local wakes up quiet highs. */
        float global = wv[i] / (s_agc * 1.28f);
        float local = wv[i] / (s_bin_ref[i] * 1.18f);
        if (global > 1.0f) {
            global = 1.0f;
        }
        if (local > 1.0f) {
            local = 1.0f;
        }
        float norm = global * 0.62f + local * 0.38f;
        uint8_t target = (uint8_t)(norm * BT_RHYTHM_VISUAL_MAX + 0.5f);
        /* Fast asymmetric EMA: jump up almost instantly on a hit, fall a bit
         * slower. Keeps transients/drops sharp instead of smeared. */
        if (target > s_spectrum[i]) {
            s_spectrum[i] = (uint8_t)(((uint32_t)s_spectrum[i] + (uint32_t)target * 3U) >> 2);
        } else {
            s_spectrum[i] = (uint8_t)(((uint32_t)s_spectrum[i] * 3U + (uint32_t)target) >> 2);
        }
    }

    if (onset > 1.0f) {
        onset = 1.0f;
    }
    uint8_t onset_q8 = (uint8_t)(onset * 255.0f + 0.5f);
    if (onset_q8 > s_onset_q8) {
        s_onset_q8 = onset_q8;   /* peak-hold; motion task decays per tick */
    }

    s_last_audio_ms = (uint32_t)rtos_get_time();
}

void bt_rhythm_feed_sbc(const uint8_t *data, uint16_t len, uint8_t codec_type)
{
    /* Analyze whenever the engine is up (page active), regardless of the hand
     * dance toggle -- the EQ meter keeps moving even with the hand parked. */
    if (!s_inited || codec_type != BT_RHYTHM_CODEC_SBC) {
        return;
    }
    sbc_analyze_frame(data, len);
}

/* Let the visualizer bars fall when the stream pauses (no frames arrive to
 * refresh s_spectrum), so they don't freeze mid-air. ~0.875x per 25 ms tick.
 * Headset port: driven lazily from get_spectrum(), see there. */
static void spectrum_decay(void)
{
    for (uint8_t i = 0; i < BT_RHYTHM_SPECTRUM_BINS; i++) {
        s_spectrum[i] = (uint8_t)(s_spectrum[i] - (s_spectrum[i] >> 3));
    }
}

void bt_rhythm_get_bands(uint8_t *low, uint8_t *mid, uint8_t *high)
{
    if (low)  { *low = s_band_low; }
    if (mid)  { *mid = s_band_mid; }
    if (high) { *high = s_band_high; }
}

void bt_rhythm_get_spectrum(uint8_t *bands, uint8_t count)
{
    if (bands == NULL || count == 0) {
        return;
    }
    /* Lazy spectrum decay (headset port has no control thread): decay once
     * per BT_RHYTHM_TICK_MS elapsed since the last call, so the bars still
     * fall when the stream pauses. The page's meter timer drives this. */
    uint32_t now = (uint32_t)rtos_get_time();
    uint32_t ticks = (now - s_last_decay_ms) / BT_RHYTHM_TICK_MS;
    if (ticks > 0U) {
        s_last_decay_ms += ticks * BT_RHYTHM_TICK_MS;
        if (ticks > 8U) {
            ticks = 8U;
        }
        while (ticks-- > 0U) {
            spectrum_decay();
        }
    }
    if (count == 1) {
        bands[0] = s_spectrum[0];
        return;
    }
    /* Linear resample the internal bins to whatever bar count the UI wants. */
    for (uint8_t j = 0; j < count; j++) {
        float pos = (float)(BT_RHYTHM_SPECTRUM_BINS - 1) * (float)j /
                    (float)(count - 1);
        uint8_t i0 = (uint8_t)pos;
        uint8_t i1 = (i0 + 1U < BT_RHYTHM_SPECTRUM_BINS) ? (uint8_t)(i0 + 1U) : i0;
        float frac = pos - (float)i0;
        float v = (float)s_spectrum[i0] * (1.0f - frac) +
                  (float)s_spectrum[i1] * frac;
        bands[j] = (uint8_t)(v + 0.5f);
    }
}

/* ============================ Lifecycle ============================ */

int bt_rhythm_init(void)
{
    if (s_inited) {
        return 0;
    }

    s_agc = BT_RHYTHM_AGC_FLOOR;
    s_onset_q8 = 0;
    for (uint8_t i = 0; i < BT_RHYTHM_SPECTRUM_BINS; i++) {
        s_spectrum[i] = 0;
        s_bin_ref[i] = BT_RHYTHM_BIN_FLOOR;
        s_bin_slow[i] = BT_RHYTHM_BIN_FLOOR;
    }
    s_last_decay_ms = (uint32_t)rtos_get_time();

    s_inited = 1;
    return 0;
}

void bt_rhythm_deinit(void)
{
    if (!s_inited) {
        return;
    }
    s_enabled = false;
    s_inited = 0;
}

void bt_rhythm_set_enabled(bool enable)
{
    if (enable && !s_inited) {
        if (bt_rhythm_init() != 0) {
            return;
        }
    }
    if (s_enabled == enable) {
        return;
    }
    s_enabled = enable;
}

bool bt_rhythm_is_enabled(void)
{
    return s_enabled;
}


