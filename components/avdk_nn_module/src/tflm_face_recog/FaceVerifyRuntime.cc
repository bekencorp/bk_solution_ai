#include "FaceVerifyRuntime.h"

#include <math.h>

#include <os/mem.h>

#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/schema/schema_generated.h"

#if !CONFIG_SDCARD
extern "C" {
extern const unsigned char face_verify_vela_tflite[];
extern const unsigned int face_verify_vela_tflite_len;
}
#endif

#if defined(__ARM_FEATURE_MVE)
#include <arm_mve.h>
#endif

#ifndef FACE_RECOG_VERIFY_MODEL_SD_PATH
#define FACE_RECOG_VERIFY_MODEL_SD_PATH "1:/tflite/face_verify_int8_vela.tflite"
#endif

static constexpr int kVerifyInputW = 112;
static constexpr int kVerifyInputH = 112;
static constexpr int kVerifyInputC = 3;
static constexpr int kVerifyInputBytes = kVerifyInputW * kVerifyInputH * kVerifyInputC;
static constexpr int kVerifyScratchSize = 80 * 1024;
static constexpr int kVerifyArenaSize = 450 * 1024;

static inline void u8_minus128_inplace(uint8_t *buf, uint32_t size)
{
#if defined(__ARM_FEATURE_MVE)
    uint32_t i = 0;
    for (; i + 16 <= size; i += 16) {
        uint8x16_t v = vld1q_u8(buf + i);
        v = vsubq_n_u8(v, 128);
        vst1q_u8(buf + i, v);
    }
    for (; i < size; i++) {
        buf[i] = (uint8_t)((int)buf[i] - 128);
    }
#else
    for (uint32_t i = 0; i < size; i++) {
        buf[i] = (uint8_t)((int)buf[i] - 128);
    }
#endif
}

FaceVerifyRuntime::FaceVerifyRuntime()
    : model_file_path_(nullptr),
      model_()
{
}

FaceVerifyRuntime::~FaceVerifyRuntime()
{
    deinit();
}

void FaceVerifyRuntime::setModelFilePath(const char *path)
{
    model_file_path_ = path;
    model_.setModelFilePath(path);
}

bool FaceVerifyRuntime::init()
{
    if (model_file_path_ == nullptr || model_file_path_[0] == '\0') {
        model_file_path_ = FACE_RECOG_VERIFY_MODEL_SD_PATH;
    }

    model_.setName("FaceVerifyRuntime");
    model_.setModelType(AVDK_NN_MODEL_TYPE_NPU);
    model_.setFastRam(AVDK_NN_MEM_TYPE_HSRAM, kVerifyScratchSize);
    model_.setArenaRam(AVDK_NN_MEM_TYPE_PSRAM_SLAB_UNCODED, kVerifyArenaSize);
#if CONFIG_SDCARD
    model_.setModelLoadType(AVDK_NN_MODEL_LOAD_TYPE_SD_FILE);
    model_.setModelRamType(AVDK_NN_MEM_TYPE_PSRAM_SLAB_UNCODED);
    model_.setModelFilePath(model_file_path_);
#else
    model_.setModelLoadType(AVDK_NN_MODEL_LOAD_TYPE_FLASH);
    model_.setModelRamType(AVDK_NN_MEM_TYPE_PSRAM_SLAB_UNCODED);
    model_.setFlashModel((uint8_t *)face_verify_vela_tflite,
                         face_verify_vela_tflite_len);
#endif

    if (model_.init() != 0) {
        MicroPrintf("FaceVerifyRuntime model init failed\r\n");
        return false;
    }
    return true;
}

void FaceVerifyRuntime::deinit()
{
    releaseInterpreter();
    model_.deinit();
}

void FaceVerifyRuntime::releaseInterpreter()
{
    model_.releaseInterpreter();
}

bool FaceVerifyRuntime::extract(const uint8_t *rgb_image,
                                size_t image_len,
                                FaceVerifyResult *result)
{
    if (rgb_image == nullptr || result == nullptr) {
        return false;
    }
    if (image_len != (size_t)kVerifyInputBytes) {
        MicroPrintf("FaceVerify invalid input len=%u expect=%u\r\n",
                    (unsigned)image_len, (unsigned)kVerifyInputBytes);
        return false;
    }
    if (!initInterpreter()) {
        return false;
    }

    tflite::MicroInterpreter *interpreter = model_.interpreter();
    if (interpreter == nullptr) {
        return false;
    }
    TfLiteTensor *input = interpreter->input(0);
    TfLiteTensor *output = interpreter->output(0);
    if (input == nullptr || output == nullptr || input->type != kTfLiteInt8 ||
        (size_t)input->bytes != image_len) {
        MicroPrintf("FaceVerify tensor/input mismatch\r\n");
        return false;
    }

    os_memcpy(input->data.int8, rgb_image, image_len);
    u8_minus128_inplace((uint8_t *)input->data.int8, (uint32_t)image_len);

    if (interpreter->Invoke() != kTfLiteOk) {
        MicroPrintf("FaceVerify Invoke failed\r\n");
        return false;
    }

    return copyOutputEmbedding(output, result);
}

bool FaceVerifyRuntime::initInterpreter()
{
    if (model_.prepareInterpreter() != 0) {
        return false;
    }

    tflite::MicroInterpreter *interpreter = model_.interpreter();
    if (interpreter == nullptr) {
        return false;
    }

    TfLiteTensor *input = interpreter->input(0);
    TfLiteTensor *output = interpreter->output(0);
    if (input == nullptr || output == nullptr || input->dims == nullptr ||
        input->dims->size != 4 ||
        input->dims->data[0] != 1 ||
        input->dims->data[1] != kVerifyInputH ||
        input->dims->data[2] != kVerifyInputW ||
        input->dims->data[3] != kVerifyInputC ||
        tensorElementCount(output) != kFaceEmbeddingDim) {
        MicroPrintf("FaceVerify tensor shape mismatch\r\n");
        releaseInterpreter();
        return false;
    }
    return true;
}

int FaceVerifyRuntime::tensorElementCount(const TfLiteTensor *tensor) const
{
    if (tensor == nullptr || tensor->dims == nullptr || tensor->dims->size <= 0) {
        return 0;
    }

    int count = 1;
    for (int i = 0; i < tensor->dims->size; i++) {
        count *= tensor->dims->data[i];
    }
    return count;
}

bool FaceVerifyRuntime::copyOutputEmbedding(const TfLiteTensor *output, FaceVerifyResult *result)
{
    if (output == nullptr || result == nullptr ||
        tensorElementCount(output) != kFaceEmbeddingDim) {
        return false;
    }

    float sum_sq = 0.0f;
    for (int i = 0; i < kFaceEmbeddingDim; i++) {
        float value = 0.0f;
        if (output->type == kTfLiteInt8) {
            value = ((float)output->data.int8[i] -
                     (float)output->params.zero_point) * output->params.scale;
        } else if (output->type == kTfLiteFloat32) {
            value = output->data.f[i];
        } else {
            MicroPrintf("FaceVerify unsupported output type=%d\r\n", output->type);
            return false;
        }

        result->normalized[i] = value;
        sum_sq += value * value;
    }

    const float norm = sqrtf(sum_sq);
    if (norm <= 0.0f) {
        MicroPrintf("FaceVerify embedding norm is zero\r\n");
        return false;
    }

    for (int i = 0; i < kFaceEmbeddingDim; i++) {
        result->normalized[i] /= norm;
    }
    result->norm = norm;
    result->valid = true;
    return true;
}

float face_recognition_cosine_similarity(const float *a, const float *b, int dim)
{
    if (a == nullptr || b == nullptr || dim <= 0) {
        return 0.0f;
    }

    float score = 0.0f;
    for (int i = 0; i < dim; i++) {
        score += a[i] * b[i];
    }
    return score;
}

bool face_recognition_is_same_person(const float *a, const float *b, int dim, float threshold)
{
    return face_recognition_cosine_similarity(a, b, dim) >= threshold;
}
