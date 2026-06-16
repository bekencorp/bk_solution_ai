/**
 * @file demo_catalog.h
 * @brief Data-driven catalog of the demo-center categories and the
 *        device-settings entries.
 *
 * The catalog centralizes "which feature lives in which menu" so that
 * re-grouping demos only touches the tables in demo_catalog.c. Demo center is
 * rendered as category tabs on page_3, while device settings still uses the
 * shared ui_list_menu component.
 *
 * Menu structure:
 *   Demo center (page_3 tabbed)
 *     - End-side AI: ASR / sound localization / palm / face / gesture
 *     - Cloud AI: AI chat / vision / AI camera
 *     - Entertainment: music / robot video
 *   Device settings (reached from the home screen)
 *     - Volume / U-disk / Factory reset
 */
#ifndef __BK_DEMO_CATALOG_H__
#define __BK_DEMO_CATALOG_H__

#include "lvgl.h"
#include "beken_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Render the tabbed demo center onto an existing screen (page_3). */
void demo_center_build(lv_obj_t *screen);

/** Release the demo-center UI bound by demo_center_build(). */
void demo_center_unbuild(lv_obj_t *screen);

/** Navigate to the demo-center tabbed page (generated page_3 slot). */
int demo_center_enter(void);

/** Open Demo center with the End-side AI tab selected. */
int demo_category_edge_enter(void);

/** Open Demo center with the Cloud AI tab selected. */
int demo_category_cloud_enter(void);

/** Open Demo center with the Entertainment tab selected. */
int demo_category_fun_enter(void);

/** Open the device-settings list (volume / U-disk / factory reset). */
int device_settings_enter(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_CATALOG_H__ */
