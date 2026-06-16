#pragma once

#include "AvdkDetectionModel.h"

typedef void (*handGestureActionCallbackT)(uint8_t preset_id);
typedef void (*handGestureResultCallbackT)(int class_id, const char *class_name,
                                           float score, int count);

class HandGestureDetectionModel : public AvdkDetectionModel
{
public:
    HandGestureDetectionModel();

    void setModelFilePath(const char *path);
    void setGestureActionCallback(handGestureActionCallbackT cb);
    void setGestureResultCallback(handGestureResultCallbackT cb);

    void resolverLoad(void) override;
    void resourceLoad(void) override;
    void resourceUnload(void) override;
    int run(uint8_t *data, uint32_t size, bk_pixel_format_t format) override;

private:
    handGestureActionCallbackT gesture_action_callback_;
    handGestureResultCallbackT gesture_result_callback_;
    bool checkTensors(void);
};
