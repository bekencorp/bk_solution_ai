#ifndef PROJECT_LV_CONF_CUSTOM_H
#define PROJECT_LV_CONF_CUSTOM_H

/*
 * Project-level LVGL config.
 * Include the default component config first, then override only the
 * options needed by this project.
 */
#include "lv_conf.h"

#undef LV_USE_DEMO_WIDGETS
#define LV_USE_DEMO_WIDGETS 0

#undef LV_USE_BAF
#define LV_USE_BAF 1

/* lv_fs FATFS driver so lv_baf_set_src_file("S:/baf/xxx.baf") can read the TF
 * card. Drive letter 'S' maps to the FATFS SD volume "1:" via LV_FS_FATFS_PATH. */
#undef LV_USE_FS_FATFS
#define LV_USE_FS_FATFS 1
#undef LV_FS_FATFS_LETTER
#define LV_FS_FATFS_LETTER 'S'
#undef LV_FS_FATFS_PATH
#define LV_FS_FATFS_PATH "1:"

#endif /* PROJECT_LV_CONF_CUSTOM_H */
