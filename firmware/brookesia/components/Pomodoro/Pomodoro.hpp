/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "pomodoro_timer.h"

namespace esp_brookesia::apps {

/**
 * @brief Pomodoro timer: focus and break phases on a ring around the round panel
 *
 * Touch selects minutes or seconds; the matching - and + controls set the stopped phase.
 * The timer keeps running when the app is left or closed.
 */
class Pomodoro: public systems::phone::App {
public:
    static Pomodoro *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~Pomodoro();

    using systems::phone::App::startRecordResource;
    using systems::phone::App::endRecordResource;

protected:
    Pomodoro(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;
    bool deinit(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    static Pomodoro *_instance;

    lv_obj_t *_ring = nullptr;
    lv_obj_t *_chips[POMO_PHASE_MAX] = {};
    lv_obj_t *_time_row = nullptr;
    lv_obj_t *_minutes_label = nullptr;
    lv_obj_t *_seconds_label = nullptr;
    lv_obj_t *_dots[POMO_LONG_BREAK_EVERY] = {};
    lv_obj_t *_hint_label = nullptr;
    lv_obj_t *_play_btn = nullptr;
    lv_obj_t *_play_icon = nullptr;
    lv_timer_t *_timer = nullptr;

    uint32_t _seen_finished_seq = 0;
    uint32_t _banner_since = 0;
    bool _banner_active = false;
    bool _edit_seconds = false;

    void refresh(void);

    static void timer_cb(lv_timer_t *timer);
    static void chip_event_cb(lv_event_t *e);
    static void control_event_cb(lv_event_t *e);
    static void time_event_cb(lv_event_t *e);
};

} // namespace esp_brookesia::apps
