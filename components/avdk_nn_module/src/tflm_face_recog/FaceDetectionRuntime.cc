#include "FaceDetectionRuntime.h"

#include <algorithm>
#include <math.h>

#include <components/bk_frame_buffer.h>
#include <os/mem.h>

#include "tensorflow/lite/micro/micro_log.h"

#if !CONFIG_SDCARD
extern "C" {
extern const unsigned char face_detection_vela_tflite[];
extern const unsigned int face_detection_vela_tflite_len;
}
#endif

#ifndef FACE_RECOG_DETECT_MODEL_SD_PATH
#define FACE_RECOG_DETECT_MODEL_SD_PATH "1:/tflite/face_detection_int8_vela.tflite"
#endif

static constexpr int kDetectInputW = 320;
static constexpr int kDetectInputH = 320;
static constexpr int kDetectScratchSize = 80 * 1024;
static constexpr int kDetectArenaSize = 750 * 1024;
static constexpr int kDetectNumOutputs = 9;
static constexpr int kDetectNumStrides = 3;
static constexpr int kDetectNumAnchors = 2;
static constexpr int kDetectScoreChannels = 1;
static constexpr int kDetectBboxChannels = 4;
static constexpr int kDetectKeypointChannels = 10;
static constexpr int kDetectTopkCandidates = 32;
static constexpr float kDetectThreshold = 0.5f;
static constexpr float kNmsThreshold = 0.4f;

FaceDetectionRuntime::FaceDetectionRuntime()
    : specs_{{8, 3200, nullptr, nullptr, nullptr},
             {16, 800, nullptr, nullptr, nullptr},
             {32, 200, nullptr, nullptr, nullptr}},
      candidates_(nullptr),
      model_(),
      model_file_path_(nullptr),
      output_specs_ready_(false)
{
}

FaceDetectionRuntime::~FaceDetectionRuntime()
{
    deinit();
}

void FaceDetectionRuntime::setModelFilePath(const char *path)
{
    model_file_path_ = path;
    model_.setModelFilePath(path);
}

bool FaceDetectionRuntime::init()
{
    if (model_file_path_ == nullptr || model_file_path_[0] == '\0') {
        model_file_path_ = FACE_RECOG_DETECT_MODEL_SD_PATH;
    }

    model_.setName("FaceDetectionRuntime");
    model_.setModelType(AVDK_NN_MODEL_TYPE_NPU);
    model_.setFastRam(AVDK_NN_MEM_TYPE_HSRAM, kDetectScratchSize);
    model_.setArenaRam(AVDK_NN_MEM_TYPE_PSRAM_SLAB_UNCODED, kDetectArenaSize);
#if CONFIG_SDCARD
    model_.setModelLoadType(AVDK_NN_MODEL_LOAD_TYPE_SD_FILE);
    model_.setModelRamType(AVDK_NN_MEM_TYPE_PSRAM_SLAB_UNCODED);
    model_.setModelFilePath(model_file_path_);
#else
    model_.setModelLoadType(AVDK_NN_MODEL_LOAD_TYPE_FLASH);
    model_.setModelRamType(AVDK_NN_MEM_TYPE_PSRAM_SLAB_UNCODED);
    model_.setFlashModel((uint8_t *)face_detection_vela_tflite,
                         face_detection_vela_tflite_len);
#endif

    if (model_.init() != 0) {
        MicroPrintf("FaceDetectionRuntime model init failed\r\n");
        return false;
    }

    if (candidates_ == nullptr) {
        candidates_ = (FaceDetection *)bk_frame_buffer_malloc(
            MEM_SLAB_HEAP_UNCODED, sizeof(FaceDetection) * kDetectTopkCandidates);
        if (candidates_ == nullptr) {
            MicroPrintf("FaceDetectionRuntime alloc candidates failed\r\n");
            model_.deinit();
            return false;
        }
    }

    return true;
}

void FaceDetectionRuntime::deinit()
{
    if (candidates_ != nullptr) {
        bk_frame_buffer_free(candidates_);
        candidates_ = nullptr;
    }
    releaseInterpreter();
    model_.deinit();
    resetOutputSpecs();
}

void FaceDetectionRuntime::releaseInterpreter()
{
    model_.releaseInterpreter();
    resetOutputSpecs();
}

tflite::MicroInterpreter *FaceDetectionRuntime::prepareInterpreter()
{
    if (model_.prepareInterpreter() != 0) {
        return nullptr;
    }
    return model_.interpreter();
}

void FaceDetectionRuntime::resetOutputSpecs()
{
    for (int i = 0; i < kDetectNumStrides; i++) {
        specs_[i].score = nullptr;
        specs_[i].bbox = nullptr;
        specs_[i].keypoints = nullptr;
    }
    output_specs_ready_ = false;
}

bool FaceDetectionRuntime::buildOutputSpecs(tflite::MicroInterpreter *interpreter)
{
    if (interpreter == nullptr) {
        return false;
    }
    if (output_specs_ready_) {
        return true;
    }

    resetOutputSpecs();
    if (interpreter->outputs_size() != kDetectNumOutputs) {
        MicroPrintf("FaceDetect output count mismatch expect=%d got=%u\r\n",
                    kDetectNumOutputs, (unsigned)interpreter->outputs_size());
        return false;
    }

    for (size_t i = 0; i < interpreter->outputs_size(); i++) {
        TfLiteTensor *tensor = interpreter->output(i);
        if (tensor == nullptr || tensor->dims == nullptr || tensor->dims->size != 3) {
            MicroPrintf("FaceDetect output[%u] invalid dims\r\n", (unsigned)i);
            return false;
        }

        const int feature_count = tensor->dims->data[1];
        const int channels = tensor->dims->data[2];
        OutputSpec *spec = nullptr;
        for (int s = 0; s < kDetectNumStrides; s++) {
            if (specs_[s].feature_count == feature_count) {
                spec = &specs_[s];
                break;
            }
        }
        if (spec == nullptr) {
            MicroPrintf("FaceDetect output[%u] unexpected feature_count=%d\r\n",
                        (unsigned)i, feature_count);
            return false;
        }

        if (channels == kDetectScoreChannels) {
            spec->score = tensor;
        } else if (channels == kDetectBboxChannels) {
            spec->bbox = tensor;
        } else if (channels == kDetectKeypointChannels) {
            spec->keypoints = tensor;
        } else {
            MicroPrintf("FaceDetect output[%u] unexpected channels=%d\r\n",
                        (unsigned)i, channels);
            return false;
        }
    }

    for (int i = 0; i < kDetectNumStrides; i++) {
        if (specs_[i].score == nullptr || specs_[i].bbox == nullptr ||
            specs_[i].keypoints == nullptr) {
            MicroPrintf("FaceDetect missing tensors for stride=%d\r\n", specs_[i].stride);
            return false;
        }
    }

    output_specs_ready_ = true;
    return true;
}

int FaceDetectionRuntime::invokeAndDecode(tflite::MicroInterpreter *interpreter,
                                          FaceDetection *out,
                                          int max_out)
{
    if (interpreter == nullptr || out == nullptr || max_out <= 0 || !init()) {
        return 0;
    }
    if (!buildOutputSpecs(interpreter)) {
        return 0;
    }
    if (interpreter->Invoke() != kTfLiteOk) {
        MicroPrintf("FaceDetect Invoke failed\r\n");
        return 0;
    }

    int candidate_count = decode(candidates_, kDetectTopkCandidates);
    int kept = nms(candidates_, candidate_count, out, max_out);
    return kept;
}

float FaceDetectionRuntime::tensorValue(const TfLiteTensor *tensor, int flat_index) const
{
    if (tensor == nullptr) {
        return 0.0f;
    }
    if (tensor->type == kTfLiteInt8) {
        return ((float)tensor->data.int8[flat_index] -
                (float)tensor->params.zero_point) * tensor->params.scale;
    }
    if (tensor->type == kTfLiteFloat32) {
        return tensor->data.f[flat_index];
    }
    return 0.0f;
}

float FaceDetectionRuntime::keypointTensorValue(const TfLiteTensor *tensor,
                                                int logical_idx,
                                                int channel,
                                                int stride) const
{
    int flat_index = logical_idx * kDetectKeypointChannels + channel;
    const int height = kDetectInputH / stride;
    const int width = kDetectInputW / stride;
    const int pretranspose_channels = kDetectNumAnchors * kDetectKeypointChannels;

    if (height == width && width == pretranspose_channels) {
        const int out_flat = flat_index;
        const int h = out_flat / (width * pretranspose_channels);
        const int rem = out_flat % (width * pretranspose_channels);
        const int w = rem / pretranspose_channels;
        const int c = rem % pretranspose_channels;
        flat_index = (c * height + h) * width + w;
    }

    return tensorValue(tensor, flat_index);
}

int FaceDetectionRuntime::decode(FaceDetection *candidates, int max_candidates)
{
    int count = 0;
    int order = 0;

    for (int spec_i = 0; spec_i < kDetectNumStrides; spec_i++) {
        const OutputSpec *spec = &specs_[spec_i];
        const int stride = spec->stride;
        const int cols = kDetectInputW / stride;

        for (int idx = 0; idx < spec->feature_count; idx++) {
            const float score = tensorValue(spec->score, idx);
            if (score < kDetectThreshold) {
                continue;
            }

            int out_index = count;
            if (count < max_candidates) {
                count++;
            } else {
                int min_index = 0;
                for (int i = 1; i < max_candidates; i++) {
                    if (candidates[i].score < candidates[min_index].score) {
                        min_index = i;
                    }
                }
                if (score <= candidates[min_index].score) {
                    order++;
                    continue;
                }
                out_index = min_index;
            }

            const int grid_index = idx / kDetectNumAnchors;
            const int r = grid_index / cols;
            const int c = grid_index % cols;
            const float cx = (float)(c * stride);
            const float cy = (float)(r * stride);

            FaceDetection *det = &candidates[out_index];
            det->order = order++;
            det->score = score;
            det->x1 = cx - tensorValue(spec->bbox, idx * 4 + 0) * stride;
            det->y1 = cy - tensorValue(spec->bbox, idx * 4 + 1) * stride;
            det->x2 = cx + tensorValue(spec->bbox, idx * 4 + 2) * stride;
            det->y2 = cy + tensorValue(spec->bbox, idx * 4 + 3) * stride;

            for (int k = 0; k < kFaceDetectKeypointCount; k++) {
                det->keypoints[k].x =
                    cx + keypointTensorValue(spec->keypoints, idx, k * 2 + 0, stride) * stride;
                det->keypoints[k].y =
                    cy + keypointTensorValue(spec->keypoints, idx, k * 2 + 1, stride) * stride;
            }
        }
    }

    return count;
}

static float face_iou(const FaceDetection &a, const FaceDetection &b)
{
    const float xx1 = fmaxf(a.x1, b.x1);
    const float yy1 = fmaxf(a.y1, b.y1);
    const float xx2 = fminf(a.x2, b.x2);
    const float yy2 = fminf(a.y2, b.y2);
    const float w = fmaxf(0.0f, xx2 - xx1 + 1.0f);
    const float h = fmaxf(0.0f, yy2 - yy1 + 1.0f);
    const float inter = w * h;
    const float area_a = fmaxf(0.0f, a.x2 - a.x1 + 1.0f) *
                         fmaxf(0.0f, a.y2 - a.y1 + 1.0f);
    const float area_b = fmaxf(0.0f, b.x2 - b.x1 + 1.0f) *
                         fmaxf(0.0f, b.y2 - b.y1 + 1.0f);
    const float denom = area_a + area_b - inter;
    return denom > 1e-6f ? inter / denom : 0.0f;
}

int FaceDetectionRuntime::nms(FaceDetection *candidates,
                              int count,
                              FaceDetection *out,
                              int max_out)
{
    if (count <= 0) {
        return 0;
    }

    std::sort(candidates, candidates + count, [](const FaceDetection &a, const FaceDetection &b) {
        if (a.score > b.score) {
            return true;
        }
        if (a.score < b.score) {
            return false;
        }
        return a.order > b.order;
    });

    int kept = 0;
    for (int i = 0; i < count && kept < max_out; i++) {
        bool keep = true;
        for (int j = 0; j < kept; j++) {
            if (face_iou(candidates[i], out[j]) > kNmsThreshold) {
                keep = false;
                break;
            }
        }
        if (keep) {
            out[kept++] = candidates[i];
        }
    }

    return kept;
}
