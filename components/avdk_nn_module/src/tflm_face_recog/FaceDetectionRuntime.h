#pragma once

#include <stdint.h>

#include "AvdkMultiModel.h"
#include "tensorflow/lite/micro/micro_interpreter.h"

constexpr int kFaceDetectKeypointCount = 5;

struct FacePoint {
    float x;
    float y;
};

struct FaceDetection {
    float x1;
    float y1;
    float x2;
    float y2;
    float score;
    int order;
    FacePoint keypoints[kFaceDetectKeypointCount];
};

class FaceDetectionRuntime {
public:
    FaceDetectionRuntime();
    ~FaceDetectionRuntime();

    void setModelFilePath(const char *path);
    bool init();
    void deinit();
    tflite::MicroInterpreter *prepareInterpreter();
    void releaseInterpreter();
    void resetOutputSpecs();

    bool buildOutputSpecs(tflite::MicroInterpreter *interpreter);
    int invokeAndDecode(tflite::MicroInterpreter *interpreter, FaceDetection *out, int max_out);

private:
    struct OutputSpec {
        int stride;
        int feature_count;
        TfLiteTensor *score;
        TfLiteTensor *bbox;
        TfLiteTensor *keypoints;
    };

    float tensorValue(const TfLiteTensor *tensor, int flat_index) const;
    float keypointTensorValue(const TfLiteTensor *tensor, int logical_idx, int channel, int stride) const;
    int decode(FaceDetection *candidates, int max_candidates);
    int nms(FaceDetection *candidates, int count, FaceDetection *out, int max_out);

    OutputSpec specs_[3];
    FaceDetection *candidates_;
    AvdkMultiModel model_;
    const char *model_file_path_;
    bool output_specs_ready_;
};
