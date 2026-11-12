/**
 * @file muse_display.h
 * @brief display.draw_url / display.show_animation command support.
 *
 * Mirrors the upstream esp32 contract (noise_control.cpp display commands +
 * image_fetch): download an image over http/https, decode a baseline JPEG
 * (shrunk 1/2, 1/4, 1/8 to fit) or raw RGB565, draw it centred on the
 * 320x385 RGB565 panel, then report {format,width,height,scale,bytes,ms}.
 */
#ifndef __BK_MUSE_DISPLAY_H__
#define __BK_MUSE_DISPLAY_H__

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MUSE_DISPLAY_ERR_MAX   96
#define MUSE_DISPLAY_CODE_MAX  32

/* Full-CJK UI font (U+4E00-9FFF + punctuation, 16 px) with montserrat_14
 * chained for ASCII. Use this instead of the lv_font_ali_* glyph subsets
 * for any page that shows arbitrary Chinese text. */
lv_font_t *muse_cjk_font(void);

/* 30 px title font: lv_font_ali_30 with muse_font_cjk_30 chained as
 * fallback so 30 px CJK title glyphs (助/记/...) also render. */
lv_font_t *muse_title_font(void);

typedef struct {
    bool ok;
    char format[8];     /* "jpeg" | "rgb565" */
    int width;          /* drawn pixels */
    int height;
    int scale;          /* 1, 2, 4 or 8 (1 for raw) */
    int bytes;          /* downloaded body bytes */
    int ms;             /* fetch+decode+draw time */
    char err_code[MUSE_DISPLAY_CODE_MAX];
    char err_msg[MUSE_DISPLAY_ERR_MAX];
} muse_display_result_t;

/* row < 0 centres the image vertically. Always fills *result. */
int muse_display_draw_url(const char *url, int row, muse_display_result_t *result);

/* Clears the drawn overlay and returns to the normal UI. */
int muse_display_show_animation(void);

/* Voice reply text overlay (push-to-talk): black full-screen panel with a
 * title and a wrapped body. Replaces any draw_url image overlay; callers
 * own the auto-dismiss timing. Returns 0 on success.
 *
 * While the camera preview owns the screen (LVGL stopped) a show/hide is
 * stashed instead of drawn; camera_preview calls
 * muse_display_flush_pending() once LVGL is running again. */
int muse_display_show_voice_text(const char *title, const char *body);
int muse_display_hide_voice_text(void);
int muse_display_flush_pending(void);

#ifdef __cplusplus
}
#endif

#endif
