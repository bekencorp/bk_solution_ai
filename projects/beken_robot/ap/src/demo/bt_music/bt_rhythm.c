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
#include <driver/gpio.h>

#include "bk_hiwonder_hand_servo.h"

#define TAG "bt_rhythm"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* bk_a2dp codec type for SBC (matches CODEC_AUDIO_SBC in a2dp_sink.c). */
#define BT_RHYTHM_CODEC_SBC          0x00U

/* ---- Hand servo hardware map (same as src/demo/edge_ai/hand_gesture.cc) ----
 * Entry i drives servo id (i + 1) on the beken_robot board: {PWM ch, GPIO}. */
static const bk_hiwonder_hand_servo_hw_map_t s_servo_hw_map[] = {
    {8, GPIO_22},  /* servo 1 (thumb) */
    {6, GPIO_23},  /* servo 2 */
    {7, GPIO_24},  /* servo 3 */
    {5, GPIO_25},  /* servo 4 */
    {4, GPIO_26},  /* servo 5 */
    {3, GPIO_27},  /* servo 6 (wrist) */
};
enum {
    BT_RHYTHM_SERVO_COUNT = (sizeof(s_servo_hw_map) / sizeof(s_servo_hw_map[0])),
};

/* ---- Servo pose calibration (microseconds). Tune to your linkage. ----
 * Fingers ride in [FINGER_MIN_US,FINGER_MAX_US]; the wrist (id 6) in
 * [WRIST_MIN_US,WRIST_MAX_US]. Floats keep the smoothing math clean. */
#define FINGER_OPEN_US       1900.0f  /* fingers extended / spread */
#define FINGER_CLOSE_US       950.0f  /* fingers curled into a fist */
#define FINGER_MID_US        1420.0f  /* relaxed center, wave pivot */
#define FINGER_WAVE_AMP_US    230.0f  /* max +/- swing of the finger wave */
#define WRIST_CENTER_US      1500.0f
#define WRIST_SWAY_US         320.0f  /* max +/- swing of the wrist */

#define FINGER_MIN_US         900.0f
#define FINGER_MAX_US        2200.0f
#define WRIST_MIN_US          500.0f
#define WRIST_MAX_US         2500.0f

#define FINGER_COUNT            5U    /* servo id 1..5 */
#define WRIST_IDX               5U    /* 0-based index, servo id 6 */

/* ---- Loudness-driven motion (model: "move like a person listening") ----
 * Instead of letting the frequency *shape* drive the fingers, the hand follows
 * the *sound itself*: louder & more agitated music -> bigger motion; silence ->
 * stillness. Each finger's open fraction is
 *
 *   open = FLOOR                                (tiny, so it never locks dead)
 *        + LOUD * envelope * (1 + ACT*activity) (main drive: loud & busy = big)
 *        + BAND * band_contribution            (which finger leads, with a
 *                                               vocal/mid emphasis)
 *        + onset pop
 *
 * Fractions are of the full close<->open finger range. */
#define FINGER_FLOOR_FRAC     0.06f   /* tiny floor: ~still on silence, not frozen */
#define FINGER_GROOVE_FRAC    0.10f   /* subtle base groove, so music never feels dead */
#define FINGER_GROOVE_STEP    0.18f
#define FINGER_GROOVE_SPREAD  1.10f
#define FINGER_LOUD_FRAC      0.58f   /* loudness envelope -> overall hand openness */
#define FINGER_ACT_FRAC       0.60f   /* "agitation" boosts the loudness drive      */
#define FINGER_BAND_FRAC      0.38f   /* per-finger frequency texture               */
#define FINGER_LOUD_GAMMA     1.4f    /* >1: quiet stays low, only loud parts pop up
                                       * (keeps verses from pinning at 70% while the
                                       *  chorus still has somewhere to go)         */
#define WRIST_IDLE_FRAC       0.10f

/* High-freq "whole hand" response: continuous hats/cymbals can look twitchy if
 * every treble pulse directly hits the wrist/thumb. Gate and smooth the treble
 * first, so highs become a flowing accent instead of a rapid shake. */
#define HF_GATE               0.38f   /* ignore low/constant treble hash           */
#define HF_ATTACK             0.075f  /* slow rise: no sudden wrist snap           */
#define HF_RELEASE            0.030f  /* slow fall: continuous highs stay smooth    */
#define HF_WRIST_FLICK        0.20f   /* wrist swing added on strong highs (frac)   */
#define HF_HAND_LIFT          0.045f  /* small lift to every finger on strong highs */

/* Vocal (mid-band) emphasis: human voice mostly sits in the mid bands. We don't
 * look at the mid's absolute level (drums keep every band high); instead we look
 * at how much the mid rises above its OWN slow baseline -- a sung phrase / pitch
 * change lifts the mid envelope even when the beat is steady. That relative
 * "vocal activity" both leads the middle fingers and drives the whole hand, so
 * the hand visibly reacts to the singer, not just to volume. */
#define VOCAL_EMPHASIS        0.85f   /* extra mid-finger weight on vocal activity */
#define VOCAL_HAND_FRAC       0.30f   /* whole-hand opening added by vocal activity */
#define VOCAL_BASE_RATE_UP    0.04f   /* mid baseline tracks up slowly ...         */
#define VOCAL_BASE_RATE_DN    0.02f   /* ... and down even slower (so it's a ref)  */

/* ---- Frequency->finger band mapping (low freq -> high freq) ----
 * fband[0] = lowest freq .. fband[4] = highest. The UI bars are drawn in this
 * same low->high order; bar i is mirrored from this band. Treble carries far
 * less energy than bass, so the higher bands get progressively more gain. */
#define FBANDS                 5U
static const float s_fband_gain[FBANDS] = {
    0.90f, 1.05f, 1.30f, 1.75f, 2.60f,   /* low .. high */
};
/* Per-band vocal weight: middle fingers (mid freq) get the most vocal emphasis;
 * pinky(low)/thumb(high) the least. Indexed by band, low->high (== fb[]). */
static const float s_vocal_w[FBANDS] = {
    0.15f, 0.70f, 1.00f, 0.70f, 0.20f,
};
#define HF_THROW_BOOST         0.12f   /* extra thumb travel from smoothed highs */

/* ---- Control loop ---- */
#define BT_RHYTHM_TICK_MS      25U    /* fine tick -> fluid motion */
#define BT_RHYTHM_MOVE_MS      40U    /* let hiwonder's 20ms timer smooth each step */
#define SMOOTH_ALPHA         0.45f    /* per-tick low-pass toward target (0..1):
                                       * snappier so beats/drops aren't smeared */
#define BT_RHYTHM_TASK_PRIO     4
#define BT_RHYTHM_TASK_STACK 3072

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
 * consumes & clears it so each onset fires the hand once, then decays fast. */
static volatile uint8_t s_onset_q8;

/* ---- Pose mirror (single source of truth for screen<->hand) ---- *
 * The motion task writes each servo's normalized travel here every tick:
 *   idx 0..4 = fingers, 0 = fully curled .. 100 = fully open;
 *   idx 5    = wrist/base, 0 = centered .. 100 = max rotation off center.
 * The UI reads these 6 values so each bar IS the joint that's moving. */
#define BT_RHYTHM_POSE_COUNT  6U
static volatile uint8_t s_pose_level[BT_RHYTHM_POSE_COUNT];

static volatile bool s_enabled;
/* Claw-button gate for the physical hand only; engine/UI keep running. */
static volatile bool s_hand_output = true;
static bk_hiwonder_hand_servo_handle_t s_servo;
static beken_thread_t s_task;
static volatile bool s_task_run;
static uint8_t s_inited;

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

void bt_rhythm_get_pose_levels(uint8_t *levels, uint8_t count)
{
    if (levels == NULL || count == 0) {
        return;
    }
    for (uint8_t i = 0; i < count; i++) {
        levels[i] = (i < BT_RHYTHM_POSE_COUNT) ? s_pose_level[i] : 0;
    }
}

/* Let the visualizer bars fall when the stream pauses (no frames arrive to
 * refresh s_spectrum), so they don't freeze mid-air. ~0.875x per 25 ms tick. */
static void spectrum_decay(void)
{
    for (uint8_t i = 0; i < BT_RHYTHM_SPECTRUM_BINS; i++) {
        s_spectrum[i] = (uint8_t)(s_spectrum[i] - (s_spectrum[i] >> 3));
    }
}

/* When the hand is parked (dance off / silence) the bars mirror a resting hand,
 * so ease the mirrored pose levels down to flat instead of freezing them. */
static void pose_levels_decay(void)
{
    for (uint8_t i = 0; i < BT_RHYTHM_POSE_COUNT; i++) {
        s_pose_level[i] = (uint8_t)(s_pose_level[i] - (s_pose_level[i] >> 2));
    }
}

/* ============================ Motion control ============================ */
/*
 * The verified rhythm_robot algorithm works in a 0-based 6-servo pose array.
 * beken_robot's existing hand driver exposes 1-based servo IDs, owns the PWM
 * channels, and performs the 20 ms ramp internally. This layer only clamps and
 * forwards the smoothed pose to that driver.
 */

static float s_cur[BT_RHYTHM_SERVO_COUNT];

static float clampf(float v, float lo, float hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

static void commit_pose(void)
{
    if (s_servo == NULL) {
        return;
    }

    for (uint8_t i = 0; i < BT_RHYTHM_SERVO_COUNT; i++) {
        float min_us = (i == WRIST_IDX) ? WRIST_MIN_US : FINGER_MIN_US;
        float max_us = (i == WRIST_IDX) ? WRIST_MAX_US : FINGER_MAX_US;
        uint16_t pulse_us = (uint16_t)clampf(s_cur[i], min_us, max_us);
        bk_hiwonder_hand_servo_set_pulse_and_time(s_servo, i + 1U,
                                                  pulse_us, BT_RHYTHM_MOVE_MS);
    }
}

static void glide_to(const float target[BT_RHYTHM_SERVO_COUNT])
{
    for (uint8_t i = 0; i < BT_RHYTHM_SERVO_COUNT; i++) {
        s_cur[i] += (target[i] - s_cur[i]) * SMOOTH_ALPHA;
    }
    commit_pose();
}

static void pose_neutral(void)
{
    for (uint8_t i = 0; i < BT_RHYTHM_SERVO_COUNT; i++) {
        s_cur[i] = (i == WRIST_IDX) ? WRIST_CENTER_US : FINGER_MID_US;
    }
    commit_pose();
}

static void bt_rhythm_task(void *arg)
{
    (void)arg;
    float phase = 0.0f;        /* wrist sway phase */
    float fphase = 0.0f;       /* subtle finger groove phase                   */
    float env_ema = 0.0f;      /* slow loudness envelope (beat ref)            */
    float env_fast = 0.0f;     /* faster loudness envelope -> hand openness     */
    float prev_overall = 0.0f; /* last tick's loudness, for energy flux         */
    float prev_mid = 0.0f;     /* mid-band change, a better vocal proxy         */
    float mid_flux_ema = 0.0f; /* smoothed mid-band changes                     */
    float flux_ema = 0.0f;     /* smoothed positive energy change ("busyness")  */
    float act = 0.0f;          /* agitation 0..1 (flux + onset density)         */
    float accent = 0.0f;       /* raw decaying pop on detected beats/onsets     */
    float pop = 0.0f;          /* SMOOTHED pop -> fast up, slow down (no twitch)*/
    float hf_env = 0.0f;       /* smoothed treble response, avoids HF twitch    */
    float mid_slow = 0.0f;     /* adaptive mid baseline, for vocal detection    */
    bool  parked = false;
    bool  hand_parked = false; /* physical hand parked due to the claw gate      */
    float target[BT_RHYTHM_SERVO_COUNT];

    while (s_task_run) {
        float lo  = (float)s_band_low;
        float mid = (float)s_band_mid;
        float hi  = (float)s_band_high;
        float overall = (lo + mid + hi) / 3.0f;

        uint32_t age = (uint32_t)rtos_get_time() - s_last_audio_ms;
        bool silent = (overall < (float)BAND_SILENCE_LEVEL) || (age > 700U);

        /* The EQ bars fall only on real audio silence (no stream / paused),
         * independent of the hand dance toggle. */
        if (silent) {
            spectrum_decay();
        }

        /* Hand motion respects the dance toggle and pauses on silence. */
        if (!s_enabled || silent) {
            if (!parked) {
                pose_neutral();
                parked = true;
            }
            pose_levels_decay();   /* let the mirrored UI bars fall to flat too */
            /* Relax the envelopes so motion ramps up fresh when sound returns. */
            env_fast *= 0.80f;
            flux_ema *= 0.80f;
            mid_flux_ema *= 0.80f;
            act      *= 0.80f;
            pop      *= 0.80f;
            hf_env   *= 0.80f;
            mid_slow  = mid_slow * 0.9f + mid * 0.1f;
            prev_overall = overall;
            prev_mid = mid;
            rtos_delay_milliseconds(BT_RHYTHM_TICK_MS);
            continue;
        }
        parked = false;

        /* ---- Loudness envelope (the main driver) ----
         * env_fast follows the sound's volume quickly-up / slowly-down, so the
         * whole hand opens with how loud the music is right now. */
        if (overall > env_fast) {
            env_fast += (overall - env_fast) * 0.50f;   /* snap up to louder */
        } else {
            env_fast += (overall - env_fast) * 0.16f;   /* ease down a bit faster */
        }
        /* Normalize against a higher full-scale so typical playback sits in the
         * lower half, then gamma-shape it: quiet/normal parts stay low and only
         * the genuinely loud "燃" sections push toward full. This is what keeps
         * the intro from pinning the first 5 bars at 70%. */
        float env_lin = clampf(env_fast / 120.0f, 0.0f, 1.0f);
        float env = powf(env_lin, FINGER_LOUD_GAMMA);

        /* ---- Agitation (how "busy/excited" the music is) ----
         * Energy flux = positive jump in loudness vs last tick; onset density =
         * how often transients fire. Together they say "the music is hyped",
         * which scales the motion bigger even at the same volume. */
        float flux = overall - prev_overall;
        prev_overall = overall;
        if (flux < 0.0f) {
            flux = 0.0f;
        }
        flux_ema = flux_ema * 0.85f + flux * 0.15f;
        float mid_flux = mid - prev_mid;
        prev_mid = mid;
        if (mid_flux < 0.0f) {
            mid_flux = 0.0f;
        }
        mid_flux_ema = mid_flux_ema * 0.82f + mid_flux * 0.18f;

        /* Slow beat reference + transient onset from the analyzer. */
        env_ema = env_ema * 0.875f + overall * 0.125f;
        uint8_t onset_q8 = s_onset_q8;
        s_onset_q8 = 0;
        float onset = (float)onset_q8 / 255.0f;
        if (overall > (float)BAND_ACTIVE_LEVEL && overall > env_ema * 1.25f) {
            accent = 1.0f;                 /* loudness beat */
        }
        if (onset > accent) {
            accent = onset;                /* spectral transient */
        }
        accent *= 0.90f;                   /* slower decay -> pop lingers, less twitchy */

        /* Smooth the pop so an accent ramps up fast but FALLS BACK SLOWLY,
         * instead of snapping open and instantly collapsing (the "一抽一抽"
         * twitch). This is what the fingers/wrist actually add on a hit. */
        if (accent > pop) {
            pop += (accent - pop) * 0.28f;   /* attack: rise, but don't snap */
        } else {
            pop += (accent - pop) * 0.055f;  /* release: ease down slowly */
        }

        /* Blend flux + onset into a smoothed agitation level (0..1). */
        float act_now = clampf(flux_ema / 12.0f + onset * 0.6f, 0.0f, 1.0f);
        act = act * 0.80f + act_now * 0.20f;

        /* ---- Vocal/chorus proxy ----
         * We cannot isolate vocals from SBC scale factors. Waka Waka keeps drums
         * and backing percussion loud, so a pure "mid beats lo/hi" detector often
         * stays flat. Use a more listener-like proxy instead:
         *   - mid presence: voice-range energy exists;
         *   - mid flux: sung syllables / pitch changes move the mid band;
         *   - chorus lift: when overall loudness is high, let the hand open even
         *     if the frequency split itself looks similar.
         * This makes the hand react to vocal phrases and choruses, not just to a
         * mathematically "dominant" mid band. */
        float mrate = (mid > mid_slow) ? VOCAL_BASE_RATE_UP : VOCAL_BASE_RATE_DN;
        mid_slow += (mid - mid_slow) * mrate;
        float mid_rel = clampf((mid - mid_slow * 1.06f) / 18.0f, 0.0f, 1.0f);
        float mid_presence = clampf((mid - 20.0f) / 58.0f, 0.0f, 1.0f);
        float mid_motion = clampf(mid_flux_ema / 7.0f, 0.0f, 1.0f);
        float chorus = clampf((env_lin - 0.42f) / 0.42f, 0.0f, 1.0f);
        float vocal = clampf(mid_rel * 0.35f + mid_presence * 0.30f +
                             mid_motion * 0.25f + chorus * 0.30f,
                             0.0f, 1.0f);

        const float span = FINGER_OPEN_US - FINGER_CLOSE_US;
        uint8_t fb[FBANDS];
        bt_rhythm_get_spectrum(fb, FBANDS);

        /* High-frequency strength (top band): gate + curve + slow envelope.
         * Continuous hi-hats often keep the top band busy; treating each pulse
         * as a separate "hit" makes the thumb/wrist twitch. After the gate, the
         * squared curve keeps weak/constant treble small while still allowing a
         * real high-frequency accent to swell smoothly. */
        float hf_raw = clampf((float)fb[FBANDS - 1U] / 100.0f * s_fband_gain[FBANDS - 1U],
                              0.0f, 1.0f);
        float hf_gate = clampf((hf_raw - HF_GATE) / (1.0f - HF_GATE), 0.0f, 1.0f);
        float hf_target = hf_gate * hf_gate;
        if (hf_target > hf_env) {
            hf_env += (hf_target - hf_env) * HF_ATTACK;
        } else {
            hf_env += (hf_target - hf_env) * HF_RELEASE;
        }
        float hf = hf_env;

        /* Loudness drive shared by every finger: louder & busier -> bigger.
         * A strong treble adds a small lift; vocal activity opens the hand too,
         * so the hand reacts to the singer even when the volume is steady. */
        float drive = FINGER_LOUD_FRAC * env * (1.0f + FINGER_ACT_FRAC * act)
                    + HF_HAND_LIFT * hf
                    + VOCAL_HAND_FRAC * vocal;
        fphase += FINGER_GROOVE_STEP + env * 0.20f + act * 0.12f;

        for (uint8_t b = 0; b < FBANDS; b++) {
            uint8_t servo = (uint8_t)(FBANDS - 1U - b);   /* low->pinky, high->thumb */

            float v = clampf((float)fb[b] / 100.0f * s_fband_gain[b], 0.0f, 1.0f);
            float groove = FINGER_GROOVE_FRAC *
                           (0.5f + 0.5f * sinf(fphase + (float)b * FINGER_GROOVE_SPREAD));

            /* Per-finger texture: its own band, plus extra weight to the mid
             * fingers when a vocal is present (rough "follow the singer"). */
            float band_term = FINGER_BAND_FRAC * v * (1.0f + VOCAL_EMPHASIS * vocal * s_vocal_w[b]);

            float extra = (b == FBANDS - 1U) ? (HF_THROW_BOOST * hf) : 0.0f;
            float frac = FINGER_FLOOR_FRAC + groove + drive + band_term + extra;
            float open_us = FINGER_CLOSE_US + frac * span;
            open_us += pop * 85.0f;       /* smoothed pop: no fast twitch */
            target[servo] = open_us;

            /* Mirror to UI: normalized 0..100 finger travel (close..open). */
            float norm = (open_us - FINGER_CLOSE_US) / span;
            s_pose_level[servo] = (uint8_t)(clampf(norm, 0.0f, 1.0f) * 100.0f + 0.5f);
        }

        /* Wrist/base: sway amplitude follows loudness + agitation (no music ->
         * barely moves). A strong treble adds a clear flick, and onsets pop. */
        phase += 0.28f + env * 0.55f + hf * 0.08f;   /* highs gently speed the sway */
        float wrist_amp = WRIST_IDLE_FRAC
                        + (1.0f - WRIST_IDLE_FRAC) * clampf(env * (0.6f + 0.6f * act) + HF_WRIST_FLICK * hf, 0.0f, 1.0f);
        float wrist_off = WRIST_SWAY_US * wrist_amp * sinf(phase * 0.4f);
        wrist_off += pop * WRIST_SWAY_US * 0.14f * ((wrist_off >= 0.0f) ? 1.0f : -1.0f);
        target[WRIST_IDX] = WRIST_CENTER_US + wrist_off;

        /* Mirror base to UI bar 6: magnitude of rotation off center (0..100). */
        {
            float mag = wrist_off / WRIST_SWAY_US;
            if (mag < 0.0f) {
                mag = -mag;
            }
            s_pose_level[WRIST_IDX] = (uint8_t)(clampf(mag, 0.0f, 1.0f) * 100.0f + 0.5f);
        }

        /* UI mirror already updated above; gate only the physical servos. */
        if (s_hand_output) {
            hand_parked = false;
            glide_to(target);
        } else if (!hand_parked) {
            pose_neutral();   /* claw off: park once, then hold */
            hand_parked = true;
        }

        rtos_delay_milliseconds(BT_RHYTHM_TICK_MS);
    }

    s_task = NULL;
    rtos_delete_thread(NULL);
}

/* ============================ Lifecycle ============================ */

int bt_rhythm_init(void)
{
    if (s_inited) {
        return 0;
    }

    s_servo = bk_hiwonder_hand_servo_init(s_servo_hw_map, BT_RHYTHM_SERVO_COUNT);
    if (s_servo == NULL) {
        LOGE("hand servo init failed\n");
        return -1;
    }

    s_agc = BT_RHYTHM_AGC_FLOOR;
    s_onset_q8 = 0;
    for (uint8_t i = 0; i < BT_RHYTHM_SPECTRUM_BINS; i++) {
        s_spectrum[i] = 0;
        s_bin_ref[i] = BT_RHYTHM_BIN_FLOOR;
        s_bin_slow[i] = BT_RHYTHM_BIN_FLOOR;
    }
    for (uint8_t i = 0; i < BT_RHYTHM_POSE_COUNT; i++) {
        s_pose_level[i] = 0;
    }

    pose_neutral();

    s_task_run = true;
    bk_err_t ret = rtos_create_thread(&s_task, BT_RHYTHM_TASK_PRIO, "bt_rhythm",
                                      (beken_thread_function_t)bt_rhythm_task,
                                      BT_RHYTHM_TASK_STACK, 0);
    if (ret != BK_OK) {
        LOGE("rhythm task create failed %d\n", ret);
        s_task_run = false;
        bk_hiwonder_hand_servo_deinit(s_servo);
        s_servo = NULL;
        return -1;
    }

    s_inited = 1;
    LOGI("bt_rhythm inited (%u servos)\n", BT_RHYTHM_SERVO_COUNT);
    return 0;
}

void bt_rhythm_deinit(void)
{
    if (!s_inited) {
        return;
    }
    s_enabled = false;
    s_task_run = false;
    /* Let the task observe s_task_run and self-delete. */
    rtos_delay_milliseconds(BT_RHYTHM_TICK_MS * 2);

    if (s_servo) {
        bk_hiwonder_hand_servo_deinit(s_servo);
        s_servo = NULL;
    }
    s_inited = 0;
}

void bt_rhythm_set_enabled(bool enable)
{
    if (enable && !s_inited) {
        if (bt_rhythm_init() != 0) {
            return;
        }
    }
    s_enabled = enable;
    LOGI("rhythm dance %s\n", enable ? "ON" : "OFF");
}

bool bt_rhythm_is_enabled(void)
{
    return s_enabled;
}

void bt_rhythm_set_hand_output(bool enable)
{
    s_hand_output = enable;
    LOGI("rhythm hand output %s\n", enable ? "ON" : "OFF");
}

bool bt_rhythm_hand_output_enabled(void)
{
    return s_hand_output;
}
