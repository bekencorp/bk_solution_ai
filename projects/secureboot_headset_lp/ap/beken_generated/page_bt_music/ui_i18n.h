/**
 * @file ui_i18n.h
 * @brief Headset stub of the robot i18n helpers. Fixed Simplified-Chinese
 *        strings, rendered with lv_font_ali_16 (BTM_TEXT_FONT) -- the
 *        ui_i18n_get_lang() != UI_LANG_EN branch keeps toast rendering on
 *        the CJK font so no English fallback font is needed.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int ui_str_id_t;

#define UI_LANG_EN  1

#define STR_BT_MUSIC_CONNECT_PHONE  1
#define STR_BT_MUSIC_LOW_MEM        2
#define STR_BT_MUSIC_PAIR           3
#define STR_BT_MUSIC_CONNECTED      4
#define STR_BT_MUSIC_SEARCHING      5

static inline int ui_i18n_get_lang(void)
{
    return 0; /* != UI_LANG_EN: toasts render ui_tr() with the CJK font */
}

static inline const char *ui_tr(ui_str_id_t id)
{
    switch (id) {
    case STR_BT_MUSIC_CONNECT_PHONE: return "请先连接手机";
    case STR_BT_MUSIC_LOW_MEM:       return "内存不足，请重试";
    case STR_BT_MUSIC_PAIR:          return "配对";
    case STR_BT_MUSIC_CONNECTED:     return "已连接";
    case STR_BT_MUSIC_SEARCHING:     return "待连接";
    default:                         return "";
    }
}

#ifdef __cplusplus
}
#endif
