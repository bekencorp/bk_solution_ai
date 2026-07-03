#include "FaceRecognitionModel.h"

#include "FaceDetectionRuntime.h"
#include "FaceVerifyRuntime.h"

#include <math.h>
#include <stdint.h>

#include <app_gpu.h>
#include <components/bk_frame_buffer.h>
#include <components/bk_gpu.h>
#include <driver/aon_rtc.h>
#include <modules/vg_lite_gpu/vg_lite.h>
#include <os/mem.h>

#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

#if defined(__ARM_FEATURE_MVE)
#include <arm_mve.h>
#endif

#ifndef FACE_RECOG_DETECT_MODEL_SD_PATH
#define FACE_RECOG_DETECT_MODEL_SD_PATH "1:/tflite/face_detection_int8_vela.tflite"
#endif

#ifndef FACE_RECOG_VERIFY_MODEL_SD_PATH
#define FACE_RECOG_VERIFY_MODEL_SD_PATH "1:/tflite/face_verify_int8_vela.tflite"
#endif

static constexpr int kDetectInputW = 320;
static constexpr int kDetectInputH = 320;
static constexpr int kDetectInputC = 3;

static constexpr int kAlignedFaceW = 112;
static constexpr int kAlignedFaceH = 112;
static constexpr int kAlignedFaceC = 3;
static constexpr int kAlignedFaceBytes = kAlignedFaceW * kAlignedFaceH * kAlignedFaceC;
static constexpr int kMaxFaceDetections = 32;

static Box s_face_out_boxes[kMaxFaceDetections];

static const FacePoint kArcfaceDst[kFaceDetectKeypointCount] = {
    {38.2946f, 51.6963f},
    {73.5318f, 51.5014f},
    {56.0252f, 71.7366f},
    {41.5493f, 92.3655f},
    {70.7299f, 92.2041f},
};

static bool bgrx_to_rgb_int8_simd(const uint8_t *src, int8_t *dst, uint32_t width, uint32_t height)
{
    if (src == nullptr || dst == nullptr) {
        return false;
    }

    const uint32_t pixels = width * height;
    uint32_t i = 0;

#if defined(__ARM_FEATURE_MVE)
    for (; i + 16 <= pixels; i += 16) {
        static const uint8_t kMveRgbOffsets[16] = {
            0, 3, 6, 9, 12, 15, 18, 21,
            24, 27, 30, 33, 36, 39, 42, 45,
        };
        const uint8x16x4_t bgrx = vld4q_u8(src + i * 4U);
        const uint8x16_t offset = vld1q_u8(kMveRgbOffsets);
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

static bool estimate_similarity_transform(const FacePoint *src,
                                          const FacePoint *dst,
                                          int n,
                                          float matrix[6])
{
    if (src == nullptr || dst == nullptr || matrix == nullptr || n < 2) {
        return false;
    }

    float src_cx = 0.0f;
    float src_cy = 0.0f;
    float dst_cx = 0.0f;
    float dst_cy = 0.0f;
    for (int i = 0; i < n; i++) {
        src_cx += src[i].x;
        src_cy += src[i].y;
        dst_cx += dst[i].x;
        dst_cy += dst[i].y;
    }
    src_cx /= (float)n;
    src_cy /= (float)n;
    dst_cx /= (float)n;
    dst_cy /= (float)n;

    float denom = 0.0f;
    float a_num = 0.0f;
    float b_num = 0.0f;
    for (int i = 0; i < n; i++) {
        const float xs = src[i].x - src_cx;
        const float ys = src[i].y - src_cy;
        const float xd = dst[i].x - dst_cx;
        const float yd = dst[i].y - dst_cy;
        denom += xs * xs + ys * ys;
        a_num += xs * xd + ys * yd;
        b_num += xs * yd - ys * xd;
    }
    if (denom <= 1e-10f) {
        return false;
    }

    const float a = a_num / denom;
    const float b = b_num / denom;
    matrix[0] = a;
    matrix[1] = -b;
    matrix[2] = dst_cx - a * src_cx + b * src_cy;
    matrix[3] = b;
    matrix[4] = a;
    matrix[5] = dst_cy - b * src_cx - a * src_cy;
    return true;
}

static bool gpu_align_rgb112(uint8_t *src_frame,
                             int src_width,
                             int src_height,
                             vg_lite_buffer_format_t src_format,
                             const float matrix[6],
                             uint8_t *dst_data)
{
    if (app_gpu_handle_get() == nullptr || app_gpu_lock() != AVDK_ERR_OK) {
        MicroPrintf("FaceRecognition GPU lock failed\r\n");
        return false;
    }

    vg_lite_buffer_t src = {};
    src.width = src_width;
    src.height = src_height;
    src.format = src_format;
    src.tiled = VG_LITE_LINEAR;
    src.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    src.compress_mode = VG_LITE_DEC_DISABLE;

    vg_lite_buffer_t dst = {};
    dst.width = kAlignedFaceW;
    dst.height = kAlignedFaceH;
    dst.format = VG_LITE_RGB888;
    dst.tiled = VG_LITE_LINEAR;
    dst.image_mode = VG_LITE_NORMAL_IMAGE_MODE;
    dst.compress_mode = VG_LITE_DEC_DISABLE;

    vg_lite_matrix_t vg_matrix = {};
    vg_matrix.m[0][0] = matrix[0];
    vg_matrix.m[0][1] = matrix[1];
    vg_matrix.m[0][2] = matrix[2];
    vg_matrix.m[1][0] = matrix[3];
    vg_matrix.m[1][1] = matrix[4];
    vg_matrix.m[1][2] = matrix[5];
    vg_matrix.m[2][0] = 0.0f;
    vg_matrix.m[2][1] = 0.0f;
    vg_matrix.m[2][2] = 1.0f;

    bool ok = true;
    vg_lite_error_t err = vg_lite_allocate_with_data(&src, src_frame, nullptr, nullptr, nullptr);
    if (err != VG_LITE_SUCCESS) {
        MicroPrintf("FaceRecognition vg_lite src failed err=%d\r\n", err);
        ok = false;
        goto exit;
    }

    err = vg_lite_allocate_with_data(&dst, dst_data, nullptr, nullptr, nullptr);
    if (err != VG_LITE_SUCCESS) {
        MicroPrintf("FaceRecognition vg_lite dst failed err=%d\r\n", err);
        ok = false;
        goto exit;
    }

    err = vg_lite_blit(&dst, &src, &vg_matrix, VG_LITE_BLEND_NONE, 0, VG_LITE_FILTER_LINEAR);
    if (err != VG_LITE_SUCCESS) {
        MicroPrintf("FaceRecognition vg_lite_blit failed err=%d\r\n", err);
        ok = false;
        goto exit;
    }

    err = vg_lite_finish();
    if (err != VG_LITE_SUCCESS) {
        MicroPrintf("FaceRecognition vg_lite_finish failed err=%d\r\n", err);
        ok = false;
    }

exit:
    vg_lite_free_without_free_data(&dst);
    vg_lite_free_without_free_data(&src);

    if (app_gpu_unlock() != AVDK_ERR_OK) {
        MicroPrintf("FaceRecognition GPU unlock failed\r\n");
        ok = false;
    }
    return ok;
}

FaceRecognitionModel::FaceRecognitionModel()
    : detector_(new FaceDetectionRuntime()),
      verifier_(new FaceVerifyRuntime()),
      verify_model_file_path_(nullptr),
      verify_result_callback_(nullptr),
      enroll_result_callback_(nullptr),
      verify_enabled_(false)
{
    name = "FaceRecognitionModel";
    width = kDetectInputW;
    height = kDetectInputH;
    format = BK_PIXEL_FORMAT_RGB888;
    model_type = AVDK_NN_MODEL_TYPE_NPU;
}

FaceRecognitionModel::~FaceRecognitionModel()
{
    delete detector_;
    detector_ = nullptr;
    delete verifier_;
    verifier_ = nullptr;
}

void FaceRecognitionModel::setModelFilePath(const char *path)
{
    modelFilePath = path;
    if (detector_ != nullptr) {
        detector_->setModelFilePath(path);
    }
}

void FaceRecognitionModel::setVerifyModelFilePath(const char *path)
{
    verify_model_file_path_ = path;
    if (verifier_ != nullptr) {
        verifier_->setModelFilePath(path);
    }
}

void FaceRecognitionModel::setVerifyResultCallback(faceVerifyResultCallbackT cb)
{
    verify_result_callback_ = cb;
}

void FaceRecognitionModel::setEnrollResultCallback(faceEnrollResultCallbackT cb)
{
    enroll_result_callback_ = cb;
}

void FaceRecognitionModel::setVerifyEnabled(bool enable)
{
    verify_enabled_ = enable;
}

int FaceRecognitionModel::init(void)
{
    resourceLoad();
    return 0;
}

int FaceRecognitionModel::deinit(void)
{
    resourceUnload();
    return 0;
}

void FaceRecognitionModel::resolverLoad(void)
{
    micro_op_resolver.AddEthosU();
    MicroPrintf("FaceRecognitionModel resolverLoad\r\n");
}

void FaceRecognitionModel::resourceLoad(void)
{
    name = "FaceRecognitionModel";
    width = kDetectInputW;
    height = kDetectInputH;
    format = BK_PIXEL_FORMAT_RGB888;
    model_type = AVDK_NN_MODEL_TYPE_NPU;
    model_ram_type = AVDK_NN_MEM_TYPE_FALSH;
    model_flash_data = nullptr;
    model_flash_data_size = 0;
    model_data = nullptr;
    model_data_size = 0;

    if (modelFilePath == nullptr || modelFilePath[0] == '\0') {
        modelFilePath = FACE_RECOG_DETECT_MODEL_SD_PATH;
    }
    if (verify_model_file_path_ == nullptr || verify_model_file_path_[0] == '\0') {
        verify_model_file_path_ = FACE_RECOG_VERIFY_MODEL_SD_PATH;
    }
    if (detector_ != nullptr) {
        detector_->setModelFilePath(modelFilePath);
    }
    if (verifier_ != nullptr) {
        verifier_->setModelFilePath(verify_model_file_path_);
    }

    if (detector_ != nullptr && !detector_->init()) {
        MicroPrintf("FaceRecognition detector runtime init failed\r\n");
    }
    if (verifier_ != nullptr && !verifier_->init()) {
        MicroPrintf("FaceRecognition verify runtime init failed\r\n");
    }

    MicroPrintf("FaceRecognitionModel resourceLoad detect=%s verify=%s\r\n",
                modelFilePath, verify_model_file_path_);
}

void FaceRecognitionModel::resourceUnload(void)
{
    releaseDetectorInterpreter();
    if (detector_ != nullptr) {
        detector_->deinit();
    }
    if (verifier_ != nullptr) {
        verifier_->deinit();
    }
}

void FaceRecognitionModel::releaseDetectorInterpreter(void)
{
    if (detector_ != nullptr) {
        detector_->releaseInterpreter();
    }
    pinterpreter = nullptr;
}

bool FaceRecognitionModel::ensureDetectorInterpreter(void)
{
    if (detector_ == nullptr) {
        return false;
    }

    pinterpreter = detector_->prepareInterpreter();
    if (pinterpreter == nullptr) {
        return false;
    }
    return true;
}

int FaceRecognitionModel::run(uint8_t *data, uint32_t size, bk_pixel_format_t pixel_format)
{
    (void)size;
    (void)pixel_format;

#define FACE_RECOG_NOTIFY_ENROLL_FAIL()                                      \
    do {                                                                     \
        if (verify_enabled_ && enroll_result_callback_ != nullptr) {          \
            enroll_result_callback_(nullptr, nullptr, 0);                     \
        }                                                                    \
    } while (0)

    const unsigned long long run_start_us = bk_aon_rtc_get_us();
    if (data == nullptr || detector_ == nullptr || verifier_ == nullptr) {
        FACE_RECOG_NOTIFY_ENROLL_FAIL();
        return 0;
    }
    if (!ensureDetectorInterpreter()) {
        FACE_RECOG_NOTIFY_ENROLL_FAIL();
        return 0;
    }

    TfLiteTensor *input = pinterpreter->input(0);
    if (input == nullptr || input->type != kTfLiteInt8) {
        MicroPrintf("FaceRecognition invalid detector input tensor\r\n");
        FACE_RECOG_NOTIFY_ENROLL_FAIL();
        return 0;
    }

    unsigned long long start_us = bk_aon_rtc_get_us();
    unsigned long long input_prepare_us = 0;
    unsigned long long detect_us = 0;
    unsigned long long align_us = 0;
    unsigned long long verify_us = 0;
    if (!bgrx_to_rgb_int8_simd(data, input->data.int8, kDetectInputW, kDetectInputH)) {
        MicroPrintf("FaceRecognition detector input prepare failed\r\n");
        FACE_RECOG_NOTIFY_ENROLL_FAIL();
        return 0;
    }
    input_prepare_us = bk_aon_rtc_get_us() - start_us;

    FaceDetection *detections = (FaceDetection *)bk_frame_buffer_malloc(
        MEM_SLAB_HEAP_UNCODED, sizeof(FaceDetection) * kMaxFaceDetections);
    if (detections == nullptr) {
        MicroPrintf("FaceRecognition alloc detections failed\r\n");
        FACE_RECOG_NOTIFY_ENROLL_FAIL();
        return 0;
    }
    start_us = bk_aon_rtc_get_us();
    const int det_count = detector_->invokeAndDecode(pinterpreter, detections, kMaxFaceDetections);
    detect_us = bk_aon_rtc_get_us() - start_us;
    if (det_count <= 0) {
        onBoxDetectionCallback(NULL, 0);
        bk_frame_buffer_free(detections);
        return 0;
    }

    int box_count = 0;
    for (int i = 0; i < det_count; i++) {
        float x1 = detections[i].x1;
        float y1 = detections[i].y1;
        float x2 = detections[i].x2;
        float y2 = detections[i].y2;

        if (x1 < 0.0f) x1 = 0.0f;
        if (y1 < 0.0f) y1 = 0.0f;
        if (x1 > (float)kDetectInputW) x1 = (float)kDetectInputW;
        if (y1 > (float)kDetectInputH) y1 = (float)kDetectInputH;
        if (x2 < 0.0f) x2 = 0.0f;
        if (y2 < 0.0f) y2 = 0.0f;
        if (x2 > (float)kDetectInputW) x2 = (float)kDetectInputW;
        if (y2 > (float)kDetectInputH) y2 = (float)kDetectInputH;
        if (x2 <= x1 || y2 <= y1) {
            continue;
        }

        s_face_out_boxes[box_count].x = x1;
        s_face_out_boxes[box_count].y = y1;
        s_face_out_boxes[box_count].w = x2 - x1;
        s_face_out_boxes[box_count].h = y2 - y1;
        s_face_out_boxes[box_count].score = detections[i].score;
        box_count++;
    }
    onBoxDetectionCallback(box_count > 0 ? s_face_out_boxes : NULL, box_count);

    if (!verify_enabled_ && verify_result_callback_ == nullptr) {
        bk_frame_buffer_free(detections);
        return 1;
    }
    MicroPrintf("FaceRecognition: verify/enroll frame begin\r\n");

    float affine[6];
    if (!estimate_similarity_transform(detections[0].keypoints,
                                       kArcfaceDst,
                                       kFaceDetectKeypointCount,
                                       affine)) {
        MicroPrintf("FaceRecognition estimate affine failed\r\n");
        if (verify_enabled_ && enroll_result_callback_ != nullptr) {
            enroll_result_callback_(nullptr, nullptr, 0);
        }
        bk_frame_buffer_free(detections);
        return 0;
    }

    uint8_t *aligned_rgb = (uint8_t *)bk_frame_buffer_malloc(MEM_SLAB_HEAP_UNCODED,
                                                             kAlignedFaceBytes);
    if (aligned_rgb == nullptr) {
        MicroPrintf("FaceRecognition alloc aligned RGB112 failed\r\n");
        if (verify_enabled_ && enroll_result_callback_ != nullptr) {
            enroll_result_callback_(nullptr, nullptr, 0);
        }
        bk_frame_buffer_free(detections);
        return 0;
    }

    start_us = bk_aon_rtc_get_us();
    MicroPrintf("FaceRecognition: align begin\r\n");
    bool align_ok = gpu_align_rgb112(data,
                                     kDetectInputW,
                                     kDetectInputH,
                                     VG_LITE_BGRX8888,
                                     affine,
                                     aligned_rgb);
    align_us = bk_aon_rtc_get_us() - start_us;
    MicroPrintf("FaceRecognition: align end ok=%d us=%llu\r\n", align_ok, align_us);
    if (!align_ok) {
        if (verify_enabled_ && enroll_result_callback_ != nullptr) {
            enroll_result_callback_(nullptr, nullptr, 0);
        }
        bk_frame_buffer_free(aligned_rgb);
        bk_frame_buffer_free(detections);
        return 0;
    }

    FaceVerifyResult *result = (FaceVerifyResult *)bk_frame_buffer_malloc(
        MEM_SLAB_HEAP_UNCODED, sizeof(FaceVerifyResult));
    if (result == nullptr) {
        MicroPrintf("FaceRecognition alloc verify result failed\r\n");
        if (verify_enabled_ && enroll_result_callback_ != nullptr) {
            enroll_result_callback_(nullptr, nullptr, 0);
        }
        bk_frame_buffer_free(aligned_rgb);
        bk_frame_buffer_free(detections);
        return 0;
    }
    os_memset(result, 0, sizeof(FaceVerifyResult));

    releaseDetectorInterpreter();
    start_us = bk_aon_rtc_get_us();
    MicroPrintf("FaceRecognition: verify begin\r\n");
    bool verify_ok = verifier_->extract(aligned_rgb,
                                        kAlignedFaceBytes,
                                        result);
    verify_us = bk_aon_rtc_get_us() - start_us;
    MicroPrintf("FaceRecognition: verify end ok=%d us=%llu\r\n", verify_ok, verify_us);
    verifier_->releaseInterpreter();

    if (!verify_ok) {
        if (verify_enabled_ && enroll_result_callback_ != nullptr) {
            enroll_result_callback_(nullptr, nullptr, 0);
        }
        bk_frame_buffer_free(detections);
        bk_frame_buffer_free(aligned_rgb);
        bk_frame_buffer_free(result);
        return 0;
    }

    if (verify_enabled_ && enroll_result_callback_ != nullptr) {
        MicroPrintf("FaceRecognition: enroll callback\r\n");
        enroll_result_callback_(result, aligned_rgb, kAlignedFaceBytes);
    }

    if (verify_result_callback_ != nullptr) {
        verify_result_callback_(result);
    }

    bk_frame_buffer_free(detections);
    bk_frame_buffer_free(aligned_rgb);
    bk_frame_buffer_free(result);
    MicroPrintf("FaceRecognition: input=%llu us, detect=%llu us, align=%llu us, verify=%llu us, total=%llu us\r\n",
                input_prepare_us,
                detect_us,
                align_us,
                verify_us,
                bk_aon_rtc_get_us() - run_start_us);
#undef FACE_RECOG_NOTIFY_ENROLL_FAIL
    return 1;
}
