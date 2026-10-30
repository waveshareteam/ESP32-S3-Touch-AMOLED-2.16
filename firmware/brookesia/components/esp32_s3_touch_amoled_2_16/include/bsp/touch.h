#pragma once

#include "bsp/display.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_touch_new(const bsp_display_cfg_t *config,
                        esp_lcd_touch_handle_t *ret_touch);

#ifdef __cplusplus
}
#endif
