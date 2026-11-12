/**
 * @file muse_tls_probe.c
 * @brief Debug-only TLS uplink probe to the Muse noise host.
 *
 * Answer the blame question behind voice stalls: does a plain TLS POST to
 * the SAME edge host at ~4 KB/s also stall (local/stack/wifi/edge fault),
 * or does the server simply refuse to drain our /voice stream (protocol)?
 * If the probe stalls too, the problem is transport-wide; if it sails,
 * the /voice contract is the problem and the voice proto file must be
 * adjusted against pinned upstream.
 */
#include "muse_tls_probe.h"

#include <stdio.h>
#include <string.h>

#include <components/log.h>
#include <os/os.h>

#include "bk_ssl.h"
#include "muse_ca_cert.h"

#define TAG "muse_probe"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define PROBE_HOST          "hatch.metaaivm.com"
#define PROBE_PORT          443
#define PROBE_CHUNK_BYTES   1024
#define PROBE_CHUNK_GAP_MS  250     /* ~4 KB/s sustained */
#define PROBE_WRITE_TIMEOUT 10000
#define PROBE_CT_CONNECT_MS 15000

int muse_tls_probe_run(int seconds, char *out, size_t out_size)
{
    transport_bk_tls_t ssl;
    static const char body[PROBE_CHUNK_BYTES] =
        "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB";
    int chunks;
    int sent_ok = 0;
    uint32_t worst_write = 0;
    uint32_t started;
    int rc = -1;

    memset(&ssl, 0, sizeof(ssl));
    ssl.sockfd = -1;
    /* Same trust anchor family the session path uses (draw_url's builtin). */
    ssl.cfg.cacert_buf =
        (const unsigned char *)g_muse_builtin_ca_cert;
    ssl.cfg.cacert_bytes =
        (unsigned int)(strlen(g_muse_builtin_ca_cert) + 1U);
    ssl.cfg.skip_common_name = false;

    LOGI("probe: connecting %s:%d\r\n", PROBE_HOST, PROBE_PORT);
    if (_ssl_connect(&ssl, PROBE_HOST, PROBE_PORT, PROBE_CT_CONNECT_MS) != 0 ||
        ssl.tls == NULL) {
        LOGE("probe: tls connect failed\r\n");
        snprintf(out, out_size, "tls connect failed\r\n");
        (void)_ssl_base_close(&ssl);
        return -1;
    }

    /* Body pacing: one 1 KB chunk every PROBE_CHUNK_GAP_MS. */
    chunks = seconds > 0 && seconds <= 300
                 ? (seconds * 1000) / PROBE_CHUNK_GAP_MS
                 : 40;
    started = rtos_get_time();

    /* A minimal legal HTTP request head; the edge handler may 404 this
     * path and we do not care - we are measuring WRITES, not routing. */
    {
        char head[160];
        int hn = snprintf(head, sizeof(head),
                          "POST /voice-probe HTTP/1.1\r\n"
                          "Host: %s\r\n"
                          "Content-Type: audio/opus\r\n"
                          "Content-Length: %d\r\n"
                          "Connection: close\r\n\r\n",
                          PROBE_HOST, chunks * PROBE_CHUNK_BYTES);
        if (hn <= 0 || _ssl_write(&ssl, head, hn, PROBE_WRITE_TIMEOUT) <= 0) {
            LOGE("probe: header write failed\r\n");
            snprintf(out, out_size, "header write failed\r\n");
            (void)_ssl_base_close(&ssl);
            return -1;
        }
    }

    for (sent_ok = 0; sent_ok < chunks; sent_ok++) {
        uint32_t wstart = rtos_get_time();
        int n = _ssl_write(&ssl, body, PROBE_CHUNK_BYTES,
                           PROBE_WRITE_TIMEOUT);
        uint32_t wms = rtos_get_time() - wstart;
        if (wms > worst_write) {
            worst_write = wms;
        }
        if (n <= 0) {
            LOGE("probe: write #%d failed after %ums (sent %d KB)\r\n",
                 sent_ok, (unsigned)wms, sent_ok);
            snprintf(out, out_size,
                     "STALLED at chunk #%d after %ums; sent %d KB, "
                     "worst write %ums\r\n",
                     sent_ok, (unsigned)wms, sent_ok, (unsigned)worst_write);
            (void)_ssl_base_close(&ssl);
            return -1;
        }
        if ((sent_ok % 4) == 3) {
            LOGI("probe: %d KB sent, worst write %ums\r\n",
                 sent_ok + 1, (unsigned)worst_write);
        }
        rtos_delay_milliseconds(PROBE_CHUNK_GAP_MS);
    }

    {
        uint32_t total = rtos_get_time() - started;
        LOGI("probe: sent %d KB in %ums, worst write %ums\r\n",
             sent_ok, (unsigned)total, (unsigned)worst_write);
        snprintf(out, out_size,
                 "OK: sent %d KB in %ums, worst write %ums\r\n",
                 sent_ok, (unsigned)total, (unsigned)worst_write);
        rc = 0;
    }
    (void)_ssl_base_close(&ssl);
    return rc;
}
