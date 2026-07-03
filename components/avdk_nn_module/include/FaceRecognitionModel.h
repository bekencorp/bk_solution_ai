#pragma once

#include <stdint.h>

#include "AvdkDetectionModel.h"

constexpr int kFaceEmbeddingDim = 512;
constexpr float kFaceVerifySameThreshold = 0.6f;

struct FaceVerifyResult {
    bool valid;
    float norm;
    float normalized[kFaceEmbeddingDim];
};

typedef void (*faceVerifyResultCallbackT)(const FaceVerifyResult *result);
typedef void (*faceEnrollResultCallbackT)(const FaceVerifyResult *result,
                                          const uint8_t *aligned_rgb,
                                          uint32_t aligned_rgb_size);

float face_recognition_cosine_similarity(const float *a, const float *b, int dim);
bool face_recognition_is_same_person(const float *a, const float *b, int dim, float threshold);

class FaceDetectionRuntime;
class FaceVerifyRuntime;

class FaceRecognitionModel : public AvdkDetectionModel {
public:
    FaceRecognitionModel();
    ~FaceRecognitionModel() override;

    void setModelFilePath(const char *path);
    void setVerifyModelFilePath(const char *path);
    void setVerifyResultCallback(faceVerifyResultCallbackT cb);
    void setEnrollResultCallback(faceEnrollResultCallbackT cb);
    void setVerifyEnabled(bool enable);

    int init(void) override;
    int deinit(void) override;
    void resolverLoad(void) override;
    void resourceLoad(void) override;
    void resourceUnload(void) override;
    int run(uint8_t *data, uint32_t size, bk_pixel_format_t format) override;

private:
    bool ensureDetectorInterpreter(void);
    void releaseDetectorInterpreter(void);

    FaceDetectionRuntime *detector_;
    FaceVerifyRuntime *verifier_;
    const char *verify_model_file_path_;
    faceVerifyResultCallbackT verify_result_callback_;
    faceEnrollResultCallbackT enroll_result_callback_;
    volatile bool verify_enabled_;
};
