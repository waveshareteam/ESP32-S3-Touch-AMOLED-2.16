/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include "bsp/esp-bsp.h"
#include "cbin_font.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "app_fonts.h"

static const char *TAG = "app_fonts";
static const char FONT_20_PATH[] = BSP_SPIFFS_MOUNT_POINT "/fonts/font20.bin";
static const char FONT_30_PATH[] = BSP_SPIFFS_MOUNT_POINT "/xiaozhi/font.bin";
static const size_t MAX_FONT_SIZE = 3 * 1024 * 1024;

typedef struct {
    lv_font_t *font;
    uint8_t *data;
} loaded_font_t;

static loaded_font_t s_font_20;
static loaded_font_t s_font_30;
static unsigned int s_references;

static const lv_font_t *load(loaded_font_t *cache, const char *path, const lv_font_t *fallback)
{
    if (cache->font != NULL) {
        return cache->font;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        ESP_LOGW(TAG, "Open font failed: %s", path);
        return fallback;
    }
    const long length = ftell(file);
    if (length <= 0 || (size_t)length > MAX_FONT_SIZE || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        ESP_LOGW(TAG, "Invalid font size: %ld", length);
        return fallback;
    }

    uint8_t *data = heap_caps_malloc((size_t)length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data == NULL) {
        fclose(file);
        ESP_LOGW(TAG, "No PSRAM for font: %s", path);
        return fallback;
    }
    const size_t read = fread(data, 1, (size_t)length, file);
    fclose(file);
    if (read != (size_t)length) {
        heap_caps_free(data);
        ESP_LOGW(TAG, "Read font failed: %s", path);
        return fallback;
    }

    lv_font_t *font = cbin_font_create(data);
    if (font == NULL) {
        heap_caps_free(data);
        ESP_LOGW(TAG, "Decode font failed: %s", path);
        return fallback;
    }
    font->fallback = fallback;
    cache->data = data;
    cache->font = font;
    ESP_LOGI(TAG, "Loaded font from SPIFFS: %s", path);
    return font;
}

static void release(loaded_font_t *cache)
{
    if (cache->font != NULL) {
        cbin_font_delete(cache->font);
        cache->font = NULL;
    }
    if (cache->data != NULL) {
        heap_caps_free(cache->data);
        cache->data = NULL;
    }
}

const lv_font_t *app_font_text_20(void)
{
    return load(&s_font_20, FONT_20_PATH, &lv_font_montserrat_20);
}

const lv_font_t *app_font_text_30(void)
{
    return load(&s_font_30, FONT_30_PATH, &lv_font_montserrat_30);
}

void app_fonts_acquire(void)
{
    ++s_references;
}

void app_fonts_release(void)
{
    if (s_references == 0) {
        ESP_LOGW(TAG, "Ignoring unbalanced font release");
        return;
    }
    --s_references;
    if (s_references != 0) {
        return;
    }
    release(&s_font_20);
    release(&s_font_30);
}
