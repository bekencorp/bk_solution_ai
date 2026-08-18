/**
 * @file tf_card.c
 *
 * On-board TF/SD card access via FATFS over SDIO1 (drive "1:"), plus a `tf` CLI
 * to list directories and files. Modelled on beken_robot's SD-NAND handling.
 *
 * Hardware (Robot V1): SDIO1 CLK/CMD/DATA0-3 on GPIO 14-19 (muxed by
 * usr_gpio_cfg.h, needs CONFIG_USR_GPIO_CFG_EN), card power on GPIO 53 (already
 * driven high at boot by robot_board_power_on() in ap_main.c). The first mount
 * triggers bk_sd_card_init() inside the FATFS disk glue.
 */

#include "tf_card.h"

#include <common/bk_include.h>
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <components/log.h>
#include "cli.h"
#include "ff.h"

#define TAG "tf_card"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define TF_DRIVE     "1:"     /* DISK_NUMBER_SDIO_SD */
#define TF_ROOT      "1:/"

static FATFS *s_fs;
static bool   s_mounted;

static int tf_card_mount(void)
{
    if (s_mounted) {
        return 0;
    }

    FATFS *fs = (FATFS *)os_malloc(sizeof(FATFS));
    if (fs == NULL) {
        LOGE("no memory for FATFS object\r\n");
        return -1;
    }

    /* opt=1: mount immediately (probe + init the card now) so errors surface
     * here rather than on the first file access. */
    FRESULT fr = f_mount(fs, TF_DRIVE, 1);
    if (fr != FR_OK) {
        LOGE("mount %s failed: FRESULT=%d (card inserted / formatted?)\r\n",
             TF_DRIVE, (int)fr);
        os_free(fs);
        return -1;
    }

    s_fs = fs;
    s_mounted = true;
    LOGI("mounted %s\r\n", TF_DRIVE);
    return 0;
}

static void tf_card_unmount(void)
{
    if (!s_mounted) {
        return;
    }
    (void)f_mount(NULL, TF_DRIVE, 0);
    os_free(s_fs);
    s_fs = NULL;
    s_mounted = false;
    LOGI("unmounted %s\r\n", TF_DRIVE);
}

/* Normalise a user-supplied path into a FATFS "1:/..." path. Accepts:
 *   (none)        -> "1:/"
 *   "1:/foo"      -> unchanged
 *   "/foo"        -> "1:/foo"
 *   "foo"         -> "1:/foo"
 */
static void tf_build_path(const char *in, char *out)
{
    const char *prefix;

    if (in == NULL || in[0] == '\0') {
        os_strcpy(out, TF_ROOT);
        return;
    }
    if (in[0] == '1' && in[1] == ':') {
        os_strcpy(out, in);
        return;
    }
    /* "/foo" -> "1:" + "/foo"; "foo" -> "1:/" + "foo" */
    prefix = (in[0] == '/') ? TF_DRIVE : TF_ROOT;
    os_strcpy(out, prefix);
    os_strcpy(out + os_strlen(prefix), in);
}

static void tf_card_ls(const char *path)
{
    DIR dir;
    FILINFO fno;
    uint32_t files = 0;
    uint32_t dirs = 0;

    FRESULT fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        LOGE("opendir '%s' failed: FRESULT=%d\r\n", path, (int)fr);
        return;
    }

    LOGI("%s\r\n", path);
    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) {
            LOGE("readdir failed: FRESULT=%d\r\n", (int)fr);
            break;
        }
        if (fno.fname[0] == '\0') {
            break;   /* end of directory */
        }
        if (fno.fattrib & AM_DIR) {
            LOGI("  <DIR>        %s\r\n", fno.fname);
            dirs++;
        } else {
            LOGI("  %10u   %s\r\n", (unsigned)fno.fsize, fno.fname);
            files++;
        }
    }
    (void)f_closedir(&dir);
    LOGI("total: %u dir(s), %u file(s)\r\n", (unsigned)dirs, (unsigned)files);
}

/* True if @s ends with @suf, comparing ASCII case-insensitively. */
static bool str_ends_with_ci(const char *s, const char *suf)
{
    size_t ls = os_strlen(s);
    size_t lf = os_strlen(suf);
    if (lf > ls) {
        return false;
    }
    const char *p = s + (ls - lf);
    for (size_t i = 0; i < lf; i++) {
        char a = p[i];
        char b = suf[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
        if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
        if (a != b) {
            return false;
        }
    }
    return true;
}

int tf_card_list_ext(const char *dir, const char *ext,
                     char out[][TF_PATH_MAX], int max)
{
    if (dir == NULL || ext == NULL || out == NULL || max <= 0) {
        return -1;
    }
    if (tf_card_mount() != 0) {
        return -1;
    }

    DIR d;
    FILINFO fno;
    FRESULT fr = f_opendir(&d, dir);
    if (fr != FR_OK) {
        LOGE("opendir '%s' failed: FRESULT=%d\r\n", dir, (int)fr);
        return -1;
    }

    size_t dlen = os_strlen(dir);
    bool has_slash = (dlen > 0U && dir[dlen - 1U] == '/');
    int n = 0;

    for (;;) {
        fr = f_readdir(&d, &fno);
        if (fr != FR_OK || fno.fname[0] == '\0') {
            break;   /* error or end of directory */
        }
        if (fno.fattrib & AM_DIR) {
            continue;
        }
        if (!str_ends_with_ci(fno.fname, ext)) {
            continue;
        }
        /* dir + optional '/' + name + NUL must fit TF_PATH_MAX. */
        if (dlen + (has_slash ? 0U : 1U) + os_strlen(fno.fname) + 1U > TF_PATH_MAX) {
            LOGW("skip '%s': path too long\r\n", fno.fname);
            continue;
        }
        if (n >= max) {
            LOGW("listing truncated at %d entries\r\n", max);
            break;
        }

        char full[TF_PATH_MAX];
        os_strcpy(full, dir);
        size_t w = dlen;
        if (!has_slash) {
            full[w++] = '/';
        }
        os_strcpy(full + w, fno.fname);

        /* Insertion sort into out[] so playback order is stable. */
        int pos = n;
        while (pos > 0 && os_strcmp(out[pos - 1], full) > 0) {
            os_strcpy(out[pos], out[pos - 1]);
            pos--;
        }
        os_strcpy(out[pos], full);
        n++;
    }

    (void)f_closedir(&d);
    return n;
}

static void cli_tf_cmd(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc >= 2 && os_strcmp(argv[1], "ls") == 0) {
        char path[TF_PATH_MAX];
        if (tf_card_mount() != 0) {
            return;
        }
        tf_build_path(argc >= 3 ? argv[2] : NULL, path);
        tf_card_ls(path);
        return;
    }
    if (argc >= 2 && os_strcmp(argv[1], "mount") == 0) {
        (void)tf_card_mount();
        return;
    }
    if (argc >= 2 && os_strcmp(argv[1], "unmount") == 0) {
        tf_card_unmount();
        return;
    }

    LOGI("usage: tf ls [path] | tf mount | tf unmount\r\n");
}

/* Auto-register via the linker .cli_cmdtabl section (kept regardless of CLI
 * init ordering). The object is pulled into the link by tf_card_init(). */
COMPONENTS_CLI_CMD_EXPORT
static const struct cli_command s_tf_commands[] = {
    {"tf", "tf ls [path] | tf mount | tf unmount", cli_tf_cmd},
};

void tf_card_init(void)
{
    /* Card power (GPIO 53) is already up via robot_board_power_on(). Mount once
     * at boot so `tf ls` is ready immediately; failure is non-fatal (no card or
     * unformatted -- the `tf mount` CLI can retry later). */
    if (tf_card_mount() != 0) {
        LOGW("TF card not mounted at boot; use `tf mount` after inserting/formatting\r\n");
    }
}
