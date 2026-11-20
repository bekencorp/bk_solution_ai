/**
 * @file muse_gadget.c
 * @brief Headless CLI wrapper for the experimental Muse Gadget service.
 */
#include "demo/muse_gadget.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <sys/time.h>
#include <driver/aon_rtc.h>

#include "bk_cli.h"
#include "cli.h"
#include <components/log.h>
#include <os/os.h>        /* rtos_get_psram_*_heap_size() for `muse mem` */

#include "muse_commands.h"
#include "muse_service.h"
#include "muse_vm_api.h"
#if CONFIG_MUSE_GADGET_VOICE
#include "muse_tls_probe.h"
#include "muse_voice.h"
#include "camera_preview.h"
#endif

#define TAG "muse_gadget"
#define MUSE_O_RDONLY 0
#ifndef CONFIG_MUSE_GADGET_CA_PATH
#define CONFIG_MUSE_GADGET_CA_PATH "/sd0/muse_ca.pem"
#endif

extern int bk_vfs_open(const char *path, int oflag);
extern int bk_vfs_close(int fd);
extern ssize_t bk_vfs_read(int fd, void *buf, size_t count);

static int muse_load_ca_file(const char *path)
{
    char *pem;
    size_t used = 0;
    int fd;
    int rc = -1;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    fd = bk_vfs_open(path, MUSE_O_RDONLY);
    if (fd < 0) {
        /* Not fatal: the service falls back to the built-in public root CAs. */
        BK_LOGI(TAG, "no SD CA at %s; using built-in roots\r\n", path);
        return -1;
    }
    pem = (char *)malloc(MUSE_VM_API_CA_CERT_MAX);
    if (pem == NULL) {
        (void)bk_vfs_close(fd);
        return -1;
    }
    while (used < MUSE_VM_API_CA_CERT_MAX - 1U) {
        ssize_t n = bk_vfs_read(fd, pem + used,
                                MUSE_VM_API_CA_CERT_MAX - 1U - used);
        if (n < 0) {
            goto done;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
    }
    if (used == 0U) {
        goto done;
    }
    if (used == MUSE_VM_API_CA_CERT_MAX - 1U) {
        char extra;
        if (bk_vfs_read(fd, &extra, 1U) != 0) {
            goto done;
        }
    }
    pem[used] = '\0';
    rc = muse_service_set_ca_cert(pem, used + 1U);

done:
    (void)bk_vfs_close(fd);
    free(pem);
    if (rc == 0) {
        BK_LOGI(TAG, "CA loaded from %s\r\n", path);
    }
    return rc;
}

static void muse_cli_handler(char *out, int out_len, int argc, char **argv)
{
    muse_service_status_t status;
    int rc = -1;

    if (argc < 2 || strcmp(argv[1], "status") == 0) {
        muse_service_get_status(&status);
        snprintf(out, (size_t)out_len,
                 "state=%s paired=%u wifi=%u noise=%u error=%d\r\n",
                 muse_service_state_name(status.state), status.paired,
                 status.network_ready, status.noise_connected,
                 status.last_error);
        return;
    }
    if (strcmp(argv[1], "start") == 0) {
        (void)muse_load_ca_file(CONFIG_MUSE_GADGET_CA_PATH);
        rc = muse_service_start();
    } else if (strcmp(argv[1], "stop") == 0) {
        rc = muse_service_stop();
    } else if (strcmp(argv[1], "confirm") == 0) {
        rc = muse_service_confirm_pairing();
    } else if (strcmp(argv[1], "clear") == 0) {
        rc = muse_service_clear_pairing();
    } else if (strcmp(argv[1], "newid") == 0) {
        /* Escape a dead VM binding: advertise/pair as a new device so the
         * backend allocates a fresh VM instead of the same broken one. */
        uint32_t salt = 0;
        rc = muse_service_new_identity(&salt);
        if (rc == 0) {
            snprintf(out, (size_t)out_len,
                     "new identity MuseGadget-%02X%02X%02X; pairing cleared, "
                     "run 'muse start' and re-pair from the app\r\n",
                     (unsigned)((salt >> 16) & 0xffU),
                     (unsigned)((salt >> 8) & 0xffU),
                     (unsigned)(salt & 0xffU));
            return;
        }
    } else if (strcmp(argv[1], "ca-load") == 0) {
        rc = muse_load_ca_file(argc >= 3 ? argv[2]
                                         : CONFIG_MUSE_GADGET_CA_PATH);
    } else if (strcmp(argv[1], "token") == 0) {
        if (argc < 3 || argv[2][0] == '\0') {
            snprintf(out, (size_t)out_len,
                     "usage: muse token <mgst_...>\r\n");
            return;
        }
        rc = muse_service_set_sdk_token(argv[2]);
    } else if (strcmp(argv[1], "asrkey") == 0) {
#if CONFIG_MUSE_GADGET_VOICE
        if (argc < 3 || argv[2][0] == '\0') {
            snprintf(out, (size_t)out_len,
                     "usage: muse asrkey <Access-Token>\r\n");
            return;
        }
        rc = muse_voice_set_volc_asr_key(argv[2]);
#else
        snprintf(out, (size_t)out_len, "voice not enabled\r\n");
        return;
#endif
    } else if (strcmp(argv[1], "ttskey") == 0) {
#if CONFIG_MUSE_GADGET_VOICE
        if (argc < 3 || argv[2][0] == '\0') {
            snprintf(out, (size_t)out_len,
                     "usage: muse ttskey <Access-Token>\r\n");
            return;
        }
        rc = muse_voice_set_volc_tts_key(argv[2]);
#else
        snprintf(out, (size_t)out_len, "voice not enabled\r\n");
        return;
#endif
    } else if (strcmp(argv[1], "volcappid") == 0) {
#if CONFIG_MUSE_GADGET_VOICE
        if (argc < 3 || argv[2][0] == '\0') {
            snprintf(out, (size_t)out_len,
                     "usage: muse volcappid <APP-ID>\r\n");
            return;
        }
        rc = muse_voice_set_volc_appid(argv[2]);
#else
        snprintf(out, (size_t)out_len, "voice not enabled\r\n");
        return;
#endif
    } else if (strcmp(argv[1], "asrres") == 0) {
#if CONFIG_MUSE_GADGET_VOICE
        if (argc < 3 || argv[2][0] == '\0') {
            snprintf(out, (size_t)out_len,
                     "usage: muse asrres <resource-id>\r\n");
            return;
        }
        rc = muse_voice_set_volc_asr_res(argv[2]);
#else
        snprintf(out, (size_t)out_len, "voice not enabled\r\n");
        return;
#endif
    } else if (strcmp(argv[1], "ttsvoice") == 0) {
#if CONFIG_MUSE_GADGET_VOICE
        if (argc < 3 || argv[2][0] == '\0') {
            snprintf(out, (size_t)out_len,
                     "usage: muse ttsvoice <speaker-id>\r\n");
            return;
        }
        rc = muse_voice_set_volc_tts_voice(argv[2]);
#else
        snprintf(out, (size_t)out_len, "voice not enabled\r\n");
        return;
#endif
    } else if (strcmp(argv[1], "mem") == 0) {
        /* PSRAM heap watermarks: `free` is the current headroom of the
         * 3.4 MB general heap (psram_malloc), `min` the worst level seen
         * since boot -- the number that matters for alloc failures. */
        snprintf(out, (size_t)out_len,
                 "psram heap: total=%u free=%u min=%u (bytes)\r\n",
                 (unsigned)rtos_get_psram_total_heap_size(),
                 (unsigned)rtos_get_psram_free_heap_size(),
                 (unsigned)rtos_get_psram_minimum_free_heap_size());
        return;
    } else if (strcmp(argv[1], "register-json") == 0) {
        rc = muse_commands_build_register(
            "bk7259", "00000000-0000-4000-8000-000000000000",
            out, (size_t)out_len);
        if (rc >= 0) {
            return;
        }
#if CONFIG_MUSE_GADGET_VOICE
    } else if (strcmp(argv[1], "voice") == 0) {
        if (argc >= 3 && strcmp(argv[2], "start") == 0) {
            rc = muse_voice_ptt_start();
        } else if (argc >= 3 && strcmp(argv[2], "stop") == 0) {
            rc = muse_voice_ptt_stop();
        } else if (argc < 3 || strcmp(argv[2], "toggle") == 0) {
            rc = muse_voice_ptt_toggle();
        } else {
            snprintf(out, (size_t)out_len,
                     "usage: muse voice [start|stop|toggle]\r\n");
            return;
        }
        snprintf(out, (size_t)out_len, "%s (recording=%u)\r\n",
                 rc == 0 ? "OK" : "ERROR",
                 (unsigned)muse_voice_recording());
        return;
    } else if (strcmp(argv[1], "photo") == 0) {
        /* Test hook: freeze the running camera preview, capture one JPEG
         * and upload it to the Muse chat for recognition. No-op with an
         * error line when the preview is not running. */
        rc = camera_preview_take_photo();
        if (rc != 0) {
            snprintf(out, (size_t)out_len,
                     "ERROR take_photo %d (camera preview running?)\r\n", rc);
            return;
        }
        snprintf(out, (size_t)out_len,
                 "OK photo capture armed; JPEG will upload when ready\r\n");
        return;
    } else if (strcmp(argv[1], "probe") == 0) {
        /* Uplink stall forensics: `muse probe <seconds>` blasts ~4 KB/s of
         * dummy body to the noise host over a plain TLS POST. */
        int seconds = argc >= 3 ? atoi(argv[2]) : 10;
        snprintf(out, (size_t)out_len, "probe running %ds...\r\n", seconds);
        (void)muse_tls_probe_run(seconds, out + strlen(out),
                                 (size_t)out_len - strlen(out));
        return;
#endif
    } else if (strcmp(argv[1], "time") == 0) {
        /* Diagnostic: show what the device believes "now" is, so TLS
         * EXPIRED/FUTURE verify failures can be attributed to a wrong
         * clock without a full provisioning run. bk_rtc_gettimeofday is
         * the same time base the SDK time()/mbedTLS path reads. */
        struct timeval tv;
        if (bk_rtc_gettimeofday(&tv, NULL) != BK_OK) {
            snprintf(out, (size_t)out_len, "rtc read failed\r\n");
            return;
        }
        time_t now = (time_t)tv.tv_sec;
        struct tm *tm = gmtime(&now);
        if (tm != NULL) {
            snprintf(out, (size_t)out_len,
                     "device now: %04d-%02d-%02d %02d:%02d:%02d UTC "
                     "(epoch=%lu)\r\n",
                     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                     tm->tm_hour, tm->tm_min, tm->tm_sec,
                     (unsigned long)now);
        } else {
            snprintf(out, (size_t)out_len,
                     "device now: epoch=%lu (no gmtime)\r\n",
                     (unsigned long)now);
        }
        return;
    } else {
        snprintf(out, (size_t)out_len,
                 "usage: muse [start|stop|status|confirm|clear|newid|ca-load [path]|token <mgst_...>|volcappid <id>|asrkey <key>|ttskey <key>|asrres <rid>|ttsvoice <spk>|mem|register-json|time|voice [start|stop|toggle]|probe [seconds]]\r\n");
        return;
    }
    snprintf(out, (size_t)out_len, "%s\r\n", rc == 0 ? "OK" : "ERROR");
}

static const struct cli_command s_muse_commands[] = {
    {"muse", "muse [start|stop|status|confirm|clear|newid|ca-load [path]|token <mgst_...>|volcappid <id>|asrres <rid>|ttsvoice <spk>|mem|register-json|time|voice [start|stop|toggle]|probe [seconds]]",
     muse_cli_handler},
};

int muse_gadget_init(void)
{
    int rc = muse_service_init();
    if (rc != 0) {
        return rc;
    }
    rc = cli_register_commands(s_muse_commands,
                               sizeof(s_muse_commands) /
                               sizeof(s_muse_commands[0]));
    if (rc == 0) {
        BK_LOGI(TAG, "CLI registered; use 'muse start'\r\n");
    }
    /* beken_muse is a Muse-first product: start the service at boot so a
     * paired box connects on its own and an unpaired one enters BLE
     * pairing without any CLI. (beken_robot kept this manual.) */
    if (muse_service_start() == 0) {
        BK_LOGI(TAG, "muse service auto-started\r\n");
    } else {
        BK_LOGW(TAG, "muse service auto-start failed; use 'muse start'\r\n");
    }
    return rc;
}

int muse_gadget_start(void)
{
    return muse_service_start();
}

int muse_gadget_stop(void)
{
    return muse_service_stop();
}

const bk_demo_iface_t g_demo_muse_gadget = {
    "muse_gadget",
    muse_gadget_init,
    muse_gadget_start,
    muse_gadget_stop,
};
