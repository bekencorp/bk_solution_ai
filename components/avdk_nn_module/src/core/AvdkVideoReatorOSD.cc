#include "AvdkDetectionModel.h"
#include <common/bk_include.h>
#include <os/mem.h>
#include <os/str.h>
#include <os/os.h>
#include <stdint.h>
#include <driver/int.h>
#include <common/bk_err.h>

#include <components/bk_flexa_bond.h>

#include "AvdkVideoReatorOSD.h"

#include "app_camera.h"
#include "app_camera_types.h"
#include "app_display.h"



#if (CONFIG_PSRAM_WRITE_THROUGH)
#include <driver/psram_types.h>
#include <driver/psram.h>
#endif

#if CONFIG_VG_LITE_GPU
#include "app_gpu.h"
#endif

#include "driver/drv_tp.h"

static const char* TAG = "vi-reator";


#define WORKER_READ_TIMEOUT_MS  1000

#define LOGI(...) BK_LOGW((char*)TAG, ##__VA_ARGS__)
#define LOGW(...) BK_LOGW((char*)TAG, ##__VA_ARGS__)
#define LOGE(...) BK_LOGE((char*)TAG, ##__VA_ARGS__)
#define LOGD(...) BK_LOGD((char*)TAG, ##__VA_ARGS__)
#define LOGV(...)

AvdkVideoReatorOSD::AvdkVideoReatorOSD(AvdkDetectionModel *detection_model)
{
    this->detection_model = detection_model;
    this->infer_thread_running = 0;
    this->display_thread_running = 0;
    this->infer_thread = NULL;
    this->display_thread = NULL;
    this->thread = NULL;
    this->soruce_frame = NULL;
    this->display_frame = NULL;
    this->frame_size = 0;
    this->detect_enable = 0;

    this->isp_gpu_bond = NULL;

    this->infer_thread_sem = NULL;
    this->display_thread_sem = NULL;
    this->worker_exited_sem = NULL;

    /* Initialize semaphore for infer thread synchronization */
    rtos_init_semaphore_ex(&infer_thread_sem, 1, 0);
    /* Initialize semaphore for display thread synchronization */
    rtos_init_semaphore_ex(&display_thread_sem, 1, 0);

    this->worker_stop_req = 0;
    rtos_init_semaphore_ex(&worker_exited_sem, 1, 0);
}

AvdkVideoReatorOSD::~AvdkVideoReatorOSD()
{
    stop();

    if (infer_thread_sem != NULL) {
        rtos_deinit_semaphore(&infer_thread_sem);
        infer_thread_sem = NULL;
    }

    if (display_thread_sem != NULL) {
        rtos_deinit_semaphore(&display_thread_sem);
        display_thread_sem = NULL;
    }

    if (worker_exited_sem != NULL) {
        rtos_deinit_semaphore(&worker_exited_sem);
        worker_exited_sem = NULL;
    }
}

static void WorkerThreadEntry(void *arg)
{
    if (arg)
        ((AvdkVideoReatorOSD *)arg)->WorkerThread();
}

void AvdkVideoReatorOSD::WorkerThread()
{
    LOGI("AvdkVideoReatorOSD::WorkerThread\n");
    int ret = BK_OK;

    while(1)
    {
        if (worker_stop_req) {
            break;
        }

        ret = ReadCameraFrame(soruce_frame, frame_size, WORKER_READ_TIMEOUT_MS);
        if (ret != BK_OK) {
            LOGD("read frame failed ret=%d, retry\n", ret);
        }

        if (worker_stop_req) {
            break;
        }

        if (ret != BK_OK)
        {
            LOGD("read frame failed ret=%d, retry\n", ret);
            rtos_delay_milliseconds(20);
            continue;
        }

        LOGV("read frame: %p, size: %d, %d\n", soruce_frame, frame_size, ret);

        ret = detection_model->run(soruce_frame, frame_size, BK_PIXEL_FORMAT_BGRA8888);
        if (ret < 0) {
            LOGD("run model failed ret=%d, retry\n", ret);
        }
    }

    LOGI("############### Thread Exit ################\n");

    if (worker_exited_sem) {
        rtos_set_semaphore(&worker_exited_sem);
    }
    rtos_delete_thread(NULL);
}

int AvdkVideoReatorOSD::init()
{
    return init(true);
}

int AvdkVideoReatorOSD::init(bool init_model)
{
    LOGI("AvdkVideoReatorOSD::init\n");

    if (init_model) {
        detection_model->LogEnable(true);
        int ret = detection_model->init();
        if (ret != BK_OK)
        {
            LOGE("detection model init fail, ret: %d\n", ret);
            return ret;
        }
    }

    if (BK_PIXEL_FORMAT_RGB888 == detection_model->getFormat())
    {
        frame_size = detection_model->getWidth() * detection_model->getHeight() * 4;
    }
    else
    {
        frame_size = bk_image_size_get(detection_model->getWidth(), detection_model->getHeight(), detection_model->getFormat());
    }

    soruce_frame = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_CODED, frame_size);

    if (soruce_frame == NULL)
    {
        LOGE("soruce_frame malloc fail, size: %d\n", frame_size);
        (void)detection_model->deinit();
        return BK_FAIL;
    }

    return BK_OK;
}

int AvdkVideoReatorOSD::start()
{
    int ret = rtos_create_hsram_thread(&thread,
                                        BEKEN_DEFAULT_WORKER_PRIORITY,
                                        "worker_thread",
                                        (beken_thread_function_t)WorkerThreadEntry,
                                        1024 * 8,
                                        (void *)this);

    if (ret != BK_OK)
    {
        LOGE("create worker_thread fail\n");
        return ret;
    }

    return 0;
}

int AvdkVideoReatorOSD::stop()
{
    LOGI("AvdkVideoReatorOSD::stop\n");

    if (thread != NULL) {
        worker_stop_req = 1;

        if (worker_exited_sem != NULL) {
            bk_err_t wret = rtos_get_semaphore(&worker_exited_sem, 5000);
            if (wret != BK_OK) {
                LOGE("worker did not exit in time (%d)\n", wret);
                return BK_FAIL;
            }
            /* Worker sets the semaphore just before self-delete; yield once so
             * the RTOS can run the delete/idle cleanup before memory snapshots. */
            rtos_delay_milliseconds(20);
        }
        thread = NULL;
    }

    if (soruce_frame != NULL) {
        bk_frame_buffer_free(soruce_frame);
        soruce_frame = NULL;
    }

    return BK_OK;
}

int AvdkVideoReatorOSD::OpenISPCamera()
{
    LOGI("AvdkVideoReatorOSD::OpenISPCamera\n");

    camera_board_config_t *board_config = app_camera_board_config_get();
    if (board_config == NULL) {
        LOGE("%s, camera board config is NULL\n", __func__);
        return BK_FAIL;
    }

    board_config->isp.sp_width = detection_model->getWidth();
    board_config->isp.sp_height = detection_model->getHeight();
    board_config->isp.sp_format = detection_model->getFormat();

    int ret = app_isp_mipi_camera_turn_on(board_config);
    if (ret != BK_OK) {
        LOGE("%s, app_isp_mipi_camera_turn_on failed, ret=%d\n", __func__, ret);
        return ret;
    }

    ret = app_isp_camera_sp_channel_turn_on(board_config);
    if (ret != BK_OK) {
        LOGE("%s, app_isp_camera_sp_channel_turn_on failed, ret=%d\n", __func__, ret);
        (void)app_isp_camera_turn_off();
        return ret;
    }

    return BK_OK;
}

#if (CONFIG_PSRAM_WRITE_THROUGH)
static psram_write_through_area_t s_psram_cover_area = PSRAM_WRITE_THROUGH_AREA_COUNT;
#endif

static void *gpu_frame_malloc(uint32_t size)
{
    void *disp_frame = NULL;

    disp_frame = bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED, size);
    if (disp_frame == NULL)
    {
        LOGE("GPU failed to malloc frame\r\n");
        return NULL;
    }

#if (CONFIG_PSRAM_WRITE_THROUGH)
    if (bk_psram_enable_write_through(s_psram_cover_area, (uint32_t)disp_frame, (uint32_t)((uint32_t)disp_frame + size)) != BK_OK)
    {
        LOGE("Failed to enable write through\r\n");
        bk_frame_buffer_free(disp_frame);
        return NULL;
    }
#endif

    return disp_frame;
}

static avdk_err_t doorbell_frame_free(void *ptr)
{
    bk_frame_buffer_free(ptr);

    return AVDK_ERR_OK;
}

int AvdkVideoReatorOSD::CloseCamera()
{
    LOGI("AvdkVideoReatorOSD::CloseCamera\n");

    /* Camera is opened via app_isp_mipi_camera_turn_on() in app_camera.c.
     * Use app_isp_camera_turn_off() to stop MP/SP instances and cleanup safely.
     */
    bk_err_t ret = app_isp_camera_turn_off();
    if (ret != BK_OK)
    {
        LOGE("app_isp_camera_turn_off failed, ret: %d\n", ret);
        return ret;
    }

    return BK_OK;
}

int AvdkVideoReatorOSD::ReadCameraFrame(uint8_t *frame, uint32_t size, uint32_t timeout)
{
    LOGV("AvdkVideoReatorOSD::ReadCameraFrame\n");
    int ret = BK_FAIL;
    // current support sp channel
    if (frame == NULL)
    {
        LOGE("%s, %d frame malloc failed\n", __func__, __LINE__);
        return ret;
    }

    ret = app_isp_camera_channel_read(APP_ISP_SP_CHN_ID, frame, size, timeout);

    LOGV("AvdkVideoReatorOSD::ReadCameraFrame ret: %d, timeout: %X\n", ret, timeout);

    return ret;
}


int AvdkVideoReatorOSD::OpenDisplay()
{
    avdk_err_t ret = AVDK_ERR_OK;
    bool display_started_here = false;
#if CONFIG_VG_LITE_GPU
    bk_gpu_ctlr_handle_t gpu_handle = NULL;
    void *isp_handle = NULL;
#endif
    LOGI("AvdkVideoReatorOSD::OpenDisplay\n");
    if (!app_mipi_lcd_state_get()) {
        ret = app_mipi_lcd_turn_on(app_display_board_config_get());
        if (ret != BK_OK) {
            LOGE("%s, app_mipi_lcd_turn_on failed, ret = %d\n", __func__, ret);
            return ret;
        }
        display_started_here = true;
    }
#if CONFIG_VG_LITE_GPU

    ret = app_gpu_turn_on(app_gpu_board_config_get());
    if (ret != BK_OK) {
        LOGE("%s, app_gpu_turn_on failed, ret = %d\n", __func__, ret);
        goto error;
    }
    gpu_handle = app_gpu_handle_get();
    if (gpu_handle == NULL) {
        LOGE("%s, app_gpu_handle_get failed\n", __func__);
        ret = BK_FAIL;
        goto error;
    }
    isp_handle = app_isp_handle_get();
    if (isp_handle == NULL) {
        LOGE("%s, app_isp_handle_get failed\n", __func__);
        ret = BK_FAIL;
        goto error;
    }
    ret = bk_flexa_isp_gpu_bond_start(&isp_gpu_bond, isp_handle, gpu_handle);
    if (ret != BK_OK) {
        LOGE("%s, bk_flexa_isp_gpu_bond_start failed, ret = %d\n", __func__, ret);
        goto error;
    }

#endif
    return 0;

error:

#if CONFIG_VG_LITE_GPU
    if (isp_gpu_bond != NULL) {
        bk_flexa_isp_gpu_bond_stop(isp_gpu_bond);
        isp_gpu_bond = NULL;
    }

    if (gpu_handle != NULL) {
        (void)app_gpu_turn_off(gpu_handle);
    }
#endif
    if (display_started_here) {
        app_mipi_lcd_turn_off();
    }

    return ret;
}

int AvdkVideoReatorOSD::CloseDisplay()
{
    LOGI("AvdkVideoReatorOSD::CloseDisplay\n");

#if CONFIG_VG_LITE_GPU
    if (isp_gpu_bond != NULL) {
        bk_flexa_isp_gpu_bond_stop(isp_gpu_bond);
        isp_gpu_bond = NULL;
    }

    bk_gpu_ctlr_handle_t gpu_handle = app_gpu_handle_get();
    if (gpu_handle != NULL) {
        (void)app_gpu_turn_off(gpu_handle);
    }
#endif

    return 0;
}