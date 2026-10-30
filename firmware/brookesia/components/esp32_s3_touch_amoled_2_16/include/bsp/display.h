#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lv_adapter.h"

typedef struct {
    int max_transfer_sz;
} bsp_display_config_t;

typedef enum {
    BSP_DISPLAY_ROTATE_0 = 0,
    BSP_DISPLAY_ROTATE_90,
    BSP_DISPLAY_ROTATE_180,
    BSP_DISPLAY_ROTATE_270,
} bsp_display_rotation_t;

typedef struct {
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
} bsp_touch_flags_t;

typedef struct {
    esp_lv_adapter_config_t lv_adapter_cfg;
    esp_lv_adapter_rotation_t rotation;
    esp_lv_adapter_tear_avoid_mode_t tear_avoid_mode;
    bsp_touch_flags_t touch_flags;
} bsp_display_cfg_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io);
esp_err_t bsp_display_brightness_init(void);
esp_err_t bsp_display_brightness_set(int brightness_percent);
int bsp_display_brightness_get(void);
esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);
esp_err_t bsp_display_rotation_set(bsp_display_rotation_t rotation);

#ifdef __cplusplus
}
#endif
