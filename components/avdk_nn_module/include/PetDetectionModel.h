// Pet (cat/dog) detection model: 320x320 int8 YOLOv8n vela flatbuffer.
#pragma once

#include "AvdkDetectionModel.h"

class PetDetectionModel : public AvdkDetectionModel
{
public:
    PetDetectionModel();

    void setModelFilePath(const char *path);
    void setLetterboxParams(float scale, int pad_top, int pad_left, int orig_w, int orig_h);

    void resolverLoad(void) override;
    void resourceLoad(void) override;
    void resourceUnload(void) override;
    int run(uint8_t *data, uint32_t size, bk_pixel_format_t format) override;

private:
    bool checkTensors(void);
};
