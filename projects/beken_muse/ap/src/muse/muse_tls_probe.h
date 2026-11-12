/**
 * @file muse_tls_probe.h
 * @brief Debug-only TLS uplink probe (see muse_tls_probe.c).
 */
#ifndef __BK_MUSE_TLS_PROBE_H__
#define __BK_MUSE_TLS_PROBE_H__

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Sends ~4 KB/s of dummy HTTP body to the Muse noise host for `seconds`.
 * Returns 0 when every write went through, <0 on stall/connect failure.
 * A short verdict string is copied into out. */
int muse_tls_probe_run(int seconds, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
