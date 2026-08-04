// Copyright 2020-2021 Beken
// Face-detection overlay demo: MIPI camera + GPU display path driven by an
// NN pipeline. This is a DISPLAY-ONLY variant of palm_tracking: it draws every
// detected face box on the OSD but does not drive any servo (no pan/tilt
// tracking).
//
// Note on the "no LVGL in src/demo/" rule: like palm tracking, this is an
// overlay demo that takes the framebuffer away from LVGL, so the lifecycle
// inherently has to coordinate with the LVGL vendor (pause on enter, resume +
// navigate-back on exit). lv_vendor.h + a minimal LVGL include are therefore
// admitted here as a documented exception.

/* os/os.h does NOT wrap its declarations in `extern "C"`, so including it
 * directly from a .cc file causes C++ name mangling on RTOS APIs such as
 * rtos_create_thread / rtos_delete_thread, and the linker then cannot find
 * the un-mangled C symbols compiled from the SDK. Wrap them ourselves. */
extern "C" {
#include <os/os.h>
#include <os/mem.h>
#include <os/str.h>
#include <driver/gpio.h>
#include <components/bk_frame_buffer.h>
#include "ff.h"
}
#include <stdbool.h>
#include <stdio.h>
#include "yoloface_detection.h"
#include "demo/yoloface_tracking.h"

#include "app_display.h"
#include "app_camera.h"
#include "app_gpu.h"
#include "board_usb_switch.h"
#include "bk_camera_lvgl_blend.h"

#include "AvdkVideoReatorOSD.h"
#include "AvdkDetectionModel.h"
#include "FaceRecognitionModel.h"
#include "YolofaceDetectionModel.h"
#include "box.h"

#if CONFIG_LVGL
extern "C" {
#include "lvgl.h"
#include "lv_vendor.h"
#include "beken_ui.h"
#include "event_runtime.h"
#include "ui_overlay_swipe.h"
#include "ui_theme.h"

bk_err_t bk_robot_lvgl_resume_display(void);
int page_edge_ai_enter(void);
int page_edge_ai_archive_enter(void);
void page_edge_ai_solution_set_status(const char *text);
void page_edge_ai_solution_destroy(void);
}
#endif

static AvdkVideoReatorOSD *s_video_reator = NULL;
static AvdkDetectionModel *s_model = NULL;
static FaceRecognitionModel *s_solution_face_model = NULL;

#ifndef YOLOFACE_TRACKING_MODEL_SD_PATH
#define YOLOFACE_TRACKING_MODEL_SD_PATH "1:/tflite/yoloface_int8_vela.tflite"
#endif

#ifndef YOLOFACE_SOLUTION_FACE_DETECT_MODEL_SD_PATH
#define YOLOFACE_SOLUTION_FACE_DETECT_MODEL_SD_PATH "1:/tflite/face_detection_int8_vela.tflite"
#endif

#ifndef YOLOFACE_SOLUTION_FACE_VERIFY_MODEL_SD_PATH
#define YOLOFACE_SOLUTION_FACE_VERIFY_MODEL_SD_PATH "1:/tflite/face_verify_int8_vela.tflite"
#endif

/* Display canvas (GPU mp output) the OSD boxes are scaled to. Must match the
 * mp_width/mp_height programmed in yoloface_detection_config() below. The NN
 * input (model->getWidth()/getHeight()) is the *source* coordinate space the
 * boxes are reported in, and the ISP secondary path is auto-sized to it by
 * AvdkVideoReatorOSD::OpenISPCamera(). */
#define YOLOFACE_DISPLAY_W   400
#define YOLOFACE_DISPLAY_H   320
#define YOLOFACE_LVGL_CAMERA_W   256
#define YOLOFACE_LVGL_CAMERA_H   256
#define YOLOFACE_BLEND_BG_W       320
#define YOLOFACE_BLEND_BG_H       388
#define YOLOFACE_BLEND_BG_SIZE    (YOLOFACE_BLEND_BG_W * YOLOFACE_BLEND_BG_H)
/* Solution page preview logical top-left is panel(25,20)+border(2,2).
 * LVGL flush is ROTATE_90, so physical_x=logical_y and
 * physical_y=LVGL_DISP_HEIGHT-logical_x-preview_width. */
#define YOLOFACE_BLEND_FG_X       22
#define YOLOFACE_BLEND_FG_Y       102
#define YOLOFACE_BLEND_BOX_MAX    8

#define YOLOFACE_FACE_DIR_BASE        "1:/faces"
#define YOLOFACE_FACE_DIR_PREFIX      "face_"
#define YOLOFACE_FACE_PATH_BUF_LEN    96
#define YOLOFACE_FACE_MAX_ID          9999
#define YOLOFACE_FACE_SAMPLES_PER_ID  3
#define YOLOFACE_FACE_RGB112_SIZE     (112 * 112 * 3)
#define YOLOFACE_FACE_VERIFY_AVG_THRESHOLD 0.62f
#define YOLOFACE_FACE_VERIFY_PASS_FRAMES   2
#define YOLOFACE_ENROLL_SAVE_QUEUE_LEN     1
#define YOLOFACE_ENROLL_SAVE_TASK_STACK    (1024 * 6)
#define YOLOFACE_ENROLL_SAVE_TASK_NAME     "face_enroll_save"
#define YOLOFACE_ENROLL_SAVE_STOP_WAIT_MS  5000
#define YOLOFACE_ACTIVE_REQUEST_TIMEOUT_MS 2000
#define YOLOFACE_FACE_STATUS_VALID_MS      1000

typedef enum {
    YOLOFACE_FACE_MSG_ENROLL_SAVE = 0,
    YOLOFACE_FACE_MSG_VERIFY,
} yoloface_face_msg_type_t;

typedef struct {
    yoloface_face_msg_type_t type;
    FaceVerifyResult *result;
    uint8_t *aligned_rgb;
    uint32_t aligned_rgb_size;
} yoloface_enroll_save_msg_t;

static FATFS *s_face_fs = NULL;
static FIL s_yoloface_file;
static volatile bool s_faces_storage_ready = false;
static volatile bool s_yoloface_enroll_pending = false;
static volatile bool s_yoloface_verify_pending = false;
static volatile bool s_yoloface_verify_result_pending = false;
static volatile bool s_yoloface_enroll_session_active = false;
static uint32_t s_yoloface_enroll_profile_id = 0;
static uint32_t s_yoloface_enroll_sample_index = 0;
static uint32_t s_yoloface_active_request_ms = 0;
static volatile int s_yoloface_latest_face_count = -1;
static volatile uint32_t s_yoloface_latest_face_ms = 0;
static uint32_t s_yoloface_verify_candidate_profile = 0;
static uint32_t s_yoloface_verify_pass_streak = 0;
static beken_queue_t s_yoloface_enroll_save_queue = NULL;
static beken_thread_t s_yoloface_enroll_save_thread = NULL;
static volatile bool s_yoloface_enroll_save_stop = false;

/* Same offload rationale as palm tracking: the start-up path (sensor probe,
 * ISP init, C++ object construction, thread creation, ...) is heavy and deep,
 * so it must NOT run on a tiny stack such as the FreeRTOS Timer Service Task.
 * We offload it to a dedicated one-shot worker thread and guard against
 * double-start so repeated key presses cannot create duplicate pipelines. */
#define YOLOFACE_START_TASK_STACK_SIZE   (1024 * 8)
#define YOLOFACE_START_TASK_NAME         "yoloface_start"

static beken_thread_t s_yoloface_start_thread = NULL;
static volatile bool s_yoloface_started = false;
static volatile bool s_yoloface_return_to_edge_ai = false;
static volatile bool s_yoloface_return_to_archive = false;
static volatile bool s_yoloface_keep_model_on_stop = false;
static volatile bool s_yoloface_model_ready = false;
static volatile bool s_yoloface_lvgl_camera_blend = false;

#if CONFIG_LVGL
static beken_thread_t s_yoloface_exit_thread = NULL;
static volatile bool s_yoloface_lvgl_stopped = false;
#endif

#if CONFIG_LVGL && CONFIG_TP
static void yoloface_overlay_back(void *arg)
{
    (void)arg;
    (void)yoloface_detection_exit_to_menu();
}
#endif

static void yoloface_solution_status(const char *text)
{
#if CONFIG_LVGL
    page_edge_ai_solution_set_status(text);
#else
    (void)text;
#endif
}

static void yoloface_verify_session_reset(void);

static void yoloface_solution_enroll_set_pending(bool pending)
{
    s_yoloface_enroll_pending = pending;
    if (pending) {
        s_yoloface_active_request_ms = (uint32_t)rtos_get_time();
    }
    if (s_yoloface_lvgl_camera_blend) {
        bk_camera_lvgl_blend_set_suspended(s_yoloface_enroll_pending ||
                                           s_yoloface_verify_pending ||
                                           s_yoloface_verify_result_pending);
    }
}

static void yoloface_solution_verify_set_pending(bool pending)
{
    s_yoloface_verify_pending = pending;
    if (pending) {
        s_yoloface_active_request_ms = (uint32_t)rtos_get_time();
    }
    if (s_yoloface_lvgl_camera_blend) {
        bk_camera_lvgl_blend_set_suspended(s_yoloface_enroll_pending ||
                                           s_yoloface_verify_pending ||
                                           s_yoloface_verify_result_pending);
    }
}

static bool yoloface_solution_has_active_request(void)
{
    if (s_yoloface_enroll_pending || s_yoloface_verify_pending) {
        uint32_t now = (uint32_t)rtos_get_time();
        if ((uint32_t)(now - s_yoloface_active_request_ms) >
            YOLOFACE_ACTIVE_REQUEST_TIMEOUT_MS) {
            bk_printf("yoloface: active request timeout, cancel pending\n");
            if (s_solution_face_model != NULL) {
                s_solution_face_model->setVerifyEnabled(false);
            }
            yoloface_verify_session_reset();
            s_yoloface_enroll_pending = false;
            s_yoloface_verify_pending = false;
            s_yoloface_verify_result_pending = false;
            s_yoloface_enroll_session_active = false;
            bk_camera_lvgl_blend_set_suspended(false);
            return false;
        }
    }
    return s_yoloface_enroll_pending || s_yoloface_verify_pending;
}

static bool yoloface_solution_is_busy(void)
{
    return s_yoloface_enroll_pending || s_yoloface_verify_pending ||
           s_yoloface_verify_result_pending ||
           s_yoloface_enroll_session_active;
}

static void yoloface_verify_session_reset(void)
{
    s_yoloface_verify_candidate_profile = 0;
    s_yoloface_verify_pass_streak = 0;
}

static bool yoloface_verify_session_accept(uint32_t profile_id)
{
    if (profile_id == 0) {
        yoloface_verify_session_reset();
        return false;
    }

    if (s_yoloface_verify_candidate_profile == profile_id) {
        s_yoloface_verify_pass_streak++;
    } else {
        s_yoloface_verify_candidate_profile = profile_id;
        s_yoloface_verify_pass_streak = 1;
    }

    return s_yoloface_verify_pass_streak >= YOLOFACE_FACE_VERIFY_PASS_FRAMES;
}

static void yoloface_solution_record_face_count(int count)
{
    s_yoloface_latest_face_count = count > 0 ? count : 0;
    s_yoloface_latest_face_ms = (uint32_t)rtos_get_time();
}

static bool yoloface_solution_recent_no_face(void)
{
    uint32_t now = (uint32_t)rtos_get_time();

    return s_yoloface_latest_face_count == 0 &&
           (uint32_t)(now - s_yoloface_latest_face_ms) <=
           YOLOFACE_FACE_STATUS_VALID_MS;
}

static void yoloface_enroll_session_reset(void)
{
    s_yoloface_enroll_session_active = false;
}

static void yoloface_enroll_session_cancel(void)
{
    yoloface_enroll_session_reset();
    yoloface_solution_enroll_set_pending(false);
}

static bool yoloface_parse_face_dir_id(const char *name, uint32_t *out_id)
{
    if (name == NULL || out_id == NULL) {
        return false;
    }
    if (os_strncmp(name, YOLOFACE_FACE_DIR_PREFIX, 5) != 0) {
        return false;
    }

    uint32_t id = 0;
    const char *p = name + 5;
    if (*p == '\0') {
        return false;
    }
    while (*p != '\0') {
        if (*p < '0' || *p > '9') {
            return false;
        }
        id = id * 10 + (uint32_t)(*p - '0');
        if (id > YOLOFACE_FACE_MAX_ID) {
            return false;
        }
        p++;
    }
    *out_id = id;
    return true;
}

static bool yoloface_parse_sample_id(const char *name, uint32_t *out_id)
{
    const char prefix[] = "feature_";
    const char suffix[] = ".bin";
    const int prefix_len = (int)(sizeof(prefix) - 1);
    const int suffix_len = (int)(sizeof(suffix) - 1);
    int len = 0;

    if (name == NULL || out_id == NULL || os_strncmp(name, prefix, prefix_len) != 0) {
        return false;
    }
    while (name[len] != '\0') {
        len++;
    }
    if (len <= prefix_len + suffix_len ||
        os_strcmp(name + len - suffix_len, suffix) != 0) {
        return false;
    }

    uint32_t id = 0;
    for (int i = prefix_len; i < len - suffix_len; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        id = id * 10 + (uint32_t)(name[i] - '0');
    }
    *out_id = id;
    return true;
}

static bool yoloface_parse_ppm_id(const char *name, uint32_t *out_id)
{
    const char prefix[] = "face_";
    const char suffix[] = ".ppm";
    const int prefix_len = (int)(sizeof(prefix) - 1);
    const int suffix_len = (int)(sizeof(suffix) - 1);
    int len = 0;

    if (name == NULL || out_id == NULL || os_strncmp(name, prefix, prefix_len) != 0) {
        return false;
    }
    while (name[len] != '\0') {
        len++;
    }
    if (len <= prefix_len + suffix_len ||
        os_strcmp(name + len - suffix_len, suffix) != 0) {
        return false;
    }

    uint32_t id = 0;
    for (int i = prefix_len; i < len - suffix_len; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return false;
        }
        id = id * 10 + (uint32_t)(name[i] - '0');
    }
    *out_id = id;
    return true;
}

static int yoloface_faces_mount(void)
{
    if (s_faces_storage_ready) {
        return 0;
    }

    if (board_usb_switch_prepare_nand_access() != BK_OK ||
        board_sd_nand_power_on() != BK_OK) {
        bk_printf("yoloface_faces_mount: prepare nand access failed\n");
        return -1;
    }
    rtos_delay_milliseconds(50);

    FRESULT fr = f_mkdir(YOLOFACE_FACE_DIR_BASE);
    if (fr == FR_OK || fr == FR_EXIST) {
        s_faces_storage_ready = true;
        return 0;
    }

    if (s_face_fs == NULL) {
        s_face_fs = (FATFS *)os_malloc(sizeof(FATFS));
        if (s_face_fs == NULL) {
            bk_printf("yoloface_faces_mount: alloc FATFS failed\n");
            return -1;
        }
    }

    fr = f_mount(s_face_fs, "1:", 1);
    if (fr != FR_OK) {
        bk_printf("yoloface_faces_mount: f_mount failed fr=%d\n", fr);
        return -1;
    }

    fr = f_mkdir(YOLOFACE_FACE_DIR_BASE);
    if (fr == FR_OK || fr == FR_EXIST) {
        s_faces_storage_ready = true;
        return 0;
    }
    bk_printf("yoloface_faces_mount: mkdir %s failed fr=%d\n",
              YOLOFACE_FACE_DIR_BASE, fr);
    return -1;
}

static int yoloface_count_profile_samples(uint32_t profile_id, uint32_t *out_next_sample)
{
    DIR *dir = NULL;
    FILINFO *fno = NULL;
    char path[YOLOFACE_FACE_PATH_BUF_LEN];
    uint32_t max_sample = 0;
    FRESULT fr;
    int rc = -1;

    int n = snprintf(path, sizeof(path), "%s/%s%04u",
                     YOLOFACE_FACE_DIR_BASE, YOLOFACE_FACE_DIR_PREFIX,
                     (unsigned)profile_id);
    if (n <= 0 || n >= (int)sizeof(path)) {
        return -1;
    }

    dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    if (dir == NULL || fno == NULL) {
        goto out;
    }

    fr = f_opendir(dir, path);
    if (fr != FR_OK) {
        goto out;
    }
    while (1) {
        fr = f_readdir(dir, fno);
        if (fr != FR_OK || fno->fname[0] == '\0') {
            break;
        }
        if (fno->fattrib & AM_DIR) {
            continue;
        }
        uint32_t sample = 0;
        if (yoloface_parse_sample_id(fno->fname, &sample) && sample > max_sample) {
            max_sample = sample;
        }
    }
    (void)f_closedir(dir);

    *out_next_sample = max_sample + 1;
    rc = 0;

out:
    if (dir != NULL) { os_free(dir); }
    if (fno != NULL) { os_free(fno); }
    return rc;
}

static int yoloface_archive_scan_profile(const char *dir_name,
                                         yoloface_archive_item_t *item)
{
    DIR *dir = NULL;
    FILINFO *fno = NULL;
    char path[YOLOFACE_FACE_PATH_BUF_LEN];
    uint32_t profile_id = 0;
    int rc = -1;

    if (dir_name == NULL || item == NULL ||
        !yoloface_parse_face_dir_id(dir_name, &profile_id)) {
        return -1;
    }
    int n = snprintf(path, sizeof(path), "%s/%s", YOLOFACE_FACE_DIR_BASE, dir_name);
    if (n <= 0 || n >= (int)sizeof(path)) {
        return -1;
    }

    dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    if (dir == NULL || fno == NULL) {
        goto out;
    }
    if (f_opendir(dir, path) != FR_OK) {
        goto out;
    }

    os_memset(item, 0, sizeof(*item));
    item->profile_id = profile_id;
    while (f_readdir(dir, fno) == FR_OK && fno->fname[0] != '\0') {
        uint32_t sample = 0;
        if (fno->fattrib & AM_DIR) {
            continue;
        }
        if (yoloface_parse_sample_id(fno->fname, &sample)) {
            item->feature_count++;
            if (sample > item->last_sample) { item->last_sample = sample; }
        } else if (yoloface_parse_ppm_id(fno->fname, &sample)) {
            item->ppm_count++;
            if (sample > item->last_sample) { item->last_sample = sample; }
        }
    }
    (void)f_closedir(dir);
    rc = 0;

out:
    if (dir != NULL) { os_free(dir); }
    if (fno != NULL) { os_free(fno); }
    return rc;
}

static int yoloface_find_next_profile_id(uint32_t *out_profile_id)
{
    DIR *dir = NULL;
    FILINFO *fno = NULL;
    uint32_t max_id = 0;
    bool have_id = false;
    FRESULT fr;
    int rc = -1;

    if (out_profile_id == NULL) {
        return -1;
    }

    dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    if (dir == NULL || fno == NULL) {
        goto out;
    }

    fr = f_opendir(dir, YOLOFACE_FACE_DIR_BASE);
    if (fr != FR_OK) {
        goto out;
    }
    while (1) {
        fr = f_readdir(dir, fno);
        if (fr != FR_OK || fno->fname[0] == '\0') {
            break;
        }
        if (!(fno->fattrib & AM_DIR)) {
            continue;
        }
        uint32_t id = 0;
        if (yoloface_parse_face_dir_id(fno->fname, &id) && (!have_id || id > max_id)) {
            max_id = id;
            have_id = true;
        }
    }
    (void)f_closedir(dir);

    if (!have_id) {
        *out_profile_id = 1;
        rc = 0;
        goto out;
    }

    if (max_id >= YOLOFACE_FACE_MAX_ID) {
        goto out;
    }
    *out_profile_id = max_id + 1;
    rc = 0;

out:
    if (dir != NULL) { os_free(dir); }
    if (fno != NULL) { os_free(fno); }
    return rc;
}

static int yoloface_pick_enroll_slot(uint32_t *out_profile_id,
                                     uint32_t *out_sample_index)
{
    if (out_profile_id == NULL || out_sample_index == NULL) {
        return -1;
    }

    if (s_yoloface_enroll_profile_id != 0 &&
        s_yoloface_enroll_sample_index < YOLOFACE_FACE_SAMPLES_PER_ID) {
        *out_profile_id = s_yoloface_enroll_profile_id;
        *out_sample_index = s_yoloface_enroll_sample_index + 1;
        return 0;
    }

    uint32_t profile_id = 0;
    if (yoloface_find_next_profile_id(&profile_id) != 0) {
        return -1;
    }

    if (profile_id > 1) {
        uint32_t last_profile = profile_id - 1;
        uint32_t next_sample = 0;
        if (yoloface_count_profile_samples(last_profile, &next_sample) == 0 &&
            next_sample <= YOLOFACE_FACE_SAMPLES_PER_ID) {
            *out_profile_id = last_profile;
            *out_sample_index = next_sample;
            return 0;
        }
    }

    *out_profile_id = profile_id;
    *out_sample_index = 1;
    return 0;
}

static int yoloface_write_file(const char *path, const void *data, uint32_t size)
{
    if (path == NULL || data == NULL || size == 0) {
        bk_printf("yoloface_write_file: invalid args path=%p data=%p size=%u\n",
                  path, data, (unsigned)size);
        return -1;
    }

    FRESULT fr = f_open(&s_yoloface_file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        bk_printf("yoloface_write_file: open %s failed fr=%d\n", path, fr);
        return -1;
    }

    UINT bw = 0;
    fr = f_write(&s_yoloface_file, data, size, &bw);
    (void)f_close(&s_yoloface_file);
    if (fr != FR_OK || bw != size) {
        bk_printf("yoloface_write_file: write %s failed fr=%d bw=%u size=%u\n",
                  path, fr, (unsigned)bw, (unsigned)size);
    }
    return (fr == FR_OK && bw == size) ? 0 : -1;
}

static int yoloface_read_file(const char *path, void *data, uint32_t size)
{
    if (path == NULL || data == NULL || size == 0) {
        return -1;
    }

    FRESULT fr = f_open(&s_yoloface_file, path, FA_READ);
    if (fr != FR_OK) {
        return -1;
    }

    UINT br = 0;
    fr = f_read(&s_yoloface_file, data, size, &br);
    (void)f_close(&s_yoloface_file);
    return (fr == FR_OK && br == size) ? 0 : -1;
}

static int yoloface_remove_dir_files(const char *dir_path)
{
    DIR *dir = NULL;
    FILINFO *fno = NULL;
    char file_path[YOLOFACE_FACE_PATH_BUF_LEN];

    if (dir_path == NULL) {
        return -1;
    }
    dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    if (dir == NULL || fno == NULL) {
        if (dir != NULL) { os_free(dir); }
        if (fno != NULL) { os_free(fno); }
        return -1;
    }

    if (f_opendir(dir, dir_path) != FR_OK) {
        os_free(dir);
        os_free(fno);
        return 0;
    }
    while (f_readdir(dir, fno) == FR_OK && fno->fname[0] != '\0') {
        if (fno->fattrib & AM_DIR) {
            continue;
        }
        int n = snprintf(file_path, sizeof(file_path), "%s/%s",
                         dir_path, fno->fname);
        if (n > 0 && n < (int)sizeof(file_path)) {
            (void)f_unlink(file_path);
        }
    }
    (void)f_closedir(dir);
    os_free(dir);
    os_free(fno);
    (void)f_unlink(dir_path);
    return 0;
}

static int yoloface_verify_saved_faces(const FaceVerifyResult *current,
                                       float *out_best_score,
                                       float *out_best_avg_score,
                                       uint32_t *out_best_profile,
                                       uint32_t *out_best_sample)
{
    DIR *base_dir = NULL;
    DIR *face_dir = NULL;
    FILINFO *fno = NULL;
    float *stored = NULL;
    char dir_path[YOLOFACE_FACE_PATH_BUF_LEN];
    char file_path[YOLOFACE_FACE_PATH_BUF_LEN];
    uint32_t best_profile = 0;
    uint32_t best_sample = 0;
    float best_score = -2.0f;
    float best_avg_score = -2.0f;
    uint32_t feature_count = 0;
    FRESULT fr;
    int rc = -1;

    if (current == NULL || !current->valid ||
        out_best_score == NULL || out_best_avg_score == NULL ||
        out_best_profile == NULL ||
        out_best_sample == NULL) {
        return -1;
    }
    if (yoloface_faces_mount() != 0) {
        bk_printf("yoloface_verify_saved_faces: faces mount failed\n");
        return -1;
    }

    base_dir = (DIR *)os_malloc(sizeof(DIR));
    face_dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    stored = (float *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                             sizeof(current->normalized));
    if (base_dir == NULL || face_dir == NULL || fno == NULL || stored == NULL) {
        bk_printf("yoloface_verify_saved_faces: alloc failed\n");
        goto out;
    }

    fr = f_opendir(base_dir, YOLOFACE_FACE_DIR_BASE);
    if (fr != FR_OK) {
        bk_printf("yoloface_verify_saved_faces: opendir base failed fr=%d\n", fr);
        goto out;
    }

    while (f_readdir(base_dir, fno) == FR_OK && fno->fname[0] != '\0') {
        uint32_t profile_id = 0;
        if (!(fno->fattrib & AM_DIR) ||
            !yoloface_parse_face_dir_id(fno->fname, &profile_id)) {
            continue;
        }
        {
            yoloface_archive_item_t item;
            if (yoloface_archive_scan_profile(fno->fname, &item) != 0 ||
                item.feature_count < YOLOFACE_FACE_SAMPLES_PER_ID) {
                continue;
            }
        }

        int n = snprintf(dir_path, sizeof(dir_path), "%s/%s",
                         YOLOFACE_FACE_DIR_BASE, fno->fname);
        if (n <= 0 || n >= (int)sizeof(dir_path)) {
            continue;
        }
        if (f_opendir(face_dir, dir_path) != FR_OK) {
            continue;
        }

        float profile_best_score = -2.0f;
        uint32_t profile_best_sample = 0;
        float profile_score_sum = 0.0f;
        uint32_t profile_feature_count = 0;
        while (f_readdir(face_dir, fno) == FR_OK && fno->fname[0] != '\0') {
            uint32_t sample_id = 0;
            if ((fno->fattrib & AM_DIR) ||
                !yoloface_parse_sample_id(fno->fname, &sample_id)) {
                continue;
            }
            n = snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, fno->fname);
            if (n <= 0 || n >= (int)sizeof(file_path)) {
                continue;
            }
            if (yoloface_read_file(file_path, stored,
                                   sizeof(current->normalized)) != 0) {
                bk_printf("yoloface_verify_saved_faces: read %s failed\n", file_path);
                continue;
            }

            float score = face_recognition_cosine_similarity(current->normalized,
                                                             stored,
                                                             kFaceEmbeddingDim);
            feature_count++;
            profile_feature_count++;
            profile_score_sum += score;
            if (score > profile_best_score) {
                profile_best_score = score;
                profile_best_sample = sample_id;
            }
        }
        (void)f_closedir(face_dir);
        if (profile_feature_count >= YOLOFACE_FACE_SAMPLES_PER_ID &&
            profile_best_score > best_score) {
            best_score = profile_best_score;
            best_avg_score = profile_score_sum / (float)profile_feature_count;
            best_profile = profile_id;
            best_sample = profile_best_sample;
        }
    }
    (void)f_closedir(base_dir);

    if (feature_count == 0) {
        bk_printf("yoloface_verify_saved_faces: no feature files\n");
        goto out;
    }

    *out_best_score = best_score;
    *out_best_avg_score = best_avg_score;
    *out_best_profile = best_profile;
    *out_best_sample = best_sample;
    rc = 0;

out:
    if (stored != NULL) { bk_frame_buffer_free(stored); }
    if (base_dir != NULL) { os_free(base_dir); }
    if (face_dir != NULL) { os_free(face_dir); }
    if (fno != NULL) { os_free(fno); }
    return rc;
}

static int yoloface_write_enroll_pair(const char *dir_path,
                                      uint32_t sample_index,
                                      const FaceVerifyResult *result,
                                      const uint8_t *aligned_rgb,
                                      uint32_t aligned_rgb_size)
{
    char file_path[YOLOFACE_FACE_PATH_BUF_LEN];
    FRESULT fr = f_mkdir(dir_path);
    if (fr != FR_OK && fr != FR_EXIST) {
        bk_printf("yoloface_save_enroll_sample: mkdir %s failed fr=%d\n",
                  dir_path, fr);
        return -1;
    }

    int n = snprintf(file_path, sizeof(file_path), "%s/face_%04u.ppm",
                     dir_path, (unsigned)sample_index);
    if (n <= 0 || n >= (int)sizeof(file_path)) {
        bk_printf("yoloface_save_enroll_sample: ppm path overflow\n");
        return -1;
    }

    fr = f_open(&s_yoloface_file, file_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK) {
        bk_printf("yoloface_save_enroll_sample: open %s failed fr=%d\n",
                  file_path, fr);
        return -1;
    }
    char ppm_header[32];
    n = snprintf(ppm_header, sizeof(ppm_header), "P6\n112 112\n255\n");
    UINT bw = 0;
    bool ok = (n > 0 &&
               f_write(&s_yoloface_file, ppm_header, (UINT)n, &bw) == FR_OK && bw == (UINT)n &&
               f_write(&s_yoloface_file, aligned_rgb, aligned_rgb_size, &bw) == FR_OK &&
               bw == aligned_rgb_size);
    (void)f_close(&s_yoloface_file);
    if (!ok) {
        bk_printf("yoloface_save_enroll_sample: write %s failed bw=%u size=%u\n",
                  file_path, (unsigned)bw, (unsigned)aligned_rgb_size);
        return -1;
    }

    n = snprintf(file_path, sizeof(file_path), "%s/feature_%04u.bin",
                 dir_path, (unsigned)sample_index);
    if (n <= 0 || n >= (int)sizeof(file_path)) {
        bk_printf("yoloface_save_enroll_sample: feature path overflow\n");
        return -1;
    }
    if (yoloface_write_file(file_path, result->normalized,
                            sizeof(result->normalized)) != 0) {
        bk_printf("yoloface_save_enroll_sample: write feature failed\n");
        return -1;
    }
    return 0;
}

static int yoloface_save_enroll_sample(const FaceVerifyResult *result,
                                       const uint8_t *aligned_rgb,
                                       uint32_t aligned_rgb_size,
                                       uint32_t *out_saved_count,
                                       uint32_t *out_profile_id)
{
    if (result == NULL || !result->valid ||
        aligned_rgb == NULL || aligned_rgb_size != YOLOFACE_FACE_RGB112_SIZE ||
        out_saved_count == NULL || out_profile_id == NULL) {
        return -1;
    }
    if (yoloface_faces_mount() != 0) {
        bk_printf("yoloface_save_enroll_sample: faces mount failed\n");
        return -1;
    }

    uint32_t profile_id = 0;
    uint32_t sample_index = 0;
    if (yoloface_pick_enroll_slot(&profile_id, &sample_index) != 0) {
        bk_printf("yoloface_save_enroll_sample: pick slot failed\n");
        return -1;
    }

    char dir_path[YOLOFACE_FACE_PATH_BUF_LEN];
    int n = snprintf(dir_path, sizeof(dir_path), "%s/%s%04u",
                     YOLOFACE_FACE_DIR_BASE, YOLOFACE_FACE_DIR_PREFIX,
                     (unsigned)profile_id);
    if (n <= 0 || n >= (int)sizeof(dir_path)) {
        return -1;
    }

    if (yoloface_write_enroll_pair(dir_path, sample_index,
                                   result, aligned_rgb, aligned_rgb_size) != 0) {
        return -1;
    }

    s_yoloface_enroll_profile_id = profile_id;
    s_yoloface_enroll_sample_index = sample_index;
    *out_saved_count = sample_index;
    *out_profile_id = profile_id;
    if (sample_index >= YOLOFACE_FACE_SAMPLES_PER_ID) {
        s_yoloface_enroll_profile_id = 0;
        s_yoloface_enroll_sample_index = 0;
    }
    return 0;
}

static void yoloface_free_enroll_msg(yoloface_enroll_save_msg_t *msg)
{
    if (msg == NULL) {
        return;
    }
    if (msg->result != NULL) {
        bk_frame_buffer_free(msg->result);
        msg->result = NULL;
    }
    if (msg->aligned_rgb != NULL) {
        bk_frame_buffer_free(msg->aligned_rgb);
        msg->aligned_rgb = NULL;
    }
    msg->aligned_rgb_size = 0;
}

static void yoloface_enroll_save_task(void *arg)
{
    (void)arg;

    while (!s_yoloface_enroll_save_stop) {
        yoloface_enroll_save_msg_t msg = {};
        if (rtos_pop_from_queue(&s_yoloface_enroll_save_queue,
                                &msg,
                                BEKEN_WAIT_FOREVER) != BK_OK) {
            continue;
        }
        if (s_yoloface_enroll_save_stop) {
            yoloface_free_enroll_msg(&msg);
            break;
        }

        if (msg.type == YOLOFACE_FACE_MSG_VERIFY) {
            float best_score = 0.0f;
            float best_avg_score = 0.0f;
            uint32_t best_profile = 0;
            uint32_t best_sample = 0;
            bool request_next_verify = false;
            if (yoloface_verify_saved_faces(msg.result, &best_score,
                                            &best_avg_score,
                                            &best_profile, &best_sample) == 0) {
                bool score_pass = best_score >= kFaceVerifySameThreshold &&
                                  best_avg_score >= YOLOFACE_FACE_VERIFY_AVG_THRESHOLD;
                bk_printf("yoloface_verify: best profile=%u sample=%u score=%.3f avg=%.3f threshold=%.3f/%.3f streak=%u/%u\n",
                          (unsigned)best_profile, (unsigned)best_sample,
                          best_score, best_avg_score,
                          kFaceVerifySameThreshold,
                          YOLOFACE_FACE_VERIFY_AVG_THRESHOLD,
                          (unsigned)s_yoloface_verify_pass_streak,
                          (unsigned)YOLOFACE_FACE_VERIFY_PASS_FRAMES);
                if (score_pass && yoloface_verify_session_accept(best_profile)) {
                    yoloface_solution_status("验证通过");
                    yoloface_verify_session_reset();
                } else if (score_pass) {
                    yoloface_solution_status("请保持正脸");
                    request_next_verify = true;
                } else {
                    yoloface_verify_session_reset();
                    yoloface_solution_status("验证失败");
                }
            } else {
                yoloface_verify_session_reset();
                yoloface_solution_status("请先录入");
            }
            s_yoloface_verify_result_pending = false;
            if (request_next_verify) {
                if (s_solution_face_model != NULL) {
                    s_solution_face_model->setVerifyEnabled(true);
                }
                yoloface_solution_verify_set_pending(true);
            }
        } else {
            uint32_t saved_count = 0;
            uint32_t profile_id = 0;
            int save_rc = yoloface_save_enroll_sample(msg.result,
                                                      msg.aligned_rgb,
                                                      msg.aligned_rgb_size,
                                                      &saved_count,
                                                      &profile_id);
            if (save_rc == 0) {
                char text[64];
                if (saved_count >= YOLOFACE_FACE_SAMPLES_PER_ID) {
                    snprintf(text, sizeof(text), "录入完成 %u/%u",
                             (unsigned)YOLOFACE_FACE_SAMPLES_PER_ID,
                             (unsigned)YOLOFACE_FACE_SAMPLES_PER_ID);
                } else {
                    snprintf(text, sizeof(text), "录入成功 %u/%u,  请继续录入",
                             (unsigned)saved_count,
                             (unsigned)YOLOFACE_FACE_SAMPLES_PER_ID);
                }
                yoloface_enroll_session_cancel();
                yoloface_solution_status(text);
            } else {
                yoloface_enroll_session_cancel();
                yoloface_solution_status("录入失败,  请重新录入");
            }
        }
        yoloface_free_enroll_msg(&msg);
    }

    s_yoloface_enroll_save_thread = NULL;
    rtos_delete_thread(NULL);
}

static int yoloface_enroll_save_worker_start(void)
{
    if (s_yoloface_enroll_save_queue == NULL) {
        if (rtos_init_queue(&s_yoloface_enroll_save_queue,
                            "face_enroll_q",
                            sizeof(yoloface_enroll_save_msg_t),
                            YOLOFACE_ENROLL_SAVE_QUEUE_LEN) != BK_OK) {
            s_yoloface_enroll_save_queue = NULL;
            return -1;
        }
    }

    if (s_yoloface_enroll_save_thread != NULL) {
        return 0;
    }

    s_yoloface_enroll_save_stop = false;
    bk_err_t ret = rtos_create_thread(&s_yoloface_enroll_save_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      YOLOFACE_ENROLL_SAVE_TASK_NAME,
                                      (beken_thread_function_t)yoloface_enroll_save_task,
                                      YOLOFACE_ENROLL_SAVE_TASK_STACK,
                                      NULL);
    if (ret != BK_OK) {
        s_yoloface_enroll_save_thread = NULL;
        return -1;
    }
    return 0;
}

static int yoloface_post_enroll_save(const FaceVerifyResult *result,
                                     const uint8_t *aligned_rgb,
                                     uint32_t aligned_rgb_size)
{
    if (result == NULL || !result->valid ||
        aligned_rgb == NULL || aligned_rgb_size != YOLOFACE_FACE_RGB112_SIZE) {
        bk_printf("yoloface_post_enroll_save: invalid input result=%p valid=%d rgb=%p size=%u\n",
                  result, result != NULL ? result->valid : 0,
                  aligned_rgb, (unsigned)aligned_rgb_size);
        return -1;
    }
    if (yoloface_enroll_save_worker_start() != 0) {
        bk_printf("yoloface_post_enroll_save: worker start failed\n");
        return -1;
    }

    yoloface_enroll_save_msg_t msg = {};
    msg.type = YOLOFACE_FACE_MSG_ENROLL_SAVE;
    msg.result = (FaceVerifyResult *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                                            sizeof(FaceVerifyResult));
    msg.aligned_rgb = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                                        aligned_rgb_size);
    if (msg.result == NULL || msg.aligned_rgb == NULL) {
        bk_printf("yoloface_post_enroll_save: alloc copy failed result=%p rgb=%p size=%u\n",
                  msg.result, msg.aligned_rgb, (unsigned)aligned_rgb_size);
        yoloface_free_enroll_msg(&msg);
        return -1;
    }
    os_memcpy(msg.result, result, sizeof(FaceVerifyResult));
    os_memcpy(msg.aligned_rgb, aligned_rgb, aligned_rgb_size);
    msg.aligned_rgb_size = aligned_rgb_size;

    if (rtos_push_to_queue(&s_yoloface_enroll_save_queue,
                           &msg,
                           BEKEN_NO_WAIT) != BK_OK) {
        bk_printf("yoloface_post_enroll_save: queue full\n");
        yoloface_free_enroll_msg(&msg);
        return -1;
    }
    return 0;
}

static int yoloface_post_verify_request(const FaceVerifyResult *result)
{
    if (result == NULL || !result->valid) {
        bk_printf("yoloface_post_verify_request: invalid result=%p valid=%d\n",
                  result, result != NULL ? result->valid : 0);
        return -1;
    }
    if (yoloface_enroll_save_worker_start() != 0) {
        bk_printf("yoloface_post_verify_request: worker start failed\n");
        return -1;
    }

    yoloface_enroll_save_msg_t msg = {};
    msg.type = YOLOFACE_FACE_MSG_VERIFY;
    msg.result = (FaceVerifyResult *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                                            sizeof(FaceVerifyResult));
    if (msg.result == NULL) {
        bk_printf("yoloface_post_verify_request: alloc result failed\n");
        return -1;
    }
    os_memcpy(msg.result, result, sizeof(FaceVerifyResult));

    s_yoloface_verify_result_pending = true;
    if (rtos_push_to_queue(&s_yoloface_enroll_save_queue,
                           &msg,
                           BEKEN_NO_WAIT) != BK_OK) {
        bk_printf("yoloface_post_verify_request: queue full\n");
        s_yoloface_verify_result_pending = false;
        yoloface_free_enroll_msg(&msg);
        return -1;
    }
    return 0;
}

static void yoloface_enroll_result_cb(const FaceVerifyResult *result,
                                      const uint8_t *aligned_rgb,
                                      uint32_t aligned_rgb_size)
{
    bool verify_pending = s_yoloface_verify_pending;

    if (result == NULL || !result->valid) {
        bk_printf("yoloface_enroll_result_cb: invalid result=%p valid=%d size=%u\n",
                  result, result != NULL ? result->valid : 0,
                  (unsigned)aligned_rgb_size);
    }

    if (s_solution_face_model != NULL) {
        s_solution_face_model->setVerifyEnabled(false);
    }
    yoloface_solution_enroll_set_pending(false);
    yoloface_solution_verify_set_pending(false);

    if (verify_pending) {
        if (yoloface_post_verify_request(result) == 0) {
            yoloface_solution_status("验证中");
        } else {
            yoloface_solution_status("验证失败,  请重试");
        }
    } else if (yoloface_post_enroll_save(result, aligned_rgb, aligned_rgb_size) == 0) {
        yoloface_solution_status("保存中");
    } else {
        yoloface_enroll_session_cancel();
        yoloface_solution_status("录入失败,  请重新录入");
    }
}

static void yoloface_enroll_save_worker_stop(void)
{
    if (s_yoloface_enroll_save_queue == NULL) {
        return;
    }

    s_yoloface_enroll_save_stop = true;
    yoloface_enroll_save_msg_t msg = {};
    (void)rtos_push_to_queue(&s_yoloface_enroll_save_queue, &msg, BEKEN_NO_WAIT);

    for (int i = 0; i < YOLOFACE_ENROLL_SAVE_STOP_WAIT_MS / 20 &&
            s_yoloface_enroll_save_thread != NULL; i++) {
        rtos_delay_milliseconds(20);
    }

    if (s_yoloface_enroll_save_thread == NULL) {
        while (rtos_pop_from_queue(&s_yoloface_enroll_save_queue,
                                   &msg,
                                   BEKEN_NO_WAIT) == BK_OK) {
            yoloface_free_enroll_msg(&msg);
        }
        (void)rtos_deinit_queue(&s_yoloface_enroll_save_queue);
        s_yoloface_enroll_save_queue = NULL;
    } else {
        bk_printf("yoloface_enroll_save_worker_stop: worker still running after %u ms\n",
                  (unsigned)YOLOFACE_ENROLL_SAVE_STOP_WAIT_MS);
    }
}

/* Display-only callback: draw every detected face. No servo is driven.
 *
 * src = model input size, dst = display canvas size. The OSD holds the previous
 * boxes until the next successful frame overwrites them, so on an empty frame we
 * explicitly clear the path. */
static void yoloface_detection_box_cb(Box *boxes, int count)
{
    bool has_face = boxes != NULL && count > 0;

    yoloface_solution_record_face_count(has_face ? count : 0);

    if (!has_face) {
        if (s_yoloface_lvgl_camera_blend) {
            (void)bk_camera_lvgl_blend_set_boxes(NULL, 0);
        }
        if (yoloface_solution_has_active_request()) {
            if (s_solution_face_model != NULL) {
                s_solution_face_model->setVerifyEnabled(false);
            }
            if (s_yoloface_enroll_session_active) {
                yoloface_enroll_session_cancel();
            }
            yoloface_solution_enroll_set_pending(false);
            yoloface_solution_verify_set_pending(false);
            yoloface_solution_status("未检测到人脸");
        }
        box_detection_path_clear();
        return;
    }

    if (s_yoloface_lvgl_camera_blend &&
        (s_yoloface_verify_pending || s_yoloface_verify_result_pending)) {
        (void)bk_camera_lvgl_blend_set_boxes(NULL, 0);
        box_detection_path_clear();
        return;
    }

    bk_printf("yoloface_detection_box_cb: count=%d top score=%.3f xywh=(%.2f,%.2f,%.2fx%.2f)\n",
              count, boxes[0].score,
              boxes[0].x, boxes[0].y, boxes[0].w, boxes[0].h);

    if (s_yoloface_lvgl_camera_blend) {
        bk_camera_lvgl_blend_rect_t rects[YOLOFACE_BLEND_BOX_MAX];
        int rect_count = (count > YOLOFACE_BLEND_BOX_MAX) ? YOLOFACE_BLEND_BOX_MAX : count;
        float src_w = (float)s_model->getWidth();
        float src_h = (float)s_model->getHeight();
        float xscale = (float)YOLOFACE_LVGL_CAMERA_W / src_h;
        float yscale = (float)YOLOFACE_LVGL_CAMERA_H / src_w;

        for (int i = 0; i < rect_count; i++) {
            float x0 = boxes[i].x;
            float y0 = boxes[i].y;
            float x1 = boxes[i].x + boxes[i].w;
            float y1 = boxes[i].y + boxes[i].h;

            float rx0 = y0;
            float ry0 = src_w - x0;
            float rx1 = y1;
            float ry1 = src_w - x1;

            float fxmin = rx0 * xscale;
            float fymin = ry0 * yscale;
            float fxmax = rx1 * xscale;
            float fymax = ry1 * yscale;

            if (fxmin > fxmax) { float t = fxmin; fxmin = fxmax; fxmax = t; }
            if (fymin > fymax) { float t = fymin; fymin = fymax; fymax = t; }
            if (fxmin < 0.0f) fxmin = 0.0f;
            if (fymin < 0.0f) fymin = 0.0f;
            if (fxmax > (float)(YOLOFACE_LVGL_CAMERA_W - 1)) fxmax = (float)(YOLOFACE_LVGL_CAMERA_W - 1);
            if (fymax > (float)(YOLOFACE_LVGL_CAMERA_H - 1)) fymax = (float)(YOLOFACE_LVGL_CAMERA_H - 1);

            rects[i].x1 = (int16_t)(fxmin + 0.5f);
            rects[i].y1 = (int16_t)(fymin + 0.5f);
            rects[i].x2 = (int16_t)(fxmax + 0.5f);
            rects[i].y2 = (int16_t)(fymax + 0.5f);
        }

        (void)bk_camera_lvgl_blend_set_boxes(rects, rect_count);
        box_detection_path_clear();
        return;
    }

    box_detection_path_build(boxes, count, count, 0,
                             s_model->getWidth(), s_model->getHeight(),
                             YOLOFACE_DISPLAY_W, YOLOFACE_DISPLAY_H);
}

static AvdkDetectionModel *yoloface_detection_create_model(void)
{
    if (s_yoloface_lvgl_camera_blend) {
        FaceRecognitionModel *model = new FaceRecognitionModel();
        if (model != NULL) {
            model->setModelFilePath(YOLOFACE_SOLUTION_FACE_DETECT_MODEL_SD_PATH);
            model->setVerifyModelFilePath(YOLOFACE_SOLUTION_FACE_VERIFY_MODEL_SD_PATH);
            model->setEnrollResultCallback(yoloface_enroll_result_cb);
            s_solution_face_model = model;
        }
        return model;
    }

    YolofaceDetectionModel *model = new YolofaceDetectionModel();
    if (model != NULL) {
        model->setModelFilePath(YOLOFACE_TRACKING_MODEL_SD_PATH);
    }
    return model;
}

static void yoloface_detection_frame_complete(void *frame, uint32_t frame_size, void *args)
{
    (void)args;

    if (s_yoloface_lvgl_camera_blend) {
        if (s_yoloface_enroll_pending || s_yoloface_verify_pending ||
            s_yoloface_verify_result_pending) {
            app_gpu_frame_free(frame);
            return;
        }
        (void)bk_camera_lvgl_blend_push_camera_frame(frame, frame_size);
    } else {
        app_gpu_frame_free(frame);
    }
}

static void yoloface_detection_config(void)
{
    camera_board_config_t camera_board = {0};
    gpu_board_config_t gpu_board = {0};

    camera_board.mipi.enable = true;
    camera_board.mipi.pin_scl = GPIO_70;
    camera_board.mipi.pin_sda = GPIO_71;
    camera_board.mipi.i2c_id = 1;
    camera_board.mipi.pin_reset = GPIO_31;
    camera_board.mipi.pin_pwdn = -1;
    camera_board.mipi.pin_xclk = GPIO_59;
    camera_board.mipi.sensor_max_width = 1088;
    camera_board.mipi.sensor_max_height = 1088;
    camera_board.mipi.sensor_fps = 15;
    camera_board.mipi.hmirror = 1;
    camera_board.mipi.vflip = 0;
    camera_board.isp.mp_enable = true;
    camera_board.isp.mp_flexa = true;

    if (s_yoloface_lvgl_camera_blend) {
        camera_board.isp.mp_width = YOLOFACE_LVGL_CAMERA_W;
        camera_board.isp.mp_height = YOLOFACE_LVGL_CAMERA_H;
    } else {
        camera_board.isp.mp_width = YOLOFACE_DISPLAY_W;
        camera_board.isp.mp_height = YOLOFACE_DISPLAY_H;
    }
    camera_board.isp.mp_format = BK_PIXEL_FORMAT_NV12;
    camera_board.isp.sp_enable = false;
    camera_board.isp.sp_flexa = false;

    gpu_board.flexa.enable = true;
    gpu_board.flexa.degree = 270;
    if (s_yoloface_lvgl_camera_blend) {
        gpu_board.flexa.tess_width = YOLOFACE_LVGL_CAMERA_W / 2;
        gpu_board.flexa.tess_height = YOLOFACE_LVGL_CAMERA_H / 2;
        gpu_board.flexa.src_width = YOLOFACE_LVGL_CAMERA_W;
        gpu_board.flexa.src_height = YOLOFACE_LVGL_CAMERA_H;
        gpu_board.flexa.dst_width = YOLOFACE_LVGL_CAMERA_W;
        gpu_board.flexa.dst_height = YOLOFACE_LVGL_CAMERA_H;
    } else {
        gpu_board.flexa.tess_width = YOLOFACE_DISPLAY_W / 2;
        gpu_board.flexa.tess_height = YOLOFACE_DISPLAY_H / 2;
        gpu_board.flexa.src_width = YOLOFACE_DISPLAY_W;
        gpu_board.flexa.src_height = YOLOFACE_DISPLAY_H;
        gpu_board.flexa.dst_width = YOLOFACE_DISPLAY_W;
        gpu_board.flexa.dst_height = YOLOFACE_DISPLAY_H;
    }
    gpu_board.flexa.src_format = BK_PIXEL_FORMAT_NV12;
    gpu_board.flexa.dst_format = BK_PIXEL_FORMAT_ARGB8888;
    gpu_board.flexa.dst_compress = true;
    gpu_board.flexa.scale = false;

    if (s_yoloface_lvgl_camera_blend) {
        gpu_board.flexa.frame_done = yoloface_detection_frame_complete;
        gpu_board.flexa.frame_done_args = NULL;
    } else {
        gpu_board.flexa.frame_done = NULL;
        gpu_board.flexa.frame_done_args = NULL;
    }

    /* Board config for Multimedia config */
    app_camera_board_config_set(&camera_board);
    app_gpu_board_config_set(&gpu_board);

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = true;
        display_config->dpu_video.decompress = true;
        display_config->dpu_video.format = BK_PIXEL_FORMAT_ARGB8888;
    }
}

static void yoloface_detection_start_task(void *arg)
{
    (void)arg;
    int ret = BK_OK;
    bool camera_opened = false;
    bool display_open_attempted = false;
#if CONFIG_LVGL
    bool model_init_failed = false;
#endif
    bool reuse_model = false;

    s_yoloface_latest_face_count = -1;
    s_yoloface_latest_face_ms = 0;

#if CONFIG_LVGL
    if (!s_yoloface_lvgl_camera_blend) {
        lv_vendor_stop();
        s_yoloface_lvgl_stopped = true;
    } else {
        s_yoloface_lvgl_stopped = false;
    }
#endif

    if (s_yoloface_lvgl_camera_blend) {
        bk_camera_lvgl_blend_config_t blend_cfg = {
            .bg_width = YOLOFACE_BLEND_BG_W,
            .bg_height = YOLOFACE_BLEND_BG_H,
            .bg_frame_size = YOLOFACE_BLEND_BG_SIZE,
            .fg_width = YOLOFACE_LVGL_CAMERA_W,
            .fg_height = YOLOFACE_LVGL_CAMERA_H,
            .fg_x = YOLOFACE_BLEND_FG_X,
            .fg_y = YOLOFACE_BLEND_FG_Y,
        };
        ret = bk_camera_lvgl_blend_start(&blend_cfg);
        if (ret != BK_OK) {
            bk_printf("yoloface_detection_start_task: blend start failed (%d)\n", ret);
            goto fail;
        }
#if CONFIG_LVGL
        lv_vendor_disp_lock();
        {
            lv_obj_t *active = lv_screen_active();
            if (active != NULL) {
                lv_obj_invalidate(active);
            }
        }
        lv_vendor_disp_unlock();
#endif
    }

    if (s_yoloface_lvgl_camera_blend) {
        if (yoloface_enroll_save_worker_start() != 0) {
            yoloface_solution_status("存储线程未就绪");
        }
        if (yoloface_faces_mount() != 0) {
            yoloface_solution_status("存储未就绪");
        }
    }

    yoloface_detection_config();

    reuse_model = (s_model != NULL && s_yoloface_model_ready);
    if (!reuse_model) {
        s_model = yoloface_detection_create_model();
    }
    if (s_model == NULL) {
        bk_printf("yoloface_detection_start_task: model alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }
    s_model->setBoxDetectionCallback(yoloface_detection_box_cb);

    s_video_reator = new AvdkVideoReatorOSD(s_model);
    if (s_video_reator == NULL) {
        bk_printf("yoloface_detection_start_task: video reator alloc failed\n");
        ret = BK_FAIL;
        goto fail;
    }

    ret = s_video_reator->init(false);
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: init failed (%d)\n", ret);
        goto fail;
    }

    ret = s_video_reator->OpenISPCamera();
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: OpenISPCamera failed (%d)\n", ret);
        goto fail;
    }
    camera_opened = true;

    display_open_attempted = true;
    ret = s_video_reator->OpenDisplay();
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: OpenDisplay failed (%d)\n", ret);
        goto fail;
    }

    if (!reuse_model) {
        s_model->LogEnable(true);
        ret = s_model->init();
        if (ret != BK_OK) {
            bk_printf("yoloface_detection_start_task: model init failed (%d)\n", ret);
#if CONFIG_LVGL
            if (ret == -1) {
                model_init_failed = true;
            }
#endif
            goto fail;
        }
    }
    s_yoloface_model_ready = true;

#if CONFIG_LVGL
    if (s_yoloface_lvgl_camera_blend) {
        lv_vendor_disp_lock();
        {
            lv_obj_t *active = lv_screen_active();
            if (active != NULL) {
                lv_obj_invalidate(active);
            }
        }
        lv_vendor_disp_unlock();
    }
#endif

    ret = s_video_reator->start();
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: start failed (%d)\n", ret);
        goto fail;
    }

#if CONFIG_LVGL && CONFIG_TP
    ret = ui_overlay_swipe_back_start(yoloface_overlay_back, NULL);
    if (ret != BK_OK) {
        bk_printf("yoloface_detection_start_task: overlay swipe start failed (%d)\n", ret);
    }
#endif

    bk_printf("yoloface_detection_start_task: done, exiting worker\n");

    s_yoloface_start_thread = NULL;
    rtos_delete_thread(NULL);
    return;

fail:
    bk_printf("yoloface_detection_start_task: failed (%d), aborting\n", ret);

    if (s_yoloface_lvgl_camera_blend) {
        yoloface_enroll_save_worker_stop();
    }

    if (s_video_reator != NULL) {
        (void)s_video_reator->stop();
        if (display_open_attempted) {
            (void)s_video_reator->CloseDisplay();
        }
        if (camera_opened) {
            (void)s_video_reator->CloseCamera();
        }
        delete s_video_reator;
        s_video_reator = NULL;
    }

    if (!reuse_model && s_model != NULL) {
        (void)s_model->deinit();
        delete s_model;
        s_model = NULL;
        s_solution_face_model = NULL;
        s_yoloface_model_ready = false;
    }

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = false;
    }

#if CONFIG_LVGL
    if (s_yoloface_lvgl_camera_blend) {
        (void)bk_camera_lvgl_blend_stop();
    }
    if (s_yoloface_lvgl_stopped && display_open_attempted) {
        if (bk_robot_lvgl_resume_display() != BK_OK) {
            bk_printf("yoloface_detection_start_task: resume display failed\n");
        }
    }
#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif
    if (s_yoloface_lvgl_stopped) {
        lv_vendor_start();
    }
    lv_vendor_disp_lock();
    {
        lv_obj_t *active = lv_screen_active();
        if (active != NULL) {
            lv_obj_invalidate(active);
        }
    }
    lv_vendor_disp_unlock();
    s_yoloface_lvgl_stopped = false;
    if (model_init_failed) {
        ui_theme_create_popup("Model file not exist!");
    }
#endif

    s_yoloface_started = false;
    yoloface_solution_enroll_set_pending(false);
    yoloface_solution_verify_set_pending(false);
    s_yoloface_verify_result_pending = false;
    yoloface_verify_session_reset();
    yoloface_enroll_session_cancel();
    s_yoloface_lvgl_camera_blend = false;
    s_yoloface_start_thread = NULL;
    rtos_delete_thread(NULL);
}

static int yoloface_detection_start(void)
{
    if (s_yoloface_started) {
        bk_printf("yoloface_detection_start: already started, ignore\n");
        return 0;
    }

    s_yoloface_started = true;

    bk_err_t ret = rtos_create_thread(&s_yoloface_start_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      YOLOFACE_START_TASK_NAME,
                                      (beken_thread_function_t)yoloface_detection_start_task,
                                      YOLOFACE_START_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_yoloface_started = false;
        s_yoloface_start_thread = NULL;
        bk_printf("yoloface_detection_start: create thread failed, ret=%d\n", ret);
        return -1;
    }

    return 0;
}

static bool yoloface_detection_can_start(void)
{
    /* Pipeline currently active (live or mid-startup). */
    if (s_yoloface_started || s_yoloface_start_thread != NULL) {
        return false;
    }
#if CONFIG_LVGL
    /* Exit task from the previous session is still tearing things down;
     * starting now would race the teardown on shared statics. */
    if (s_yoloface_exit_thread != NULL) {
        return false;
    }
#endif
    return true;
}

extern "C" bool yoloface_detection_is_active(void)
{
#if CONFIG_LVGL
    if (s_yoloface_exit_thread != NULL) {
        return true;
    }
#endif
    return s_yoloface_started;
}

extern "C" bool yoloface_solution_ui_is_active(void)
{
    return s_yoloface_started && s_yoloface_lvgl_camera_blend;
}

static int yoloface_detection_stop(void)
{
    if (!s_yoloface_started) {
        return 0;
    }

#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif

    for (int i = 0; i < 50 && s_yoloface_start_thread != NULL; i++) {
        rtos_delay_milliseconds(20);
    }

    if (s_yoloface_start_thread != NULL) {
        bk_printf("yoloface_detection_stop: start task still running, abort stop\n");
        return BK_FAIL;
    }

#if CONFIG_LVGL
    if (!s_yoloface_lvgl_stopped) {
        lv_vendor_stop();
        s_yoloface_lvgl_stopped = true;
    }
#endif

    if (s_yoloface_lvgl_camera_blend) {
        (void)bk_camera_lvgl_blend_stop();
        yoloface_enroll_save_worker_stop();
    }

    if (s_video_reator != NULL) {
        int ret = s_video_reator->stop();
        if (ret != BK_OK) {
            bk_printf("yoloface_detection_stop: video stop failed (%d), abort stop\n", ret);
            return ret;
        }
    }

    box_detection_path_clear();

    if (s_video_reator != NULL) {
        (void)s_video_reator->CloseDisplay();
        (void)s_video_reator->CloseCamera();
        delete s_video_reator;
        s_video_reator = NULL;
    }

    if (s_model != NULL && !s_yoloface_keep_model_on_stop) {
        (void)s_model->deinit();
        delete s_model;
        s_model = NULL;
        s_solution_face_model = NULL;
        s_yoloface_model_ready = false;
    } else if (s_model != NULL) {
        bk_printf("yoloface_detection_stop: keep face model loaded for fast resume\n");
    }

    display_board_config_t *display_config = app_display_board_config_get();
    if (display_config) {
        display_config->dpu_video.enable = false;
    }

    s_yoloface_started = false;
    yoloface_solution_enroll_set_pending(false);
    yoloface_solution_verify_set_pending(false);
    s_yoloface_verify_result_pending = false;
    yoloface_verify_session_reset();
    if (s_yoloface_enroll_session_active) {
        yoloface_enroll_session_cancel();
    }
    s_yoloface_lvgl_camera_blend = false;
    s_yoloface_keep_model_on_stop = false;
    return 0;
}

#if CONFIG_LVGL
#define YOLOFACE_EXIT_TASK_STACK_SIZE   (1024 * 8)
#define YOLOFACE_EXIT_TASK_NAME         "yoloface_exit"

static void yoloface_detection_exit_task(void *arg)
{
    (void)arg;
    bool return_to_edge_ai = s_yoloface_return_to_edge_ai;
    bool return_to_archive = s_yoloface_return_to_archive;
    bool lvgl_stopped = false;
    s_yoloface_return_to_edge_ai = false;
    s_yoloface_return_to_archive = false;

    int ret = yoloface_detection_stop();
    if (ret != 0) {
        bk_printf("yoloface_detection_exit_task: stop failed (%d)\n", ret);
        goto done;
    }

    lvgl_stopped = s_yoloface_lvgl_stopped;
    if (lvgl_stopped && bk_robot_lvgl_resume_display() != BK_OK) {
        bk_printf("yoloface_detection_exit_task: resume display failed\n");
        goto done;
    }

#if CONFIG_TP
    ui_overlay_swipe_back_stop();
#endif
    if (lvgl_stopped) {
        lv_vendor_start();
    }

    lv_vendor_disp_lock();
    if (return_to_archive) {
        (void)page_edge_ai_archive_enter();
    } else if (return_to_edge_ai) {
        (void)page_edge_ai_enter();
        page_edge_ai_solution_destroy();
    } else {
        navigate_to_screen((lv_obj_t **)&bk_lv_tool_ui.page_3,
                           LV_SCR_LOAD_ANIM_NONE, 0, 0, false,
                           init_page_page_3);
        page_edge_ai_solution_destroy();
    }

    {
        lv_obj_t *active = lv_screen_active();
        if (active != NULL) {
            lv_obj_invalidate(active);
        }
    }
    lv_vendor_disp_unlock();
    s_yoloface_lvgl_stopped = false;

done:
    s_yoloface_exit_thread = NULL;
    rtos_delete_thread(NULL);
}
#endif /* CONFIG_LVGL */

extern "C" int yoloface_detection_exit_to_menu(void)
{
#if CONFIG_LVGL
    ui_overlay_swipe_back_stop();

    /* Idempotent fast path: nothing to exit. */
    if (!s_yoloface_started) {
        return 0;
    }

    if (s_yoloface_exit_thread != NULL) {
        return 0;
    }

    bk_err_t ret = rtos_create_thread(&s_yoloface_exit_thread,
                                      BEKEN_DEFAULT_WORKER_PRIORITY,
                                      YOLOFACE_EXIT_TASK_NAME,
                                      (beken_thread_function_t)yoloface_detection_exit_task,
                                      YOLOFACE_EXIT_TASK_STACK_SIZE,
                                      NULL);
    if (ret != BK_OK) {
        s_yoloface_exit_thread = NULL;
        bk_printf("yoloface_detection_exit_to_menu: create thread failed, ret=%d\n", ret);
        return -1;
    }
    return 0;
#else
    return yoloface_detection_stop();
#endif
}

/* ----------------------------------------------------------------------
 * bk_demo_iface_t wiring.
 *
 * start() pauses LVGL before spawning the NN pipeline worker so the GPU
 * display path can take over the framebuffer; stop() routes through
 * yoloface_detection_exit_to_menu() which tears down and navigates back.
 * -------------------------------------------------------------------- */

extern "C" int yoloface_tracking_init(void)
{
    return 0;
}

extern "C" int yoloface_tracking_start(void)
{
    if (!yoloface_detection_can_start()) {
        bk_printf("yoloface_tracking_start: busy (start/exit in progress), ignore\n");
        return -1;
    }

    if (yoloface_detection_start() != 0) {
        bk_printf("yoloface_detection_start trigger failed\r\n");
        return -1;
    }

    return 0;
}

extern "C" void yoloface_tracking_set_return_to_edge_ai(bool enable)
{
    s_yoloface_return_to_edge_ai = enable;
}

extern "C" void yoloface_tracking_set_lvgl_camera_blend(bool enable)
{
    s_yoloface_lvgl_camera_blend = enable;
}

extern "C" int yoloface_solution_enroll_request(void)
{
    if (!s_yoloface_started || !s_yoloface_lvgl_camera_blend ||
        s_solution_face_model == NULL) {
        return -1;
    }
    if (yoloface_solution_is_busy()) {
        return YOLOFACE_SOLUTION_ERR_BUSY;
    }
    if (yoloface_solution_recent_no_face()) {
        if (s_yoloface_lvgl_camera_blend) {
            (void)bk_camera_lvgl_blend_set_boxes(NULL, 0);
        }
        yoloface_solution_status("未检测到人脸");
        return YOLOFACE_SOLUTION_ERR_NO_FACE;
    }

    yoloface_enroll_session_reset();
    s_yoloface_enroll_session_active = true;
    s_solution_face_model->setVerifyEnabled(true);
    yoloface_solution_enroll_set_pending(true);
    yoloface_solution_status("录入中");
    return 0;
}

extern "C" int yoloface_solution_enroll_cancel(void)
{
    if (s_solution_face_model != NULL) {
        s_solution_face_model->setVerifyEnabled(false);
    }
    yoloface_solution_verify_set_pending(false);
    s_yoloface_verify_result_pending = false;
    yoloface_verify_session_reset();
    yoloface_enroll_session_cancel();
    yoloface_solution_status("已取消录入");
    return 0;
}

extern "C" bool yoloface_solution_enroll_is_active(void)
{
    return s_yoloface_enroll_session_active || s_yoloface_enroll_pending;
}

extern "C" int yoloface_solution_verify_request(void)
{
    if (!s_yoloface_started || !s_yoloface_lvgl_camera_blend ||
        s_solution_face_model == NULL) {
        return -1;
    }
    if (yoloface_solution_is_busy()) {
        return YOLOFACE_SOLUTION_ERR_BUSY;
    }
    if (yoloface_solution_recent_no_face()) {
        if (s_yoloface_lvgl_camera_blend) {
            (void)bk_camera_lvgl_blend_set_boxes(NULL, 0);
        }
        return YOLOFACE_SOLUTION_ERR_NO_FACE;
    }

    yoloface_verify_session_reset();
    if (s_yoloface_lvgl_camera_blend) {
        (void)bk_camera_lvgl_blend_set_boxes(NULL, 0);
    }
    bk_printf("yoloface_verify: hide boxes during verify\n");
    s_solution_face_model->setVerifyEnabled(true);
    yoloface_solution_verify_set_pending(true);
    return 0;
}

extern "C" int yoloface_solution_archive_enter_request(void)
{
    if (yoloface_solution_is_busy()) {
        return YOLOFACE_SOLUTION_ERR_BUSY;
    }
    s_yoloface_return_to_archive = true;
    s_yoloface_return_to_edge_ai = false;
    s_yoloface_keep_model_on_stop = true;
    return yoloface_detection_exit_to_menu();
}

extern "C" int yoloface_solution_archive_query(yoloface_archive_info_t *info)
{
    DIR *dir = NULL;
    FILINFO *fno = NULL;
    uint32_t stored_count = 0;

    if (info == NULL) {
        return -1;
    }
    os_memset(info, 0, sizeof(*info));
    if (yoloface_faces_mount() != 0) {
        bk_printf("yoloface_archive_query: faces mount failed\n");
        return -1;
    }

    dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    if (dir == NULL || fno == NULL) {
        if (dir != NULL) { os_free(dir); }
        if (fno != NULL) { os_free(fno); }
        bk_printf("yoloface_archive_query: alloc dir/fno failed\n");
        return -1;
    }

    FRESULT fr = f_opendir(dir, YOLOFACE_FACE_DIR_BASE);
    if (fr != FR_OK) {
        bk_printf("yoloface_archive_query: opendir %s failed fr=%d\n",
                  YOLOFACE_FACE_DIR_BASE, fr);
        if (fr == FR_NO_PATH || fr == FR_NO_FILE) {
            (void)f_mkdir(YOLOFACE_FACE_DIR_BASE);
            fr = f_opendir(dir, YOLOFACE_FACE_DIR_BASE);
        }
    }
    if (fr != FR_OK) {
        os_free(dir);
        os_free(fno);
        return (fr == FR_NO_PATH || fr == FR_NO_FILE) ? 0 : -1;
    }

    while (f_readdir(dir, fno) == FR_OK && fno->fname[0] != '\0') {
        yoloface_archive_item_t item;
        if (!(fno->fattrib & AM_DIR) ||
            yoloface_archive_scan_profile(fno->fname, &item) != 0) {
            continue;
        }
        if (item.feature_count < YOLOFACE_FACE_SAMPLES_PER_ID) {
            continue;
        }
        info->profile_count++;
        info->total_features += item.feature_count;
        info->total_ppm += item.ppm_count;
        if (item.feature_count != item.ppm_count) {
            info->incomplete_count++;
        }
        if (stored_count < YOLOFACE_ARCHIVE_MAX_ITEMS) {
            info->items[stored_count++] = item;
        }
    }
    (void)f_closedir(dir);
    os_free(dir);
    os_free(fno);
    return 0;
}

extern "C" int yoloface_solution_archive_delete(uint32_t profile_id)
{
    DIR *dir = NULL;
    FILINFO *fno = NULL;
    char dir_path[YOLOFACE_FACE_PATH_BUF_LEN];
    char file_path[YOLOFACE_FACE_PATH_BUF_LEN];
    int rc = -1;

    if (profile_id == 0 || profile_id > YOLOFACE_FACE_MAX_ID ||
        yoloface_faces_mount() != 0) {
        return -1;
    }
    int n = snprintf(dir_path, sizeof(dir_path), "%s/%s%04u",
                     YOLOFACE_FACE_DIR_BASE, YOLOFACE_FACE_DIR_PREFIX,
                     (unsigned)profile_id);
    if (n <= 0 || n >= (int)sizeof(dir_path)) {
        return -1;
    }

    dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    if (dir == NULL || fno == NULL) {
        goto out;
    }
    if (f_opendir(dir, dir_path) != FR_OK) {
        goto out;
    }

    while (f_readdir(dir, fno) == FR_OK && fno->fname[0] != '\0') {
        if (fno->fattrib & AM_DIR) {
            continue;
        }
        n = snprintf(file_path, sizeof(file_path), "%s/%s", dir_path, fno->fname);
        if (n <= 0 || n >= (int)sizeof(file_path)) {
            continue;
        }
        FRESULT fr = f_unlink(file_path);
        if (fr != FR_OK) {
            bk_printf("yoloface_archive_delete: unlink %s failed fr=%d\n",
                      file_path, fr);
        }
    }
    (void)f_closedir(dir);

    if (f_unlink(dir_path) == FR_OK) {
        rc = 0;
    }

out:
    if (dir != NULL) { os_free(dir); }
    if (fno != NULL) { os_free(fno); }
    return rc;
}

extern "C" int yoloface_solution_archive_clear_all(void)
{
    DIR *dir = NULL;
    FILINFO *fno = NULL;
    int deleted = 0;

    if (yoloface_solution_is_busy()) {
        return YOLOFACE_SOLUTION_ERR_BUSY;
    }

    if (yoloface_faces_mount() != 0) {
        return -1;
    }

    dir = (DIR *)os_malloc(sizeof(DIR));
    fno = (FILINFO *)os_malloc(sizeof(FILINFO));
    if (dir == NULL || fno == NULL) {
        if (dir != NULL) { os_free(dir); }
        if (fno != NULL) { os_free(fno); }
        return -1;
    }

    while (true) {
        uint32_t profile_id = 0;
        bool found = false;
        FRESULT fr = f_opendir(dir, YOLOFACE_FACE_DIR_BASE);
        if (fr != FR_OK) {
            os_free(dir);
            os_free(fno);
            return (fr == FR_NO_PATH || fr == FR_NO_FILE) ? deleted : -1;
        }

        while (f_readdir(dir, fno) == FR_OK && fno->fname[0] != '\0') {
            if ((fno->fattrib & AM_DIR) &&
                yoloface_parse_face_dir_id(fno->fname, &profile_id)) {
                found = true;
                break;
            }
        }
        (void)f_closedir(dir);

        if (!found) {
            break;
        }

        if (yoloface_solution_archive_delete(profile_id) == 0) {
            deleted++;
        } else {
            bk_printf("yoloface_archive_clear_all: delete face_%04u failed\n",
                      (unsigned)profile_id);
            break;
        }
    }

    os_free(dir);
    os_free(fno);
    return deleted;
}

extern "C" int yoloface_tracking_stop(void)
{
    return yoloface_detection_exit_to_menu();
}

extern "C" const bk_demo_iface_t g_demo_yoloface_tracking = {
    "yoloface_tracking",
    yoloface_tracking_init,
    yoloface_tracking_start,
    yoloface_tracking_stop,
};
