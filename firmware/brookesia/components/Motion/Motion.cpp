/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Motion"
#include "esp_lib_utils.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "motion_gfx.hpp"
#include "Motion.hpp"

#define APP_NAME "Motion"

LV_IMG_DECLARE(img_app_motion);

namespace esp_brookesia::apps {

using namespace motion_gfx;

static constexpr float PI = 3.14159265f;
static constexpr float RAD_TO_DEG = 180.0f / PI;
static constexpr float CX = Motion::SCREEN / 2.0f;
static constexpr float CY = Motion::SCREEN / 2.0f;
// QSPI transfers are more expensive on the S3 than the P4's display path.
// 25 fps keeps the reference canvas rendering responsive without starving LVGL.
static constexpr uint32_t FRAME_MS = 40;
static constexpr const char *PAGE_NAMES[] = {"Level", "Horizon", "Marble", "Sensors"};
static constexpr const char *NVS_NAMESPACE = "motion";
// v3 applies the corrected screen axes to every instrument. Convert an old
// calibration when loading it, and preserve the v2 blob for older firmware.
static constexpr const char *NVS_KEY_ZERO = "zero_v3";
static constexpr const char *NVS_KEY_ZERO_V2 = "zero_v2";

/* Level: the bubble's distance grows quickly near level and slowly further out, so a tenth of a
 * degree still shows and 20 degrees still fit */
static constexpr float LEVEL_RANGE = 200.0f;
static constexpr float LEVEL_SOFTNESS = 8.0f;
static constexpr float LEVEL_ENTER_DEG = 0.25f;
static constexpr float LEVEL_LEAVE_DEG = 0.5f;
static constexpr float BUBBLE_RADIUS = 27.0f;
static constexpr float TARGET_RADIUS = BUBBLE_RADIUS + 5.0f;
static constexpr float BUBBLE_SPRING = 90.0f;      // Liquid drag on the bubble: a little sway, no wobble
static constexpr float BUBBLE_DAMPING = 14.0f;
static constexpr float LEVEL_RINGS[] = {2, 5, 10, 20};

/* Horizon */
static constexpr float PX_PER_DEG = 4.0f;
static constexpr float PITCH_SHOWN = 72.0f;        // Beyond this the ladder stops, so the colours stay in their table
static constexpr float LADDER_VISIBLE = 172.0f;
static constexpr float INSTRUMENT_RADIUS = 214.0f;

/* Marble */
static constexpr float BALL_RADIUS = 26.0f;
static constexpr float ARENA_RADIUS = 214.0f;
static constexpr float MARBLE_GRAVITY = 2200.0f;   // px/s² for 1 g of tilt
static constexpr float MARBLE_FRICTION = 0.7f;     // Share of the speed lost per second rolling
static constexpr float MARBLE_BOUNCE = 0.55f;
static constexpr float MARBLE_STEP = 0.004f;

/* Sensors: the traces run across two panels */
static constexpr int PANEL_LEFT = 66;
static constexpr int PANEL_RIGHT = 400;
static constexpr int PANEL_TOP[2] = {118, 262};
static constexpr int PANEL_HEIGHT = 94;
static constexpr float PANEL_RANGE[2] = {2.0f, 300.0f};     // g and °/s at the panel edge
static constexpr uint16_t AXIS_COLORS[3] = {hex(0xFF5D6C), hex(0x4BE38A), hex(0x4DA3FF)};

static constexpr uint32_t COLOR_HINT = 0x8E929E;

void extend_area(lv_area_t *area, float cx, float cy, float radius)
{
    const int x1 = static_cast<int>(std::floor(cx - radius));
    const int y1 = static_cast<int>(std::floor(cy - radius));
    const int x2 = static_cast<int>(std::ceil(cx + radius));
    const int y2 = static_cast<int>(std::ceil(cy + radius));
    area->x1 = std::min<lv_coord_t>(area->x1, static_cast<lv_coord_t>(x1));
    area->y1 = std::min<lv_coord_t>(area->y1, static_cast<lv_coord_t>(y1));
    area->x2 = std::max<lv_coord_t>(area->x2, static_cast<lv_coord_t>(x2));
    area->y2 = std::max<lv_coord_t>(area->y2, static_cast<lv_coord_t>(y2));
}

lv_area_t empty_area()
{
    return {.x1 = Motion::SCREEN, .y1 = Motion::SCREEN, .x2 = -1, .y2 = -1};
}

Motion *Motion::_instance = nullptr;

Motion *Motion::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new Motion(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

Motion::Motion(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_motion, true, use_status_bar, use_navigation_bar)
{
}

Motion::~Motion()
{
    (void)releaseImu();
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

bool Motion::init(void)
{
    if (_imu_ready) {
        return true;
    }
    const i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    ESP_UTILS_CHECK_NULL_RETURN(bus, false, "Get board I2C bus failed");
    esp_err_t result = qmi8658_init(&_imu, bus, QMI8658_ADDRESS_HIGH);
    if (result == ESP_OK) {
        result = qmi8658_set_accel_range(&_imu, QMI8658_ACCEL_RANGE_8G);
    }
    if (result == ESP_OK) {
        result = qmi8658_set_accel_odr(&_imu, QMI8658_ACCEL_ODR_500HZ);
    }
    if (result == ESP_OK) {
        qmi8658_set_accel_unit_mps2(&_imu, true);
        qmi8658_set_gyro_unit_dps(&_imu, true);
        result = qmi8658_write_register(&_imu, QMI8658_CTRL5, 0x03);
    }
    if (result != ESP_OK) {
        ESP_UTILS_LOGE("Initialize QMI8658 failed: %s", esp_err_to_name(result));
        (void)releaseImu();
        return false;
    }
    _imu_ready = true;
    return true;
}

bool Motion::deinit(void)
{
    return releaseImu();
}

bool Motion::run(void)
{
    ESP_UTILS_LOGD("Run");
    ESP_UTILS_CHECK_FALSE_RETURN(_imu_ready, false, "QMI8658 is not initialized");
    loadZero();
    prepareSprites();
    ESP_UTILS_CHECK_FALSE_RETURN(buildUi(), false, "Build UI failed");
    startSensor();
    setPage(PAGE_LEVEL, true);
    _timer = lv_timer_create(timer_cb, FRAME_MS, this);
    return true;
}

bool Motion::back(void)
{
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool Motion::close(void)
{
    ESP_UTILS_LOGD("Close");
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    _sensor_ok = false;
    // The objects go with the app's screen; the backgrounds stay for the next visit
    heap_caps_free(_pixels);
    _pixels = nullptr;
    _canvas = _title = _toast = _level_angle = _level_axes = _level_state = nullptr;
    _pitch = _roll = _footer = _no_sensor = nullptr;
    memset(_dots, 0, sizeof(_dots));
    memset(_page_labels, 0, sizeof(_page_labels));
    memset(_ladder, 0, sizeof(_ladder));
    memset(_values, 0, sizeof(_values));
    return true;
}

bool Motion::pause(void)
{
    ESP_UTILS_LOGD("Pause");
    if (_timer) {
        lv_timer_pause(_timer);
    }
    _sensor_ok = false;
    return true;
}

bool Motion::resume(void)
{
    ESP_UTILS_LOGD("Resume");
    startSensor();
    _force_full_canvas = true;
    primeCanvasForPage();
    lv_obj_invalidate(_canvas);
    if (_timer) {
        lv_timer_resume(_timer);
    }
    return true;
}

void Motion::startSensor()
{
    _sensor_ok = _imu_ready;
    if (!_sensor_ok) {
        ESP_UTILS_LOGE("Motion sensor unavailable");
    }
    _last_us = 0;
    _last_count = 0;
    _bubble[0] = _bubble[1] = _bubble_v[0] = _bubble_v[1] = 0;
    _ball_v[0] = _ball_v[1] = 0;
    _history_count = 0;
}

esp_err_t Motion::readSensor(motion_sample_t *sample)
{
    if (sample == nullptr || !_imu_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    qmi8658_data_t data = {};
    ESP_RETURN_ON_ERROR(qmi8658_read_sensor_data(&_imu, &data), ESP_UTILS_LOG_TAG,
                        "Read QMI8658 failed");
    constexpr float gravity = 9.80665F;
    // Apply the S3 screen orientation once here so all four pages, including
    // the sensor traces, use the same corrected axes.
    sample->accel[0] = data.accelX / gravity;
    sample->accel[1] = data.accelY / gravity;
    sample->accel[2] = data.accelZ / gravity;
    sample->gyro[0] = data.gyroX;
    sample->gyro[1] = data.gyroY;
    sample->gyro[2] = data.gyroZ;
    sample->up[0] = sample->accel[0];
    sample->up[1] = sample->accel[1];
    sample->up[2] = sample->accel[2];
    const float length = std::sqrt(sample->up[0] * sample->up[0] +
                                   sample->up[1] * sample->up[1] +
                                   sample->up[2] * sample->up[2]);
    if (length > 0.01F) {
        sample->up[0] /= length;
        sample->up[1] /= length;
        sample->up[2] /= length;
    }
    sample->temperature = data.temperature;
    sample->count = ++_sample_count;
    return ESP_OK;
}

bool Motion::releaseImu()
{
    if (_imu.dev_handle == nullptr) {
        _imu_ready = false;
        return true;
    }
    const esp_err_t result = i2c_master_bus_rm_device(_imu.dev_handle);
    if (result != ESP_OK) {
        ESP_UTILS_LOGE("Remove QMI8658 I2C device failed: %s", esp_err_to_name(result));
        return false;
    }
    _imu = {};
    _imu_ready = false;
    return true;
}

/* ---------------------------------------------------------------------------
 * Level reference
 * ------------------------------------------------------------------------- */

void Motion::loadZero()
{
    float zero[3] = {0, 0, 1};
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = sizeof(zero);
        esp_err_t result = nvs_get_blob(nvs, NVS_KEY_ZERO, zero, &len);
        if (result == ESP_ERR_NVS_NOT_FOUND) {
            len = sizeof(zero);
            result = nvs_get_blob(nvs, NVS_KEY_ZERO_V2, zero, &len);
            if (result == ESP_OK && len == sizeof(zero)) {
                zero[0] = -zero[0];
                zero[1] = -zero[1];
            }
        }
        if (result != ESP_OK || len != sizeof(zero)) {
            zero[0] = zero[1] = 0;
            zero[2] = 1;
        }
        nvs_close(nvs);
    }
    setZero(zero);
}

/* Takes up as level from now on: the rotation that turns it onto the screen's normal */
void Motion::setZero(const float up[3])
{
    float z[3] = {up[0], up[1], up[2]};
    const float len = std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2]);
    if (len < 0.5f) {
        return;
    }
    for (float &v : z) {
        v /= len;
    }
    memcpy(_zero, z, sizeof(z));
    // Rodrigues: axis z x (0, 0, 1), cosine z[2]
    const float vx = z[1], vy = -z[0];
    const float s2 = vx * vx + vy * vy;
    const float c = z[2];
    float r[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    if (s2 > 1e-9f) {
        const float k = (1 - c) / s2;
        // R = I + [v]x + k [v]x², with v = (vx, vy, 0)
        r[0][0] = 1 - k * vy * vy;
        r[0][1] = k * vx * vy;
        r[0][2] = vy;
        r[1][0] = k * vx * vy;
        r[1][1] = 1 - k * vx * vx;
        r[1][2] = -vx;
        r[2][0] = -vy;
        r[2][1] = vx;
        r[2][2] = 1 - k * s2;
    }
    memcpy(_zero_rot, r, sizeof(r));
}

void Motion::relativeUp(const float up[3], float out[3]) const
{
    for (int i = 0; i < 3; i++) {
        out[i] = _zero_rot[i][0] * up[0] + _zero_rot[i][1] * up[1] + _zero_rot[i][2] * up[2];
    }
}

/* ---------------------------------------------------------------------------
 * UI
 * ------------------------------------------------------------------------- */

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(label, "");
    return label;
}

static lv_obj_t *make_layer(lv_obj_t *screen)
{
    lv_obj_t *layer = lv_obj_create(screen);
    lv_obj_remove_style_all(layer);
    lv_obj_set_size(layer, Motion::SCREEN, Motion::SCREEN);
    lv_obj_remove_flag(layer, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(layer, LV_OBJ_FLAG_HIDDEN);
    return layer;
}

/* A dark pill behind text that sits on the busy horizon */
static void make_pill(lv_obj_t *label)
{
    lv_obj_set_style_bg_color(label, lv_color_hex(0x0A0F14), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_radius(label, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(label, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(label, 4, LV_PART_MAIN);
}

bool Motion::buildUi()
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    _pixels = static_cast<uint16_t *>(heap_caps_calloc(SCREEN * SCREEN, sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    ESP_UTILS_CHECK_NULL_RETURN(_pixels, false, "No memory for the canvas");
    _canvas = lv_canvas_create(screen);
    lv_obj_set_size(_canvas, SCREEN, SCREEN);
    lv_obj_center(_canvas);
    lv_canvas_set_buffer(_canvas, _pixels, SCREEN, SCREEN, LV_COLOR_FORMAT_RGB565);
    // Swipes change the page and taps set the level, so the canvas keeps its gestures
    lv_obj_add_flag(_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(_canvas, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_add_event_cb(_canvas, canvas_event_cb, LV_EVENT_GESTURE, this);
    lv_obj_add_event_cb(_canvas, canvas_event_cb, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_add_event_cb(_canvas, canvas_event_cb, LV_EVENT_LONG_PRESSED, this);

    createLabels(screen);
    createPageDots(screen);
    return true;
}

void Motion::createLabels(lv_obj_t *screen)
{
    for (lv_obj_t *&layer : _page_labels) {
        layer = make_layer(screen);
    }

    // Level: the tilt in big figures, its two components, and a word once it is level
    lv_obj_t *level = _page_labels[PAGE_LEVEL];
    for (float deg : LEVEL_RINGS) {
        lv_obj_t *mark = make_label(level, &lv_font_montserrat_14, 0x5F8B96);
        lv_label_set_text_fmt(mark, "%d°", static_cast<int>(deg));
        const float r = LEVEL_RANGE * (1 - std::exp(-deg / LEVEL_SOFTNESS));
        lv_obj_set_pos(mark, static_cast<int>(CX + r + 4), static_cast<int>(CY - 19));
    }
    _level_state = make_label(level, &lv_font_montserrat_18, 0x4BE38A);
    lv_label_set_text(_level_state, "LEVEL");
    lv_obj_align(_level_state, LV_ALIGN_CENTER, 0, 110);
    _level_angle = make_label(level, &lv_font_montserrat_44, 0xFFFFFF);
    lv_obj_align(_level_angle, LV_ALIGN_CENTER, 0, 148);
    _level_axes = make_label(level, &lv_font_montserrat_16, COLOR_HINT);
    lv_obj_align(_level_axes, LV_ALIGN_CENTER, 0, 186);

    // Horizon: pitch and roll at the sides, numbers on the pitch ladder
    lv_obj_t *horizon = _page_labels[PAGE_HORIZON];
    _pitch = make_label(horizon, &lv_font_montserrat_16, 0xFFFFFF);
    make_pill(_pitch);
    lv_obj_set_style_text_align(_pitch, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(_pitch, LV_ALIGN_CENTER, -118, 96);
    _roll = make_label(horizon, &lv_font_montserrat_16, 0xFFFFFF);
    make_pill(_roll);
    lv_obj_set_style_text_align(_roll, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(_roll, LV_ALIGN_CENTER, 118, 96);
    for (int i = 0; i < LADDER_LABELS; i++) {
        const int deg = (i < LADDER_LABELS / 2 ? i - LADDER_LABELS / 2 : i - LADDER_LABELS / 2 + 1) * 10;
        _ladder[i] = make_label(horizon, &lv_font_montserrat_14, 0xFFFFFF);
        lv_label_set_text_fmt(_ladder[i], "%d", std::abs(deg));
        // Placed by its centre: shifted back by half its own size
        lv_obj_set_style_translate_x(_ladder[i], lv_pct(-50), LV_PART_MAIN);
        lv_obj_set_style_translate_y(_ladder[i], lv_pct(-50), LV_PART_MAIN);
    }

    // Marble: nothing but a hint, as the marble speaks for itself
    lv_obj_t *marble = _page_labels[PAGE_MARBLE];
    lv_obj_t *hint = make_label(marble, &lv_font_montserrat_16, 0x6E6A86);
    lv_label_set_text(hint, "Tilt to roll  •  tap to bring it back");
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 160);

    // Sensors: values over each panel, a footer with the total and the temperature
    lv_obj_t *sensors = _page_labels[PAGE_SENSORS];
    static constexpr const char *PANEL_NAMES[2] = {"ACCEL  g", "GYRO  °/s"};
    for (int p = 0; p < 2; p++) {
        lv_obj_t *name = make_label(sensors, &lv_font_montserrat_14, 0x6A7A8C);
        lv_label_set_text(name, PANEL_NAMES[p]);
        lv_obj_set_pos(name, PANEL_LEFT + 12, PANEL_TOP[p] + 6);
        for (int a = 0; a < 3; a++) {
            static constexpr uint32_t COLORS[3] = {0xFF5D6C, 0x4BE38A, 0x4DA3FF};
            _values[p][a] = make_label(sensors, &lv_font_montserrat_16, COLORS[a]);
            lv_obj_set_width(_values[p][a], 108);
            lv_obj_set_style_text_align(_values[p][a], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_pos(_values[p][a], static_cast<int>(CX) - 162 + a * 108, PANEL_TOP[p] - 26);
        }
    }
    _footer = make_label(sensors, &lv_font_montserrat_16, COLOR_HINT);
    lv_obj_align(_footer, LV_ALIGN_TOP_MID, 0, PANEL_TOP[1] + PANEL_HEIGHT + 12);

    _no_sensor = make_label(screen, &lv_font_montserrat_20, COLOR_HINT);
    lv_label_set_text(_no_sensor, "Motion sensor not found");
    lv_obj_center(_no_sensor);
    lv_obj_add_flag(_no_sensor, LV_OBJ_FLAG_HIDDEN);

    _toast = make_label(screen, &lv_font_montserrat_20, 0xFFFFFF);
    make_pill(_toast);
    lv_obj_align(_toast, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_style_opa(_toast, LV_OPA_TRANSP, LV_PART_MAIN);

    _title = make_label(screen, &lv_font_montserrat_24, 0xFFFFFF);
    make_pill(_title);
    lv_obj_align(_title, LV_ALIGN_TOP_MID, 0, 40);
}

// Small dots near the bottom of the round panel; the current page is a wider white pill
void Motion::createPageDots(lv_obj_t *screen)
{
    lv_obj_t *bar = lv_obj_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, LV_PART_MAIN);
    lv_obj_remove_flag(bar, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -28);
    for (lv_obj_t *&dot : _dots) {
        dot = lv_obj_create(bar);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_40, LV_PART_MAIN);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
    }
}

void Motion::setPage(int page, bool announce)
{
    page = ((page % PAGE_COUNT) + PAGE_COUNT) % PAGE_COUNT;
    _page = static_cast<Page>(page);
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_set_width(_dots[i], i == page ? 22 : 8);
        lv_obj_set_style_bg_opa(_dots[i], i == page ? LV_OPA_COVER : LV_OPA_40, LV_PART_MAIN);
        if (i == page) {
            lv_obj_remove_flag(_page_labels[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_page_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (announce) {
        lv_label_set_text(_title, PAGE_NAMES[page]);
        lv_obj_set_style_opa(_title, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_fade_out(_title, 400, 1200);
    }
    // Put a complete static page into the canvas *before* invalidating it.  If
    // the display task processes this before the next sensor frame, it still
    // sees the page background instead of stale pixels from the previous app.
    _force_full_canvas = true;
    primeCanvasForPage();
    lv_obj_invalidate(_canvas);
}

void Motion::showToast(const char *text)
{
    lv_label_set_text(_toast, text);
    lv_obj_set_style_opa(_toast, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_fade_out(_toast, 400, 1200);
}

void Motion::invalidateCanvasArea(const lv_area_t &area)
{
    if (_force_full_canvas || _canvas == nullptr || area.x1 > area.x2 || area.y1 > area.y2) {
        return;
    }
    lv_area_t canvas;
    lv_obj_get_coords(_canvas, &canvas);
    lv_area_t dirty = {
        .x1 = std::max<lv_coord_t>(canvas.x1, static_cast<lv_coord_t>(canvas.x1 + area.x1 - 3)),
        .y1 = std::max<lv_coord_t>(canvas.y1, static_cast<lv_coord_t>(canvas.y1 + area.y1 - 3)),
        .x2 = std::min<lv_coord_t>(canvas.x2, static_cast<lv_coord_t>(canvas.x1 + area.x2 + 3)),
        .y2 = std::min<lv_coord_t>(canvas.y2, static_cast<lv_coord_t>(canvas.y1 + area.y2 + 3)),
    };
    lv_obj_invalidate_area(_canvas, &dirty);
}

void Motion::primeCanvasForPage()
{
    if (_pixels == nullptr) {
        return;
    }
    switch (_page) {
    case PAGE_LEVEL:
    case PAGE_MARBLE:
    case PAGE_SENSORS: {
        uint16_t *bg = background(_page);
        if (bg != nullptr) {
            memcpy(_pixels, bg, SCREEN * SCREEN * sizeof(uint16_t));
        }
        break;
    }
    case PAGE_HORIZON: {
        // Horizon has no static bitmap.  Render the reference's level horizon
        // now so the forced first display update is a complete dial.
        static constexpr float flat[3] = {0, 0, 1};
        drawHorizon(flat);
        break;
    }
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Sprites and backgrounds
 * ------------------------------------------------------------------------- */

static float smoothstep(float a, float b, float x)
{
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

void Motion::prepareSprites()
{
    if (_ball_alpha[BALL_SPRITE / 2 * BALL_SPRITE + BALL_SPRITE / 2]) {
        return;
    }
    // Level bubble: a lens of air in green-tinted liquid, bright at its rim, with a glint
    static constexpr uint32_t BUBBLE_COLORS[2] = {0x3FD8F0, 0x4BE38A};
    for (int k = 0; k < 2; k++) {
        const uint16_t base = hex(BUBBLE_COLORS[k]);
        for (int y = 0; y < BUBBLE_SPRITE; y++) {
            for (int x = 0; x < BUBBLE_SPRITE; x++) {
                const float dx = x + 0.5f - BUBBLE_SPRITE / 2.0f;
                const float dy = y + 0.5f - BUBBLE_SPRITE / 2.0f;
                const float d = std::sqrt(dx * dx + dy * dy);
                const float cover = std::clamp(BUBBLE_RADIUS + 0.5f - d, 0.0f, 1.0f);
                const float rim = smoothstep(BUBBLE_RADIUS - 7, BUBBLE_RADIUS - 0.5f, d);
                const float gx = dx + 9, gy = dy + 10;
                const float glint = 1 - smoothstep(2, 9, std::sqrt(gx * gx + gy * gy));
                const uint16_t c = mix(mix(base, hex(0xFFFFFF), 0.25f * rim), hex(0xFFFFFF), glint);
                const float a = std::clamp(0.42f + 0.5f * rim + 0.5f * glint, 0.0f, 1.0f) * cover;
                _bubble_px[k][y * BUBBLE_SPRITE + x] = c;
                if (k == 0) {
                    _bubble_alpha[y * BUBBLE_SPRITE + x] = static_cast<uint8_t>(a * 255);
                }
            }
        }
    }
    // Marble: a glossy sphere lit from the top left
    const float lx = -0.45f, ly = -0.55f, lz = 0.70f;
    const float ln = std::sqrt(lx * lx + ly * ly + lz * lz);
    const float l[3] = {lx / ln, ly / ln, lz / ln};
    float h[3] = {l[0], l[1], l[2] + 1};
    const float hn = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
    for (float &v : h) {
        v /= hn;
    }
    for (int y = 0; y < BALL_SPRITE; y++) {
        for (int x = 0; x < BALL_SPRITE; x++) {
            const float dx = (x + 0.5f - BALL_SPRITE / 2.0f) / BALL_RADIUS;
            const float dy = (y + 0.5f - BALL_SPRITE / 2.0f) / BALL_RADIUS;
            const float d2 = dx * dx + dy * dy;
            const float cover = std::clamp(BALL_RADIUS + 0.5f - std::sqrt(d2) * BALL_RADIUS, 0.0f, 1.0f);
            const float nz = std::sqrt(std::max(0.0f, 1 - d2));
            const float diffuse = std::max(0.0f, dx * l[0] + dy * l[1] + nz * l[2]);
            const float spec = std::pow(std::max(0.0f, dx * h[0] + dy * h[1] + nz * h[2]), 40.0f);
            // Warm coral, with some light bounced back from the floor at the bottom edge
            const float bounce = 0.18f * smoothstep(0.3f, 1.0f, dy) * (1 - nz);
            const float shade = 0.18f + 0.9f * diffuse + bounce;
            const float r = std::min(255.0f, 255 * shade + 240 * spec);
            const float g = std::min(255.0f, 92 * shade + 240 * spec);
            const float b = std::min(255.0f, 112 * shade + 240 * spec);
            _ball_px[y * BALL_SPRITE + x] = rgb(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                                static_cast<uint8_t>(b));
            _ball_alpha[y * BALL_SPRITE + x] = static_cast<uint8_t>(cover * 255);
        }
    }
    // Soft contact shadow
    for (int y = 0; y < SHADOW_SPRITE; y++) {
        for (int x = 0; x < SHADOW_SPRITE; x++) {
            const float dx = x + 0.5f - SHADOW_SPRITE / 2.0f;
            const float dy = y + 0.5f - SHADOW_SPRITE / 2.0f;
            const float d = std::sqrt(dx * dx + dy * dy);
            const float a = 1 - smoothstep(BALL_RADIUS - 8, BALL_RADIUS + 12, d);
            _shadow_px[y * SHADOW_SPRITE + x] = 0;
            _shadow_alpha[y * SHADOW_SPRITE + x] = static_cast<uint8_t>(a * a * 150);
        }
    }
    // Horizon colours by distance from the horizon line, sky positive, with a white line on it
    static constexpr float SKY[2][3] = {{0x5D, 0xA9, 0xE9}, {0x0F, 0x35, 0x76}};
    static constexpr float GROUND[2][3] = {{0xA0, 0x68, 0x2F}, {0x3A, 0x22, 0x0E}};
    for (int phase = 0; phase < 4; phase++) {
        for (int i = 0; i < 2048; i++) {
            const float s = (i - 1024) / 2.0f;
            const float (*ends)[3] = s >= 0 ? SKY : GROUND;
            const float t = std::min(std::fabs(s) / 210.0f, 1.0f);
            const float line = std::clamp(1.6f - std::fabs(s), 0.0f, 1.0f);
            float ch[3];
            for (int k = 0; k < 3; k++) {
                ch[k] = ends[0][k] + (ends[1][k] - ends[0][k]) * t;
                ch[k] += (255 - ch[k]) * line;
            }
            // The phase picks the dither cell, as the frame later picks the table by pixel position
            _horizon_lut[phase][i] = dither(ch[0], ch[1], ch[2], phase & 1, phase >> 1);
        }
    }
}

static void radial_fill(uint16_t *pixels, uint32_t inner, uint32_t outer)
{
    for (int y = 0; y < Motion::SCREEN; y++) {
        for (int x = 0; x < Motion::SCREEN; x++) {
            const float dx = x + 0.5f - CX, dy = y + 0.5f - CY;
            const float t = std::min(1.0f, (dx * dx + dy * dy) / (CX * CX));
            float ch[3];
            for (int i = 0; i < 3; i++) {
                const float a = (inner >> (16 - 8 * i)) & 0xFF, b = (outer >> (16 - 8 * i)) & 0xFF;
                ch[i] = a + (b - a) * t;
            }
            pixels[y * Motion::SCREEN + x] = dither(ch[0], ch[1], ch[2], x, y);
        }
    }
}

uint16_t *Motion::background(Page page)
{
    if (!_backgrounds[page]) {
        uint16_t *pixels = static_cast<uint16_t *>(heap_caps_malloc(SCREEN * SCREEN * sizeof(uint16_t),
                                                                    MALLOC_CAP_SPIRAM));
        if (!pixels) {
            return nullptr;
        }
        switch (page) {
        case PAGE_LEVEL:
            drawLevelBackground(pixels);
            break;
        case PAGE_MARBLE:
            drawMarbleBackground(pixels);
            break;
        case PAGE_SENSORS:
            drawSensorsBackground(pixels);
            break;
        default:
            break;
        }
        _backgrounds[page] = pixels;
    }
    return _backgrounds[page];
}

void Motion::drawLevelBackground(uint16_t *pixels)
{
    radial_fill(pixels, 0x0F2C36, 0x03080A);
    Canvas cv(pixels, SCREEN, SCREEN);
    const uint16_t ring = hex(0x2F5E6B);
    // The vial's glass edge
    cv.ring(CX, CY, LEVEL_RANGE + BUBBLE_RADIUS + 3, 2.0f, hex(0x24505C));
    for (float deg : LEVEL_RINGS) {
        cv.ring(CX, CY, LEVEL_RANGE * (1 - std::exp(-deg / LEVEL_SOFTNESS)), 1.4f, ring);
    }
    // Crosshair, broken around the target
    for (int i = 0; i < 4; i++) {
        const float a = i * PI / 2;
        const float c = std::cos(a), s = std::sin(a);
        cv.line(CX + c * (TARGET_RADIUS + 10), CY + s * (TARGET_RADIUS + 10), CX + c * LEVEL_RANGE,
                CY + s * LEVEL_RANGE, 1.2f, ring, 170);
    }
    // Ticks round the outside every 15 degrees, longer every 45
    for (int i = 0; i < 24; i++) {
        const float a = i * PI / 12;
        const float len = (i % 3 == 0) ? 12.0f : 6.0f;
        const float r0 = LEVEL_RANGE + BUBBLE_RADIUS + 8;
        cv.line(CX + std::cos(a) * r0, CY + std::sin(a) * r0, CX + std::cos(a) * (r0 + len),
                CY + std::sin(a) * (r0 + len), 1.6f, ring);
    }
    cv.line(CX - 6, CY, CX + 6, CY, 1.4f, hex(0x6E8C96));
    cv.line(CX, CY - 6, CX, CY + 6, 1.4f, hex(0x6E8C96));
}

void Motion::drawMarbleBackground(uint16_t *pixels)
{
    radial_fill(pixels, 0x1A1626, 0x050408);
    Canvas cv(pixels, SCREEN, SCREEN);
    // A floor of faint dots, to see the marble roll over something
    for (int y = -7; y <= 7; y++) {
        for (int x = -7; x <= 7; x++) {
            const float px = CX + x * 29.0f, py = CY + y * 29.0f;
            const float d = std::hypot(px - CX, py - CY);
            if (d < ARENA_RADIUS - 14) {
                cv.disc(px, py, 1.4f, hex(0x5A5288), static_cast<uint32_t>(255 * (1 - 0.7f * d / ARENA_RADIUS)));
            }
        }
    }
    // The wall: a soft inner glow under a lit rim
    for (int i = 0; i < 6; i++) {
        cv.ring(CX, CY, ARENA_RADIUS - 2 - i * 2.0f, 2.0f, hex(0x6B4FC8), 26 - i * 4);
    }
    cv.ring(CX, CY, ARENA_RADIUS + 1, 3.0f, hex(0x7C64D8));
    cv.ring(CX, CY, ARENA_RADIUS + 5, 3.0f, hex(0x241C3E));
}

/* What never moves on the horizon page: the case, the bank scale on it and the aircraft */
void Motion::drawHorizonFixed(uint16_t *pixels)
{
    Canvas cv(pixels, SCREEN, SCREEN);
    static constexpr int BANK_MARKS[] = {-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60};
    for (int mark : BANK_MARKS) {
        const float a = mark * PI / 180;
        const float sx = std::sin(a), sy = -std::cos(a);
        const bool major = mark % 30 == 0;
        const float r0 = INSTRUMENT_RADIUS - (major ? 22.0f : 14.0f);
        cv.line(CX + sx * r0, CY + sy * r0, CX + sx * (INSTRUMENT_RADIUS - 4), CY + sy * (INSTRUMENT_RADIUS - 4),
                major ? 3.0f : 2.0f, hex(0xFFFFFF), 220);
    }
    // Outlined so it stands out on sky and ground alike
    const uint16_t wing = hex(0xFF8A1F);
    for (int pass = 0; pass < 2; pass++) {
        const uint16_t c = pass ? wing : hex(0x1A0E04);
        const float w = pass ? 6.0f : 9.0f;
        for (int side = -1; side <= 1; side += 2) {
            cv.line(CX + side * 100, CY, CX + side * 40, CY, w, c);
            cv.line(CX + side * 40, CY, CX + side * 40, CY + 14, w, c);
        }
        cv.disc(CX, CY, pass ? 6.0f : 8.0f, c);
    }
    // The case is black, as is everything outside the sky: only its smooth inner edge needs drawing
    cv.ring(CX, CY, INSTRUMENT_RADIUS + 2, 3.0f, 0);
    cv.ring(CX, CY, INSTRUMENT_RADIUS, 1.5f, hex(0xFFFFFF), 70);
}

/* Draws the fixed parts over black and over white; the difference gives each pixel's alpha, and the
 * picture over black its colour. Each frame then only blends the pixels they cover. */
bool Motion::prepareHorizonOverlay()
{
    if (_overlay_index) {
        return true;
    }
    const size_t len = SCREEN * SCREEN;
    uint16_t *black = static_cast<uint16_t *>(heap_caps_calloc(len, sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    uint16_t *white = static_cast<uint16_t *>(heap_caps_malloc(len * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    bool ok = black && white;
    if (ok) {
        std::fill(white, white + len, 0xFFFF);
        drawHorizonFixed(black);
        drawHorizonFixed(white);
        int count = 0;
        for (size_t i = 0; i < len; i++) {
            count += black[i] != 0 || white[i] != 0xFFFF;
        }
        _overlay_index = static_cast<uint32_t *>(heap_caps_malloc(count * sizeof(uint32_t), MALLOC_CAP_SPIRAM));
        _overlay_color = static_cast<uint16_t *>(heap_caps_malloc(count * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
        _overlay_alpha = static_cast<uint8_t *>(heap_caps_malloc(count, MALLOC_CAP_SPIRAM));
        ok = _overlay_index && _overlay_color && _overlay_alpha;
        int n = 0;
        for (size_t i = 0; ok && i < len; i++) {
            if (black[i] == 0 && white[i] == 0xFFFF) {
                continue;
            }
            // Green has the most bits: over white it shows (1 - alpha) of full scale more than over black
            const int gb = (black[i] >> 5) & 0x3F, gw = (white[i] >> 5) & 0x3F;
            const float a = std::clamp(1.0f - (gw - gb) / 63.0f, 1.0f / 255, 1.0f);
            const int r = std::min(31, static_cast<int>(((black[i] >> 11) & 0x1F) / a + 0.5f));
            const int g = std::min(63, static_cast<int>(gb / a + 0.5f));
            const int b = std::min(31, static_cast<int>((black[i] & 0x1F) / a + 0.5f));
            _overlay_index[n] = i;
            _overlay_color[n] = static_cast<uint16_t>(r << 11 | g << 5 | b);
            _overlay_alpha[n] = static_cast<uint8_t>(a * 255 + 0.5f);
            n++;
        }
        _overlay_count = ok ? n : 0;
    }
    heap_caps_free(black);
    heap_caps_free(white);
    return ok;
}

static void round_rect(Canvas &cv, int x0, int y0, int x1, int y1, int r, uint16_t c)
{
    for (int y = y0; y <= y1; y++) {
        int inset = 0;
        const int dy = y < y0 + r ? y0 + r - y : (y > y1 - r ? y - (y1 - r) : 0);
        if (dy) {
            inset = r - static_cast<int>(std::sqrt(static_cast<float>(r * r - dy * dy)));
        }
        cv.span(y, x0 + inset, x1 - inset, c, OPAQUE);
    }
}

void Motion::drawSensorsBackground(uint16_t *pixels)
{
    radial_fill(pixels, 0x0B1118, 0x030507);
    Canvas cv(pixels, SCREEN, SCREEN);
    for (int p = 0; p < 2; p++) {
        const int top = PANEL_TOP[p];
        const int bottom = top + PANEL_HEIGHT;
        round_rect(cv, PANEL_LEFT - 1, top - 1, PANEL_RIGHT + 1, bottom + 1, 13, hex(0x1C2733));
        round_rect(cv, PANEL_LEFT, top, PANEL_RIGHT, bottom, 12, hex(0x0D141C));
        const float mid = top + PANEL_HEIGHT / 2.0f;
        cv.line(PANEL_LEFT + 8, mid, PANEL_RIGHT - 8, mid, 1.0f, hex(0x2C3B4A));
        for (int k = -1; k <= 1; k += 2) {
            const float y = mid + k * PANEL_HEIGHT / 4.0f;
            for (int x = PANEL_LEFT + 10; x < PANEL_RIGHT - 10; x += 8) {
                cv.span(static_cast<int>(y), x, x + 3, hex(0x1B2633), OPAQUE);
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * Frames
 * ------------------------------------------------------------------------- */

void Motion::timer_cb(lv_timer_t *timer)
{
    static_cast<Motion *>(lv_timer_get_user_data(timer))->frame();
}

void Motion::frame()
{
    // The message only changes with the sensor's state, so it is not redrawn every frame
    if (_sensor_ok != lv_obj_has_flag(_no_sensor, LV_OBJ_FLAG_HIDDEN)) {
        if (_sensor_ok) {
            lv_obj_add_flag(_no_sensor, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(_no_sensor, LV_OBJ_FLAG_HIDDEN);
        }
    }
    motion_sample_t sample;
    if (!_sensor_ok || readSensor(&sample) != ESP_OK || sample.count == _last_count) {
        return;
    }
    _last_count = sample.count;
    const int64_t now = esp_timer_get_time();
    const float dt = _last_us ? std::clamp((now - _last_us) / 1e6f, 0.0f, 0.05f) : 0.0f;
    _last_us = now;

    float up[3];
    relativeUp(sample.up, up);
    switch (_page) {
    case PAGE_LEVEL:
        drawLevel(up, dt);
        break;
    case PAGE_HORIZON:
        drawHorizon(up);
        // The sky/ground gradient and bank ladder can change across the entire round dial.
        lv_obj_invalidate(_canvas);
        break;
    case PAGE_MARBLE:
        drawMarble(up, dt);
        break;
    case PAGE_SENSORS:
        drawSensors(sample);
        break;
    default:
        break;
    }
    if (_force_full_canvas) {
        // Dirty invalidation is deliberately suppressed for the first dynamic
        // frame after a page/app transition.  Commit that complete frame once.
        lv_obj_invalidate(_canvas);
        _force_full_canvas = false;
    }
}

void Motion::drawLevel(const float up[3], float dt)
{
    uint16_t *bg = background(PAGE_LEVEL);
    if (!bg) {
        return;
    }
    // The target changes colour at the level threshold, so retain its small
    // centre area along with the old and new 64 px bubble sprites.
    lv_area_t dirty = empty_area();
    extend_area(&dirty, CX + _bubble[0], CY + _bubble[1], BUBBLE_SPRITE / 2.0f);
    extend_area(&dirty, CX, CY, TARGET_RADIUS + 8.0f);
    memcpy(_pixels, bg, SCREEN * SCREEN * sizeof(uint16_t));
    Canvas cv(_pixels, SCREEN, SCREEN);

    const float tilt = std::acos(std::clamp(up[2], -1.0f, 1.0f)) * RAD_TO_DEG;
    const float plane = std::hypot(up[0], up[1]);
    // The bubble rises to the high side; screen Y increases downwards.
    float target[2] = {0, 0};
    if (plane > 1e-6f) {
        const float r = LEVEL_RANGE * (1 - std::exp(-tilt / LEVEL_SOFTNESS));
        target[0] = up[0] / plane * r;
        target[1] = -up[1] / plane * r;
    }
    for (float t = dt; t > 0; t -= MARBLE_STEP) {
        const float h = std::min(t, MARBLE_STEP);
        for (int i = 0; i < 2; i++) {
            _bubble_v[i] += ((target[i] - _bubble[i]) * BUBBLE_SPRING - _bubble_v[i] * BUBBLE_DAMPING) * h;
            _bubble[i] += _bubble_v[i] * h;
        }
    }
    if (dt == 0) {
        _bubble[0] = target[0];
        _bubble[1] = target[1];
    }

    _is_level = tilt < (_is_level ? LEVEL_LEAVE_DEG : LEVEL_ENTER_DEG);
    if (_is_level) {
        cv.ring(CX, CY, TARGET_RADIUS, 10.0f, hex(0x4BE38A), 45);
        cv.ring(CX, CY, TARGET_RADIUS, 2.5f, hex(0x4BE38A));
    } else {
        cv.ring(CX, CY, TARGET_RADIUS, 2.0f, hex(0x8FB3BD), 200);
    }
    cv.sprite(_bubble_px[_is_level ? 1 : 0], _bubble_alpha, BUBBLE_SPRITE, CX + _bubble[0], CY + _bubble[1]);

    lv_label_set_text_fmt(_level_angle, "%.1f°", tilt);
    const float tilt_x = std::atan2(up[0], up[2]) * RAD_TO_DEG;
    const float tilt_y = std::atan2(up[1], up[2]) * RAD_TO_DEG;
    lv_label_set_text_fmt(_level_axes, "X %+.1f°      Y %+.1f°", tilt_x, tilt_y);
    if (_is_level) {
        lv_obj_remove_flag(_level_state, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_level_state, LV_OBJ_FLAG_HIDDEN);
    }
    extend_area(&dirty, CX + _bubble[0], CY + _bubble[1], BUBBLE_SPRITE / 2.0f);
    invalidateCanvasArea(dirty);
}

void Motion::drawHorizon(const float up[3])
{
    // The board as an aircraft: the top edge is the nose, the right edge the right wing
    const float pitch = std::atan2(up[1], up[2]) * RAD_TO_DEG;
    const float roll = std::atan2(-up[0], std::hypot(up[1], up[2]));
    // Banking right turns the world the other way round: the horizon rises on the right
    const float nx = -std::sin(roll), ny = -std::cos(roll);     // Towards the sky
    const float dx = std::cos(roll), dy = -std::sin(roll);      // Along the horizon
    const float offset = std::clamp(pitch, -PITCH_SHOWN, PITCH_SHOWN) * PX_PER_DEG;
    if (!prepareHorizonOverlay()) {
        return;
    }

    // Sky and ground by each pixel's distance from the horizon, in half pixels with 8 fraction bits
    const int32_t step = static_cast<int32_t>(nx * 512);
    // Inside the case only; outside it the case covers everything. Distances there stay within the table.
    const float fill_r2 = (INSTRUMENT_RADIUS + 1) * (INSTRUMENT_RADIUS + 1);
    for (int y = 0; y < SCREEN; y++) {
        const float ry = y + 0.5f - CY;
        uint16_t *row = &_pixels[y * SCREEN];
        if (ry * ry >= fill_r2) {
            memset(row, 0, SCREEN * sizeof(uint16_t));
            continue;
        }
        const float half = std::sqrt(fill_r2 - ry * ry);
        const int x0 = std::max(0, static_cast<int>(CX - half));
        const int x1 = std::min(SCREEN - 1, static_cast<int>(CX + half));
        if (x0 > 0) {
            memset(row, 0, x0 * sizeof(uint16_t));
        }
        if (x1 < SCREEN - 1) {
            memset(row + x1 + 1, 0, (SCREEN - 1 - x1) * sizeof(uint16_t));
        }
        const uint16_t *even = _horizon_lut[(y & 1) << 1];
        const uint16_t *odd = _horizon_lut[((y & 1) << 1) | 1];
        int32_t s = static_cast<int32_t>((nx * (x0 + 0.5f - CX) + ny * ry + offset) * 512);
        int x = x0;
        if (x & 1) {
            row[x++] = odd[(s >> 8) + 1024];
            s += step;
        }
        // Two pixels per store; rows start on 4-byte boundaries, as the width is even
        uint32_t *pair = reinterpret_cast<uint32_t *>(row + x);
        for (; x < x1; x += 2) {
            const uint32_t lo = even[(s >> 8) + 1024];
            s += step;
            const uint32_t hi = odd[(s >> 8) + 1024];
            s += step;
            *pair++ = lo | hi << 16;
        }
        if (x == x1) {
            row[x] = even[(s >> 8) + 1024];
        }
    }

    Canvas cv(_pixels, SCREEN, SCREEN);
    const float hx = CX - nx * offset, hy = CY - ny * offset;   // The horizon's point nearest the centre
    int label = 0;
    for (int k = -40; k <= 40; k += 5) {
        if (k == 0) {
            continue;
        }
        const float px = hx + nx * k * PX_PER_DEG, py = hy + ny * k * PX_PER_DEG;
        const bool major = k % 10 == 0;
        const float half_len = major ? 44.0f : 20.0f;
        const bool visible = std::hypot(px - CX, py - CY) < LADDER_VISIBLE;
        if (visible) {
            cv.line(px - dx * half_len, py - dy * half_len, px + dx * half_len, py + dy * half_len, 2.2f,
                    hex(0xFFFFFF), 225);
        }
        if (major) {
            lv_obj_t *number = _ladder[label++];
            if (visible) {
                lv_obj_remove_flag(number, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_pos(number, static_cast<int>(px + dx * (half_len + 18)),
                               static_cast<int>(py + dy * (half_len + 18)));
            } else {
                lv_obj_add_flag(number, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // The pointer on the sky, under the fixed bank scale, case and aircraft
    const float tip = INSTRUMENT_RADIUS - 26;
    cv.triangle(CX + nx * tip, CY + ny * tip, CX + nx * (tip - 16) + dx * 9, CY + ny * (tip - 16) + dy * 9,
                CX + nx * (tip - 16) - dx * 9, CY + ny * (tip - 16) - dy * 9, hex(0xFFC531));
    for (int i = 0; i < _overlay_count; i++) {
        uint16_t *p = &_pixels[_overlay_index[i]];
        *p = blend(*p, _overlay_color[i], _overlay_alpha[i]);
    }

    lv_label_set_text_fmt(_pitch, "PITCH\n%+.0f°", pitch);
    lv_label_set_text_fmt(_roll, "ROLL\n%+.0f°", roll * RAD_TO_DEG);
}

void Motion::drawMarble(const float up[3], float dt)
{
    uint16_t *bg = background(PAGE_MARBLE);
    if (!bg) {
        return;
    }
    lv_area_t dirty = empty_area();
    const auto extend_marble = [this, &dirty]() {
        // The old image contains the prior ball, 80 px contact shadow and all
        // fading trail discs.  Include both old and new states so cleared trail
        // pixels never remain on the AMOLED.
        extend_area(&dirty, CX + _ball[0], CY + _ball[1], BALL_SPRITE / 2.0f);
        extend_area(&dirty, CX + _ball[0] + 7, CY + _ball[1] + 10, SHADOW_SPRITE / 2.0f);
        for (const auto &point : _trail) {
            extend_area(&dirty, CX + point[0], CY + point[1], 21.0f);
        }
    };
    extend_marble();
    memcpy(_pixels, bg, SCREEN * SCREEN * sizeof(uint16_t));
    Canvas cv(_pixels, SCREEN, SCREEN);

    // The marble rolls downhill: away from a raised edge
    const float ax = -up[0] * MARBLE_GRAVITY;
    const float ay = up[1] * MARBLE_GRAVITY;
    const float limit = ARENA_RADIUS - BALL_RADIUS;
    for (float t = dt; t > 0; t -= MARBLE_STEP) {
        const float h = std::min(t, MARBLE_STEP);
        _ball_v[0] += ax * h;
        _ball_v[1] += ay * h;
        const float keep = 1 - MARBLE_FRICTION * h;
        _ball_v[0] *= keep;
        _ball_v[1] *= keep;
        _ball[0] += _ball_v[0] * h;
        _ball[1] += _ball_v[1] * h;
        const float d = std::hypot(_ball[0], _ball[1]);
        if (d > limit) {
            const float n[2] = {_ball[0] / d, _ball[1] / d};
            const float vn = _ball_v[0] * n[0] + _ball_v[1] * n[1];
            if (vn > 0) {
                _ball_v[0] -= (1 + MARBLE_BOUNCE) * vn * n[0];
                _ball_v[1] -= (1 + MARBLE_BOUNCE) * vn * n[1];
            }
            _ball[0] = n[0] * limit;
            _ball[1] = n[1] * limit;
        }
    }
    // A fading trail, oldest first
    _trail[_trail_head][0] = _ball[0];
    _trail[_trail_head][1] = _ball[1];
    _trail_head = (_trail_head + 1) % TRAIL;
    const float speed = std::hypot(_ball_v[0], _ball_v[1]);
    const uint32_t trail_alpha = static_cast<uint32_t>(std::clamp(speed / 900.0f, 0.0f, 1.0f) * 110);
    if (trail_alpha > 4) {
        for (int i = 0; i < TRAIL - 1; i++) {
            const int idx = (_trail_head + i) % TRAIL;
            const float age = (i + 1) / static_cast<float>(TRAIL);
            cv.disc(CX + _trail[idx][0], CY + _trail[idx][1], 4 + 16 * age, hex(0xFF5A6E),
                    static_cast<uint32_t>(trail_alpha * age * age));
        }
    }
    cv.sprite(_shadow_px, _shadow_alpha, SHADOW_SPRITE, CX + _ball[0] + 7, CY + _ball[1] + 10);
    cv.sprite(_ball_px, _ball_alpha, BALL_SPRITE, CX + _ball[0], CY + _ball[1]);
    extend_marble();
    invalidateCanvasArea(dirty);
}

void Motion::drawSensors(const motion_sample_t &sample)
{
    uint16_t *bg = background(PAGE_SENSORS);
    if (!bg) {
        return;
    }
    for (int a = 0; a < 3; a++) {
        _history[a][_history_head] = sample.accel[a];
        _history[3 + a][_history_head] = sample.gyro[a];
    }
    _history_head = (_history_head + 1) % HISTORY;
    _history_count = std::min(_history_count + 1, HISTORY);

    memcpy(_pixels, bg, SCREEN * SCREEN * sizeof(uint16_t));
    Canvas cv(_pixels, SCREEN, SCREEN);
    const float step = (PANEL_RIGHT - PANEL_LEFT - 16) / static_cast<float>(HISTORY - 1);
    for (int p = 0; p < 2; p++) {
        const float mid = PANEL_TOP[p] + PANEL_HEIGHT / 2.0f;
        const float scale = (PANEL_HEIGHT / 2.0f - 4) / PANEL_RANGE[p];
        for (int a = 0; a < 3; a++) {
            const float *series = _history[p * 3 + a];
            float px = 0, py = 0;
            for (int i = 0; i < _history_count; i++) {
                // Newest at the right edge
                const int idx = (_history_head - _history_count + i + HISTORY) % HISTORY;
                const float x = PANEL_RIGHT - 8 - (_history_count - 1 - i) * step;
                const float v = std::clamp(series[idx] * scale, -PANEL_HEIGHT / 2.0f + 3, PANEL_HEIGHT / 2.0f - 3);
                const float y = mid - v;
                if (i > 0) {
                    cv.line(px, py, x, y, 1.8f, AXIS_COLORS[a]);
                }
                px = x;
                py = y;
            }
        }
    }

    static constexpr const char AXES[3] = {'X', 'Y', 'Z'};
    for (int a = 0; a < 3; a++) {
        lv_label_set_text_fmt(_values[0][a], "%c %+.2f", AXES[a], sample.accel[a]);
        lv_label_set_text_fmt(_values[1][a], "%c %+.1f", AXES[a], sample.gyro[a]);
    }
    const float total = std::sqrt(sample.accel[0] * sample.accel[0] + sample.accel[1] * sample.accel[1] +
                                  sample.accel[2] * sample.accel[2]);
    lv_label_set_text_fmt(_footer, "|a| %.2f g      %.1f °C", total, sample.temperature);

    // Labels invalidate themselves.  The canvas only needs the two plot interiors.
    invalidateCanvasArea({
        .x1 = PANEL_LEFT + 5,
        .y1 = PANEL_TOP[0] + 2,
        .x2 = PANEL_RIGHT - 5,
        .y2 = PANEL_TOP[1] + PANEL_HEIGHT - 2,
    });
}

/* ---------------------------------------------------------------------------
 * Input
 * ------------------------------------------------------------------------- */

void Motion::canvas_event_cb(lv_event_t *e)
{
    Motion *app = static_cast<Motion *>(lv_event_get_user_data(e));
    if (!app) {
        return;
    }
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_GESTURE) {
        // Swipe left for the next page, right for the previous one
        lv_indev_t *indev = lv_indev_active();
        const lv_dir_t dir = indev ? lv_indev_get_gesture_dir(indev) : LV_DIR_NONE;
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
            app->setPage(app->_page + (dir == LV_DIR_LEFT ? 1 : -1), true);
            lv_indev_wait_release(indev);
        }
        return;
    }
    if (app->_page == PAGE_MARBLE) {
        if (code == LV_EVENT_SHORT_CLICKED) {
            app->_ball[0] = app->_ball[1] = app->_ball_v[0] = app->_ball_v[1] = 0;
            // The previous ball coordinate was discarded, so remove it with a
            // one-time full refresh before subsequent dirty updates resume.
            app->_force_full_canvas = true;
            app->primeCanvasForPage();
            lv_obj_invalidate(app->_canvas);
        }
        return;
    }
    if (app->_page != PAGE_LEVEL && app->_page != PAGE_HORIZON) {
        return;
    }
    motion_sample_t sample;
    if (code == LV_EVENT_SHORT_CLICKED && app->readSensor(&sample) == ESP_OK) {
        app->setZero(sample.up);
        app->showToast("Level set here");
    } else if (code == LV_EVENT_LONG_PRESSED) {
        static const float FLAT[3] = {0, 0, 1};
        app->setZero(FLAT);
        app->showToast("Level reset");
    } else {
        return;
    }
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_blob(nvs, NVS_KEY_ZERO, app->_zero, sizeof(app->_zero));
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, Motion, APP_NAME, []() {
    return std::shared_ptr<Motion>(Motion::requestInstance(), [](Motion *) {});
});

} // namespace esp_brookesia::apps
