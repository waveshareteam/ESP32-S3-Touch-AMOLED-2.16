/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include "lvgl.h"
// waveshare/qmi8658 2.0.1 unconditionally defines M_PI.  IDF 6.1's
// picolibc math header may already provide it, and does not permit that
// redefinition with warnings promoted to errors.
#ifdef M_PI
#undef M_PI
#endif
#include "qmi8658.h"
#ifdef M_PI
#undef M_PI
#endif
#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

struct motion_sample_t {
    float up[3];
    float accel[3];
    float gyro[3];
    float temperature;
    uint32_t count;
};

/**
 * @brief What the QMI8658 motion sensor feels, on four pages
 *
 * A bubble level, an attitude indicator, a marble that rolls around the round screen and live
 * sensor traces. Every page draws into one full-screen RGB565 canvas; the static parts of a page
 * are drawn once into a background that each frame starts from. A horizontal swipe changes the
 * page; a tap sets the current tilt as level, and a long press clears that.
 */
class Motion: public systems::phone::App {
public:
    static constexpr int SCREEN = 480;

    static Motion *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~Motion();

protected:
    Motion(bool use_status_bar, bool use_navigation_bar);

    bool init(void) override;
    bool deinit(void) override;
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    enum Page : uint8_t {
        PAGE_LEVEL,
        PAGE_HORIZON,
        PAGE_MARBLE,
        PAGE_SENSORS,
        PAGE_COUNT,
    };

    static constexpr int HISTORY = 160;         // Samples in each sensor trace
    static constexpr int TRAIL = 14;            // Marble positions in its trail
    static constexpr int LADDER_LABELS = 8;     // Pitch ladder numbers: -40 to 40 in steps of 10, without 0
    static constexpr int BUBBLE_SPRITE = 64;
    static constexpr int BALL_SPRITE = 64;
    static constexpr int SHADOW_SPRITE = 80;

    static Motion *_instance;

    lv_obj_t *_canvas = nullptr;
    uint16_t *_pixels = nullptr;
    uint16_t *_backgrounds[PAGE_COUNT] = {};    // Kept between visits; the horizon has none
    lv_timer_t *_timer = nullptr;
    Page _page = PAGE_LEVEL;
    int64_t _last_us = 0;
    uint32_t _last_count = 0;
    bool _sensor_ok = false;
    bool _force_full_canvas = true;
    qmi8658_dev_t _imu = {};
    bool _imu_ready = false;
    uint32_t _sample_count = 0;

    // Labels over the canvas
    lv_obj_t *_title = nullptr;
    lv_obj_t *_toast = nullptr;
    lv_obj_t *_dots[PAGE_COUNT] = {};
    lv_obj_t *_page_labels[PAGE_COUNT] = {};    // Holds each page's own labels, shown with the page
    lv_obj_t *_level_angle = nullptr;
    lv_obj_t *_level_axes = nullptr;
    lv_obj_t *_level_state = nullptr;
    lv_obj_t *_pitch = nullptr;
    lv_obj_t *_roll = nullptr;
    lv_obj_t *_ladder[LADDER_LABELS] = {};
    lv_obj_t *_values[2][3] = {};
    lv_obj_t *_footer = nullptr;
    lv_obj_t *_no_sensor = nullptr;

    // Tilt is measured from this up vector, the one a tap called level; saved across resets
    float _zero[3] = {0, 0, 1};
    float _zero_rot[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

    // Level
    float _bubble[2] = {0, 0};
    float _bubble_v[2] = {0, 0};
    bool _is_level = false;
    uint16_t _bubble_px[2][BUBBLE_SPRITE * BUBBLE_SPRITE] = {};     // Plain and level colours
    uint8_t _bubble_alpha[BUBBLE_SPRITE * BUBBLE_SPRITE] = {};

    // Marble, in pixels from the centre
    float _ball[2] = {0, 0};
    float _ball_v[2] = {0, 0};
    float _trail[TRAIL][2] = {};
    int _trail_head = 0;
    uint16_t _ball_px[BALL_SPRITE * BALL_SPRITE] = {};
    uint8_t _ball_alpha[BALL_SPRITE * BALL_SPRITE] = {};
    uint8_t _shadow_alpha[SHADOW_SPRITE * SHADOW_SPRITE] = {};
    uint16_t _shadow_px[SHADOW_SPRITE * SHADOW_SPRITE] = {};

    // Sensors
    float _history[6][HISTORY] = {};
    int _history_head = 0;
    int _history_count = 0;

    // Horizon colours along the distance from the horizon, in half pixels, for each 2x2 dither phase
    uint16_t _horizon_lut[4][2048] = {};
    // The horizon's fixed parts (case, bank scale, aircraft) as the pixels they cover, built once
    uint32_t *_overlay_index = nullptr;
    uint16_t *_overlay_color = nullptr;
    uint8_t *_overlay_alpha = nullptr;
    int _overlay_count = 0;

    bool buildUi();
    void createLabels(lv_obj_t *screen);
    void createPageDots(lv_obj_t *screen);
    void setPage(int page, bool announce);
    void showToast(const char *text);
    void startSensor();
    esp_err_t readSensor(motion_sample_t *sample);
    bool releaseImu();
    void invalidateCanvasArea(const lv_area_t &area);
    void primeCanvasForPage();

    void loadZero();
    void setZero(const float up[3]);
    void relativeUp(const float up[3], float out[3]) const;

    void prepareSprites();
    uint16_t *background(Page page);
    void drawLevelBackground(uint16_t *pixels);
    void drawMarbleBackground(uint16_t *pixels);
    void drawSensorsBackground(uint16_t *pixels);
    static void drawHorizonFixed(uint16_t *pixels);
    bool prepareHorizonOverlay();

    void frame();
    void drawLevel(const float up[3], float dt);
    void drawHorizon(const float up[3]);
    void drawMarble(const float up[3], float dt);
    void drawSensors(const motion_sample_t &sample);

    static void timer_cb(lv_timer_t *timer);
    static void canvas_event_cb(lv_event_t *e);
};

} // namespace esp_brookesia::apps
