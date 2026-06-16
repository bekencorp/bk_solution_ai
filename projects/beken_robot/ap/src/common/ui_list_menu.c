/**
 * @file ui_list_menu.c
 * @brief Implementation of the reusable vertical long-bar list menu.
 *
 * See ui_list_menu.h for the design rationale. Each live menu (attached or
 * dynamic) owns a context slot from a small static pool; the nav-router
 * callbacks resolve the right context from the active LVGL screen, so an
 * attached page (e.g. page_3) and a dynamic sub-menu can coexist while only
 * one of them is on screen at a time.
 */
#include "ui_list_menu.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "beken_ui.h"
#include "event_runtime.h"
#include "ui_nav_router.h"
#include "page_hooks.h"
#include "ui_theme.h"
#include "ui_touch_gesture.h"

/* Logical canvas is 385 x 320 (landscape after the ap_main ROTATE_90). */
#define LM_PANEL_X        0
#define LM_PANEL_Y_TABS   102
#define LM_PANEL_Y_PLAIN  74
#define LM_PANEL_W        LOGICAL_SCREEN_WIDTH
#define LM_PANEL_H_TABS   (LOGICAL_SCREEN_HEIGHT - LM_PANEL_Y_TABS - 8)
#define LM_PANEL_H_PLAIN  (LOGICAL_SCREEN_HEIGHT - LM_PANEL_Y_PLAIN - 8)
#define LM_ROW_H          58
#define LM_ROW_GAP        10

#define LM_MAX_INSTANCES   6

typedef struct {
    bool                  used;
    bool                  dynamic;     /* created (owns the screen) vs attached. */
    lv_obj_t             *screen;
    lv_obj_t             *panel;
    lv_obj_t             *bars[UI_LIST_MENU_MAX_ITEMS];
    int                   item_count;
    int                   focus_idx;
    ui_touch_tap_state_t  tap_state;
    ui_list_menu_config_t cfg;
} ui_list_menu_ctx_t;

static ui_list_menu_ctx_t s_pool[LM_MAX_INSTANCES];
static ui_list_menu_ctx_t *s_current_dynamic;     /* most recent created menu. */
static ui_menu_enter_fn_t  s_return_menu_fn;

static ui_list_menu_ctx_t *ctx_alloc(void)
{
    for (int i = 0; i < LM_MAX_INSTANCES; i++) {
        if (!s_pool[i].used) {
            memset(&s_pool[i], 0, sizeof(s_pool[i]));
            s_pool[i].used = true;
            return &s_pool[i];
        }
    }
    return NULL;
}

static void ctx_free(ui_list_menu_ctx_t *ctx)
{
    if (ctx != NULL) {
        memset(ctx, 0, sizeof(*ctx));
    }
}

static ui_list_menu_ctx_t *ctx_for_screen(lv_obj_t *screen)
{
    if (screen == NULL) {
        return NULL;
    }
    for (int i = 0; i < LM_MAX_INSTANCES; i++) {
        if (s_pool[i].used && s_pool[i].screen == screen) {
            return &s_pool[i];
        }
    }
    return NULL;
}

static ui_list_menu_ctx_t *ctx_active(void)
{
    return ctx_for_screen(lv_screen_active());
}

static void apply_focus(ui_list_menu_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    for (int i = 0; i < ctx->item_count; i++) {
        lv_obj_t *bar = ctx->bars[i];
        if (bar == NULL || !lv_obj_is_valid(bar)) {
            continue;
        }
        bool focused = (i == ctx->focus_idx);
        const char *desc = ctx->cfg.descriptions != NULL ? ctx->cfg.descriptions[i] : NULL;
        ui_theme_icon_kind_t icon = ctx->cfg.icons != NULL ?
                                    ctx->cfg.icons[i] : UI_THEME_ICON_DEMO;
        ui_theme_set_row_focus(bar, ctx->cfg.items[i], desc, icon, focused);
        if (focused) {
            lv_obj_scroll_to_view(bar, LV_ANIM_ON);
        }
    }
}

static bool nav_intercepted(ui_list_menu_ctx_t *ctx, ui_nav_event_t ev)
{
    if (ctx != NULL && ctx->cfg.on_nav_intercept != NULL) {
        return ctx->cfg.on_nav_intercept(ev, ctx->cfg.user_data);
    }
    return false;
}

static void on_focus_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    ui_list_menu_ctx_t *ctx = ctx_active();
    if (ctx == NULL || ctx->item_count <= 0) {
        return;
    }
    if (nav_intercepted(ctx, UI_NAV_EVENT_FOCUS_PREV)) {
        return;
    }
    ctx->focus_idx = (ctx->focus_idx + ctx->item_count - 1) % ctx->item_count;
    apply_focus(ctx);
}

static void on_focus_next(bk_lv_ui_t *ui)
{
    (void)ui;
    ui_list_menu_ctx_t *ctx = ctx_active();
    if (ctx == NULL || ctx->item_count <= 0) {
        return;
    }
    if (nav_intercepted(ctx, UI_NAV_EVENT_FOCUS_NEXT)) {
        return;
    }
    ctx->focus_idx = (ctx->focus_idx + 1) % ctx->item_count;
    apply_focus(ctx);
}

static void on_screen_prev(bk_lv_ui_t *ui)
{
    (void)ui;
    ui_list_menu_ctx_t *ctx = ctx_active();
    if (ctx == NULL) {
        return;
    }
    if (nav_intercepted(ctx, UI_NAV_EVENT_SCREEN_PREV)) {
        return;
    }
    ui_menu_enter_fn_t back = ctx->cfg.on_back;
    if (back != NULL) {
        (void)back();
    }
}

static void on_screen_next(bk_lv_ui_t *ui)
{
    (void)ui;
    ui_list_menu_ctx_t *ctx = ctx_active();
    if (ctx == NULL || ctx->item_count <= 0) {
        return;
    }
    if (nav_intercepted(ctx, UI_NAV_EVENT_SCREEN_NEXT)) {
        return;
    }
    if (ctx->cfg.on_select != NULL &&
        ctx->focus_idx >= 0 && ctx->focus_idx < ctx->item_count) {
        ctx->cfg.on_select(ctx->focus_idx, ctx->cfg.user_data);
    }
}

static const ui_page_nav_ops_t s_list_menu_nav_ops = {
    .on_focus_prev = on_focus_prev,
    .on_focus_next = on_focus_next,
    .on_screen_prev = on_screen_prev,
    .on_screen_next = on_screen_next,
    .on_confirm_long = NULL,
};

static void bar_press_cb(lv_event_t *e)
{
    ui_list_menu_ctx_t *ctx = ctx_active();
    if (ctx == NULL) {
        return;
    }
    ui_touch_tap_press(e, &ctx->tap_state);
}

static void bar_release_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    ui_list_menu_ctx_t *ctx = ctx_active();

    if (ctx == NULL || idx < 0 || idx >= ctx->item_count) {
        return;
    }
    if (!ui_touch_tap_release(e, &ctx->tap_state,
                              UI_TOUCH_TAP_MOVE_LIMIT_DEFAULT)) {
        return;
    }

    if (nav_intercepted(ctx, UI_NAV_EVENT_SCREEN_NEXT)) {
        return;
    }
    ctx->focus_idx = idx;
    apply_focus(ctx);
    if (ctx->cfg.on_select != NULL) {
        ctx->cfg.on_select(idx, ctx->cfg.user_data);
    }
}

static void bar_press_lost_cb(lv_event_t *e)
{
    (void)e;
    ui_list_menu_ctx_t *ctx = ctx_active();
    if (ctx != NULL) {
        ui_touch_tap_reset(&ctx->tap_state);
    }
}

static lv_obj_t *create_menu_row(ui_list_menu_ctx_t *ctx, int idx, int y)
{
    const char *desc = NULL;
    if (ctx->cfg.descriptions != NULL) {
        desc = ctx->cfg.descriptions[idx];
    }
    ui_theme_icon_kind_t icon = UI_THEME_ICON_DEMO;
    if (ctx->cfg.icons != NULL) {
        icon = ctx->cfg.icons[idx];
    }
    lv_obj_t *bar = ui_theme_create_row(ctx->panel, ctx->cfg.items[idx], desc,
                                        y, false, icon);

    lv_obj_add_event_cb(bar, bar_press_cb, LV_EVENT_PRESSED,
                        (void *)(intptr_t)idx);
    lv_obj_add_event_cb(bar, bar_release_cb, LV_EVENT_RELEASED,
                        (void *)(intptr_t)idx);
    lv_obj_add_event_cb(bar, bar_press_lost_cb, LV_EVENT_PRESS_LOST,
                        (void *)(intptr_t)idx);
    return bar;
}

static void build_menu(ui_list_menu_ctx_t *ctx, lv_obj_t *screen,
                       const ui_list_menu_config_t *cfg)
{
    int count = cfg->item_count;
    if (count > UI_LIST_MENU_MAX_ITEMS) {
        count = UI_LIST_MENU_MAX_ITEMS;
    }

    ctx->screen = screen;
    ctx->cfg = *cfg;
    ctx->item_count = count;
    ctx->focus_idx = 0;

    ui_theme_apply_screen(screen);

    if (cfg->title != NULL) {
        (void)ui_theme_create_title(screen, cfg->title);
    }
    if (cfg->subtitle != NULL) {
        (void)ui_theme_create_subtitle(screen, cfg->subtitle);
    }

    if (cfg->tab_count > 0 && cfg->tabs != NULL) {
        char page_text[12];
        int active = cfg->active_tab;
        if (active < 0 || active >= cfg->tab_count) {
            active = 0;
        }
        snprintf(page_text, sizeof(page_text), "%d / %d", active + 1, cfg->tab_count);
        (void)ui_theme_create_text(screen, page_text, 316, 24, 46,
                                   &lv_font_ali_16, UI_THEME_COLOR_MUTED,
                                   LV_TEXT_ALIGN_RIGHT);

        int x = UI_THEME_SAFE_LEFT;
        for (int i = 0; i < cfg->tab_count; i++) {
            int w = (i == cfg->tab_count - 1) ? 88 : 86;
            (void)ui_theme_create_pill(screen, cfg->tabs[i], x, 64, w, 30, i == active);
            x += w + 8;
        }
    }

    lv_obj_t *panel = lv_obj_create(screen);
    lv_obj_remove_style_all(panel);
    int panel_y = (cfg->tab_count > 0 && cfg->tabs != NULL) ? LM_PANEL_Y_TABS : LM_PANEL_Y_PLAIN;
    int panel_h = (cfg->tab_count > 0 && cfg->tabs != NULL) ? LM_PANEL_H_TABS : LM_PANEL_H_PLAIN;
    lv_obj_set_pos(panel, LM_PANEL_X, panel_y);
    lv_obj_set_size(panel, LM_PANEL_W, panel_h);
    lv_obj_set_scroll_dir(panel, LV_DIR_VER);
    int content_h = count * LM_ROW_H + (count > 0 ? (count - 1) * LM_ROW_GAP : 0);
    lv_obj_set_scrollbar_mode(panel, content_h > panel_h ? LV_SCROLLBAR_MODE_ON : LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_bottom(panel, LM_ROW_GAP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_width(panel, 4, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_THEME_COLOR_PRIMARY),
                              LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_40, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(panel, 2, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    ctx->panel = panel;

    for (int i = 0; i < count; i++) {
        int y = i * (LM_ROW_H + LM_ROW_GAP);
        ctx->bars[i] = create_menu_row(ctx, i, y);
    }

    apply_focus(ctx);
}

void ui_list_menu_attach(lv_obj_t *screen, const ui_list_menu_config_t *cfg)
{
    if (screen == NULL || cfg == NULL) {
        return;
    }
    /* An attach on an already-tracked screen replaces its content tracking. */
    ui_list_menu_ctx_t *old = ctx_for_screen(screen);
    if (old != NULL) {
        ui_nav_unregister_screen(screen);
        ctx_free(old);
    }

    ui_list_menu_ctx_t *ctx = ctx_alloc();
    if (ctx == NULL) {
        return;
    }
    ctx->dynamic = false;
    build_menu(ctx, screen, cfg);
    (void)ui_nav_register_screen(screen, &s_list_menu_nav_ops);
}

void ui_list_menu_detach(lv_obj_t *screen)
{
    ui_list_menu_ctx_t *ctx = ctx_for_screen(screen);
    if (ctx == NULL) {
        return;
    }
    ui_nav_unregister_screen(screen);
    ctx_free(ctx);
}

lv_obj_t *ui_list_menu_create(const ui_list_menu_config_t *cfg)
{
    if (cfg == NULL) {
        return NULL;
    }

    /* Single dynamic-screen stack: drop the previous dynamic menu first. */
    ui_list_menu_destroy_current();

    ui_list_menu_ctx_t *ctx = ctx_alloc();
    if (ctx == NULL) {
        return NULL;
    }

    lv_obj_t *screen = lv_obj_create(NULL);
    ui_theme_apply_screen(screen);

    ctx->dynamic = true;
    build_menu(ctx, screen, cfg);

    bk_page_attach_right_swipe_gesture(screen);
    lv_screen_load(screen);
    (void)ui_nav_register_screen(screen, &s_list_menu_nav_ops);

    s_current_dynamic = ctx;
    return screen;
}

void ui_list_menu_destroy_current(void)
{
    ui_list_menu_ctx_t *ctx = s_current_dynamic;
    if (ctx == NULL) {
        return;
    }
    s_current_dynamic = NULL;

    lv_obj_t *screen = ctx->screen;
    if (screen != NULL && lv_obj_is_valid(screen)) {
        ui_nav_unregister_screen(screen);
        lv_obj_del(screen);
    }
    ctx_free(ctx);
}

void ui_demo_set_return_menu(ui_menu_enter_fn_t enter_fn)
{
    s_return_menu_fn = enter_fn;
}

int ui_demo_return_to_menu(void)
{
    /* Drop any leftover dynamic menu screen before re-entering it cleanly. */
    ui_list_menu_destroy_current();

    if (s_return_menu_fn != NULL) {
        return s_return_menu_fn();
    }

    /* Default: the demo center lives on the generated page_3 slot. */
    navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_3,
                       LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                       init_page_page_3);
    return 0;
}
