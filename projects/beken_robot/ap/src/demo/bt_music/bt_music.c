/**
 * @file bt_music.c
 * @brief Bluetooth A2DP music + hand-rhythm demo backend (beken_robot).
 *
 * Lifecycle (bk_demo_iface_t):
 *   - bt_music_init():  lightweight, called once at boot by demo_registry. We do
 *                       NOT bring up the classic-BT stack here to avoid touching
 *                       boot / BLE provisioning ordering.
 *   - bt_music_start(): lazy one-time BT bring-up (bt_manager + A2DP sink), frees
 *                       the DAC from the voice engine, starts the rhythm engine
 *                       (hand begins to groove) and enters the bt_music page.
 *   - bt_music_stop():  parks the hand, tears down audio/A2DP/classic-BT and
 *                       releases IRAM so other demos can use the heap.
 *
 * A small worker thread serializes the (potentially blocking) BT/AVRCP calls so
 * the LVGL UI thread never stalls -- same pattern as rhythm_robot's robot_app.c.
 */
#include <os/os.h>
#include <stdio.h>
#include <stdbool.h>
#include <components/system.h>
#include <components/log.h>
#if CONFIG_BT
#include "components/bluetooth/bk_dm_bluetooth.h"
#include "bluetooth_storage.h"
#include "bt_manager.h"
#include "bk_avrcp_ct_service.h"
#endif
#include "demo/a2dp_sink.h"
#include "demo/bt_a2dp_config.h"
#include "demo/bt_rhythm.h"
#include "demo/bt_music.h"
#include "audio_engine.h"
#include "lvgl.h"

/* Implemented in beken_generated/page_bt_music/page_bt_music.c. */
int page_bt_music_enter(void);
void page_bt_music_show_low_mem_hint(void);

#define TAG "bt_music"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define BT_MUSIC_TASK_PRIORITY   (4)
#define BT_MUSIC_TASK_STACK      (2048)
#define BT_MUSIC_QUEUE_LEN       (16)
#define BT_MUSIC_START_MIN_HEAP  (18U * 1024U)
#define BT_MUSIC_TEARDOWN_WAIT_MS 8000U

typedef enum {
    BT_MUSIC_EVT_PAIRING = 0,
    BT_MUSIC_EVT_VOL_UP,
    BT_MUSIC_EVT_VOL_DOWN,
    BT_MUSIC_EVT_PLAY_PAUSE,
    BT_MUSIC_EVT_NEXT,
    BT_MUSIC_EVT_PREV,
    BT_MUSIC_EVT_DANCE_TOGGLE,
    BT_MUSIC_EVT_TEARDOWN,
} bt_music_evt_type_t;

typedef struct {
    uint8_t type;
    uint32_t arg;
} bt_music_evt_t;

static beken_queue_t s_queue;
static beken_thread_t s_thread;
static beken_semaphore_t s_teardown_done;
static bool s_dance_user_enabled = true;
static bool s_page_active;
static bool s_render_ready;
static bool s_playing;
static lv_timer_t *s_page_enter_timer = NULL;
#if CONFIG_BT
static uint8_t s_bt_ready;       /* classic BT stack brought up */
#endif

static void bt_music_apply_rhythm_state(void)
{
    /* Engine follows playback (UI keeps animating); claw gates only the hand. */
    bt_rhythm_set_enabled(s_page_active && s_render_ready && s_playing);
    bt_rhythm_set_hand_output(s_dance_user_enabled);
}

/* ---------------- one-time classic-BT bring-up ---------------- */

static int bt_music_bringup(void)
{
#if CONFIG_BT
    if (s_bt_ready) {
        return 0;
    }

    uint8_t bt_mac[6] = {0};
    static char local_name[30] = {0};
    if (bk_bluetooth_get_address(bt_mac) == BK_OK) {
        snprintf(local_name, sizeof(local_name), "%s_%02x%02x%02x",
                 LOCAL_NAME, bt_mac[2], bt_mac[1], bt_mac[0]);
    } else {
        snprintf(local_name, sizeof(local_name), "%s", LOCAL_NAME);
    }


    bt_manager_cfg_t cfg = {
        .local_name = local_name,
        .device_class = COD_SOUNDBAR,
        .page_scan_interval = PAGE_SCAN_INTV,
        .page_scan_window = PAGE_SCAN_WIN,
        .page_timeout = CONFIG_PAGE_TIMEOUT,
        .reconnect_interval_ms = CONFIG_RECONN_INTERVAL,
        .max_reconnect_count = CONFIG_MAX_RECONN_COUNT,
        .io_capability = BK_BT_IO_CAP_NONE,
    };
    if (bt_manager_init(&cfg) != BK_OK) {
        LOGE("bt_manager_init failed\n");
        return -1;
    }

    if (a2dp_sink_demo_init(0 /*aac off*/, 1 /*auto-accept*/) != BK_OK) {
        LOGE("a2dp_sink_demo_init failed\n");
        (void)bt_manager_deinit();
        return -1;
    }

    s_bt_ready = 1;
    LOGI("classic BT + A2DP sink up, name=%s\n", local_name);
#endif
    return 0;
}

/* ---------------- worker ---------------- */

#if CONFIG_BT
static void bt_music_bringdown_locked(bool pause_remote)
{
    if (!s_bt_ready) {
        return;
    }

    if (pause_remote) {
        bk_avrcp_ct_pause();
    }
    a2dp_sink_demo_audio_spk_enable(0);
    (void)a2dp_sink_demo_wait_player_end();
    (void)a2dp_sink_demo_try_disconnect_current();
    (void)a2dp_sink_demo_deinit();
    (void)bt_manager_deinit();
    s_bt_ready = 0;

    LOGI("classic BT + A2DP sink down\n");
}
#endif

static void bt_music_handle(const bt_music_evt_t *evt)
{
    switch (evt->type) {
#if CONFIG_BT
    case BT_MUSIC_EVT_TEARDOWN:
        bt_music_bringdown_locked(evt->arg != 0);
        break;
    case BT_MUSIC_EVT_PAIRING:
        if (!s_bt_ready) {
            break;
        }
        {
            uint8_t recon_addr[6] = {0};
            if (bluetooth_storage_get_newest_linkkey_info(recon_addr, NULL) >= 0) {
                LOGI("bt_music reconnect %02x:%02x:%02x:%02x:%02x:%02x\n",
                     recon_addr[5], recon_addr[4], recon_addr[3],
                     recon_addr[2], recon_addr[1], recon_addr[0]);
                bt_manager_start_reconnect(recon_addr, 1);
            } else {
                LOGI("bt_music no bonded phone, enter pairing mode\n");
                bk_bt_enter_pairing_mode(1);
            }
        }
        break;
    case BT_MUSIC_EVT_VOL_UP:
        if (!s_bt_ready) {
            break;
        }
        bk_avrcp_ct_vol_up();
        break;
    case BT_MUSIC_EVT_VOL_DOWN:
        if (!s_bt_ready) {
            break;
        }
        bk_avrcp_ct_vol_down();
        break;
    case BT_MUSIC_EVT_PLAY_PAUSE:
        if (!s_bt_ready) {
            break;
        }
        if (s_playing) {
            bk_avrcp_ct_pause();
            s_playing = false;
        } else {
            bk_avrcp_ct_play();
            s_playing = true;
        }
        bt_music_apply_rhythm_state();
        break;
    case BT_MUSIC_EVT_NEXT:
        if (!s_bt_ready) {
            break;
        }
        bk_avrcp_ct_next();
        break;
    case BT_MUSIC_EVT_PREV:
        if (!s_bt_ready) {
            break;
        }
        bk_avrcp_ct_prev();
        break;
#endif
    case BT_MUSIC_EVT_DANCE_TOGGLE:
        s_dance_user_enabled = !s_dance_user_enabled;
        bt_music_apply_rhythm_state();
        break;
    default:
        break;
    }
}

static void bt_music_task(void *arg)
{
    (void)arg;
    while (1) {
        bt_music_evt_t evt = {0};
        if (rtos_pop_from_queue(&s_queue, &evt, BEKEN_WAIT_FOREVER) == BK_OK) {
            bool teardown_evt = (evt.type == BT_MUSIC_EVT_TEARDOWN);
            bt_music_handle(&evt);
            if (teardown_evt && s_teardown_done != NULL) {
                (void)rtos_set_semaphore(&s_teardown_done);
            }
        }
    }
}

static void bt_music_post(bt_music_evt_type_t type, uint32_t arg)
{
    if (s_queue == NULL) {
        LOGW("queue not ready, drop %d\n", type);
        return;
    }
    bt_music_evt_t evt = { .type = (uint8_t)type, .arg = arg };
    (void)rtos_push_to_queue(&s_queue, &evt, BEKEN_NO_WAIT);
}

/* ---------------- demo iface ---------------- */

int bt_music_init(void)
{
    if (s_thread || s_queue) {
        return 0;
    }
    if (rtos_init_queue(&s_queue, "bt_music_q", sizeof(bt_music_evt_t),
                        BT_MUSIC_QUEUE_LEN) != BK_OK) {
        LOGE("queue init failed\n");
        return -1;
    }
    if (rtos_create_thread(&s_thread, BT_MUSIC_TASK_PRIORITY, "bt_music",
                           (beken_thread_function_t)bt_music_task,
                           BT_MUSIC_TASK_STACK, 0) != BK_OK) {
        LOGE("thread create failed\n");
        rtos_deinit_queue(&s_queue);
        s_queue = NULL;
        return -1;
    }
    if (s_teardown_done == NULL &&
        rtos_init_semaphore(&s_teardown_done, 1) != BK_OK) {
        LOGE("teardown semaphore init failed\n");
    }
    LOGI("bt_music init ok\n");
    return 0;
}

/* ---------------- deferred UI entry ---------------- */

static void bt_music_deferred_page_enter(lv_timer_t *timer)
{
    (void)timer;
    s_page_enter_timer = NULL;
    if (page_bt_music_enter() != 0) {
        LOGW("bt_music page rejected by low heap, stop backend\n");
        (void)bt_music_stop();
    }
}

static void bt_music_schedule_page_enter(void)
{
    if (s_page_enter_timer != NULL) {
        lv_timer_delete(s_page_enter_timer);
        s_page_enter_timer = NULL;
    }
    s_page_enter_timer = lv_timer_create(bt_music_deferred_page_enter, 100, NULL);
    if (s_page_enter_timer != NULL) {
        lv_timer_set_repeat_count(s_page_enter_timer, 1);
    } else {
        if (page_bt_music_enter() != 0) {
            LOGW("bt_music page immediate enter rejected, stop backend\n");
            (void)bt_music_stop();
        }
    }
}

int bt_music_start(void)
{
    uint32_t heap_before_bt;

    if (bt_music_init() != 0) {
        return -1;
    }

    /* Free voice/ASR heap before classic BT stack allocates. */
    if (audio_engine_is_running()) {
        (void)audio_engine_stop();
    }

    heap_before_bt = rtos_get_free_heap_size();
    if (heap_before_bt < BT_MUSIC_START_MIN_HEAP) {
        LOGW("bt_music start blocked, iram free=%u min=%u\n",
             (unsigned)heap_before_bt,
             (unsigned)rtos_get_minimum_free_heap_size());
        page_bt_music_show_low_mem_hint();
        return -1;
    }

    if (bt_music_bringup() != 0) {
        return -1;
    }
    s_page_active = true;
    s_render_ready = false;
    s_dance_user_enabled = true;
    s_playing = false;

#if CONFIG_BT
    if (!a2dp_sink_demo_is_connected()) {
        bt_music_post(BT_MUSIC_EVT_PAIRING, 0);
    }
#endif
    bt_music_schedule_page_enter();
    return 0;
}

int bt_music_stop(void)
{
    bool was_playing = s_playing;

    if (s_page_enter_timer != NULL) {
        lv_timer_delete(s_page_enter_timer);
        s_page_enter_timer = NULL;
    }
    s_page_active = false;
    s_render_ready = false;
    s_playing = false;
    bt_rhythm_deinit();
#if CONFIG_BT
    if (s_bt_ready) {
        if (s_queue != NULL && s_teardown_done != NULL) {
            bt_music_evt_t evt = {
                .type = (uint8_t)BT_MUSIC_EVT_TEARDOWN,
                .arg = was_playing ? 1U : 0U,
            };
            if (rtos_push_to_queue(&s_queue, &evt, BEKEN_WAIT_FOREVER) == BK_OK) {
                int ret = rtos_get_semaphore(&s_teardown_done, BT_MUSIC_TEARDOWN_WAIT_MS);
                if (ret != BK_OK) {
                    LOGW("bt_music teardown wait timeout, force bringdown\n");
                    bt_music_bringdown_locked(was_playing);
                    (void)rtos_set_semaphore(&s_teardown_done);
                }
            } else {
                LOGW("bt_music teardown queue push failed, force bringdown\n");
                bt_music_bringdown_locked(was_playing);
            }
        } else {
            bt_music_bringdown_locked(was_playing);
        }
    }
#endif
    return 0;
}

void bt_music_play_pause(void)   { bt_music_post(BT_MUSIC_EVT_PLAY_PAUSE, 0); }
void bt_music_next(void)         { bt_music_post(BT_MUSIC_EVT_NEXT, 0); }
void bt_music_prev(void)         { bt_music_post(BT_MUSIC_EVT_PREV, 0); }
void bt_music_vol_up(void)       { bt_music_post(BT_MUSIC_EVT_VOL_UP, 0); }
void bt_music_vol_down(void)     { bt_music_post(BT_MUSIC_EVT_VOL_DOWN, 0); }
void bt_music_dance_toggle(void) { bt_music_post(BT_MUSIC_EVT_DANCE_TOGGLE, 0); }
bool bt_music_is_dancing(void)   { return s_dance_user_enabled; }
bool bt_music_is_playing(void)   { return s_playing; }

void bt_music_get_bands(uint8_t *low, uint8_t *mid, uint8_t *high)
{
    bt_rhythm_get_bands(low, mid, high);
}

void bt_music_get_spectrum(uint8_t *bands, uint8_t count)
{
    bt_rhythm_get_spectrum(bands, count);
}

void bt_music_get_pose_levels(uint8_t *levels, uint8_t count)
{
    bt_rhythm_get_pose_levels(levels, count);
}

bool bt_music_is_connected(void)
{
#if CONFIG_BT
    return a2dp_sink_demo_is_connected() != 0;
#else
    return false;
#endif
}

void bt_music_on_stream_start(void)
{
    s_playing = true;
    bt_music_apply_rhythm_state();
}

void bt_music_on_stream_stop(void)
{
    s_playing = false;
    bt_music_apply_rhythm_state();
}

void bt_music_enable_speaker_after_first_paint(void)
{
#if CONFIG_BT
    /* User vote so the A2DP audio player can open when a stream starts.
     * Delay it until after the first LVGL paint to avoid stacking audio-player
     * buffers on top of rounded UI draw allocations. */
    s_render_ready = true;
    bt_music_apply_rhythm_state();
    a2dp_sink_demo_audio_spk_enable(1);
#endif
}

const bk_demo_iface_t g_demo_bt_music = {
    .name  = "bt_music",
    .init  = bt_music_init,
    .start = bt_music_start,
    .stop  = bt_music_stop,
};
