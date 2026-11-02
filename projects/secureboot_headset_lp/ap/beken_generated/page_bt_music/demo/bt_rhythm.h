/**
 * @file bt_rhythm.h
 * @brief Music rhythm analyzer for the headset BT music page.
 *
 * Headset port of the beken_robot bt_rhythm engine: keeps the A2DP SBC
 * subband band-energy analysis (low/mid/high + 8-bin UI spectrum) and drops
 * the Hiwonder hand-servo motion output (no robot hand on this board). The
 * analyzer needs no control thread: feed it SBC payloads and read the
 * smoothed levels back; the spectrum decay runs lazily inside
 * bt_rhythm_get_spectrum().
 *
 * All public functions are safe to call from any task.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Reset analyzer state (idempotent). Returns 0 on ok. */
int  bt_rhythm_init(void);

/** Disable the engine and clear state. */
void bt_rhythm_deinit(void);

/** Enable/disable the engine (audio analysis for the UI meter). */
void bt_rhythm_set_enabled(bool enable);
bool bt_rhythm_is_enabled(void);

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
 * Every bin is auto-gained against its own decaying peak so quiet bands
 * (notably treble) still reach full height -- it is a "looks good"
 * visualizer feed, not a calibrated meter. Any @p count is linearly
 * resampled from the internal BT_RHYTHM_SPECTRUM_BINS bins, so the UI can
 * ask for an arbitrary number of bars.
 */
void bt_rhythm_get_spectrum(uint8_t *bands, uint8_t count);

#ifdef __cplusplus
}
#endif
