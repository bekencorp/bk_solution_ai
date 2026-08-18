/**
 * @file page_demo_menu_hooks.c
 * @brief page_3 demo center: a three-category bar list (End-side AI /
 *        Cloud AI / Entertainment) rendered by the shared ui_list_menu
 *        component.
 *
 * The actual category contents and per-demo dispatch live in the
 * data-driven demo_catalog (src/demo/demo_catalog.c); this hook only binds
 * the category list to the generated page_3 screen. Selecting a category
 * opens its sub-menu, and each demo returns to the launching sub-menu via
 * the ui_demo_return_to_menu() indirection.
 */
#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "page_hooks.h"

#ifdef ROBOT_TEST

#include "demo/demo_catalog.h"

static void page_demo_menu_on_init(bk_lv_ui_t *ui)
{
    demo_center_build(ui->page_3);
}

static void page_demo_menu_on_destroy(bk_lv_ui_t *ui)
{
    demo_center_unbuild(ui->page_3);
}

void page_demo_menu_init_hooks(void)
{
    (void)bk_page_set_init_hook(3, page_demo_menu_on_init);
    (void)bk_page_set_destroy_hook(3, page_demo_menu_on_destroy);
}

int page_demo_menu_enter(void)
{
    return demo_center_enter();
}

#else  /* !ROBOT_TEST */

void page_demo_menu_init_hooks(void) {}
int  page_demo_menu_enter(void)      { return 0; }

#endif /* ROBOT_TEST */
