#ifndef PROJECT_LV_CONF_CUSTOM_H
#define PROJECT_LV_CONF_CUSTOM_H

/*
 * Project-level LVGL config.
 * Include the default component config first, then override only the
 * options needed by this project.
 */
#include "lv_conf.h"

#undef LV_USE_SNAPSHOT
#define LV_USE_SNAPSHOT 1

/*
 * PSRAM 堆长期运行后会碎片化（websocket/TLS 反复连接），LVGL 一次
 * ~2KB 的渐变缓存分配都可能失败。渐变绘制路径对 NULL 是安全的
 * （allocate_item -> lv_draw_sw_grad_get 返回 NULL 后按纯色回退），
 * 但默认 LV_ASSERT_MALLOC 会 BK_ASSERT(0) 直接 coredump
 * （field 2026-11-04: 进状态页时 dump at allocate_item:112）。
 * 关掉它，让 OOM 走优雅回退。
 */
#undef LV_USE_ASSERT_MALLOC
#define LV_USE_ASSERT_MALLOC 0

#endif /* PROJECT_LV_CONF_CUSTOM_H */
