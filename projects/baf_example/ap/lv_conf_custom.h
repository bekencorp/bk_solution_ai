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

#endif /* PROJECT_LV_CONF_CUSTOM_H */
