/**
 * @file board_usb_switch.c
 * @brief Board-level helpers for BK7259 Robot V1 AI kit:
 *          - SD-NAND power gate (NAND_VDD via P53)
 *          - Type-C USB mux between CH340 UART and BK7259 USB (P54 / SW_SEL)
 *
 * See board_usb_switch.h for the schematic-level rationale and CLI usage.
 */
#include <common/sys_config.h>
#include <common/bk_err.h>
#include <components/log.h>
#include <os/os.h>
#include <os/mem.h>

#include "board_usb_switch.h"

#include <driver/gpio.h>
#include "gpio_driver.h"

#if CONFIG_CLI
#include "cli.h"
#endif

#if CONFIG_FATFS
#include "ff.h"
#endif

/* USB Device MSC entry points (CherryUSB).
 *
 * On BK7259 USB Host vs USB Device is a compile-time choice (the device-
 * mode mhdrc driver hits AP-side USB IRQ regs that only exist when
 * USB_RISCV_BRIDGE is OFF), so the Robot V1 firmware is built device-only
 * (see defconfig). Once `usbsw usb` flips the FSW3157A mux to BK7259 USB
 * we ALSO need to call msc_storage_init() on the AP, otherwise CherryUSB
 * never registers the MSC descriptors and the PC enumerates nothing. */
#if CONFIG_USB && CONFIG_USBD_MSC
extern int msc_storage_init(void);
extern int msc_storage_deinit(void);
#define BOARD_HAVE_USB_MSC 1
#else
#define BOARD_HAVE_USB_MSC 0
#endif

#include <string.h>
#include <stdlib.h>

#define TAG "board_usbsw"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

/* -----------------------------------------------------------------------
 * Compile-time fallback defaults (keep building even if Kconfig values
 * are stripped out of a custom defconfig).
 * --------------------------------------------------------------------- */
#ifndef CONFIG_BOARD_SD_NAND_POWER_GPIO
#define CONFIG_BOARD_SD_NAND_POWER_GPIO   53
#endif

#ifndef CONFIG_BOARD_USB_SWITCH_GPIO
#define CONFIG_BOARD_USB_SWITCH_GPIO      54
#endif

/* GPIO level that routes Type-C to BK7259 USB (per FSW3157A truth table). */
#ifndef CONFIG_BOARD_USB_SWITCH_USB_LEVEL
#define CONFIG_BOARD_USB_SWITCH_USB_LEVEL 1
#endif

#define USB_SW_LEVEL_USB   (CONFIG_BOARD_USB_SWITCH_USB_LEVEL ? 1 : 0)
#define USB_SW_LEVEL_UART  (CONFIG_BOARD_USB_SWITCH_USB_LEVEL ? 0 : 1)

/* -----------------------------------------------------------------------
 *  Low-level GPIO helper: claim a free GPIO, disable pulls, drive a level.
 * --------------------------------------------------------------------- */
static bk_err_t board_drive_gpio(gpio_id_t gpio_id, uint32_t level)
{
    /* Detach from any peripheral pinmux (best effort, ignore errors when
     * the pin was already free). */
    (void)gpio_dev_unmap(gpio_id);

    bk_err_t err = bk_gpio_disable_pull(gpio_id);
    if (err != BK_OK) {
        LOGE("GPIO_%u: disable_pull failed (err=%d)\n", (unsigned)gpio_id, err);
        return err;
    }

    err = bk_gpio_enable_output(gpio_id);
    if (err != BK_OK) {
        LOGE("GPIO_%u: enable_output failed (err=%d)\n", (unsigned)gpio_id, err);
        return err;
    }

    err = (level ? bk_gpio_set_output_high(gpio_id)
                 : bk_gpio_set_output_low(gpio_id));
    if (err != BK_OK) {
        LOGE("GPIO_%u: drive %u failed (err=%d)\n",
             (unsigned)gpio_id, (unsigned)level, err);
        return err;
    }
    return BK_OK;
}

/* =======================================================================
 *  SD-NAND power gate (NAND_VDD)
 * ===================================================================== */

#if CONFIG_BOARD_SD_NAND_ENABLE

bk_err_t board_sd_nand_power_on(void)
{
    bk_err_t err = board_drive_gpio(CONFIG_BOARD_SD_NAND_POWER_GPIO, 1);
    if (err == BK_OK) {
        LOGI("SD-NAND power ON (GPIO_%d = HIGH)\n",
             CONFIG_BOARD_SD_NAND_POWER_GPIO);
    }
    return err;
}

bk_err_t board_sd_nand_power_off(void)
{
    bk_err_t err = board_drive_gpio(CONFIG_BOARD_SD_NAND_POWER_GPIO, 0);
    if (err == BK_OK) {
        LOGI("SD-NAND power OFF (GPIO_%d = LOW)\n",
             CONFIG_BOARD_SD_NAND_POWER_GPIO);
    }
    return err;
}

/* -----------------------------------------------------------------------
 *  `sdnand` CLI: power-cycled SD-NAND sanity test.
 *
 *  Keeping NAND_VDD permanently asserted at boot would add static
 *  current draw even when the application never touches the chip, so
 *  the Robot V1 firmware leaves SD-NAND OFF by default. This CLI lets
 *  the developer (or a manufacturing-line script) prove that the chip
 *  is alive end-to-end and ALWAYS powers it back down on exit, even
 *  when a step fails.
 *
 *  Disk number 1 (DISK_NUMBER_SDIO_SD in diskio.h) is the SD-card slot,
 *  which on this board is wired to the on-board SD-NAND (U14).
 * --------------------------------------------------------------------- */
#if CONFIG_CLI && CONFIG_FATFS

#define SDNAND_DRIVE_PATH         "1:"
#define SDNAND_TEST_FILE          "1:/sdnand_test.bin"
#define SDNAND_TEST_PAYLOAD       256u
#define SDNAND_POWER_SETTLE_MS    50u
#define SDNAND_POWER_OFF_DELAY_MS 20u
#define SDNAND_MKFS_BUF_SIZE      4096u

static int sdnand_fill_pattern(uint8_t *buf, uint32_t len)
{
    static const char kMagic[] = "SDNAND-TEST-";
    const uint32_t magic_len = sizeof(kMagic) - 1;

    if (len <= magic_len) {
        return -1;
    }
    os_memcpy(buf, kMagic, magic_len);
    for (uint32_t i = magic_len; i < len; i++) {
        buf[i] = (uint8_t)((i - magic_len) ^ 0xA5);
    }
    return 0;
}

static int sdnand_run_sanity_test(void)
{
    /* FATFS (~600 B) and FIL (~580 B) are intentionally placed on the
     * heap, not the stack: the CLI task only has CONFIG_CLI_TASK_STACK_SIZE
     * (7 KB) and putting either object on the stack overflows it
     * (seen empirically as "Current Task stack overflow: cli" on the
     * very first call). The SDK's own test_fatfs.c follows the same
     * heap-allocated convention for the same reason. */
    FATFS   *fs = NULL;
    FIL     *fp = NULL;
    FRESULT  fr;
    uint8_t *tx = NULL;
    uint8_t *rx = NULL;
    UINT bw = 0, br = 0;
    int  mounted = 0;
    int  rc = -1;

    LOGI("[sdnand-test] BEGIN  (disk=%s, file=%s, payload=%u B)\n",
         SDNAND_DRIVE_PATH, SDNAND_TEST_FILE, (unsigned)SDNAND_TEST_PAYLOAD);

    /* 1. Power the chip up; give NAND_VDD a moment to settle before the
     *    SDIO controller starts CMD0/CMD8 enumeration. */
    if (board_sd_nand_power_on() != BK_OK) {
        LOGE("[sdnand-test] power_on FAILED\n");
        return -1;
    }
    rtos_delay_milliseconds(SDNAND_POWER_SETTLE_MS);

    /* 2. Allocate the FATFS / FIL state structs and the two scratch
     *    buffers on the heap. 256 B each for tx/rx is also too big to
     *    comfortably sit on the small CLI thread stack. */
    fs = (FATFS *)os_malloc(sizeof(FATFS));
    fp = (FIL *)os_malloc(sizeof(FIL));
    tx = (uint8_t *)os_malloc(SDNAND_TEST_PAYLOAD);
    rx = (uint8_t *)os_malloc(SDNAND_TEST_PAYLOAD);
    if (!fs || !fp || !tx || !rx) {
        LOGE("[sdnand-test] os_malloc FAILED\n");
        goto out_poweroff;
    }
    if (sdnand_fill_pattern(tx, SDNAND_TEST_PAYLOAD) != 0) {
        LOGE("[sdnand-test] pattern fill FAILED\n");
        goto out_poweroff;
    }

    /* 3. Mount. A virgin SD-NAND comes from the factory unformatted, so
     *    treat FR_NO_FILESYSTEM as "format once and retry" rather than a
     *    hard failure -- this lets the very first run on a new board
     *    still pass. Any other error is fatal. */
    fr = f_mount(fs, SDNAND_DRIVE_PATH, 1);
    if (fr == FR_NO_FILESYSTEM) {
        LOGW("[sdnand-test] FR_NO_FILESYSTEM -> running f_mkfs ...\n");
        uint8_t *mkfs_buf = (uint8_t *)os_malloc(SDNAND_MKFS_BUF_SIZE);
        if (!mkfs_buf) {
            LOGE("[sdnand-test] mkfs buffer alloc FAILED\n");
            goto out_poweroff;
        }
        /* au=0 -> let FatFs pick a sensible cluster size automatically. */
        fr = f_mkfs(SDNAND_DRIVE_PATH, FM_ANY, 0, mkfs_buf, SDNAND_MKFS_BUF_SIZE);
        os_free(mkfs_buf);
        if (fr != FR_OK) {
            LOGE("[sdnand-test] f_mkfs FAILED fr=%d\n", fr);
            goto out_poweroff;
        }
        fr = f_mount(fs, SDNAND_DRIVE_PATH, 1);
    }
    if (fr != FR_OK) {
        LOGE("[sdnand-test] f_mount FAILED fr=%d\n", fr);
        goto out_poweroff;
    }
    mounted = 1;
    LOGI("[sdnand-test] mounted %s\n", SDNAND_DRIVE_PATH);

    /* 4. Write the pattern. */
    fr = f_open(fp, SDNAND_TEST_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        LOGE("[sdnand-test] f_open(WR) FAILED fr=%d\n", fr);
        goto out_unmount;
    }
    fr = f_write(fp, tx, SDNAND_TEST_PAYLOAD, &bw);
    (void)f_close(fp);
    if (fr != FR_OK || bw != SDNAND_TEST_PAYLOAD) {
        LOGE("[sdnand-test] f_write FAILED fr=%d bw=%u\n", fr, (unsigned)bw);
        goto out_unmount;
    }
    LOGI("[sdnand-test] wrote %u bytes -> %s\n",
         (unsigned)bw, SDNAND_TEST_FILE);

    /* 5. Read back and verify. */
    fr = f_open(fp, SDNAND_TEST_FILE, FA_READ);
    if (fr != FR_OK) {
        LOGE("[sdnand-test] f_open(RD) FAILED fr=%d\n", fr);
        goto out_unmount;
    }
    os_memset(rx, 0, SDNAND_TEST_PAYLOAD);
    fr = f_read(fp, rx, SDNAND_TEST_PAYLOAD, &br);
    (void)f_close(fp);
    if (fr != FR_OK || br != SDNAND_TEST_PAYLOAD) {
        LOGE("[sdnand-test] f_read FAILED fr=%d br=%u\n", fr, (unsigned)br);
        goto out_unmount;
    }
    if (os_memcmp(tx, rx, SDNAND_TEST_PAYLOAD) != 0) {
        LOGE("[sdnand-test] DATA MISMATCH (first byte tx=0x%02x rx=0x%02x)\n",
             tx[0], rx[0]);
        goto out_unmount;
    }
    LOGI("[sdnand-test] read-back PASS (%u bytes match)\n", (unsigned)br);

    /* 6. Tidy up the scratch file so repeated `sdnand test` runs don't
     *    leak space. Non-fatal -- a failed unlink doesn't fail the test
     *    because the data round-trip already proved the chip is alive. */
    fr = f_unlink(SDNAND_TEST_FILE);
    if (fr != FR_OK) {
        LOGW("[sdnand-test] f_unlink fr=%d (non-fatal)\n", fr);
    } else {
        LOGI("[sdnand-test] removed %s\n", SDNAND_TEST_FILE);
    }

    rc = 0;

out_unmount:
    if (mounted) {
        fr = f_unmount(1, SDNAND_DRIVE_PATH, 0);
        if (fr != FR_OK) {
            LOGW("[sdnand-test] f_unmount fr=%d\n", fr);
        } else {
            LOGI("[sdnand-test] unmounted %s\n", SDNAND_DRIVE_PATH);
        }
    }

out_poweroff:
    if (fp) { os_free(fp); }
    if (fs) { os_free(fs); }
    if (tx) { os_free(tx); }
    if (rx) { os_free(rx); }

    /* Always cut NAND_VDD on exit, regardless of success or failure, to
     * keep the static SD-NAND current draw out of the system idle budget.
     * The short delay lets in-flight SDIO transactions drain before the
     * rail goes away. */
    rtos_delay_milliseconds(SDNAND_POWER_OFF_DELAY_MS);
    (void)board_sd_nand_power_off();

    LOGI("[sdnand-test] END    -> %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}

static void cli_sdnand_help(void)
{
    LOGI("usage:\n");
    LOGI("  sdnand test   - run a power-cycled write+read+verify sanity test\n");
    LOGI("                  (power_on -> mount -> write -> read -> unmount\n");
    LOGI("                   -> power_off, always cuts NAND_VDD on exit)\n");
    LOGI("  sdnand on     - drive NAND_VDD high  (manual debug; remember to off)\n");
    LOGI("  sdnand off    - drive NAND_VDD low\n");
}

static void cli_sdnand_cmd(char *pcWriteBuffer, int xWriteBufferLen,
                           int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2) {
        cli_sdnand_help();
        return;
    }

    const char *sub = argv[1];
    if (!strcmp(sub, "test")) {
        (void)sdnand_run_sanity_test();
    } else if (!strcmp(sub, "on")) {
        (void)board_sd_nand_power_on();
    } else if (!strcmp(sub, "off")) {
        (void)board_sd_nand_power_off();
    } else {
        LOGW("unknown subcommand '%s'\n", sub);
        cli_sdnand_help();
    }
}

static const struct cli_command s_sdnand_cmds[] = {
    {"sdnand", "sdnand test|on|off", cli_sdnand_cmd},
};

static bk_err_t board_sd_nand_cli_register(void)
{
    int rc = cli_register_commands(s_sdnand_cmds,
                                   sizeof(s_sdnand_cmds) /
                                   sizeof(s_sdnand_cmds[0]));
    if (rc != 0) {
        LOGE("sdnand CLI register failed (rc=%d)\n", rc);
        return BK_FAIL;
    }
    LOGI("sdnand CLI registered (\"sdnand test\" runs power-cycled sanity)\n");
    return BK_OK;
}

#else  /* !(CONFIG_CLI && CONFIG_FATFS) */

static bk_err_t board_sd_nand_cli_register(void) { return BK_OK; }

#endif /* CONFIG_CLI && CONFIG_FATFS */

#else  /* !CONFIG_BOARD_SD_NAND_ENABLE */

bk_err_t board_sd_nand_power_on(void)  { return BK_OK; }
bk_err_t board_sd_nand_power_off(void) { return BK_OK; }
static bk_err_t board_sd_nand_cli_register(void) { return BK_OK; }

#endif /* CONFIG_BOARD_SD_NAND_ENABLE */

/* =======================================================================
 *  USB Switch (P54 / SW_SEL)
 * ===================================================================== */

#if CONFIG_BOARD_USB_SWITCH_ENABLE

/* Tracks the *muxed* Type-C path. The USB-MSC init state below is kept in
 * sync with it whenever CONFIG_USBD_MSC is compiled in; otherwise the
 * MSC init/deinit calls degrade to no-ops. */
static volatile int s_usb_sw_in_usb_mode;  /* 0 = UART/CH340, 1 = BK7259 USB */
static volatile int s_usb_msc_initialized; /* 0 = MSC down, 1 = MSC up */

#if BOARD_HAVE_USB_MSC
static bk_err_t board_usb_msc_up(void)
{
    if (s_usb_msc_initialized) {
        return BK_OK;
    }
    LOGI("USB MSC: msc_storage_init() ...\n");
    int rc = msc_storage_init();
    if (rc != BK_OK) {
        LOGE("USB MSC: msc_storage_init failed (rc=%d)\n", rc);
        return BK_FAIL;
    }
    s_usb_msc_initialized = 1;
    LOGI("USB MSC: ready -> PC should now enumerate a removable disk\n");
    return BK_OK;
}

static bk_err_t board_usb_msc_down(void)
{
    if (!s_usb_msc_initialized) {
        return BK_OK;
    }
    LOGI("USB MSC: msc_storage_deinit() ...\n");
    int rc = msc_storage_deinit();
    if (rc != BK_OK) {
        LOGE("USB MSC: msc_storage_deinit failed (rc=%d)\n", rc);
        return BK_FAIL;
    }
    s_usb_msc_initialized = 0;
    return BK_OK;
}
#else
static bk_err_t board_usb_msc_up(void)   { return BK_OK; }
static bk_err_t board_usb_msc_down(void) { return BK_OK; }
#endif /* BOARD_HAVE_USB_MSC */

bk_err_t board_usb_switch_to_uart(void)
{
    /* Tear down MSC FIRST while the host is still wired, so the PC gets
     * a proper "disk safely removed" event instead of a yank. Then flip
     * the FSW3157A mux back to CH340 -- this also kills the UART log
     * coming from CH340, so the developer normally just reboots. */
    (void)board_usb_msc_down();

    bk_err_t err = board_drive_gpio(CONFIG_BOARD_USB_SWITCH_GPIO,
                                    USB_SW_LEVEL_UART);
    if (err == BK_OK) {
        s_usb_sw_in_usb_mode = 0;
        LOGI("USB-switch: Type-C -> CH340 UART (GPIO_%d = %u)\n",
             CONFIG_BOARD_USB_SWITCH_GPIO, USB_SW_LEVEL_UART);
    }
    return err;
}

bk_err_t board_usb_switch_prepare_nand_access(void)
{
    /* If the FSW3157A is still routed to BK7259 USB (and CherryUSB MSC
     * has the SD-NAND mounted), the AP-side FatFS cannot grab the chip
     * because the MSC layer holds the SDIO. board_usb_switch_to_uart()
     * tears MSC down and flips the mux back so subsequent f_mount on
     * drive 1 works. Idempotent when already in UART mode. */
    if (s_usb_sw_in_usb_mode) {
        return board_usb_switch_to_uart();
    }
    return BK_OK;
}

bk_err_t board_usb_switch_to_usb(void)
{
    board_sd_nand_power_on();

    /* Flip the physical mux first, then bring USB Device MSC up. Order
     * matters: msc_storage_init() pulls D+/D- and asks for enumeration,
     * but those lines are useless until the FSW3157A actually routes
     * Type-C to BK7259 USB. */
    bk_err_t err = board_drive_gpio(CONFIG_BOARD_USB_SWITCH_GPIO,
                                    USB_SW_LEVEL_USB);
    if (err != BK_OK) {
        return err;
    }
    s_usb_sw_in_usb_mode = 1;
    LOGI("USB-switch: Type-C -> BK7259 USB (GPIO_%d = %u)\n",
         CONFIG_BOARD_USB_SWITCH_GPIO, USB_SW_LEVEL_USB);
    LOGW("CH340 UART path is now disconnected from the Type-C port.\n");
    LOGW("Reset / power-cycle the board to fall back to UART mode.\n");

#if BOARD_HAVE_USB_MSC
    /* Best effort: even if MSC init fails, keep the GPIO flipped so the
     * developer can debug the failure with another USB tool. */
    (void)board_usb_msc_up();
#else
    LOGW("USB DEVICE / MSC not compiled in -> PC will see no U-disk.\n");
    LOGW("Enable CONFIG_USB_DEVICE=y + CONFIG_USBD_MSC=y in defconfig.\n");
#endif
    return BK_OK;
}

#if CONFIG_CLI

static void cli_usbsw_help(void)
{
    LOGI("usage:\n");
    LOGI("  usbsw usb     - route Type-C to BK7259 USB (e.g. MSC)\n");
    LOGI("  usbsw uart    - route Type-C to CH340 UART (default)\n");
    LOGI("  usbsw toggle  - flip the current mux state\n");
    LOGI("  usbsw status  - show current mux state\n");
}

static void cli_usbsw_status(void)
{
    LOGI("USB-switch: mux=%s (GPIO_%d, USB-level=%d), MSC=%s%s\n",
         s_usb_sw_in_usb_mode ? "BK7259 USB" : "CH340 UART",
         CONFIG_BOARD_USB_SWITCH_GPIO,
         CONFIG_BOARD_USB_SWITCH_USB_LEVEL,
         s_usb_msc_initialized ? "up" : "down",
         BOARD_HAVE_USB_MSC ? "" : " [USBD_MSC not compiled]");
}

static void cli_usbsw_cmd(char *pcWriteBuffer, int xWriteBufferLen,
                          int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2) {
        cli_usbsw_help();
        cli_usbsw_status();
        return;
    }

    const char *sub = argv[1];

    if (!strcmp(sub, "usb")) {
        (void)board_usb_switch_to_usb();
    } else if (!strcmp(sub, "uart")) {
        (void)board_usb_switch_to_uart();
    } else if (!strcmp(sub, "toggle")) {
        if (s_usb_sw_in_usb_mode) {
            (void)board_usb_switch_to_uart();
        } else {
            (void)board_usb_switch_to_usb();
        }
    } else if (!strcmp(sub, "status")) {
        cli_usbsw_status();
    } else {
        LOGW("unknown subcommand '%s'\n", sub);
        cli_usbsw_help();
    }
}

static const struct cli_command s_usbsw_cmds[] = {
    {"usbsw", "usbsw usb|uart|toggle|status", cli_usbsw_cmd},
};

static bk_err_t board_usb_switch_cli_register(void)
{
    int rc = cli_register_commands(s_usbsw_cmds,
                                   sizeof(s_usbsw_cmds) / sizeof(s_usbsw_cmds[0]));
    if (rc != 0) {
        LOGE("usbsw CLI register failed (rc=%d)\n", rc);
        return BK_FAIL;
    }
    LOGI("usbsw CLI registered (\"usbsw usb\" switches Type-C to BK7259 USB)\n");
    return BK_OK;
}

#else  /* !CONFIG_CLI */

static bk_err_t board_usb_switch_cli_register(void) { return BK_OK; }

#endif /* CONFIG_CLI */

bk_err_t board_usb_switch_init(void)
{
    /* Default boot state: route Type-C to CH340 UART so the developer
     * keeps log + bootloader download working out of the box. */
    bk_err_t err = board_usb_switch_to_uart();
    if (err != BK_OK) {
        return err;
    }
    (void)board_usb_switch_cli_register();
    /* SD-NAND is left powered OFF at boot to save the static NAND_VDD
     * draw -- the `sdnand` CLI brings it up only on demand. */
    return board_sd_nand_cli_register();
}

#else  /* !CONFIG_BOARD_USB_SWITCH_ENABLE */

bk_err_t board_usb_switch_init(void)     { return board_sd_nand_cli_register(); }
bk_err_t board_usb_switch_to_usb(void)   { return BK_OK; }
bk_err_t board_usb_switch_to_uart(void)  { return BK_OK; }
bk_err_t board_usb_switch_prepare_nand_access(void) { return BK_OK; }

#endif /* CONFIG_BOARD_USB_SWITCH_ENABLE */
