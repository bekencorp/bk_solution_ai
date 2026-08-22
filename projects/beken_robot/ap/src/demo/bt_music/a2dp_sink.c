/*
 * A2DP sink demo for beken_robot Bluetooth music.
 *
 * A2DP/AVRCP service glue and worker thread live here; audio decode/playback
 * is provided by the SMP SDK component a2dp_sink_audio (bk_bluetooth).
 */
#if CONFIG_BT
#include <components/system.h>
#include <os/mem.h>
#include <os/os.h>
#include <os/str.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "components/log.h"
#include "a2dp_sink_audio.h"
#include "spk_service.h"
#include "demo/bt_a2dp_config.h"
#include "demo/a2dp_sink.h"
#include "demo/bt_rhythm.h"
#include "bk_a2dp_sink_service.h"
#include "bk_avrcp_ct_service.h"
#include "bk_avrcp_tg_service.h"
#include "bluetooth_storage.h"
#include "bt_manager.h"
#include "components/bluetooth/bk_dm_bluetooth.h"
#include "components/bluetooth/bk_dm_avrcp.h"
#include "components/bluetooth/bk_dm_gap_bt.h"

#define TAG "a2dp_sink_demo"

#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW(TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE(TAG, ##__VA_ARGS__)

#define CODEC_AUDIO_SBC 0x00U
#define A2DP_SPK_ENABLE_TIMEOUT_MS 1000U
#define ACL_DISCONNECT_TIMEOUT_MS   5000U
#define ACL_CONNECT_ABORT_TIMEOUT_MS 3000U
#define LINK_DOWN_POLL_MS            100U
#define TEARDOWN_WAIT_MS             (ACL_DISCONNECT_TIMEOUT_MS * 2U)

extern void bt_stop_reconnect_timeout_check(void);

typedef struct
{
    uint8_t type;
    uint16_t len;
    uint8_t *data;
} bt_audio_sink_msg_t;

enum
{
    BT_AUDIO_MSG_NULL = 0,
    BT_AUDIO_A2DP_START_MSG,
    BT_AUDIO_A2DP_SUSPEND_MSG,
    BT_AUDIO_A2DP_DISCONNECT_MSG,
    BT_AUDIO_A2DP_DATA_IND_MSG,
    BT_AUDIO_USER_START_MSG,
    BT_AUDIO_VOLUME_UPDATE_MSG,
    BT_AUDIO_AVRCP_PLAY_STATUS_CHANGED_MSG,
    BT_AUDIO_AVRCP_PLAY,
    BT_AUDIO_AVRCP_PAUSE,
    BT_AUDIO_AVRCP_NEXT,
    BT_AUDIO_AVRCP_PREV,
    BT_AUDIO_AVRCP_VOL_UP,
    BT_AUDIO_AVRCP_VOL_DOWN,
    BT_AUDIO_EXIT_MSG,
};

static beken_queue_t s_a2dp_sink_msg_queue = NULL;
static beken_thread_t s_a2dp_sink_thread = NULL;
static beken_semaphore_t s_audio_player_en_sema = NULL;
static beken_semaphore_t s_a2dp_connect_sema = NULL;
static beken_semaphore_t s_acl_disconnect_sema = NULL;

static uint8_t s_user_spk_enable = 1;
static uint8_t s_mix_multi_channel = 1;
static uint8_t s_a2dp_sink_inited = 0;
static uint8_t s_a2dp_sink_closing = 0;
static uint8_t s_a2dp_connected = 0;
static uint8_t s_bt_manager_index = 0xFF;
static uint8_t s_codec_type = CODEC_AUDIO_SBC;

static uint8_t s_bt_manager_up = 0;
static a2dp_sink_playback_fn_t s_playback_on_start;
static a2dp_sink_playback_fn_t s_playback_on_stop;
static a2dp_sink_playback_fn_t s_stream_on_start;
static a2dp_sink_playback_fn_t s_stream_on_stop;

static int a2dp_sink_queue_push(uint8_t type, const void *data, uint16_t len, uint32_t timeout_ms);
static uint8_t a2dp_sink_get_local_volume(void);
static int a2dp_sink_demo_init(uint8_t aac_supported, uint8_t auto_accept_conn);
static int a2dp_sink_demo_deinit(void);
static void a2dp_sink_post_playback(uint8_t play_status);
static void a2dp_sink_disconnect_a2dp_profile(void);
static void a2dp_sink_abort_link(void);

static void a2dp_sink_post_avrcp(uint8_t type)
{
    if (s_a2dp_sink_closing || !s_a2dp_sink_inited || !bk_avrcp_ct_is_connected())
    {
        return;
    }

    (void)a2dp_sink_queue_push(type, NULL, 0, BEKEN_NO_WAIT);
}

static void a2dp_sink_run_avrcp_transport(uint8_t type)
{
    if (s_a2dp_sink_closing || !bk_avrcp_ct_is_connected())
    {
        return;
    }

    switch (type) {
    case BT_AUDIO_AVRCP_PLAY:
        (void)bk_avrcp_ct_play();
        break;
    case BT_AUDIO_AVRCP_PAUSE:
        (void)bk_avrcp_ct_pause();
        break;
    case BT_AUDIO_AVRCP_NEXT:
        (void)bk_avrcp_ct_next();
        break;
    case BT_AUDIO_AVRCP_PREV:
        (void)bk_avrcp_ct_prev();
        break;
    case BT_AUDIO_AVRCP_VOL_UP:
        (void)bk_avrcp_ct_vol_up();
        break;
    case BT_AUDIO_AVRCP_VOL_DOWN:
        (void)bk_avrcp_ct_vol_down();
        break;
    default:
        break;
    }
}

static void a2dp_sink_start_pairing(void)
{
    uint8_t recon_addr[6] = {0};

    if (bluetooth_storage_get_newest_linkkey_info(recon_addr, NULL) >= 0) {
        bt_manager_start_reconnect(recon_addr, 1);
    } else {
        (void)bk_bt_enter_pairing_mode(1);
    }
}

static void a2dp_sink_emit_playback(uint8_t play_status)
{
    if (play_status == BK_AVRCP_PLAYBACK_PLAYING)
    {
        if (s_playback_on_start) {
            s_playback_on_start();
        }
    }
    else if (play_status == BK_AVRCP_PLAYBACK_PAUSED ||
             play_status == BK_AVRCP_PLAYBACK_STOPPED)
    {
        if (s_playback_on_stop) {
            s_playback_on_stop();
        }
    }
}

static void a2dp_sink_post_playback(uint8_t play_status)
{
    (void)a2dp_sink_queue_push(BT_AUDIO_AVRCP_PLAY_STATUS_CHANGED_MSG,
                               &play_status,
                               sizeof(play_status),
                               BEKEN_NO_WAIT);
}

static int a2dp_sink_queue_push(uint8_t type, const void *data, uint16_t len, uint32_t timeout_ms)
{
    bt_audio_sink_msg_t msg = {0};
    int rc;

    if (!s_a2dp_sink_msg_queue)
    {
        LOGE("%s queue not ready\n", __func__);
        return BK_FAIL;
    }

    if (len)
    {
        if (!data)
        {
            LOGE("%s data is NULL, type %d, len %u\n", __func__, type, len);
            return BK_FAIL;
        }

        msg.data = psram_malloc(len);
        if (!msg.data)
        {
            LOGE("%s malloc failed\n", __func__);
            return BK_FAIL;
        }
        os_memcpy(msg.data, data, len);
        msg.len = len;
    }

    msg.type = type;
    rc = rtos_push_to_queue(&s_a2dp_sink_msg_queue, &msg, timeout_ms);
    if (rc != BK_OK)
    {
        if (msg.data)
        {
            psram_free(msg.data);
        }
        LOGE("%s, send queue failed, type %d, len %u, ret %d\n", __func__, type, len, rc);
        return rc;
    }

    return BK_OK;
}

static void a2dp_sink_msg_release(bt_audio_sink_msg_t *msg)
{
    if (!msg || !msg->data)
    {
        return;
    }

    psram_free(msg->data);
    msg->data = NULL;
}

static void a2dp_sink_gap_event_cb(bk_gap_bt_cb_event_t event, bk_bt_gap_cb_param_t *param)
{
    switch (event)
    {
    case BK_BT_GAP_ACL_CONN_CMPL_STAT_EVT:
        if (s_a2dp_sink_closing && param)
        {
            if (param->acl_conn_cmpl_stat.stat == 0)
            {
                (void)bk_bt_gap_disconnect(param->acl_conn_cmpl_stat.bda, 0x13);
            }
            else if (s_acl_disconnect_sema)
            {
                rtos_set_semaphore(&s_acl_disconnect_sema);
            }
        }
        break;

    case BK_BT_GAP_ACL_DISCONN_CMPL_STAT_EVT:
        /* ACL can drop without a clean A2DP DISCONNECTED callback; force the
         * audio/UI path back to idle so we don't keep "playing" on a dead link. */
        if (s_a2dp_connected)
        {
            LOGW("ACL disconnect, force a2dp stop\n");
            s_a2dp_connected = 0;
            (void)a2dp_sink_queue_push(BT_AUDIO_A2DP_DISCONNECT_MSG, NULL, 0, BEKEN_NO_WAIT);
        }
        if (s_acl_disconnect_sema)
        {
            rtos_set_semaphore(&s_acl_disconnect_sema);
        }
        break;

    case BK_BT_GAP_LINK_KEY_NOTIF_EVT:
        if (param)
        {
            bluetooth_storage_save_volume(param->link_key_notif.bda, DEFAULT_A2DP_VOLUME);
        }
        break;

    default:
        break;
    }
}

static void a2dp_sink_reconnect_fail_cb(void)
{
    /* Active reconnect exhausted its retries (e.g. phone is off or out of
     * range). Leave the sink connectable/discoverable so the phone can still
     * connect later instead of getting stuck in RECONNECTING. */
    LOGW("%s reconnect failed, back to pairing mode\n", __func__);
    bk_bt_enter_pairing_mode(1);
}

static void a2dp_sink_bt_manager_callback_register(void)
{
    if (s_bt_manager_index == 0xFF)
    {
        btm_callback_s btm_cb = {
            .gap_cb = a2dp_sink_gap_event_cb,
            .reconnect_fail_cb = a2dp_sink_reconnect_fail_cb,
        };
        s_bt_manager_index = bt_manager_register_callback(&btm_cb);
    }
}

static void a2dp_sink_bt_manager_callback_unregister(void)
{
    if (s_bt_manager_index != 0xFF)
    {
        bt_manager_unregister_callback(s_bt_manager_index);
        s_bt_manager_index = 0xFF;
    }
}

static uint8_t a2dp_sink_get_local_volume(void)
{
    return bk_avrcp_tg_get_local_volume_value();
}

static int a2dp_sink_addr_valid(const uint8_t *addr)
{
    return addr && (addr[0] | addr[1] | addr[2] | addr[3] | addr[4] | addr[5]);
}

static void a2dp_sink_resolve_peer_addr(uint8_t out[6])
{
    const uint8_t *peer = bt_manager_get_connected_device();

    os_memset(out, 0, 6);
    if (a2dp_sink_addr_valid(peer))
    {
        os_memcpy(out, peer, 6);
        return;
    }
    bt_manager_get_reconnect_device(out);
}

static int a2dp_sink_acl_wait(uint32_t timeout_ms)
{
    int ret;

    if (!s_acl_disconnect_sema &&
        rtos_init_semaphore(&s_acl_disconnect_sema, 1) != BK_OK)
    {
        return BK_FAIL;
    }

    (void)rtos_get_semaphore(&s_acl_disconnect_sema, 0);
    ret = rtos_get_semaphore(&s_acl_disconnect_sema, timeout_ms);
    if (ret == BK_OK)
    {
        return BK_OK;
    }

    /* create_conn_cancel may complete without ACL events */
    if (bt_manager_get_connect_state() == BT_STATE_IDLE)
    {
        return BK_OK;
    }

    return ret;
}

static void a2dp_sink_gap_disconnect_wait(const uint8_t *peer)
{
    if (!a2dp_sink_addr_valid(peer))
    {
        return;
    }

    (void)bk_bt_gap_disconnect((uint8_t *)peer, 0x13);
    if (a2dp_sink_acl_wait(ACL_DISCONNECT_TIMEOUT_MS) != BK_OK)
    {
        LOGW("%s acl wait timeout state=%u\n", __func__, (unsigned)bt_manager_get_connect_state());
    }
}

static void a2dp_sink_abort_link(void)
{
    uint8_t peer[6];
    uint8_t state;

    bt_stop_reconnect_timeout_check();
    a2dp_sink_resolve_peer_addr(peer);
    if (!a2dp_sink_addr_valid(peer))
    {
        return;
    }

    state = bt_manager_get_connect_state();
    if (state == BT_STATE_LINK_CONNECTED ||
        state == BT_STATE_PROFILE_CONNECTED ||
        state == BT_STATE_KEY_MISSING)
    {
        a2dp_sink_gap_disconnect_wait(peer);
        return;
    }

    if (state != BT_STATE_RECONNECTING)
    {
        return;
    }

    (void)bk_bt_gap_create_conn_cancel(peer);
    if (a2dp_sink_acl_wait(ACL_CONNECT_ABORT_TIMEOUT_MS) != BK_OK)
    {
        LOGW("%s abort wait timeout state=%u\n", __func__, (unsigned)bt_manager_get_connect_state());
    }

    state = bt_manager_get_connect_state();
    if (state == BT_STATE_LINK_CONNECTED ||
        state == BT_STATE_PROFILE_CONNECTED ||
        state == BT_STATE_KEY_MISSING)
    {
        a2dp_sink_gap_disconnect_wait(peer);
    }
}

static void a2dp_sink_disconnect_a2dp_profile(void)
{
    const uint8_t *peer = bt_manager_get_connected_device();
    int ret;

    if (!s_a2dp_connected || !peer)
    {
        return;
    }

    if (!s_a2dp_connect_sema)
    {
        if (rtos_init_semaphore(&s_a2dp_connect_sema, 1) != BK_OK)
        {
            LOGE("%s init connect sema fail\n", __func__);
            return;
        }
    }

    (void)rtos_get_semaphore(&s_a2dp_connect_sema, 0);
    ret = bk_a2dp_sink_disconnect((uint8_t *)peer);
    if (ret != BK_OK)
    {
        LOGE("%s bk_a2dp_sink_disconnect err %d\n", __func__, ret);
        return;
    }

    ret = rtos_get_semaphore(&s_a2dp_connect_sema, ACL_DISCONNECT_TIMEOUT_MS);
    if (ret != BK_OK)
    {
        LOGW("%s wait a2dp disconnect timeout a2dp=%u avrcp=%d\n",
             __func__,
             (unsigned)s_a2dp_connected,
             (int)bk_avrcp_ct_is_connected());
    }
}

static void a2dp_sink_demo_task(void *arg)
{
    uint32_t spk_task_start_vote = (1 << BK_A2DP_AUDIO_OPEN_VOTE_USER);
    (void)arg;

    while (1)
    {
        bt_audio_sink_msg_t msg = {0};
        if (rtos_pop_from_queue(&s_a2dp_sink_msg_queue, &msg, BEKEN_WAIT_FOREVER) != BK_OK)
        {
            continue;
        }

        switch (msg.type)
        {
        case BT_AUDIO_A2DP_START_MSG:
            if (msg.data && msg.len == sizeof(bk_a2dp_mcc_t))
            {
                const bk_a2dp_mcc_t *codec = (const bk_a2dp_mcc_t *)msg.data;

                spk_task_start_vote |= (1 << BK_A2DP_AUDIO_OPEN_VOTE_A2DP);
                s_codec_type = codec->type;

                if (a2dp_sink_audio_start(codec,
                                          spk_task_start_vote,
                                          s_mix_multi_channel,
                                          a2dp_sink_get_local_volume()) != BK_OK)
                {
                    LOGE("audio player open failed\n");
                }
                else if (s_stream_on_start) 
                {
                    s_stream_on_start();
                }
            }
            break;

        case BT_AUDIO_A2DP_SUSPEND_MSG:
            spk_task_start_vote &= ~(1 << BK_A2DP_AUDIO_OPEN_VOTE_A2DP);
            a2dp_sink_audio_stop();
            if (s_stream_on_stop) 
            {
                s_stream_on_stop();
            }
            break;

        case BT_AUDIO_A2DP_DISCONNECT_MSG:
            spk_task_start_vote &= ~(1 << BK_A2DP_AUDIO_OPEN_VOTE_A2DP);
            a2dp_sink_audio_stop();
            if (s_stream_on_stop) 
            {
                s_stream_on_stop();
            }
            break;

        case BT_AUDIO_A2DP_DATA_IND_MSG:
            bt_rhythm_feed_sbc(msg.data, msg.len, s_codec_type);
            a2dp_sink_audio_handle_data(msg.data, msg.len);
            break;

        case BT_AUDIO_USER_START_MSG:
            if (msg.data && msg.len == sizeof(uint8_t))
            {
                uint8_t enable = *msg.data;
                if (enable)
                {
                    spk_task_start_vote |= (1 << BK_A2DP_AUDIO_OPEN_VOTE_USER);
                    a2dp_sink_audio_open(spk_task_start_vote,
                                         s_mix_multi_channel,
                                         a2dp_sink_get_local_volume());
                }
                else
                {
                    spk_task_start_vote &= ~(1 << BK_A2DP_AUDIO_OPEN_VOTE_USER);
                    a2dp_sink_audio_stop();
                }
            }
            if (s_audio_player_en_sema)
            {
                rtos_set_semaphore(&s_audio_player_en_sema);
            }
            break;

        case BT_AUDIO_VOLUME_UPDATE_MSG:
            if (msg.data && msg.len == sizeof(uint8_t))
            {
                a2dp_sink_audio_set_gain(*(uint8_t *)msg.data);
            }
            break;

        case BT_AUDIO_AVRCP_PLAY_STATUS_CHANGED_MSG:
            if (msg.data && msg.len == sizeof(uint8_t))
            {
                a2dp_sink_emit_playback(*(uint8_t *)msg.data);
            }
            break;

        case BT_AUDIO_AVRCP_PLAY:
        case BT_AUDIO_AVRCP_PAUSE:
        case BT_AUDIO_AVRCP_NEXT:
        case BT_AUDIO_AVRCP_PREV:
        case BT_AUDIO_AVRCP_VOL_UP:
        case BT_AUDIO_AVRCP_VOL_DOWN:
            a2dp_sink_run_avrcp_transport(msg.type);
            break;

        case BT_AUDIO_EXIT_MSG:
            a2dp_sink_msg_release(&msg);
            spk_task_start_vote &= ~(1 << BK_A2DP_AUDIO_OPEN_VOTE_A2DP);
            a2dp_sink_audio_stop();
            rtos_delete_thread(NULL);
            return;

        default:
            break;
        }

        a2dp_sink_msg_release(&msg);
    }
}

static int a2dp_sink_task_init(void)
{
    bk_err_t ret;

    if (s_a2dp_sink_thread || s_a2dp_sink_msg_queue)
    {
        return BK_OK;
    }

    ret = rtos_init_queue(&s_a2dp_sink_msg_queue,
                          "bt_music_a2dp_q",
                          sizeof(bt_audio_sink_msg_t),
                          BT_AUDIO_SINK_DEMO_MSG_COUNT);
    if (ret != BK_OK)
    {
        LOGE("bt_audio sink demo msg queue failed\n");
        return ret;
    }

    ret = rtos_create_thread(&s_a2dp_sink_thread,
                             A2DP_SINK_DEMO_TASK_PRIORITY,
                             "bt_a2dp_sink",
                             (beken_thread_function_t)a2dp_sink_demo_task,
                             4096,
                             0);
    if (ret != BK_OK)
    {
        LOGE("bt_audio sink demo task fail\n");
        rtos_deinit_queue(&s_a2dp_sink_msg_queue);
        s_a2dp_sink_msg_queue = NULL;
        s_a2dp_sink_thread = NULL;
    }

    return ret;
}

static void a2dp_sink_task_deinit(void)
{
    if (s_a2dp_sink_thread)
    {
        a2dp_sink_queue_push(BT_AUDIO_EXIT_MSG, NULL, 0, BEKEN_WAIT_FOREVER);
        rtos_thread_join(&s_a2dp_sink_thread);
        s_a2dp_sink_thread = NULL;
    }

    if (s_a2dp_sink_msg_queue)
    {
        bt_audio_sink_msg_t msg = {0};
        while (rtos_pop_from_queue(&s_a2dp_sink_msg_queue, &msg, 0) == BK_OK)
        {
            a2dp_sink_msg_release(&msg);
        }
        rtos_deinit_queue(&s_a2dp_sink_msg_queue);
        s_a2dp_sink_msg_queue = NULL;
    }
}

static void on_a2dp_evt(bk_a2dp_sink_evt_t evt, void *arg, void *user_data)
{
    (void)user_data;

    switch (evt)
    {
    case BK_A2DP_SINK_EVT_CONNECTED:
        s_a2dp_connected = 1;
        break;
    case BK_A2DP_SINK_EVT_STREAM_START:
        a2dp_sink_queue_push(BT_AUDIO_A2DP_START_MSG, arg, sizeof(bk_a2dp_mcc_t), BEKEN_NO_WAIT);
        break;
    case BK_A2DP_SINK_EVT_STREAM_SUSPEND:
        a2dp_sink_queue_push(BT_AUDIO_A2DP_SUSPEND_MSG, NULL, 0, BEKEN_NO_WAIT);
        break;
    case BK_A2DP_SINK_EVT_DISCONNECTED:
        s_a2dp_connected = 0;
        if (s_a2dp_connect_sema)
        {
            rtos_set_semaphore(&s_a2dp_connect_sema);
        }
        a2dp_sink_queue_push(BT_AUDIO_A2DP_DISCONNECT_MSG, NULL, 0, BEKEN_NO_WAIT);
        break;
    case BK_A2DP_SINK_EVT_MEDIA_DATA:
    {
        const bk_a2dp_media_data_t *media = arg;
        if (!s_user_spk_enable)
        {
            break;
        }
        if (media && media->data && media->len)
        {
            a2dp_sink_queue_push(BT_AUDIO_A2DP_DATA_IND_MSG, media->data, media->len, 2);
        }
        break;
    }
    case BK_A2DP_SINK_EVT_AUDIO_CFG:
        if (arg)
        {
            s_codec_type = ((const bk_a2dp_mcc_t *)arg)->type;
            a2dp_sink_audio_set_config((bk_a2dp_mcc_t *)arg);
        }
        break;
    default:
        break;
    }
}

static void on_avrcp_tg_evt(bk_avrcp_tg_evt_t evt, void *arg, void *user_data)
{
    (void)user_data;

    switch (evt)
    {
    case BK_AVRCP_TG_EVT_VOLUME_CHANGED:
        a2dp_sink_queue_push(BT_AUDIO_VOLUME_UPDATE_MSG, arg, sizeof(uint8_t), BEKEN_NO_WAIT);
        break;
    default:
        break;
    }
}

static void on_avrcp_ct_evt(bk_avrcp_ct_evt_t evt, void *arg, void *user_data)
{
    (void)user_data;

    switch (evt)
    {
    case BK_AVRCP_CT_EVT_PLAY_STATUS_CHANGED:
    {
        uint8_t play_status = arg ? *(uint8_t *)arg : BK_AVRCP_PLAYBACK_ERROR;
        a2dp_sink_post_playback(play_status);
        break;
    }

    case BK_AVRCP_CT_EVT_TRACK_CHANGED:
        break;

    case BK_AVRCP_CT_EVT_CONNECTED:
    case BK_AVRCP_CT_EVT_DISCONNECTED:
    case BK_AVRCP_CT_EVT_PLAY_POS_CHANGED:
        break;

    default:
        LOGW("Unhandled AVRCP CT event: %d\n", evt);
        break;
    }
}

static int a2dp_sink_demo_init(uint8_t aac_supported, uint8_t auto_accept_conn)
{
    bk_a2dp_sink_cfg_t sink_cfg = {
        .aac_supported = aac_supported,
        .auto_accept_conn = auto_accept_conn,
    };
    bk_avrcp_ct_cfg_t avrcp_ct_cfg = {
        .auto_ct_connect_after_a2dp = 1,
        .remote_volume_mode = 0,
    };
    bk_avrcp_tg_cfg_t avrcp_tg_cfg = {
        .default_volume = DEFAULT_A2DP_VOLUME,
        .player_mode = 0,
    };

    if (aac_supported)
    {
#if (!CONFIG_ADK_AAC_DECODER)
        LOGE("%s AAC is not supported!\n", __func__);
        return BK_FAIL;
#endif
    }

    if (s_a2dp_sink_inited)
    {
        LOGE("%s already init\n", __func__);
        return BK_OK;
    }

    if (!s_audio_player_en_sema)
    {
        if (rtos_init_semaphore(&s_audio_player_en_sema, 1) != BK_OK)
        {
            LOGE("%s audio player semaphore init err\n", __func__);
            return BK_FAIL;
        }
    }

    if (a2dp_sink_task_init() != BK_OK)
    {
        LOGE("%s a2dp_sink_task_init err\n", __func__);
        return BK_FAIL;
    }

    bk_a2dp_sink_register_event_cb(on_a2dp_evt, NULL);
    bk_avrcp_ct_register_event_cb(on_avrcp_ct_evt, NULL);
    bk_avrcp_tg_register_event_cb(on_avrcp_tg_evt, NULL);
    a2dp_sink_bt_manager_callback_register();

    if (bk_a2dp_sink_service_init(&sink_cfg) != BK_OK)
    {
        LOGE("%s bk_a2dp_sink_service_init err\n", __func__);
        a2dp_sink_bt_manager_callback_unregister();
        a2dp_sink_task_deinit();
        return BK_FAIL;
    }

    if (bk_avrcp_ct_service_init(&avrcp_ct_cfg) != BK_OK)
    {
        LOGE("%s bk_avrcp_ct_service_init err\n", __func__);
        bk_a2dp_sink_service_deinit();
        a2dp_sink_bt_manager_callback_unregister();
        a2dp_sink_task_deinit();
        return BK_FAIL;
    }

    if (bk_avrcp_tg_service_init(&avrcp_tg_cfg) != BK_OK)
    {
        LOGE("%s bk_avrcp_tg_service_init err\n", __func__);
        bk_avrcp_ct_service_deinit();
        bk_a2dp_sink_service_deinit();
        a2dp_sink_bt_manager_callback_unregister();
        a2dp_sink_task_deinit();
        return BK_FAIL;
    }

#if AVRCP_MODIFY_SDP_FEAT
    {
        uint16_t feat = 0;
        uint16_t allow = 0;

        bk_bt_avrcp_ct_sdp_feature_operation(BK_AVRCP_SDP_FEATURE_API_METHOD_GET_ALLOWED, &allow);
        bk_bt_avrcp_ct_sdp_feature_operation(BK_AVRCP_SDP_FEATURE_API_METHOD_GET_CURRENT_ENABLE, &feat);
        feat &= ~(BK_AVRCP_SDP_FEATURE_CT_CAT_2 | BK_AVRCP_SDP_FEATURE_CT_CAT_3 | BK_AVRCP_SDP_FEATURE_CT_CAT_4
                        | BK_AVRCP_SDP_FEATURE_CT_SUPPORT_BROWSING | BK_AVRCP_SDP_FEATURE_CT_SUPPORT_CA_GIP | BK_AVRCP_SDP_FEATURE_CT_SUPPORT_CA_GI | BK_AVRCP_SDP_FEATURE_CT_SUPPORT_CA_GLT);
        feat &= allow;
        bk_bt_avrcp_ct_sdp_feature_operation(BK_AVRCP_SDP_FEATURE_API_METHOD_SET, &feat);
        feat = 0;

        bk_bt_avrcp_tg_sdp_feature_operation(BK_AVRCP_SDP_FEATURE_API_METHOD_GET_ALLOWED, &allow);
        bk_bt_avrcp_tg_sdp_feature_operation(BK_AVRCP_SDP_FEATURE_API_METHOD_GET_CURRENT_ENABLE, &feat);
        feat &= ~(BK_AVRCP_SDP_FEATURE_TG_CAT_1 | BK_AVRCP_SDP_FEATURE_TG_CAT_3 | BK_AVRCP_SDP_FEATURE_TG_CAT_4
                        | BK_AVRCP_SDP_FEATURE_TG_PLAYER_APP_SET | BK_AVRCP_SDP_FEATURE_TG_GROUP_NAV | BK_AVRCP_SDP_FEATURE_TG_SUPPORT_BROWSING | BK_AVRCP_SDP_FEATURE_TG_SUPPORT_MULT_MEDIA_PA
                        | BK_AVRCP_SDP_FEATURE_TG_SUPPORT_CA);
        feat &= allow;
        bk_bt_avrcp_tg_sdp_feature_operation(BK_AVRCP_SDP_FEATURE_API_METHOD_SET, &feat);
    }
#endif

    bk_avrcp_tg_emit_current_volume();

    s_a2dp_sink_closing = 0;
    s_a2dp_sink_inited = 1;
    return BK_OK;
}

static int a2dp_sink_demo_deinit(void)
{
    if (!s_a2dp_sink_inited)
    {
        LOGE("%s already deinit\n", __func__);
        return BK_OK;
    }

    s_a2dp_sink_closing = 1;

    a2dp_sink_audio_stop();
    rtos_delay_milliseconds(100);

    a2dp_sink_disconnect_a2dp_profile();
    a2dp_sink_abort_link();
    bt_manager_clear_reconnect_info();
    bk_avrcp_tg_service_deinit();
    bk_avrcp_ct_service_deinit();
    bk_a2dp_sink_service_deinit();
    a2dp_sink_bt_manager_callback_unregister();
    a2dp_sink_task_deinit();

    if (s_audio_player_en_sema)
    {
        rtos_deinit_semaphore(&s_audio_player_en_sema);
        s_audio_player_en_sema = NULL;
    }

    if (s_a2dp_connect_sema)
    {
        rtos_deinit_semaphore(&s_a2dp_connect_sema);
        s_a2dp_connect_sema = NULL;
    }

    if (s_acl_disconnect_sema)
    {
        rtos_deinit_semaphore(&s_acl_disconnect_sema);
        s_acl_disconnect_sema = NULL;
    }

    s_a2dp_connected = 0;
    s_a2dp_sink_closing = 0;
    s_a2dp_sink_inited = 0;
    return BK_OK;
}

void a2dp_sink_demo_begin_teardown(void)
{
    if (s_a2dp_sink_inited)
    {
        s_a2dp_sink_closing = 1;
    }
}

uint8_t a2dp_sink_demo_is_active(void)
{
    return s_a2dp_sink_inited;
}

int a2dp_sink_demo_start(uint8_t aac_supported, uint8_t auto_accept_conn)
{
    uint32_t waited_ms = 0;

    while (s_a2dp_sink_closing && waited_ms < TEARDOWN_WAIT_MS)
    {
        rtos_delay_milliseconds(LINK_DOWN_POLL_MS);
        waited_ms += LINK_DOWN_POLL_MS;
    }
    if (s_a2dp_sink_closing)
    {
        LOGE("%s teardown still running after %ums\n", __func__, (unsigned)waited_ms);
        return BK_FAIL;
    }

    if (s_a2dp_sink_inited)
    {
        return BK_OK;
    }

    if (!s_bt_manager_up) {
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
            return BK_FAIL;
        }
        s_bt_manager_up = 1;
    }

    if (a2dp_sink_demo_init(aac_supported, auto_accept_conn) != BK_OK) {
        LOGE("a2dp_sink_demo_init failed\n");
        (void)bt_manager_deinit();
        s_bt_manager_up = 0;
        return BK_FAIL;
    }

    if (!a2dp_sink_demo_is_connected()) {
        a2dp_sink_start_pairing();
    }
    return BK_OK;
}

int a2dp_sink_demo_stop(void)
{
    int ret = BK_OK;

    if (!s_a2dp_sink_inited && !s_bt_manager_up) {
        return BK_OK;
    }

    s_a2dp_sink_closing = 1;
    a2dp_sink_demo_audio_spk_enable(0);
    if (a2dp_sink_demo_deinit() != BK_OK) {
        LOGE("%s profile teardown incomplete\n", __func__);
        ret = BK_FAIL;
    } else if (s_bt_manager_up) {
        (void)bt_manager_deinit();
        s_bt_manager_up = 0;
    }
    bt_manager_clear_reconnect_info();

    /* a2dp_sink_audio_stop() only detaches A2DP; shared DAC stays in idle
     * linger. Tear it down before resume wifi / audio_engine reclaim DMA. */
    if (spk_service_deinit() != BK_OK) {
        LOGW("%s spk_service_deinit fail\n", __func__);
    }

    return ret;
}

void a2dp_sink_demo_set_playback_listener(a2dp_sink_playback_fn_t on_start,
                                          a2dp_sink_playback_fn_t on_stop)
{
    s_playback_on_start = on_start;
    s_playback_on_stop = on_stop;
}

void a2dp_sink_demo_set_stream_listener(a2dp_sink_playback_fn_t on_start,
                                        a2dp_sink_playback_fn_t on_stop)
{
    s_stream_on_start = on_start;
    s_stream_on_stop = on_stop;
}

void a2dp_sink_demo_play(void)
{
    a2dp_sink_post_avrcp(BT_AUDIO_AVRCP_PLAY);
}

void a2dp_sink_demo_pause(void)
{
    a2dp_sink_post_avrcp(BT_AUDIO_AVRCP_PAUSE);
}

void a2dp_sink_demo_next(void)
{
    a2dp_sink_post_avrcp(BT_AUDIO_AVRCP_NEXT);
}

void a2dp_sink_demo_prev(void)
{
    a2dp_sink_post_avrcp(BT_AUDIO_AVRCP_PREV);
}

void a2dp_sink_demo_vol_up(void)
{
    a2dp_sink_post_avrcp(BT_AUDIO_AVRCP_VOL_UP);
}

void a2dp_sink_demo_vol_down(void)
{
    a2dp_sink_post_avrcp(BT_AUDIO_AVRCP_VOL_DOWN);
}

int32_t a2dp_sink_demo_wait_player_end(void)
{
    return a2dp_sink_audio_wait_player_end();
}

void a2dp_sink_demo_audio_spk_enable(uint8_t enable)
{
    s_user_spk_enable = enable;
    if (a2dp_sink_queue_push(BT_AUDIO_USER_START_MSG, &enable, sizeof(enable), BEKEN_NO_WAIT) == BK_OK &&
        s_audio_player_en_sema)
    {
        int ret = rtos_get_semaphore(&s_audio_player_en_sema, A2DP_SPK_ENABLE_TIMEOUT_MS);
        if (ret != BK_OK)
        {
            LOGW("audio spk enable vote timeout ret=%d\n", ret);
        }
    }
}

uint8_t a2dp_sink_demo_is_connected(void)
{
    return s_a2dp_connected;
}

int32_t a2dp_sink_demo_try_disconnect_current(void)
{
    int32_t ret = BK_OK;

    if (s_a2dp_connected)
    {
        if (!s_a2dp_connect_sema)
        {
            if (rtos_init_semaphore(&s_a2dp_connect_sema, 1) != BK_OK)
            {
                LOGE("%s init connect sema fail\n", __func__);
                return BK_FAIL;
            }
        }

        ret = bk_a2dp_sink_disconnect(bt_manager_get_connected_device());
        if (ret != BK_OK)
        {
            LOGE("%s bk_a2dp_sink_disconnect err %d\n", __func__, ret);
        }
        else
        {
            ret = rtos_get_semaphore(&s_a2dp_connect_sema, 5000);
            if (ret != BK_OK)
            {
                LOGE("%s wait disconnect a2dp sem err %d\n", __func__, ret);
            }
        }
    }

    if (s_a2dp_connect_sema)
    {
        if (rtos_deinit_semaphore(&s_a2dp_connect_sema) != BK_OK)
        {
            LOGE("%s deinit connect sema fail\n", __func__);
        }
        s_a2dp_connect_sema = NULL;
    }

    return ret;
}
#endif /* CONFIG_BT */
