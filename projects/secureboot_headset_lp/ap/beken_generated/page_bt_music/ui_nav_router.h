/**
 * @file ui_nav_router.h
 * @brief Headset stub of the robot key-nav router. The headset has no S-keys
 *        and never leaves the music page, so registration is a no-op; the
 *        ops struct is kept so page_bt_music.c compiles unchanged.
 */
#pragma once

#include "lvgl.h"
#include "beken_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void (*on_focus_prev)(bk_lv_ui_t *ui);
    void (*on_focus_next)(bk_lv_ui_t *ui);
    void (*on_screen_prev)(bk_lv_ui_t *ui);
    void (*on_screen_next)(bk_lv_ui_t *ui);
} ui_page_nav_ops_t;

static inline int ui_nav_register_screen(lv_obj_t *scr, const ui_page_nav_ops_t *ops)
{
    (void)scr; (void)ops;
    return 0;
}

static inline void ui_nav_unregister_screen(lv_obj_t *scr)
{
    (void)scr;
}

#ifdef __cplusplus
}
#endif
