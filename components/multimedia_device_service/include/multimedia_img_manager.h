// Copyright 2020-2021 Beken
// Ported from doorbell reference project for beken_robot multimedia_device_service component.
//
// Encoded-frame manager: a pre-allocated pool of H.264 frame buffers shared
// between the encoder (producer) and the transmitter (consumer, e.g. WiFi).
//
// Producer (app_codec.c) flow:
//   bk_encoded_data_request()           -- pop a free buffer
//   bk_encoded_data_complete_request()  -- push to ready queue (encode ok)
//   bk_encoded_data_free_request()      -- push back to free queue (encode fail)
//
// Consumer (transmitter) flow:
//   bk_encoded_complete_data_request(timeout_ms) -- pop a ready frame
//   bk_encoded_complete_data_free_request(frame) -- return buffer to free queue
//
// Lifecycle:
//   bk_encoded_data_manager_init()   -- allocate the pool (called by encoder)
//   bk_encoded_data_manager_deinit() -- recycle ready frames (input=1: encoder
//                                       side stop, input=0: consumer side stop)
//
// The frame returned by bk_encoded_data_request() / bk_encoded_complete_data_request()
// is a frame_buffer_t* (declared in <common/avdk_pixel_types.h>); the H.264
// payload lives in frame->frame with frame->length bytes; frame->h264_type is
// set by the encoder (0 = P, 1 = I).

#pragma once

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t bk_encoded_data_manager_init(void);

bk_err_t bk_encoded_data_manager_deinit(uint8_t input);

void *bk_encoded_data_request(void);

bk_err_t bk_encoded_data_complete_request(uint8_t *frame);

bk_err_t bk_encoded_data_free_request(uint8_t *frame);

void *bk_encoded_complete_data_request(uint32_t timeout_ms);

bk_err_t bk_encoded_complete_data_free_request(uint8_t *frame);

#ifdef __cplusplus
}
#endif
