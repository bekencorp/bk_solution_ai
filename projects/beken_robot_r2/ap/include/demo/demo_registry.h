/**
 * @file demo_registry.h
 * @brief Init-only registry for the built-in UI demo backends.
 *
 * ap_main calls `bk_demos_init_all()` once during startup; it walks the
 * built-in demo table and invokes each backend's `init()` (CLI
 * registration, app_event subscriptions, etc.).
 *
 * Note: there is no longer a centralized menu_idx -> demo dispatcher
 * here. The page_3 (demo grid) click handler owns its own action table
 * directly in beken_generated/page_demo_menu/page_demo_menu_hooks.c so
 * the UI layer can call the matching <demo>_start() backend symbol
 * without going through an indirection in src/demo/.
 */
#ifndef __BK_DEMO_REGISTRY_H__
#define __BK_DEMO_REGISTRY_H__

#include "demo/demo_iface.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init all built-in demo backends. Called once at startup.
 *
 * @return 0 on full success; otherwise the first non-zero return code,
 *         but the loop continues so independent backends still
 *         initialize.
 */
int bk_demos_init_all(void);

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_REGISTRY_H__ */
