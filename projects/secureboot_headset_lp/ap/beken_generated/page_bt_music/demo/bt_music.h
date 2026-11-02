/**
 * @file demo/bt_music.h
 * @brief Headset stub of the robot demo-registry handle. page_bt_music.c
 *        calls g_demo_bt_music.stop() on page exit; the single-page headset
 *        product never exits the page, so the handle is an empty shell.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    int (*init)(void);
    int (*start)(void);
    int (*stop)(void);
} bk_demo_iface_t;

extern const bk_demo_iface_t g_demo_bt_music;

#ifdef __cplusplus
}
#endif
