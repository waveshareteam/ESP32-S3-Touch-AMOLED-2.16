/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class Drawpanel : public systems::phone::App {
public:
    static Drawpanel *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~Drawpanel();

    using systems::phone::App::startRecordResource;
    using systems::phone::App::endRecordResource;

protected:
    Drawpanel(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;
    bool deinit(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    static Drawpanel *_instance;

    lv_obj_t *_canvas = nullptr;
    lv_draw_buf_t *_draw_buf = nullptr;
    lv_obj_t *_selected_swatch = nullptr;
    lv_color_t _pen_color;
    lv_point_t _last_point = {};
    bool _stroke_active = false;

    void createToolbar(void);
    void clearCanvas(void);
    void drawSegment(const lv_point_t &from, const lv_point_t &to);
    static void canvas_event_cb(lv_event_t *event);
    static void swatch_event_cb(lv_event_t *event);
    static void clear_event_cb(lv_event_t *event);
};

} // namespace esp_brookesia::apps
