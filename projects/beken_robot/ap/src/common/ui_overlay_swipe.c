#include "ui_overlay_swipe.h"

#include <common/bk_err.h>
#include <common/sys_config.h>
#include <common/avdk_pixel_types.h>
#include <stdbool.h>
#include <stdint.h>

#if CONFIG_TP
#include <os/os.h>
#include <driver/drv_tp.h>
#include <components/log.h>

#define TAG "overlay_swipe"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)

#define OVERLAY_SWIPE_TASK_STACK_SIZE  (1024 * 3)
#define OVERLAY_SWIPE_TASK_NAME        "ov_swipe"
#define OVERLAY_SWIPE_POLL_MS          20
#define OVERLAY_SWIPE_MIN_DX           120
#define OVERLAY_SWIPE_MAX_DY           120
#define OVERLAY_SWIPE_EDGE_X           42
#define OVERLAY_SWIPE_MAX_MS           1500U
#define OVERLAY_SWIPE_DEBOUNCE_MS      500U
#define OVERLAY_SWIPE_STOP_WAIT_MS     400U

typedef struct {
    bool pressed;
    int32_t start_x;
    int32_t start_y;
    uint32_t start_ms;
} overlay_swipe_state_t;

typedef struct {
    int32_t x;
    int32_t y;
} overlay_point_t;

static beken_thread_t s_swipe_thread;
static volatile bool s_swipe_running;
static volatile bool s_swipe_stop_req;
static ui_overlay_swipe_back_cb_t s_back_cb;
static void *s_back_arg;
static uint32_t s_last_back_ms;
static int32_t s_tp_raw_w = 320;
static int32_t s_tp_raw_h = 385;
static int s_tp_rotation = ROTATE_NONE;

static int32_t abs_i32(int32_t v)
{
    return (v >= 0) ? v : -v;
}

static overlay_point_t overlay_tp_to_screen(const tp_point_infor_t *point)
{
    overlay_point_t screen;

    switch (s_tp_rotation) {
    case ROTATE_90:
        screen.x = s_tp_raw_h - (int32_t)point->m_y - 1;
        screen.y = (int32_t)point->m_x;
        break;
    case ROTATE_180:
        screen.x = s_tp_raw_w - (int32_t)point->m_x - 1;
        screen.y = s_tp_raw_h - (int32_t)point->m_y - 1;
        break;
    case ROTATE_270:
        screen.x = (int32_t)point->m_y;
        screen.y = s_tp_raw_w - (int32_t)point->m_x - 1;
        break;
    case ROTATE_NONE:
    default:
        screen.x = (int32_t)point->m_x;
        screen.y = (int32_t)point->m_y;
        break;
    }

    return screen;
}

static bool overlay_swipe_check_right(overlay_swipe_state_t *st,
                                      const tp_point_infor_t *point)
{
    uint32_t now = rtos_get_time();
    overlay_point_t screen = overlay_tp_to_screen(point);

    if (point->m_state) {
        if (!st->pressed) {
            st->pressed = true;
            st->start_x = screen.x;
            st->start_y = screen.y;
            st->start_ms = now;
        }
        return false;
    }

    if (!st->pressed) {
        return false;
    }

    uint32_t elapsed = now - st->start_ms;
    int32_t dx = screen.x - st->start_x;
    int32_t dy = abs_i32(screen.y - st->start_y);
    st->pressed = false;

    if (elapsed > OVERLAY_SWIPE_MAX_MS ||
        st->start_x > OVERLAY_SWIPE_EDGE_X ||
        dx < OVERLAY_SWIPE_MIN_DX ||
        dy > OVERLAY_SWIPE_MAX_DY) {
        return false;
    }

    if (s_last_back_ms != 0 && now - s_last_back_ms < OVERLAY_SWIPE_DEBOUNCE_MS) {
        return false;
    }

    s_last_back_ms = now;
    return true;
}

void ui_overlay_swipe_set_display_transform(int raw_w, int raw_h, int rotation)
{
    if (raw_w > 0 && raw_h > 0) {
        s_tp_raw_w = raw_w;
        s_tp_raw_h = raw_h;
    }

    switch (rotation) {
    case ROTATE_NONE:
    case ROTATE_90:
    case ROTATE_180:
    case ROTATE_270:
        s_tp_rotation = rotation;
        break;
    default:
        s_tp_rotation = ROTATE_NONE;
        break;
    }
}

static void overlay_swipe_mark_stopped(void)
{
    s_swipe_running = false;
    s_swipe_thread = NULL;
}

static void overlay_swipe_clear_callback(void)
{
    s_back_cb = NULL;
    s_back_arg = NULL;
}

static void overlay_swipe_task(void *arg)
{
    (void)arg;
    overlay_swipe_state_t state = {0};

    while (!s_swipe_stop_req) {
        tp_point_infor_t point;
        bool got_point = false;

        while (!s_swipe_stop_req && drv_tp_read(&point) == BK_OK) {
            got_point = true;
            if (overlay_swipe_check_right(&state, &point)) {
                ui_overlay_swipe_back_cb_t cb = s_back_cb;
                void *cb_arg = s_back_arg;
                LOGI("right swipe -> overlay back\n");
                s_swipe_stop_req = true;
                overlay_swipe_mark_stopped();
                overlay_swipe_clear_callback();
                if (cb != NULL) {
                    cb(cb_arg);
                }
                rtos_delete_thread(NULL);
                return;
            }
            if (!point.m_need_continue) {
                break;
            }
        }

        if (!got_point) {
            rtos_delay_milliseconds(OVERLAY_SWIPE_POLL_MS);
        }
    }

    overlay_swipe_mark_stopped();
    rtos_delete_thread(NULL);
}

int ui_overlay_swipe_back_start(ui_overlay_swipe_back_cb_t cb, void *arg)
{
    if (cb == NULL) {
        return BK_FAIL;
    }

    ui_overlay_swipe_back_stop();
    if (s_swipe_running) {
        LOGW("previous overlay swipe task still running\n");
        return BK_FAIL;
    }

    s_back_cb = cb;
    s_back_arg = arg;
    s_swipe_stop_req = false;
    s_swipe_running = true;

    bk_err_t ret = rtos_create_thread(&s_swipe_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      OVERLAY_SWIPE_TASK_NAME,
                                      (beken_thread_function_t)overlay_swipe_task,
                                      OVERLAY_SWIPE_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        overlay_swipe_mark_stopped();
        overlay_swipe_clear_callback();
        LOGW("create overlay swipe task failed: %d\n", ret);
        return ret;
    }

    return BK_OK;
}

void ui_overlay_swipe_back_stop(void)
{
    s_swipe_stop_req = true;
    uint32_t waited_ms = 0;
    while (s_swipe_running && waited_ms < OVERLAY_SWIPE_STOP_WAIT_MS) {
        rtos_delay_milliseconds(OVERLAY_SWIPE_POLL_MS);
        waited_ms += OVERLAY_SWIPE_POLL_MS;
    }
    if (!s_swipe_running) {
        overlay_swipe_clear_callback();
    }
}

#else

void ui_overlay_swipe_set_display_transform(int raw_w, int raw_h, int rotation)
{
    (void)raw_w;
    (void)raw_h;
    (void)rotation;
}

int ui_overlay_swipe_back_start(ui_overlay_swipe_back_cb_t cb, void *arg)
{
    (void)cb;
    (void)arg;
    return -1;
}

void ui_overlay_swipe_back_stop(void)
{
}

#endif
