// Copyright 2020-2021 Beken
// Ported from doorbell reference project for beken_robot multimedia_device_service component.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "avdk_error.h"
#include "app_display_types.h"
#include <components/bk_lcd_types.h>
#include <components/bk_display_types.h>

#define DISP_DEBUG_TIMER_ENABLE   (0)
#define DISP_DEBUG_TIMER_INTERVAL (5)

#define DISP_FRAME_NUM            (3)

/** Bring up DSI bus + panel + DPU per @p config (also votes display 1.8V LDO). */
int app_mipi_lcd_turn_on(display_board_config_t *config);

/** Tear down what app_mipi_lcd_turn_on() created and release the LDO vote. */
int app_mipi_lcd_turn_off(void);

/** @return true once turn_on() has fully succeeded; false after turn_off(). */
bool app_mipi_lcd_state_get(void);

/** Flush a frame buffer to the panel DPU (typically called from app_gpu.c). */
int app_mipi_lcd_flush(void *frame, avdk_err_t (*free_t)(void *args));

/** @return the DPU controller handle owned by app_mipi_lcd_turn_on(); NULL if off. */
void *app_mipi_lcd_handle_get(void);

int app_display_board_config_set(display_board_config_t *config);
display_board_config_t *app_display_board_config_get(void);

#ifdef __cplusplus
}
#endif
