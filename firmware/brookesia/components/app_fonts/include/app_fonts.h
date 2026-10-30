/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

const lv_font_t *app_font_text_20(void);
const lv_font_t *app_font_text_30(void);

/** Retain the shared font cache for one application's active LVGL view. */
void app_fonts_acquire(void);

/**
 * Release one application's font-cache reference. Font data is freed only
 * after the final owner releases it. Call from the LVGL task after deleting
 * every object that uses either returned font.
 */
void app_fonts_release(void);

#ifdef __cplusplus
}
#endif
