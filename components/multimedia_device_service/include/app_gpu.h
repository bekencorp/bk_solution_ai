// Copyright 2020-2021 Beken
// Ported from doorbell reference project for beken_robot multimedia_device_service component.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <components/bk_gpu_types.h>
#include "app_gpu_types.h"
#include "avdk_error.h"

avdk_err_t app_gpu_turn_on(gpu_board_config_t *config);
avdk_err_t app_gpu_turn_off(bk_gpu_ctlr_handle_t ctlr);

int app_gpu_board_config_set(gpu_board_config_t *config);
gpu_board_config_t *app_gpu_board_config_get(void);
bk_gpu_ctlr_handle_t app_gpu_handle_get(void);

#ifdef __cplusplus
}
#endif
