/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Pomodoro"
#include "esp_lib_utils.h"
#include "app_fonts.h"
#include "Pomodoro.hpp"

#define APP_NAME "Pomodoro"

LV_IMG_DECLARE(img_app_pomodoro);

namespace esp_brookesia::apps {

static constexpr uint32_t REFRESH_PERIOD_MS = 200;
static constexpr uint32_t BANNER_MS = 8000;
static constexpr uint32_t BG_COLOR = 0x0E0F14;
static constexpr uint32_t TRACK_COLOR = 0x22232D;
static constexpr uint32_t UNPICKED_COLOR = 0x5E616E;
static constexpr int TIME_Y = -36;
static constexpr uint32_t PHASE_COLORS[POMO_PHASE_MAX] = {0xFF6B57, 0x3CC48E, 0x4F8BFF};
static constexpr const char *PHASE_CHIPS[POMO_PHASE_MAX] = {"Focus", "Short", "Long"};

enum Control : uintptr_t {
    CONTROL_RESET = 1,
    CONTROL_DECREASE,
    CONTROL_PLAY_PAUSE,
    CONTROL_INCREASE,
    CONTROL_SKIP,
};

Pomodoro *Pomodoro::_instance = nullptr;

Pomodoro *Pomodoro::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new Pomodoro(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

Pomodoro::Pomodoro(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_pomodoro, true, use_status_bar, use_navigation_bar)
{
}

Pomodoro::~Pomodoro()
{
}

static lv_obj_t *create_plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *create_round_button(lv_obj_t *parent, const char *symbol, int size, uintptr_t control,
                                     lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(TRACK_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x33354A), LV_STATE_PRESSED);
    lv_obj_set_user_data(btn, reinterpret_cast<void *>(control));
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, size > 56 ? &lv_font_montserrat_30 : &lv_font_montserrat_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(icon);
    return btn;
}

bool Pomodoro::run(void)
{
    ESP_UTILS_LOGD("Run");
    ESP_UTILS_CHECK_FALSE_RETURN(pomo_init() == ESP_OK, false, "Timer init failed");

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Remaining time drains clockwise around the edge of the round panel
    _ring = lv_arc_create(screen);
    lv_obj_set_size(_ring, 460, 460);
    lv_obj_center(_ring);
    lv_arc_set_bg_angles(_ring, 0, 360);
    lv_arc_set_rotation(_ring, 270);
    lv_arc_set_range(_ring, 0, 1000);
    lv_obj_remove_style(_ring, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(_ring, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_ring, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_ring, lv_color_hex(TRACK_COLOR), LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(_ring, true, LV_PART_INDICATOR);

    lv_obj_t *chips = create_plain(screen);
    lv_obj_set_size(chips, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(chips, 10, LV_PART_MAIN);
    lv_obj_align(chips, LV_ALIGN_CENTER, 0, -122);
    for (int i = 0; i < POMO_PHASE_MAX; i++) {
        lv_obj_t *chip = lv_button_create(chips);
        lv_obj_set_size(chip, 84, 36);
        lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(chip, 0, LV_PART_MAIN);
        lv_obj_set_user_data(chip, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(chip, chip_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_t *label = lv_label_create(chip);
        lv_label_set_text(label, PHASE_CHIPS[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN);
        lv_obj_center(label);
        _chips[i] = chip;
    }

    _time_row = create_plain(screen);
    lv_obj_set_size(_time_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(_time_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_text_font(_time_row, &lv_font_montserrat_44, LV_PART_MAIN);
    lv_obj_set_style_text_color(_time_row, lv_color_white(), LV_PART_MAIN);
    // Scaled up so the countdown reads from across the desk
    lv_obj_set_style_transform_scale(_time_row, 400, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_x(_time_row, lv_pct(50), LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(_time_row, lv_pct(50), LV_PART_MAIN);
    lv_obj_align(_time_row, LV_ALIGN_CENTER, 0, TIME_Y);
    _minutes_label = lv_label_create(_time_row);
    lv_label_set_text(_minutes_label, "00");
    lv_label_set_text(lv_label_create(_time_row), ":");
    _seconds_label = lv_label_create(_time_row);
    lv_label_set_text(_seconds_label, "00");
    // A tap on the minutes or seconds chooses the field adjusted by - and +.
    for (int i = 0; i < 2; i++) {
        lv_obj_t *zone = create_plain(screen);
        lv_obj_set_size(zone, 150, 100);
        lv_obj_align(zone, LV_ALIGN_CENTER, i == 0 ? -75 : 75, TIME_Y);
        lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(zone, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
        lv_obj_add_event_cb(zone, time_event_cb, LV_EVENT_CLICKED, this);
    }

    lv_obj_t *dots = create_plain(screen);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dots, 12, LV_PART_MAIN);
    lv_obj_align(dots, LV_ALIGN_CENTER, 0, 24);
    for (int i = 0; i < POMO_LONG_BREAK_EVERY; i++) {
        _dots[i] = create_plain(dots);
        lv_obj_set_size(_dots[i], 12, 12);
        lv_obj_set_style_radius(_dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(_dots[i], LV_OPA_COVER, LV_PART_MAIN);
    }

    _hint_label = lv_label_create(screen);
    lv_obj_set_width(_hint_label, 300);
    lv_label_set_long_mode(_hint_label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(_hint_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_hint_label, app_font_text_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_hint_label, lv_color_hex(0xA0A3B1), LV_PART_MAIN);
    lv_obj_align(_hint_label, LV_ALIGN_CENTER, 0, 62);

    lv_obj_t *controls = create_plain(screen);
    lv_obj_set_size(controls, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 12, LV_PART_MAIN);
    lv_obj_align(controls, LV_ALIGN_CENTER, 0, 132);
    create_round_button(controls, LV_SYMBOL_REFRESH, 42, CONTROL_RESET, control_event_cb, this);
    create_round_button(controls, LV_SYMBOL_MINUS, 42, CONTROL_DECREASE, control_event_cb, this);
    _play_btn = create_round_button(controls, LV_SYMBOL_PLAY, 68, CONTROL_PLAY_PAUSE, control_event_cb, this);
    _play_icon = lv_obj_get_child(_play_btn, 0);
    create_round_button(controls, LV_SYMBOL_PLUS, 42, CONTROL_INCREASE, control_event_cb, this);
    create_round_button(controls, LV_SYMBOL_NEXT, 42, CONTROL_SKIP, control_event_cb, this);

    // A phase that ended while the app was away is not announced again
    pomo_state_t state;
    pomo_get_state(&state);
    _seen_finished_seq = state.finished_seq;
    _banner_active = false;
    _edit_seconds = false;
    refresh();

    _timer = lv_timer_create(timer_cb, REFRESH_PERIOD_MS, this);
    return true;
}

void Pomodoro::refresh(void)
{
    pomo_state_t state;
    pomo_get_state(&state);
    const lv_color_t color = lv_color_hex(PHASE_COLORS[state.phase]);

    const uint32_t seconds = (state.remaining_ms + 999) / 1000;
    char text[8];
    snprintf(text, sizeof(text), "%02u", (unsigned)(seconds / 60));
    if (strcmp(lv_label_get_text(_minutes_label), text) != 0) {
        lv_label_set_text(_minutes_label, text);
    }
    snprintf(text, sizeof(text), "%02u", (unsigned)(seconds % 60));
    if (strcmp(lv_label_get_text(_seconds_label), text) != 0) {
        lv_label_set_text(_seconds_label, text);
    }
    // While stopped, the time field not selected for touch adjustment is dimmed.
    const bool dim_minutes = !state.running && _edit_seconds;
    const bool dim_seconds = !state.running && !_edit_seconds;
    const lv_color_t minutes_color = dim_minutes ? lv_color_hex(UNPICKED_COLOR) : lv_color_white();
    const lv_color_t seconds_color = dim_seconds ? lv_color_hex(UNPICKED_COLOR) : lv_color_white();
    if (!lv_color_eq(lv_obj_get_style_text_color(_minutes_label, LV_PART_MAIN), minutes_color)) {
        lv_obj_set_style_text_color(_minutes_label, minutes_color, LV_PART_MAIN);
    }
    if (!lv_color_eq(lv_obj_get_style_text_color(_seconds_label, LV_PART_MAIN), seconds_color)) {
        lv_obj_set_style_text_color(_seconds_label, seconds_color, LV_PART_MAIN);
    }
    const int32_t ring_value = state.total_ms ?
                               (int32_t)((uint64_t)state.remaining_ms * 1000 / state.total_ms) : 0;
    if (lv_arc_get_value(_ring) != ring_value) {
        lv_arc_set_value(_ring, ring_value);
    }
    if (!lv_color_eq(lv_obj_get_style_arc_color(_ring, LV_PART_INDICATOR), color)) {
        lv_obj_set_style_arc_color(_ring, color, LV_PART_INDICATOR);
    }
    if (!lv_color_eq(lv_obj_get_style_bg_color(_play_btn, LV_PART_MAIN), color)) {
        lv_obj_set_style_bg_color(_play_btn, color, LV_PART_MAIN);
    }
    const char *play_symbol = state.running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY;
    if (strcmp(lv_label_get_text(_play_icon), play_symbol) != 0) {
        lv_label_set_text(_play_icon, play_symbol);
    }

    for (int i = 0; i < POMO_PHASE_MAX; i++) {
        const bool selected = i == state.phase;
        const lv_color_t background = selected ? lv_color_hex(PHASE_COLORS[i]) : lv_color_hex(TRACK_COLOR);
        const lv_color_t text_color = selected ? lv_color_white() : lv_color_hex(0xA0A3B1);
        if (!lv_color_eq(lv_obj_get_style_bg_color(_chips[i], LV_PART_MAIN), background)) {
            lv_obj_set_style_bg_color(_chips[i], background, LV_PART_MAIN);
        }
        if (!lv_color_eq(lv_obj_get_style_text_color(_chips[i], LV_PART_MAIN), text_color)) {
            lv_obj_set_style_text_color(_chips[i], text_color, LV_PART_MAIN);
        }
    }
    for (int i = 0; i < POMO_LONG_BREAK_EVERY; i++) {
        const lv_color_t dot_color = i < state.focus_done ? lv_color_hex(PHASE_COLORS[POMO_FOCUS]) :
                                     lv_color_hex(TRACK_COLOR);
        if (!lv_color_eq(lv_obj_get_style_bg_color(_dots[i], LV_PART_MAIN), dot_color)) {
            lv_obj_set_style_bg_color(_dots[i], dot_color, LV_PART_MAIN);
        }
    }

    // A finished phase is announced for a while, then the regular hint returns
    if (state.finished_seq != _seen_finished_seq) {
        _seen_finished_seq = state.finished_seq;
        _banner_active = true;
        _banner_since = lv_tick_get();
    }
    if (_banner_active && (state.running || lv_tick_elaps(_banner_since) > BANNER_MS)) {
        _banner_active = false;
    }
    const char *hint;
    if (_banner_active) {
        hint = (state.finished == POMO_FOCUS) ? "Focus done. Time for a break." : "Break over. Ready to focus?";
    } else if (state.running) {
        hint = (state.phase == POMO_FOCUS) ? "Stay focused" : "Take a break";
    } else if (state.remaining_ms < state.total_ms) {
        hint = "Paused";
    } else {
        hint = _edit_seconds ? "Tap - or + to set seconds" : "Tap - or + to set minutes";
    }
    if (strcmp(lv_label_get_text(_hint_label), hint) != 0) {
        lv_label_set_text(_hint_label, hint);
    }
}

void Pomodoro::timer_cb(lv_timer_t *timer)
{
    static_cast<Pomodoro *>(lv_timer_get_user_data(timer))->refresh();
}

void Pomodoro::time_event_cb(lv_event_t *e)
{
    Pomodoro *app = static_cast<Pomodoro *>(lv_event_get_user_data(e));
    lv_obj_t *zone = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    app->_edit_seconds = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(zone)) == 1;
    app->_banner_active = false;
    app->refresh();
}

void Pomodoro::chip_event_cb(lv_event_t *e)
{
    Pomodoro *app = static_cast<Pomodoro *>(lv_event_get_user_data(e));
    lv_obj_t *chip = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    pomo_select(static_cast<pomo_phase_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(chip))));
    app->_banner_active = false;
    app->refresh();
}

void Pomodoro::control_event_cb(lv_event_t *e)
{
    Pomodoro *app = static_cast<Pomodoro *>(lv_event_get_user_data(e));
    lv_obj_t *btn = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    pomo_state_t state;
    pomo_get_state(&state);
    switch (reinterpret_cast<uintptr_t>(lv_obj_get_user_data(btn))) {
    case CONTROL_RESET:
        pomo_reset();
        break;
    case CONTROL_DECREASE:
        if (!state.running) {
            pomo_set_seconds(state.phase, pomo_get_seconds(state.phase) + (app->_edit_seconds ? -1 : -60));
        }
        break;
    case CONTROL_PLAY_PAUSE:
        if (state.running) {
            pomo_pause();
        } else {
            pomo_start();
        }
        break;
    case CONTROL_INCREASE:
        if (!state.running) {
            pomo_set_seconds(state.phase, pomo_get_seconds(state.phase) + (app->_edit_seconds ? 1 : 60));
        }
        break;
    case CONTROL_SKIP:
        pomo_skip();
        break;
    default:
        break;
    }
    app->_banner_active = false;
    app->refresh();
}

bool Pomodoro::back(void)
{
    ESP_UTILS_LOGD("Back");
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

// The timer itself keeps running; only the screen goes away
bool Pomodoro::close(void)
{
    ESP_UTILS_LOGD("Close");
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    _ring = _time_row = _minutes_label = _seconds_label = _hint_label = _play_btn = _play_icon = nullptr;
    for (lv_obj_t *&chip : _chips) {
        chip = nullptr;
    }
    for (lv_obj_t *&dot : _dots) {
        dot = nullptr;
    }
    return true;
}

bool Pomodoro::init(void)
{
    return true;
}

bool Pomodoro::deinit(void)
{
    return true;
}

bool Pomodoro::pause(void)
{
    return true;
}

bool Pomodoro::resume(void)
{
    refresh();
    return true;
}

} // namespace esp_brookesia::apps
