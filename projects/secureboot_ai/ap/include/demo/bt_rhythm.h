/**
 * @file bt_rhythm.h
 * @brief Music-to-motion rhythm engine for the beken_robot Bluetooth hand.
 *
 * The engine taps the A2DP SBC stream (encoded payload, no PCM decode needed),
 * extracts a low/mid/high frequency-band energy envelope directly from the SBC
 * subband scale factors, and drives the on-board Hiwonder 6-DOF hand through
 * the same PWM driver used by the hand_gesture edge-AI demo (GPIO_22..27).
 *
 * The verified rhythm_robot mapping is continuous rather than pose-switching:
 *   - overall loudness   -> finger-wave amplitude and apparent tempo
 *   - treble-vs-bass mix -> how open (treble) or clenched (bass) the hand sits
 *   - detected beats     -> a short, smoothly-decaying motion accent
 *
 * A dedicated control thread low-pass filters the pose every 25 ms, then sends
 * targets to bk_hiwonder_hand_servo_set_pulse_and_time(). All public functions
 * are safe to call from any task.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bring up the hand servos + control thread (idempotent). Returns 0 on ok. */
int  bt_rhythm_init(void);

/** Tear down the control thread and release the servo PWM channels. */
void bt_rhythm_deinit(void);

/** Enable/disable the rhythm engine (audio analysis + UI pose mirror). Tracks
 *  "playing on an active page", not the claw button. Parks the hand when off. */
void bt_rhythm_set_enabled(bool enable);
bool bt_rhythm_is_enabled(void);

/** Gate only the physical hand (claw button); the engine/UI keep running. */
void bt_rhythm_set_hand_output(bool enable);
bool bt_rhythm_hand_output_enabled(void);

/**
 * @brief Feed one A2DP media payload to the analyzer.
 * @param data       A2DP media payload (SBC: 1-byte frame-count header + frames)
 * @param len        payload length in bytes
 * @param codec_type bk_a2dp codec type (0x00 = SBC; anything else is ignored)
 *
 * Cheap no-op while the feature is disabled or for non-SBC streams.
 */
void bt_rhythm_feed_sbc(const uint8_t *data, uint16_t len, uint8_t codec_type);

/** Latest smoothed band energies, each mapped to 0..100 (for a UI meter). */
void bt_rhythm_get_bands(uint8_t *low, uint8_t *mid, uint8_t *high);

/** Number of raw frequency bins the analyzer keeps for the UI spectrum. */
#define BT_RHYTHM_SPECTRUM_BINS  8U

/**
 * @brief Fill @p bands with the latest per-bin spectrum, each 0..100.
 *
 * Independent of the motion (low/mid/high) path: every bin is auto-gained
 * against its own decaying peak so quiet bands (notably treble) still reach
 * full height -- it is a "looks good" visualizer feed, not a calibrated meter.
 * Any @p count is linearly resampled from the internal BT_RHYTHM_SPECTRUM_BINS
 * bins, so the UI can ask for an arbitrary number of bars.
 */
void bt_rhythm_get_spectrum(uint8_t *bands, uint8_t count);

/** Number of joints mirrored to the UI: 5 fingers + 1 wrist/base. */
#define BT_RHYTHM_POSE_BARS  6U

/**
 * @brief Fill @p levels with each joint's normalized travel, 0..100.
 *
 * This is the "what the hand is doing right now" mirror used by the music page
 * so every on-screen bar maps 1:1 to a servo:
 *   - index 0..4 = fingers, low-freq band -> high-freq band order
 *     (0 = pinky/lowest .. 4 = thumb/highest), 0 = curled .. 100 = open;
 *   - index 5    = wrist/base, 0 = centered .. 100 = max rotation off center.
 * Fewer/more @p count than available is clamped/zero-padded.
 */
void bt_rhythm_get_pose_levels(uint8_t *levels, uint8_t count);

#ifdef __cplusplus
}
#endif
