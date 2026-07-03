#pragma once

#include <stddef.h>
#include <stdint.h>

#include "AvdkMultiModel.h"
#include "FaceRecognitionModel.h"
#include "tensorflow/lite/micro/micro_interpreter.h"

class FaceVerifyRuntime {
public:
    FaceVerifyRuntime();
    ~FaceVerifyRuntime();

    void setModelFilePath(const char *path);
    bool init();
    void deinit();
    void releaseInterpreter();

    bool extract(const uint8_t *rgb_image,
                 size_t image_len,
                 FaceVerifyResult *result);

private:
    bool initInterpreter();
    bool copyOutputEmbedding(const TfLiteTensor *output, FaceVerifyResult *result);
    int tensorElementCount(const TfLiteTensor *tensor) const;

    const char *model_file_path_;
    AvdkMultiModel model_;
};
