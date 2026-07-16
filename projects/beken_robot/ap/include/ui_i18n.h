/**
 * @file ui_i18n.h
 * @brief Lightweight bilingual (Chinese / English) string lookup for the UI.
 *
 * All scattered inline UI strings go through ui_tr(); menu item arrays that
 * are naturally tabular keep their own [UI_LANG_COUNT][N] tables near usage.
 * The selected language is persisted to flash via bk_factory_config.
 */
#ifndef __UI_I18N_H__
#define __UI_I18N_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_LANG_ZH = 0,
    UI_LANG_EN,
    UI_LANG_COUNT,
} ui_lang_t;

typedef enum {
    STR_HOME_TITLE = 0,
    STR_HOME_HINT,

    STR_SPLASH_TITLE,
    STR_SPLASH_FEATURES,
    STR_SPLASH_HINT,

    STR_PROV_TITLE,
    STR_PROV_SUBTITLE,
    STR_PROV_DEVICE_NAME,
    STR_PROV_BTN_START,
    STR_PROV_BTN_DELETE,

    STR_ASR_LISTENING,
    STR_ASR_HINT,
    STR_ASR_RECOGNIZED,
    STR_ASR_HELLO,
    STR_ASR_BYE,

    STR_VOLUME_TITLE,

    STR_DEMO_CENTER_TITLE,

    STR_SETTINGS_TITLE,
    STR_SETTINGS_SUBTITLE,
    STR_SETTINGS_LANG,

    STR_USBMODE_TITLE,
    STR_USBMODE_UART,
    STR_USBMODE_USB,
    STR_USBMODE_ON_UART,
    STR_USBMODE_ON_USB,

    STR_LANG_NATIVE_ZH,
    STR_LANG_NATIVE_EN,

    STR_ID_COUNT,
} ui_str_id_t;

/** Load the persisted language (defaults to Chinese). Call after
 *  bk_factory_init() and before the first UI page is built. */
void ui_i18n_init(void);

ui_lang_t ui_i18n_get_lang(void);

/** Switch language: persists to flash and invalidates cached generated
 *  pages so they rebuild in the new language on next navigation. */
void ui_i18n_set_lang(ui_lang_t lang);

/** Translate a string id into the current language. Never returns NULL. */
const char *ui_tr(ui_str_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* __UI_I18N_H__ */
