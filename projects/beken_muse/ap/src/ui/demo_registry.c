/**
 * @file demo_registry.c
 * @brief Init-only registry of the built-in demo backends.
 *
 * `bk_demos_init_all()` walks the table below and invokes each demo's
 * `init()` (CLI registration, app_event subscriptions, etc.). It does
 * NOT register any LVGL page hooks -- those are registered by the UI
 * layer via `bk_pages_init_all_hooks()` in
 * beken_generated/page_hooks.c.
 *
 * The page_3 (demo grid) menu_idx -> action table lives in
 * beken_generated/page_demo_menu/page_demo_menu_hooks.c so the UI layer
 * owns its own dispatch without bouncing through src/demo/.
 */
#include "ui/demo_registry.h"

#include <components/log.h>
#include <stddef.h>

#define TAG "demo_registry"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

#include "ui/volume.h"
#include "demo/camera_preview_demo.h"
#include "demo/udisk.h"
#if CONFIG_MUSE_GADGET
#include "demo/muse_gadget.h"
#endif

static const bk_demo_iface_t * const s_demo_list[] = {
    &g_demo_volume,
    &g_demo_camera_preview,
    &g_demo_udisk,
#if CONFIG_MUSE_GADGET
    &g_demo_muse_gadget,
#endif
};

#define DEMO_LIST_LEN ((int)(sizeof(s_demo_list) / sizeof(s_demo_list[0])))

int bk_demos_init_all(void)
{
    int first_err = 0;

    for (int i = 0; i < DEMO_LIST_LEN; i++) {
        const bk_demo_iface_t *d = s_demo_list[i];
        if (d == NULL || d->init == NULL) {
            continue;
        }
        int ret = d->init();
        if (ret != 0) {
            LOGW("demo %s init failed: %d\r\n",
                 d->name ? d->name : "?", ret);
            if (first_err == 0) {
                first_err = ret;
            }
        } else {
            LOGI("demo %s init ok\r\n", d->name ? d->name : "?");
        }
    }
    return first_err;
}
