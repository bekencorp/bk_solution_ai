/*
 * Copyright (c) 2025 BekenCorp. All rights reserved.
 *
 * For permission requests, write to BekenCorp at armino_support@bekencorp.com.
 *
 * Author: Beken LVGL Designer Tool
 */
/**
 * @file custom_func.h
 * @brief Custom Function Declarations.
 *
 * Designer-managed extension point for custom callbacks bound to LVGL
 * events.
 *
 * Project policy: keep this file MINIMAL. All UI demo logic (music,
 * ASR, vision, AI chat, sound localization, palm tracking, volume,
 * provisioning, ...) lives in `src/demo/<demo>.c` and is decoupled
 * from the UI through `beken_generated/page_hooks.h`. This file no
 * longer hosts any state machine or backend service call, to keep
 * future Designer regenerations conflict-free.
 */

#ifndef __CUSTOM_FUNC_H__
#define __CUSTOM_FUNC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "beken_ui.h"
#include "event_runtime.h"

/* No custom functions for now; the Designer can append new UI event
 * handlers here without pulling in any demo backend dependency. */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /* __CUSTOM_FUNC_H__ */
