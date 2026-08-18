/**
 * @file ui_nav_router.c
 * @brief Current-screen -> ui_page_nav_ops dispatcher with a small
 *        registry; each page hook registers its own nav ops via
 *        ui_nav_register_screen() at page init and unregisters at
 *        destroy. Increase UI_NAV_MAX_SCREENS if the registry ever
 *        overflows.
 */
#include "ui_nav_router.h"

#include "beken_ui.h"
#include "event_runtime.h"

#include <common/bk_err.h>
#include <common/bk_include.h>
#include <os/os.h>
#include <os/str.h>
#include <stdlib.h>

#include "lvgl.h"
#include "lv_vendor.h"

#include "bk_cli.h"
#include "cli.h"
#include <components/log.h>

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

static void ui_nav_dispatch_event_unlocked(ui_nav_event_t ev)
{
    if ((unsigned)ev >= (unsigned)UI_NAV_EVENT_COUNT) {
        return;
    }

    lv_obj_t *active = lv_screen_active();
    bk_lv_ui_t *ui = &bk_lv_tool_ui;

    const ui_page_nav_ops_t *ops = ui_nav_lookup_ops(active);
    if (ops == NULL) {
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
}

void ui_nav_dispatch_event(ui_nav_event_t ev)
{
    lv_vendor_disp_lock();
    ui_nav_dispatch_event_unlocked(ev);
    lv_vendor_disp_unlock();
}

void ui_nav_dispatch_event_from_lvgl(ui_nav_event_t ev)
{
    ui_nav_dispatch_event_unlocked(ev);
}

/* ------------------------------------------------------------------ */
/* `nav` CLI: simulate key events for headless verification of pages. */
/* ------------------------------------------------------------------ */
#define NAV_CLI_TAG "nav_cli"

/* Page-id table for `nav goto`: maps a human name to the page index used
 * by bk_lv_ui_t::page_N / init_page_page_N. Keep aligned with the
 * `beken_generated/page_<feature>/` directory naming. */
typedef struct {
    const char *name;
    int         page_id;
} nav_cli_page_t;

static const nav_cli_page_t s_nav_cli_pages[] = {
    { "splash",      1  },
    { "top",         2  },
    { "menu",        3  },  /* demo grid (page_3) */
    { "demo_menu",   3  },
    { "provisioning",4  },
    { "prov",        4  },
    { "doa",         5  },
    { "ai_chat",     6  },
    { "vision",      7  },
    { "asr",         8  },
    { "music",       9  },
    { "volume",      10 },
    { "robot_video", 11 },
    { "video",       11 },
};

static int nav_cli_find_page(const char *name)
{
    for (size_t i = 0; i < sizeof(s_nav_cli_pages) / sizeof(s_nav_cli_pages[0]); i++) {
        if (os_strcmp(s_nav_cli_pages[i].name, name) == 0) {
            return s_nav_cli_pages[i].page_id;
        }
    }
    return -1;
}

static void nav_cli_print_help(void)
{
    BK_LOGI(NAV_CLI_TAG, "nav usage:\n");
    BK_LOGI(NAV_CLI_TAG, "  nav prev        - UI_NAV_EVENT_SCREEN_PREV (S2 long)\n");
    BK_LOGI(NAV_CLI_TAG, "  nav next        - UI_NAV_EVENT_SCREEN_NEXT (S4 short)\n");
    BK_LOGI(NAV_CLI_TAG, "  nav left        - UI_NAV_EVENT_FOCUS_PREV  (S2 short)\n");
    BK_LOGI(NAV_CLI_TAG, "  nav right       - UI_NAV_EVENT_FOCUS_NEXT  (S5 short)\n");
    BK_LOGI(NAV_CLI_TAG, "  nav long        - UI_NAV_EVENT_CONFIRM_LONG(S4 long)\n");
    BK_LOGI(NAV_CLI_TAG, "  nav goto <page> - jump directly to <page>\n");
    BK_LOGI(NAV_CLI_TAG, "  nav status      - print active screen + registered pages\n");
    BK_LOGI(NAV_CLI_TAG, "  pages: splash top menu provisioning doa ai_chat vision\n");
    BK_LOGI(NAV_CLI_TAG, "         asr music volume robot_video\n");
}

static int nav_cli_active_page_id(lv_obj_t *active)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    if (active == NULL) {
        return -1;
    }
    if (active == ui->page_1)  return 1;
    if (active == ui->page_2)  return 2;
    if (active == ui->page_3)  return 3;
    if (active == ui->page_4)  return 4;
    if (active == ui->page_5)  return 5;
    if (active == ui->page_6)  return 6;
    if (active == ui->page_7)  return 7;
    if (active == ui->page_8)  return 8;
    if (active == ui->page_9)  return 9;
    if (active == ui->page_10) return 10;
    if (active == ui->page_11) return 11;
    return -1;
}

static void nav_cli_status(void)
{
    lv_vendor_disp_lock();
    lv_obj_t *active = lv_screen_active();
    int       pid    = nav_cli_active_page_id(active);
    const ui_page_nav_ops_t *ops = ui_nav_lookup_ops(active);
    lv_vendor_disp_unlock();

    BK_LOGI(NAV_CLI_TAG, "active page=%d nav_ops=%s\n",
            pid, ops ? "registered" : "MISSING");

    for (int i = 0; i < UI_NAV_MAX_SCREENS; i++) {
        if (s_slots[i].used) {
            BK_LOGI(NAV_CLI_TAG, "  slot[%d]: screen=%p ops=%p\n",
                    i, s_slots[i].screen, s_slots[i].ops);
        }
    }
}

/* `nav goto`: direct page jump. Uses init_page_page_N which fires the
 * matching on_page_init hook, just like a normal navigation does. This
 * bypasses the active screen's nav_ops so it works from any page. */
static void nav_cli_goto(int page_id)
{
    bk_lv_ui_t *ui = &bk_lv_tool_ui;
    lv_obj_t  **slot = NULL;
    void       (*init_fn)(bk_lv_ui_t *) = NULL;

    switch (page_id) {
    case 1:  slot = (lv_obj_t **)&ui->page_1;  init_fn = init_page_page_1;  break;
    case 2:  slot = (lv_obj_t **)&ui->page_2;  init_fn = init_page_page_2;  break;
    case 3:  slot = (lv_obj_t **)&ui->page_3;  init_fn = init_page_page_3;  break;
    case 4:  slot = (lv_obj_t **)&ui->page_4;  init_fn = init_page_page_4;  break;
    case 5:  slot = (lv_obj_t **)&ui->page_5;  init_fn = init_page_page_5;  break;
    case 6:  slot = (lv_obj_t **)&ui->page_6;  init_fn = init_page_page_6;  break;
    case 7:  slot = (lv_obj_t **)&ui->page_7;  init_fn = init_page_page_7;  break;
    case 8:  slot = (lv_obj_t **)&ui->page_8;  init_fn = init_page_page_8;  break;
    case 9:  slot = (lv_obj_t **)&ui->page_9;  init_fn = init_page_page_9;  break;
    case 10: slot = (lv_obj_t **)&ui->page_10; init_fn = init_page_page_10; break;
    case 11: slot = (lv_obj_t **)&ui->page_11; init_fn = init_page_page_11; break;
    default:
        BK_LOGE(NAV_CLI_TAG, "goto: invalid page_id=%d\n", page_id);
        return;
    }

    BK_LOGI(NAV_CLI_TAG, "goto page_id=%d\n", page_id);
    lv_vendor_disp_lock();
    navigate_to_screen(slot, LV_SCR_LOAD_ANIM_NONE, 0, 0, false, init_fn);
    lv_vendor_disp_unlock();
}

static void nav_cli_cmd(char *pcWriteBuffer, int xWriteBufferLen,
                        int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2) {
        nav_cli_print_help();
        return;
    }

    const char *sub = argv[1];

    if (os_strcmp(sub, "help") == 0 || os_strcmp(sub, "?") == 0) {
        nav_cli_print_help();
        return;
    }
    if (os_strcmp(sub, "status") == 0) {
        nav_cli_status();
        return;
    }
    if (os_strcmp(sub, "prev") == 0) {
        BK_LOGI(NAV_CLI_TAG, "dispatch SCREEN_PREV\n");
        ui_nav_dispatch_event(UI_NAV_EVENT_SCREEN_PREV);
        return;
    }
    if (os_strcmp(sub, "next") == 0) {
        BK_LOGI(NAV_CLI_TAG, "dispatch SCREEN_NEXT\n");
        ui_nav_dispatch_event(UI_NAV_EVENT_SCREEN_NEXT);
        return;
    }
    if (os_strcmp(sub, "left") == 0) {
        BK_LOGI(NAV_CLI_TAG, "dispatch FOCUS_PREV\n");
        ui_nav_dispatch_event(UI_NAV_EVENT_FOCUS_PREV);
        return;
    }
    if (os_strcmp(sub, "right") == 0) {
        BK_LOGI(NAV_CLI_TAG, "dispatch FOCUS_NEXT\n");
        ui_nav_dispatch_event(UI_NAV_EVENT_FOCUS_NEXT);
        return;
    }
    if (os_strcmp(sub, "long") == 0) {
        BK_LOGI(NAV_CLI_TAG, "dispatch CONFIRM_LONG\n");
        ui_nav_dispatch_event(UI_NAV_EVENT_CONFIRM_LONG);
        return;
    }
    if (os_strcmp(sub, "goto") == 0) {
        if (argc < 3) {
            BK_LOGE(NAV_CLI_TAG, "goto: missing <page> argument\n");
            nav_cli_print_help();
            return;
        }
        const char *target = argv[2];
        int page_id = nav_cli_find_page(target);
        if (page_id < 0) {
            char *endp = NULL;
            long n = strtol(target, &endp, 10);
            if (endp && *endp == '\0' && n >= 1 && n <= 11) {
                page_id = (int)n;
            }
        }
        if (page_id < 0) {
            BK_LOGE(NAV_CLI_TAG, "goto: unknown page '%s'\n", target);
            nav_cli_print_help();
            return;
        }
        nav_cli_goto(page_id);
        return;
    }

    BK_LOGE(NAV_CLI_TAG, "unknown subcommand: %s\n", sub);
    nav_cli_print_help();
}

static const struct cli_command s_nav_cli_cmds[] = {
    { "nav", "nav <prev|next|left|right|long|goto <page>|status>", nav_cli_cmd },
};

void ui_nav_router_cli_init(void)
{
    static bool registered;
    if (registered) {
        return;
    }
    int ret = cli_register_commands(s_nav_cli_cmds,
                                    sizeof(s_nav_cli_cmds) / sizeof(s_nav_cli_cmds[0]));
    if (ret == 0) {
        registered = true;
        BK_LOGI(NAV_CLI_TAG, "nav CLI registered\n");
    } else {
        BK_LOGE(NAV_CLI_TAG, "cli_register_commands failed: %d\n", ret);
    }
}
