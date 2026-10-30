#include <stdbool.h>

#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_spiffs.h"
#include "dev_display_lcd.h"
#include "dev_fs_fat.h"
#include "dev_lcd_touch.h"

#include "bsp/esp-bsp.h"

static const char *TAG = "s3_bmgr_bsp";
static bool s_manager_ready;
static dev_display_lcd_handles_t *s_lcd;
static dev_lcd_touch_handles_t *s_touch;
static uint8_t s_brightness;
sdmmc_card_t *bsp_sdcard;

static void (*s_board_coordinate_processor)(esp_lcd_touch_handle_t tp, uint16_t *x,
                                            uint16_t *y, uint16_t *strength,
                                            uint8_t *point_num, uint8_t max_point_num);

static void clip_touch_coordinates(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                                   uint16_t *strength, uint8_t *point_num,
                                   uint8_t max_point_num)
{
    if (s_board_coordinate_processor != NULL) {
        s_board_coordinate_processor(tp, x, y, strength, point_num, max_point_num);
    }
    if (x == NULL || y == NULL || point_num == NULL) {
        return;
    }

    const uint8_t count = *point_num < max_point_num ? *point_num : max_point_num;
    for (uint8_t index = 0; index < count; ++index) {
        if (x[index] >= BSP_LCD_H_RES) {
            x[index] = BSP_LCD_H_RES - 1;
        }
        if (y[index] >= BSP_LCD_V_RES) {
            y[index] = BSP_LCD_V_RES - 1;
        }
    }
}

static esp_err_t ensure_manager(void)
{
    if (s_manager_ready) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_board_manager_init(), TAG, "Board Manager init failed");
    s_manager_ready = true;
    return ESP_OK;
}

esp_err_t bsp_i2c_init(void)
{
    return ensure_manager();
}

esp_err_t bsp_i2c_deinit(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

i2c_master_bus_handle_t bsp_i2c_get_handle(void)
{
    i2c_master_bus_handle_t bus = NULL;
    if (ensure_manager() != ESP_OK ||
        esp_board_manager_get_periph_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER,
                                            (void **)&bus) != ESP_OK) {
        return NULL;
    }
    return bus;
}

esp_err_t bsp_display_new(const bsp_display_config_t *config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    (void)config;
    ESP_RETURN_ON_FALSE(ret_panel && ret_io, ESP_ERR_INVALID_ARG, TAG, "Invalid display outputs");
    ESP_RETURN_ON_ERROR(ensure_manager(), TAG, "Initialize board failed");
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(
                            ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&s_lcd),
                        TAG, "Display handle unavailable");
    *ret_panel = s_lcd->panel_handle;
    *ret_io = s_lcd->io_handle;
    return ESP_OK;
}

esp_err_t bsp_touch_new(const bsp_display_cfg_t *config,
                        esp_lcd_touch_handle_t *ret_touch)
{
    (void)config;
    ESP_RETURN_ON_FALSE(ret_touch, ESP_ERR_INVALID_ARG, TAG, "Invalid touch output");
    ESP_RETURN_ON_ERROR(ensure_manager(), TAG, "Initialize board failed");
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(
                            ESP_BOARD_DEVICE_NAME_LCD_TOUCH, (void **)&s_touch),
                        TAG, "Touch handle unavailable");
    /* The CST9217 reports coordinates inclusively up to its configured 480.
     * ESP LCD mirrors with `max - value`, so the nominal top edge otherwise
     * becomes 480 after mirror_y. Keep the existing swap/mirror orientation,
     * but use the last valid LVGL pixel as the mirror boundary. */
    s_touch->touch_handle->config.x_max = BSP_LCD_H_RES - 1;
    s_touch->touch_handle->config.y_max = BSP_LCD_V_RES - 1;
    if (s_touch->touch_handle->config.process_coordinates != clip_touch_coordinates) {
        s_board_coordinate_processor = s_touch->touch_handle->config.process_coordinates;
        s_touch->touch_handle->config.process_coordinates = clip_touch_coordinates;
    }
    *ret_touch = s_touch->touch_handle;
    return ESP_OK;
}

esp_err_t bsp_display_brightness_init(void)
{
    return bsp_display_brightness_set(100);
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    ESP_RETURN_ON_FALSE(s_lcd && brightness_percent >= 0 && brightness_percent <= 100,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid brightness state/value");
    const uint8_t value = (uint8_t)(brightness_percent * 255 / 100);
    /* CO5300 QSPI encodes an 8-bit command in the low byte of the 32-bit IO command. */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_lcd->io_handle, 0x02005100, &value, 1),
                        TAG, "Set panel brightness failed");
    s_brightness = value;
    return ESP_OK;
}

int bsp_display_brightness_get(void)
{
    return s_brightness * 100 / 255;
}

esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(100);
}

esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_rotation_set(bsp_display_rotation_t rotation)
{
    return rotation == BSP_DISPLAY_ROTATE_0 ? ESP_OK : ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_spiffs_mount(void)
{
    const esp_vfs_spiffs_conf_t config = {
        .base_path = BSP_SPIFFS_MOUNT_POINT,
        .partition_label = "storage",
        .max_files = 8,
        .format_if_mount_failed = false,
    };
    return esp_vfs_spiffs_register(&config);
}

esp_err_t bsp_spiffs_unmount(void)
{
    return esp_vfs_spiffs_unregister("storage");
}

esp_err_t bsp_sdcard_mount(void)
{
    if (bsp_sdcard != NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ensure_manager(), TAG, "Initialize board failed");
    ESP_RETURN_ON_ERROR(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD),
                        TAG, "Mount SD card failed");
    dev_fs_fat_handle_t *sd = NULL;
    ESP_RETURN_ON_ERROR(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_FS_SDCARD,
                                                             (void **)&sd),
                        TAG, "SD card handle unavailable");
    bsp_sdcard = sd->card;
    return bsp_sdcard ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (bsp_sdcard == NULL) {
        return ESP_OK;
    }
    const esp_err_t result = esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (result == ESP_OK) {
        bsp_sdcard = NULL;
    }
    return result;
}
