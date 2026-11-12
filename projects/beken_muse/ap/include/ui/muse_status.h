/**
 * @file muse_status.h
 * @brief Muse status / pairing page (home-screen "Muse" entry).
 *
 * Shows the live muse_service state (pairing / network / Noise link) and
 * offers a "clear pairing" action so the box can be re-paired to another
 * phone. Auto-refreshes while open; S3 returns home, S4 activates the
 * clear-pairing action.
 */
#ifndef __BK_MUSE_STATUS_H__
#define __BK_MUSE_STATUS_H__

#ifdef __cplusplus
extern "C" {
#endif

/** Create and show the Muse status page (dynamic screen). */
int muse_status_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK_MUSE_STATUS_H__ */
