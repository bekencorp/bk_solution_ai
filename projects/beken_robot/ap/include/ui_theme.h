/**
 * @file ui_theme.h
 * @brief Shared LVGL visual theme for the BK7259 robot UI.
 */
#ifndef __UI_THEME_H__
#define __UI_THEME_H__

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "beken_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_THEME_SAFE_LEFT      30
#define UI_THEME_SAFE_TOP       18
#define UI_THEME_SAFE_RIGHT     22
#define UI_THEME_SAFE_BOTTOM    18

#define UI_THEME_COLOR_BG_TOP       0xe7f3ff
#define UI_THEME_COLOR_BG_BOTTOM    0xdfeeff
#define UI_THEME_COLOR_CARD         0xffffff
#define UI_THEME_COLOR_CARD_ALT     0xf0f7ff
#define UI_THEME_COLOR_BORDER       0xb8d5ff
#define UI_THEME_COLOR_PRIMARY      0x1667ff
#define UI_THEME_COLOR_PRIMARY_2    0x38a8ff
#define UI_THEME_COLOR_SELECTED     0x1457d9
#define UI_THEME_COLOR_SELECTED_2   0x1d7bff
#define UI_THEME_COLOR_TAB_IDLE     0xc9dcf5
#define UI_THEME_COLOR_TAB_IDLE_2   0xd9e8fb
#define UI_THEME_COLOR_TAB_SEL      0x22d6b5
#define UI_THEME_COLOR_TAB_SEL_2    0x10b79a
#define UI_THEME_COLOR_ICON_BG      0xe1edff
#define UI_THEME_COLOR_ICON_FG      0x195fd0
#define UI_THEME_COLOR_TEAL         0x22d6b5
#define UI_THEME_COLOR_INK          0x102044
#define UI_THEME_COLOR_MUTED        0x687895
#define UI_THEME_COLOR_DESC         0x315f9e
#define UI_THEME_COLOR_HINT         0x8a98ad
#define UI_THEME_COLOR_RED          0xd84b4b
#define UI_THEME_COLOR_ONLINE_BG    0xe8fbf6
#define UI_THEME_COLOR_ONLINE_TEXT  0x17b89d

typedef enum {
    UI_THEME_BUTTON_PRIMARY = 0,
    UI_THEME_BUTTON_DANGER,
    UI_THEME_BUTTON_NEUTRAL,
} ui_theme_button_kind_t;

typedef enum {
    UI_THEME_ICON_NONE = 0,
    UI_THEME_ICON_CONNECT,
    UI_THEME_ICON_DEMO,
    UI_THEME_ICON_SETTINGS,
    UI_THEME_ICON_ASR,
    UI_THEME_ICON_DOA,
    UI_THEME_ICON_PALM,
    UI_THEME_ICON_FACE,
    UI_THEME_ICON_GESTURE,
    UI_THEME_ICON_CHAT,
    UI_THEME_ICON_VISION,
    UI_THEME_ICON_CAMERA,
    UI_THEME_ICON_MUSIC,
    UI_THEME_ICON_VIDEO,
    UI_THEME_ICON_VOLUME,
    UI_THEME_ICON_USB,
    UI_THEME_ICON_RESET,
    UI_THEME_ICON_LANG,
    UI_THEME_ICON_CAR,
    UI_THEME_ICON_BLUETOOTH,
} ui_theme_icon_kind_t;

void ui_theme_apply_screen(lv_obj_t *screen);
lv_obj_t *ui_theme_create_deco_ellipse(lv_obj_t *parent, int x, int y,
                                       int w, int h, uint32_t color);
lv_obj_t *ui_theme_create_title(lv_obj_t *parent, const char *text);
lv_obj_t *ui_theme_create_subtitle(lv_obj_t *parent, const char *text);
lv_obj_t *ui_theme_create_card(lv_obj_t *parent, int x, int y, int w, int h,
                               int radius, bool shadow);
lv_obj_t *ui_theme_create_pill(lv_obj_t *parent, const char *text, int x, int y,
                               int w, int h, bool active);
lv_obj_t *ui_theme_create_text(lv_obj_t *parent, const char *text, int x, int y,
                               int w, const lv_font_t *font, uint32_t color,
                               lv_text_align_t align);
lv_obj_t *ui_theme_create_icon_badge(lv_obj_t *parent, ui_theme_icon_kind_t kind,
                                     int x, int y, bool active);
lv_obj_t *ui_theme_create_action_button(lv_obj_t *parent, const char *text,
                                        int x, int y, int w, int h,
                                        ui_theme_button_kind_t kind);
lv_obj_t *ui_theme_create_row(lv_obj_t *parent, const char *title,
                              const char *desc, int y, bool active,
                              ui_theme_icon_kind_t icon);
void ui_theme_set_row_focus(lv_obj_t *row, const char *title,
                            const char *desc, ui_theme_icon_kind_t icon,
                            bool focused);
void ui_theme_set_button_focus(lv_obj_t *button, ui_theme_button_kind_t kind,
                               bool focused);
void ui_theme_create_popup(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __UI_THEME_H__ */
