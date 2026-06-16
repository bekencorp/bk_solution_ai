/**
 * @file page_edge_ai.c
 * @brief End-side AI entry shim.
 *
 * The End-side AI menu is now part of the data-driven demo center: it is one
 * of the demo_catalog categories rendered by the shared ui_list_menu
 * component. The overlay demos (palm / face / gesture) navigate back to this
 * menu through palm_tracking_set_return_to_edge_ai()/page_edge_ai_enter(), so
 * page_edge_ai_enter() is kept as a thin delegate to demo_category_edge_enter().
 */
#include "page_edge_ai.h"

#ifdef ROBOT_TEST

#include "demo/demo_catalog.h"

int page_edge_ai_enter(void)
{
    return demo_category_edge_enter();
}

/* The standalone "solution example" sub-page was retired together with the
 * old fixed-grid End-side AI menu. Kept as a no-op so any lingering caller
 * (and the header contract) still links. */
int page_edge_ai_solution_enter(void)
{
    return 0;
}

#else  /* !ROBOT_TEST */

int page_edge_ai_enter(void) { return 0; }
int page_edge_ai_solution_enter(void) { return 0; }

#endif /* ROBOT_TEST */
