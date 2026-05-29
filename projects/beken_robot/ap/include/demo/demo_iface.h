/**
 * @file demo_iface.h
 * @brief Common interface exposed by each UI demo backend.
 *
 * Each demo (in `src/demo/<demo>.c`) exports three entry points:
 *   - `<demo>_init()`: one-shot startup hooks (CLI registration,
 *                     app_event subscriptions, etc.). Invoked by
 *                     bk_demos_init_all() in demo_registry.
 *   - `<demo>_start()`: enter the demo's page and start the backend
 *                     service.
 *   - `<demo>_stop()`: stop the backend service and return to the
 *                     parent menu.
 *
 * Demos that have nothing to start/stop (e.g. splash or the menus
 * themselves -- which no longer live here at all) may simply return 0.
 *
 * All entry points are expected to be safe to call from any task; the
 * demo implementation is responsible for taking the right locks
 * internally.
 */
#ifndef __BK_DEMO_IFACE_H__
#define __BK_DEMO_IFACE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;           /**< Short name used in debug logs / CLI. */
    int  (*init)(void);         /**< One-shot register; returns 0 on success. */
    int  (*start)(void);        /**< Enter the demo; returns 0 once dispatched. */
    int  (*stop)(void);         /**< Leave the demo; returns 0 once dispatched. */
} bk_demo_iface_t;

#ifdef __cplusplus
}
#endif

#endif /* __BK_DEMO_IFACE_H__ */
