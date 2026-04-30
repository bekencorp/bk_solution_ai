/**
 * @file ui_nav_router.c
 * @brief 当前屏 → ui_page_nav_ops 分发；支持运行时注册任意数量页面（表满需调大 UI_NAV_MAX_SCREENS）
 *
 * 各页 page_N_nav_ops 在各 page_N_init.c 中定义；本文件集中声明并在懒注册中使用。新增页面时在本文件增加
 * extern 与 ui_nav_try_autoreg_builtin_pages 中的分支即可。
 */
#include "ui_nav_router.h"

#include "beken_ui.h"

#include <common/bk_err.h>
#include <common/bk_include.h>
#include <os/os.h>

#include "lvgl.h"
#include "lv_vendor.h"

/* 定义在各 page_N_init.c（自定义代码区）；新增页面时在此增加一行 extern */
extern const ui_page_nav_ops_t page_1_nav_ops;
extern const ui_page_nav_ops_t page_2_nav_ops;
extern const ui_page_nav_ops_t page_3_nav_ops;
extern const ui_page_nav_ops_t page_4_nav_ops;
extern const ui_page_nav_ops_t page_5_nav_ops;


#define UI_NAV_MAX_SCREENS 16

typedef struct {
    lv_obj_t *screen;
    const ui_page_nav_ops_t *ops;
    bool used;
} ui_nav_slot_t;

static ui_nav_slot_t s_slots[UI_NAV_MAX_SCREENS];
static beken_mutex_t s_reg_mutex;

static const ui_page_nav_ops_t *ui_nav_lookup_ops(lv_obj_t *active)
{
    if (active == NULL) {
        return NULL;
    }
    for (int i = 0; i < UI_NAV_MAX_SCREENS; i++) {
        if (s_slots[i].used && s_slots[i].screen == active) {
            return s_slots[i].ops;
        }
    }
    return NULL;
}

/**
 * 对内置 page_1/2/3 做懒注册：切到该屏且尚未 register 时自动挂上各 page_N_init.c 中的 page_N_nav_ops。
 * 新增 page_4 时：可在此增加分支，或（推荐）在 init_page_page_4 末尾显式 ui_nav_register_screen。
 */
static void ui_nav_try_autoreg_builtin_pages(bk_lv_ui_t *ui, lv_obj_t *active)
{
    if (ui == NULL || active == NULL) {
        return;
    }
    if (ui->page_1 != NULL && active == ui->page_1) {
        (void)ui_nav_register_screen(ui->page_1, &page_1_nav_ops);
    } else if (ui->page_2 != NULL && active == ui->page_2) {
        (void)ui_nav_register_screen(ui->page_2, &page_2_nav_ops);
    } else if (ui->page_3 != NULL && active == ui->page_3) {
        (void)ui_nav_register_screen(ui->page_3, &page_3_nav_ops);
    } else if (ui->page_4 != NULL && active == ui->page_4) {
        (void)ui_nav_register_screen(ui->page_4, &page_4_nav_ops);
    } else if (ui->page_5 != NULL && active == ui->page_5) {
        (void)ui_nav_register_screen(ui->page_5, &page_5_nav_ops);
    }
}

void ui_nav_router_init(void)
{
    static bool inited;

    if (inited) {
        return;
    }
    os_memset(s_slots, 0, sizeof(s_slots));
    if (rtos_init_mutex(&s_reg_mutex) != BK_OK) {
        return;
    }
    inited = true;
}

bk_err_t ui_nav_register_screen(lv_obj_t *screen, const ui_page_nav_ops_t *ops)
{
    if (screen == NULL || ops == NULL) {
        return BK_FAIL;
    }

    rtos_lock_mutex(&s_reg_mutex);

    for (int i = 0; i < UI_NAV_MAX_SCREENS; i++) {
        if (s_slots[i].used && s_slots[i].screen == screen) {
            s_slots[i].ops = ops;
            rtos_unlock_mutex(&s_reg_mutex);
            return BK_OK;
        }
    }

    for (int i = 0; i < UI_NAV_MAX_SCREENS; i++) {
        if (!s_slots[i].used) {
            s_slots[i].screen = screen;
            s_slots[i].ops = ops;
            s_slots[i].used = true;
            rtos_unlock_mutex(&s_reg_mutex);
            return BK_OK;
        }
    }

    rtos_unlock_mutex(&s_reg_mutex);
    return BK_ERR_NO_MEM;
}

void ui_nav_unregister_screen(lv_obj_t *screen)
{
    if (screen == NULL) {
        return;
    }
    rtos_lock_mutex(&s_reg_mutex);
    for (int i = 0; i < UI_NAV_MAX_SCREENS; i++) {
        if (s_slots[i].used && s_slots[i].screen == screen) {
            os_memset(&s_slots[i], 0, sizeof(s_slots[i]));
            break;
        }
    }
    rtos_unlock_mutex(&s_reg_mutex);
}

void ui_nav_dispatch_event(ui_nav_event_t ev)
{
    if ((unsigned)ev >= (unsigned)UI_NAV_EVENT_COUNT) {
        return;
    }

    lv_vendor_disp_lock();

    lv_obj_t *active = lv_screen_active();
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    ui_nav_try_autoreg_builtin_pages(ui, active);

    const ui_page_nav_ops_t *ops = ui_nav_lookup_ops(active);
    if (ops == NULL) {
        lv_vendor_disp_unlock();
        return;
    }

    switch (ev) {
    case UI_NAV_EVENT_FOCUS_PREV:
        if (ops->on_focus_prev) {
            ops->on_focus_prev(ui);
        }
        break;
    case UI_NAV_EVENT_FOCUS_NEXT:
        if (ops->on_focus_next) {
            ops->on_focus_next(ui);
        }
        break;
    case UI_NAV_EVENT_SCREEN_PREV:
        if (ops->on_screen_prev) {
            ops->on_screen_prev(ui);
        }
        break;
    case UI_NAV_EVENT_SCREEN_NEXT:
        if (ops->on_screen_next) {
            ops->on_screen_next(ui);
        }
        break;
    case UI_NAV_EVENT_CONFIRM_LONG:
        if (ops->on_confirm_long) {
            ops->on_confirm_long(ui);
        }
        break;
    default:
        break;
    }

    lv_vendor_disp_unlock();
}
