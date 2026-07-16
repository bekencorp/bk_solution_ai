/**
 * @file HandGestureDetectionModel.cc
 * @brief YOLOv8 hand-gesture detection (320x320, 7 classes, 2100 candidates).
 *
 * Ported from the SDK hand_gesture_demo (Gerrit 91402). Post-processing follows
 * the same YOLOv8 int8 decode + per-class NMS pipeline as the reference
 * HandGestureModel in that change.
 */

#include "HandGestureDetectionModel.h"
#include "tflm_hand_gesture_detection_model.h"
#include "box.h"

#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include <driver/aon_rtc.h>

#include "os/mem.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_log.h"

#if defined(__ARM_FEATURE_MVE)
#include <arm_mve.h>
#endif

#ifndef HAND_GESTURE_MODEL_SD_PATH
#define HAND_GESTURE_MODEL_SD_PATH "1:/tflite/hand_gesture_detection_vela.tflite"
#endif

static constexpr float k_conf_threshold = 0.5f;
static constexpr float k_iou_threshold = 0.5f;
static constexpr int k_max_detections = 32;

static constexpr const char *k_class_names[k_num_classes] = {
    "c0", "c1", "c2", "c3", "c4", "c5", "c6",
};

/* Maps NN class id -> preset index (0..4) or -1 for no action. */
static constexpr int8_t k_class_preset_map[k_num_classes] = {
    0, 1, -1, 2, 3, 4, 3,
};

struct HandGestureDet {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    int class_id;
};

/* Post-process scratch (~180 KiB), allocated from PSRAM heap in resourceLoad()
 * so it does not consume limited .bss / .psram.bss linker reservations. */
static float (*s_boxes_xyxy)[4] = nullptr;
static float (*s_scores)[k_num_classes] = nullptr;
static HandGestureDet *s_merge = nullptr;
static int *s_nms_order = nullptr;
static float *s_nms_order_score = nullptr;
static int *s_filtered = nullptr;
static int *s_nms_work = nullptr;
static int *s_nms_kept_idx = nullptr;
static HandGestureDet *s_dets = nullptr;
static Box *s_out_boxes = nullptr;
static bool s_tensors_checked = false;
static int8_t s_last_preset = -1;
static uint32_t s_run_count = 0;

static bool hand_gesture_alloc_scratch(void);
static void hand_gesture_free_scratch(void);

static bool hand_gesture_alloc_scratch(void)
{
    if (s_boxes_xyxy != nullptr) {
        return true;
    }

    s_boxes_xyxy = (float (*)[4])psram_malloc(sizeof(float) * k_num_candidates * 4U);
    s_scores = (float (*)[k_num_classes])psram_malloc(sizeof(float) * k_num_candidates * k_num_classes);
    s_merge = (HandGestureDet *)psram_malloc(sizeof(HandGestureDet) * k_num_candidates);
    s_nms_order = (int *)psram_malloc(sizeof(int) * k_num_candidates);
    s_nms_order_score = (float *)psram_malloc(sizeof(float) * k_num_candidates);
    s_filtered = (int *)psram_malloc(sizeof(int) * k_num_candidates);
    s_nms_work = (int *)psram_malloc(sizeof(int) * k_num_candidates);
    s_nms_kept_idx = (int *)psram_malloc(sizeof(int) * k_num_candidates);
    s_dets = (HandGestureDet *)psram_malloc(sizeof(HandGestureDet) * k_max_detections);
    s_out_boxes = (Box *)psram_malloc(sizeof(Box) * k_max_detections);

    if (!s_boxes_xyxy || !s_scores || !s_merge || !s_nms_order || !s_nms_order_score ||
        !s_filtered || !s_nms_work || !s_nms_kept_idx || !s_dets || !s_out_boxes) {
        hand_gesture_free_scratch();
        return false;
    }

    return true;
}

static void hand_gesture_free_scratch(void)
{
    if (s_boxes_xyxy != nullptr) {
        psram_free(s_boxes_xyxy);
    }
    if (s_scores != nullptr) {
        psram_free(s_scores);
    }
    if (s_merge != nullptr) {
        psram_free(s_merge);
    }
    if (s_nms_order != nullptr) {
        psram_free(s_nms_order);
    }
    if (s_nms_order_score != nullptr) {
        psram_free(s_nms_order_score);
    }
    if (s_filtered != nullptr) {
        psram_free(s_filtered);
    }
    if (s_nms_work != nullptr) {
        psram_free(s_nms_work);
    }
    if (s_nms_kept_idx != nullptr) {
        psram_free(s_nms_kept_idx);
    }
    if (s_dets != nullptr) {
        psram_free(s_dets);
    }
    if (s_out_boxes != nullptr) {
        psram_free(s_out_boxes);
    }

    s_boxes_xyxy = nullptr;
    s_scores = nullptr;
    s_merge = nullptr;
    s_nms_order = nullptr;
    s_nms_order_score = nullptr;
    s_filtered = nullptr;
    s_nms_work = nullptr;
    s_nms_kept_idx = nullptr;
    s_dets = nullptr;
    s_out_boxes = nullptr;
}

static void log_interval(const char *prefix, unsigned long long start_us)
{
    unsigned long long elapsed_us = bk_aon_rtc_get_us() - start_us;
    MicroPrintf("%s: %llu us\r\n", prefix, elapsed_us);
}

static bool bgrx_to_rgb_int8_simd(const uint8_t *src, int8_t *dst, uint32_t width, uint32_t height)
{
    if (src == nullptr || dst == nullptr) {
        return false;
    }

    const uint32_t pixels = width * height;
    uint32_t i = 0;

#if defined(__ARM_FEATURE_MVE)
    for (; i + 16 <= pixels; i += 16) {
        static const uint8_t k_mve_rgb_offsets[16] = {
            0, 3, 6, 9, 12, 15, 18, 21,
            24, 27, 30, 33, 36, 39, 42, 45,
        };
        const uint8x16x4_t bgrx = vld4q_u8(src + i * 4U);
        const uint8x16_t offset = vld1q_u8(k_mve_rgb_offsets);
        uint8_t *d = (uint8_t *)dst + i * 3U;

        vstrbq_scatter_offset_u8(d, offset, vsubq_n_u8(bgrx.val[2], 128));
        vstrbq_scatter_offset_u8(d + 1, offset, vsubq_n_u8(bgrx.val[1], 128));
        vstrbq_scatter_offset_u8(d + 2, offset, vsubq_n_u8(bgrx.val[0], 128));
    }
#endif

    for (; i < pixels; i++) {
        const uint8_t *s = src + i * 4U;
        int8_t *d = dst + i * 3U;

        d[0] = (int8_t)((int)s[2] - 128);
        d[1] = (int8_t)((int)s[1] - 128);
        d[2] = (int8_t)((int)s[0] - 128);
    }

    return true;
}

static float clipf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static float box_iou_xyxy(const HandGestureDet &a, const HandGestureDet &b)
{
    const float xx1 = fmaxf(a.x1, b.x1);
    const float yy1 = fmaxf(a.y1, b.y1);
    const float xx2 = fminf(a.x2, b.x2);
    const float yy2 = fminf(a.y2, b.y2);
    const float w = fmaxf(0.0f, xx2 - xx1);
    const float h = fmaxf(0.0f, yy2 - yy1);
    const float inter = w * h;
    const float area_a = fmaxf(0.0f, a.x2 - a.x1) * fmaxf(0.0f, a.y2 - a.y1);
    const float area_b = fmaxf(0.0f, b.x2 - b.x1) * fmaxf(0.0f, b.y2 - b.y1);
    const float denom = area_a + area_b - inter;

    return denom > 1e-6f ? inter / denom : 0.0f;
}

static int nms_subset(const int *idxs, int n, int class_id, float iou_thresh, int *out_idx)
{
    for (int i = 0; i < n; i++) {
        s_nms_order[i] = idxs[i];
        s_nms_order_score[i] = s_scores[idxs[i]][class_id];
    }

    for (int a = 0; a < n - 1; a++) {
        for (int b = a + 1; b < n; b++) {
            if (s_nms_order_score[b] > s_nms_order_score[a]) {
                float score_tmp = s_nms_order_score[a];
                s_nms_order_score[a] = s_nms_order_score[b];
                s_nms_order_score[b] = score_tmp;

                int idx_tmp = s_nms_order[a];
                s_nms_order[a] = s_nms_order[b];
                s_nms_order[b] = idx_tmp;
            }
        }
    }

    int kept = 0;
    for (int i = 0; i < n; i++) {
        int ri = s_nms_order[i];
        HandGestureDet cur = {
            s_boxes_xyxy[ri][0],
            s_boxes_xyxy[ri][1],
            s_boxes_xyxy[ri][2],
            s_boxes_xyxy[ri][3],
            s_scores[ri][class_id],
            class_id,
        };

        bool ok = true;
        for (int t = 0; t < kept; t++) {
            int kj = out_idx[t];
            HandGestureDet kept_det = {
                s_boxes_xyxy[kj][0],
                s_boxes_xyxy[kj][1],
                s_boxes_xyxy[kj][2],
                s_boxes_xyxy[kj][3],
                s_scores[kj][class_id],
                class_id,
            };
            if (box_iou_xyxy(cur, kept_det) > iou_thresh) {
                ok = false;
                break;
            }
        }
        if (ok) {
            out_idx[kept++] = ri;
        }
    }

    return kept;
}

static int postprocess_yolov8_int8(const int8_t *out_buf,
                                   float out_scale,
                                   int out_zp,
                                   HandGestureDet *out,
                                   int max_out)
{
    for (int i = 0; i < k_num_candidates; i++) {
        float row[k_out_channels];
        for (int k = 0; k < k_out_channels; k++) {
            int8_t q = out_buf[k * k_num_candidates + i];
            row[k] = ((float)q - (float)out_zp) * out_scale;
        }

        float cx = row[0] * (float)k_input_w;
        float cy = row[1] * (float)k_input_h;
        float w = row[2] * (float)k_input_w;
        float h = row[3] * (float)k_input_h;

        s_boxes_xyxy[i][0] = cx - w * 0.5f;
        s_boxes_xyxy[i][1] = cy - h * 0.5f;
        s_boxes_xyxy[i][2] = cx + w * 0.5f;
        s_boxes_xyxy[i][3] = cy + h * 0.5f;

        for (int c = 0; c < k_num_classes; c++) {
            s_scores[i][c] = row[4 + c];
        }
    }

    int filtered_count = 0;
    for (int i = 0; i < k_num_candidates; i++) {
        float best = s_scores[i][0];
        for (int c = 1; c < k_num_classes; c++) {
            if (s_scores[i][c] > best) {
                best = s_scores[i][c];
            }
        }
        if (best > k_conf_threshold) {
            s_filtered[filtered_count++] = i;
        }
    }
    if (filtered_count == 0) {
        return 0;
    }

    int merge_count = 0;

    for (int c = 0; c < k_num_classes; c++) {
        int work_count = 0;
        for (int j = 0; j < filtered_count; j++) {
            int i = s_filtered[j];
            if (s_scores[i][c] > k_conf_threshold) {
                s_nms_work[work_count++] = i;
            }
        }
        if (work_count == 0) {
            continue;
        }

        int kept_count = nms_subset(s_nms_work, work_count, c, k_iou_threshold, s_nms_kept_idx);
        for (int t = 0; t < kept_count && merge_count < k_num_candidates; t++) {
            int ri = s_nms_kept_idx[t];
            s_merge[merge_count].x1 = s_boxes_xyxy[ri][0];
            s_merge[merge_count].y1 = s_boxes_xyxy[ri][1];
            s_merge[merge_count].x2 = s_boxes_xyxy[ri][2];
            s_merge[merge_count].y2 = s_boxes_xyxy[ri][3];
            s_merge[merge_count].score = s_scores[ri][c];
            s_merge[merge_count].class_id = c;
            merge_count++;
        }
    }

    std::sort(s_merge, s_merge + merge_count, [](const HandGestureDet &a, const HandGestureDet &b) {
        return a.score > b.score;
    });

    int out_count = 0;
    for (int i = 0; i < merge_count && out_count < max_out; i++) {
        out[out_count].x1 = clipf(s_merge[i].x1, 0.0f, (float)k_input_w);
        out[out_count].y1 = clipf(s_merge[i].y1, 0.0f, (float)k_input_h);
        out[out_count].x2 = clipf(s_merge[i].x2, 0.0f, (float)k_input_w);
        out[out_count].y2 = clipf(s_merge[i].y2, 0.0f, (float)k_input_h);
        out[out_count].score = s_merge[i].score;
        out[out_count].class_id = s_merge[i].class_id;
        out_count++;
    }

    return out_count;
}

HandGestureDetectionModel::HandGestureDetectionModel()
    : gesture_action_callback_(nullptr),
      gesture_result_callback_(nullptr)
{
}

void HandGestureDetectionModel::setModelFilePath(const char *path)
{
    modelFilePath = path;
}

void HandGestureDetectionModel::setGestureActionCallback(handGestureActionCallbackT cb)
{
    gesture_action_callback_ = cb;
}

void HandGestureDetectionModel::setGestureResultCallback(handGestureResultCallbackT cb)
{
    gesture_result_callback_ = cb;
}

void HandGestureDetectionModel::resolverLoad(void)
{
    micro_op_resolver.AddEthosU();
    MicroPrintf("HandGestureDetectionModel resolverLoad\r\n");
}

void HandGestureDetectionModel::resourceLoad(void)
{
    name = "HandGestureDetectionModel";
    width = k_input_w;
    height = k_input_h;
    format = BK_PIXEL_FORMAT_RGB888;
    model_type = AVDK_NN_MODEL_TYPE_NPU;

#if CONFIG_SDCARD
    modelLoadType = AVDK_NN_MODEL_LOAD_TYPE_SD_FILE;
    model_ram_type = AVDK_NN_MEM_TYPE_PSRAM_SLAB;
    model_flash_data = NULL;
    model_flash_data_size = 0;
    model_data = NULL;
    model_data_size = 0;
    if (modelFilePath == NULL || modelFilePath[0] == '\0') {
        modelFilePath = HAND_GESTURE_MODEL_SD_PATH;
    }
#else
    modelLoadType = AVDK_NN_MODEL_LOAD_TYPE_FLASH;
    model_ram_type = AVDK_NN_MEM_TYPE_FALSH;
    model_flash_data = (uint8_t *)hand_gesture_detection_vela_tflite;
    model_flash_data_size = hand_gesture_detection_vela_tflite_len;
    model_data = (uint8_t *)hand_gesture_detection_vela_tflite;
    model_data_size = hand_gesture_detection_vela_tflite_len;
#endif

    fast_ram_type = AVDK_NN_MEM_TYPE_HSRAM;
    fast_ram_data_size = ETHOSU_SCRATCH_SIZE;
    fast_ram_data = nullptr;

    arena_data_size = TFLM_ARENA_SIZE;
    arena_ram_type = AVDK_NN_MEM_TYPE_PSRAM_SLAB_UNCODED;
    arena_ram_data = nullptr;

    if (!hand_gesture_alloc_scratch()) {
        MicroPrintf("HandGestureDetectionModel scratch alloc failed\r\n");
    }

    MicroPrintf("HandGestureDetectionModel resourceLoad model=%u arena=%u scratch=%u\r\n",
                (unsigned)model_data_size,
                (unsigned)arena_data_size,
                (unsigned)fast_ram_data_size);
}

void HandGestureDetectionModel::resourceUnload(void)
{
    s_tensors_checked = false;
    s_last_preset = -1;
    s_run_count = 0;
    hand_gesture_free_scratch();
}

bool HandGestureDetectionModel::checkTensors(void)
{
    if (s_tensors_checked) {
        return true;
    }

    if (pinterpreter == nullptr) {
        MicroPrintf("hand gesture interpreter is null\r\n");
        return false;
    }

    TfLiteTensor *input = pinterpreter->input(0);
    TfLiteTensor *output = pinterpreter->output(0);
    if (input == nullptr || output == nullptr) {
        MicroPrintf("hand gesture null tensor\r\n");
        return false;
    }

    if (input->type != kTfLiteInt8 || output->type != kTfLiteInt8) {
        MicroPrintf("hand gesture expected int8 in/out\r\n");
        return false;
    }

    if (input->dims->size != 4 ||
        input->dims->data[0] != 1 ||
        input->dims->data[1] != k_input_h ||
        input->dims->data[2] != k_input_w ||
        input->dims->data[3] != k_input_c) {
        MicroPrintf("hand gesture input shape mismatch: expect [1,%d,%d,%d]\r\n",
                    k_input_h, k_input_w, k_input_c);
        return false;
    }

    if (output->dims->size != 3 ||
        output->dims->data[1] != k_out_channels ||
        output->dims->data[2] != k_num_candidates) {
        MicroPrintf("hand gesture output shape mismatch: expect [1,%d,%d]\r\n",
                    k_out_channels, k_num_candidates);
        return false;
    }

    MicroPrintf("HandGestureDetectionModel arena used %u / %u bytes\r\n",
                (unsigned)pinterpreter->arena_used_bytes(), (unsigned)arena_data_size);
    s_tensors_checked = true;
    return true;
}

int HandGestureDetectionModel::run(uint8_t *data, uint32_t size, bk_pixel_format_t pixel_format)
{
    const unsigned long long run_start_us = bk_aon_rtc_get_us();
    s_run_count++;

    if (data == nullptr) {
        MicroPrintf("HandGesture run frame=%u null input\r\n", (unsigned)s_run_count);
        return 0;
    }

    const uint32_t expected_bgrx_bytes = (uint32_t)k_input_w * (uint32_t)k_input_h * 4U;
    if (size < expected_bgrx_bytes) {
        MicroPrintf("hand gesture input size too small: %u < %u\r\n",
                    (unsigned)size, (unsigned)expected_bgrx_bytes);
        return 0;
    }

    if (pixel_format != BK_PIXEL_FORMAT_BGRA8888) {
        MicroPrintf("hand gesture unexpected run format=%d, treating as BGRX8888\r\n", pixel_format);
    }

    if (!checkTensors() || s_dets == nullptr || s_out_boxes == nullptr) {
        return 0;
    }

    TfLiteTensor *input = pinterpreter->input(0);
    TfLiteTensor *output = pinterpreter->output(0);

    unsigned long long start_us = bk_aon_rtc_get_us();
    if (!bgrx_to_rgb_int8_simd(data, input->data.int8, k_input_w, k_input_h)) {
        MicroPrintf("hand gesture input prepare failed\r\n");
        return 0;
    }
    log_interval("HandGesture RGB prepare", start_us);

    start_us = bk_aon_rtc_get_us();
    if (pinterpreter->Invoke() != kTfLiteOk) {
        MicroPrintf("HandGesture Invoke failed\r\n");
        return 0;
    }
    log_interval("HandGesture Invoke", start_us);

    start_us = bk_aon_rtc_get_us();
    int det_count = postprocess_yolov8_int8(output->data.int8,
                                            output->params.scale,
                                            output->params.zero_point,
                                            s_dets,
                                            k_max_detections);
    log_interval("HandGesture postprocess", start_us);

    if ((s_run_count % 30U) == 0U || det_count > 0) {
        MicroPrintf("HandGesture run frame=%u det_count=%d\r\n",
                    (unsigned)s_run_count, det_count);
    }

    if (det_count <= 0) {
        onBoxDetectionCallback(NULL, 0);
        log_interval("HandGesture run total", run_start_us);
        return 0;
    }

    for (int i = 0; i < det_count; i++) {
        s_out_boxes[i].x = s_dets[i].x1;
        s_out_boxes[i].y = s_dets[i].y1;
        s_out_boxes[i].w = s_dets[i].x2 - s_dets[i].x1;
        s_out_boxes[i].h = s_dets[i].y2 - s_dets[i].y1;
        s_out_boxes[i].score = s_dets[i].score;
    }
    onBoxDetectionCallback(s_out_boxes, det_count);

    const int class_id = s_dets[0].class_id;
    if (class_id >= 0 && class_id < k_num_classes) {
        MicroPrintf("HandGesture top gesture class_id=%d (%s) score=%.3f count=%d\r\n",
                    class_id, k_class_names[class_id], s_dets[0].score, det_count);

        if (gesture_result_callback_ != nullptr) {
            gesture_result_callback_(class_id, k_class_names[class_id], s_dets[0].score, det_count);
        }

        const int8_t preset = k_class_preset_map[class_id];
        if (preset >= 0 && preset != s_last_preset && gesture_action_callback_ != nullptr) {
            s_last_preset = preset;
            gesture_action_callback_((uint8_t)preset);
        } else if (preset < 0) {
            MicroPrintf("hand gesture class%d no action\r\n", class_id);
        }
    }

    log_interval("HandGesture run total", run_start_us);
    return det_count;
}
