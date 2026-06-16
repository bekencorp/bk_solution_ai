/**
 * @file ui_theme.c
 * @brief Shared LVGL visual theme implementation.
 */
#include "ui_theme.h"

#include <stddef.h>
#include <stdint.h>

static void clear_obj_style(lv_obj_t *obj)
{
    if (obj == NULL) {
        return;
    }
    lv_obj_remove_style_all(obj);
}

static void set_common_text(lv_obj_t *label, const lv_font_t *font,
                            uint32_t color, lv_text_align_t align)
{
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN | LV_STATE_DEFAULT);
}

void ui_theme_apply_screen(lv_obj_t *screen)
{
    if (screen == NULL) {
        return;
    }
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(screen, LOGICAL_SCREEN_WIDTH, LOGICAL_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_hex(UI_THEME_COLOR_BG_TOP),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(screen, lv_color_hex(UI_THEME_COLOR_BG_BOTTOM),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *ui_theme_create_deco_ellipse(lv_obj_t *parent, int x, int y,
                                       int w, int h, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    clear_obj_style(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    return obj;
}

lv_obj_t *ui_theme_create_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, UI_THEME_SAFE_LEFT, 13);
    lv_obj_set_size(label, LOGICAL_SCREEN_WIDTH - UI_THEME_SAFE_LEFT - UI_THEME_SAFE_RIGHT, 38);
    set_common_text(label, &lv_font_ali_30, UI_THEME_COLOR_INK, LV_TEXT_ALIGN_LEFT);
    return label;
}

lv_obj_t *ui_theme_create_subtitle(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, UI_THEME_SAFE_LEFT, 52);
    lv_obj_set_size(label, LOGICAL_SCREEN_WIDTH - UI_THEME_SAFE_LEFT - UI_THEME_SAFE_RIGHT, 18);
    set_common_text(label, &lv_font_ali_16, UI_THEME_COLOR_DESC, LV_TEXT_ALIGN_LEFT);
    return label;
}

lv_obj_t *ui_theme_create_card(lv_obj_t *parent, int x, int y, int w, int h,
                               int radius, bool shadow)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(UI_THEME_COLOR_CARD),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(UI_THEME_COLOR_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (shadow) {
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x86a4d6),
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(card, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_opa(card, LV_OPA_40, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_offset_x(card, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_offset_y(card, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_spread(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
        lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    return card;
}

lv_obj_t *ui_theme_create_pill(lv_obj_t *parent, const char *text, int x, int y,
                               int w, int h, bool active)
{
    lv_obj_t *pill = lv_label_create(parent);
    lv_label_set_text(pill, text);
    lv_label_set_long_mode(pill, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(pill, x, y);
    lv_obj_set_size(pill, w, h);
    uint32_t bg = active ? UI_THEME_COLOR_PRIMARY : 0xdcecff;
    uint32_t fg = active ? 0xffffff : UI_THEME_COLOR_PRIMARY;
    lv_obj_set_style_bg_color(pill, lv_color_hex(bg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(pill, h / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(pill, h > 22 ? (h - 18) / 2 : 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(pill, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(pill, lv_color_hex(fg), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(pill, &lv_font_ali_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(pill, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    return pill;
}

lv_obj_t *ui_theme_create_text(lv_obj_t *parent, const char *text, int x, int y,
                               int w, const lv_font_t *font, uint32_t color,
                               lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    set_common_text(label, font, color, align);
    return label;
}

static const char *icon_text(ui_theme_icon_kind_t kind)
{
    switch (kind) {
    case UI_THEME_ICON_CONNECT:  return LV_SYMBOL_WIFI;
    case UI_THEME_ICON_DEMO:     return LV_SYMBOL_LIST;
    case UI_THEME_ICON_SETTINGS: return LV_SYMBOL_SETTINGS;
    case UI_THEME_ICON_ASR:      return "ASR";
    case UI_THEME_ICON_DOA:      return LV_SYMBOL_GPS;
    case UI_THEME_ICON_PALM:     return "PAL";
    case UI_THEME_ICON_FACE:     return LV_SYMBOL_EYE_OPEN;
    case UI_THEME_ICON_GESTURE:  return "GES";
    case UI_THEME_ICON_CHAT:     return "AI";
    case UI_THEME_ICON_VISION:   return LV_SYMBOL_EYE_OPEN;
    case UI_THEME_ICON_CAMERA:   return LV_SYMBOL_IMAGE;
    case UI_THEME_ICON_MUSIC:    return LV_SYMBOL_AUDIO;
    case UI_THEME_ICON_VIDEO:    return LV_SYMBOL_VIDEO;
    case UI_THEME_ICON_VOLUME:   return LV_SYMBOL_VOLUME_MAX;
    case UI_THEME_ICON_USB:      return LV_SYMBOL_USB;
    case UI_THEME_ICON_RESET:    return LV_SYMBOL_REFRESH;
    case UI_THEME_ICON_NONE:
    default:                     return LV_SYMBOL_LIST;
    }
}

static bool icon_uses_symbol(ui_theme_icon_kind_t kind)
{
    switch (kind) {
    case UI_THEME_ICON_CONNECT:
    case UI_THEME_ICON_DEMO:
    case UI_THEME_ICON_SETTINGS:
    case UI_THEME_ICON_DOA:
    case UI_THEME_ICON_FACE:
    case UI_THEME_ICON_VISION:
    case UI_THEME_ICON_CAMERA:
    case UI_THEME_ICON_MUSIC:
    case UI_THEME_ICON_VIDEO:
    case UI_THEME_ICON_VOLUME:
    case UI_THEME_ICON_USB:
    case UI_THEME_ICON_RESET:
        return true;
    default:
        return false;
    }
}

lv_obj_t *ui_theme_create_icon_badge(lv_obj_t *parent, ui_theme_icon_kind_t kind,
                                     int x, int y, bool active)
{
    lv_obj_t *badge = lv_obj_create(parent);
    clear_obj_style(badge);
    lv_obj_set_pos(badge, x, y);
    lv_obj_set_size(badge, 30, 30);
    lv_obj_set_style_radius(badge, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(badge,
                              lv_color_hex(active ? UI_THEME_COLOR_SELECTED_2 : UI_THEME_COLOR_ICON_BG),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(badge,
                                   lv_color_hex(active ? UI_THEME_COLOR_PRIMARY_2 : 0xf2f7ff),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(badge, LV_GRAD_DIR_VER,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(badge,
                                  lv_color_hex(active ? 0x8fc3ff : 0xc7dcff),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(badge, 1, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(badge);
    lv_label_set_text(label, icon_text(kind));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(label, 28);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label,
                                lv_color_hex(active ? 0xffffff : UI_THEME_COLOR_ICON_FG),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label,
                               icon_uses_symbol(kind) ? LV_FONT_DEFAULT : &lv_font_ali_16,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label);
    return badge;
}

static void apply_button_base(lv_obj_t *button, ui_theme_button_kind_t kind)
{
    uint32_t c1 = UI_THEME_COLOR_PRIMARY;
    uint32_t c2 = UI_THEME_COLOR_PRIMARY_2;
    uint32_t shadow = UI_THEME_COLOR_PRIMARY;

    if (kind == UI_THEME_BUTTON_DANGER) {
        c1 = UI_THEME_COLOR_RED;
        c2 = UI_THEME_COLOR_RED;
        shadow = UI_THEME_COLOR_RED;
    } else if (kind == UI_THEME_BUTTON_NEUTRAL) {
        c1 = UI_THEME_COLOR_CARD_ALT;
        c2 = UI_THEME_COLOR_CARD_ALT;
        shadow = 0x244f8c;
    }

    lv_obj_set_style_bg_color(button, lv_color_hex(c1), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(c2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_HOR, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(button, lv_color_hex(shadow), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(button, LV_OPA_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(button, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

lv_obj_t *ui_theme_create_action_button(lv_obj_t *parent, const char *text,
                                        int x, int y, int w, int h,
                                        ui_theme_button_kind_t kind)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    apply_button_base(button, kind);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_center(label);
    set_common_text(label, &lv_font_ali_25, 0xffffff, LV_TEXT_ALIGN_CENTER);
    return button;
}

lv_obj_t *ui_theme_create_row(lv_obj_t *parent, const char *title,
                              const char *desc, int y, bool active,
                              ui_theme_icon_kind_t icon)
{
    int row_w = LOGICAL_SCREEN_WIDTH - UI_THEME_SAFE_LEFT - UI_THEME_SAFE_RIGHT;
    lv_obj_t *row = ui_theme_create_card(parent, UI_THEME_SAFE_LEFT, y, row_w, 58, 20, false);
    ui_theme_set_row_focus(row, title, desc, icon, active);
    return row;
}

void ui_theme_set_row_focus(lv_obj_t *row, const char *title,
                            const char *desc, ui_theme_icon_kind_t icon,
                            bool focused)
{
    if (row == NULL) {
        return;
    }

    /* Read the explicitly configured pixel size from the style instead of the
     * resolved coords. During page init (before the screen is loaded and laid
     * out) lv_obj_get_width()/height() still return 0, which would push the
     * description off-screen and give the title a negative width. The style
     * value is the px size set via lv_obj_set_size() and is valid immediately,
     * so the first render already looks correct. */
    int row_w = lv_obj_get_style_width(row, LV_PART_MAIN);
    int row_h = lv_obj_get_style_height(row, LV_PART_MAIN);
    if (row_w <= 0) {
        row_w = lv_obj_get_width(row);
    }
    if (row_h <= 0) {
        row_h = lv_obj_get_height(row);
    }
    int icon_size = row_h >= 56 ? 30 : 28;
    int icon_x = row_h >= 56 ? 16 : 12;
    int icon_y = (row_h - icon_size) / 2;
    int title_x = icon_x + icon_size + 10;
    int title_y = row_h >= 56 ? 10 : 7;
    int desc_w = row_w >= 310 ? 92 : 82;
    int desc_x = row_w - desc_w - 15;
    int title_w = desc != NULL ? desc_x - title_x - 8 : row_w - title_x - 15;

    lv_obj_set_style_bg_color(row,
                              lv_color_hex(focused ? UI_THEME_COLOR_SELECTED : UI_THEME_COLOR_CARD),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(row,
                                   lv_color_hex(focused ? UI_THEME_COLOR_SELECTED_2 : UI_THEME_COLOR_CARD_ALT),
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_HOR,
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(row,
                                  lv_color_hex(focused ? UI_THEME_COLOR_SELECTED_2 : UI_THEME_COLOR_BORDER),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(row, focused ? 0 : 1,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_color(row,
                                  lv_color_hex(focused ? UI_THEME_COLOR_SELECTED : 0x86a4d6),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(row, focused ? 12 : 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(row, focused ? LV_OPA_30 : LV_OPA_TRANSP,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(row, focused ? 4 : 0,
                                     LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_clean(row);
    (void)ui_theme_create_icon_badge(row, icon, icon_x, icon_y, focused);
    (void)ui_theme_create_text(row, title, title_x, title_y, title_w,
                               &lv_font_ali_25,
                               focused ? 0xffffff : UI_THEME_COLOR_INK,
                               LV_TEXT_ALIGN_LEFT);
    if (desc != NULL) {
        (void)ui_theme_create_text(row, desc, desc_x, (row_h - 18) / 2,
                                   desc_w, &lv_font_ali_16,
                                   focused ? 0xdcecff : UI_THEME_COLOR_MUTED,
                                   LV_TEXT_ALIGN_RIGHT);
    }
}

void ui_theme_set_button_focus(lv_obj_t *button, ui_theme_button_kind_t kind,
                               bool focused)
{
    if (button == NULL) {
        return;
    }
    apply_button_base(button, kind);
    lv_obj_set_style_border_color(button, lv_color_hex(0xffffff),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, focused ? 2 : 0,
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_opa(button, focused ? LV_OPA_40 : LV_OPA_20,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}
