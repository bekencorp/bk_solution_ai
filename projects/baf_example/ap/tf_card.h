#ifndef TF_CARD_H
#define TF_CARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* Max length of a FATFS path this module produces (e.g. "1:/baf/name.baf"). */
#define TF_PATH_MAX  128

/* On-board TF/SD card (SDIO1, FATFS drive "1:") support.
 *
 * Call once from main() (both RAW and LVGL builds). It mounts the card and
 * registers the `tf` CLI so the user can list directories/files:
 *   tf ls [path]     - list a directory (default "1:/")
 *   tf mount         - (re)mount the card
 *   tf unmount       - unmount the card
 *
 * Card power is GPIO 53, already driven high by robot_board_power_on(); the
 * SDIO1 pins (GPIO 14-19) are muxed via usr_gpio_cfg.h (CONFIG_USR_GPIO_CFG_EN).
 */
void tf_card_init(void);

/* List the regular files under @p dir whose name ends with @p ext (matched
 * case-insensitively, e.g. ".baf"). Full "dir/name" paths are written into
 * out[0..n-1] (each buffer TF_PATH_MAX bytes), sorted ascending by name so the
 * order is stable across boots. Mounts the card if needed. Returns the number of
 * entries written (<= max), or -1 on error (mount / opendir failure). */
int tf_card_list_ext(const char *dir, const char *ext,
                     char out[][TF_PATH_MAX], int max);

#ifdef __cplusplus
}
#endif

#endif /* TF_CARD_H */
