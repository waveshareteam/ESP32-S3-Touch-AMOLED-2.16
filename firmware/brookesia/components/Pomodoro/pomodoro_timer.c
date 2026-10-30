/* SPDX-License-Identifier: Apache-2.0 */
#include "pomodoro_timer.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "nvs.h"

#define SAVE_DELAY_US (1500 * 1000)

static const char *const s_keys[POMO_PHASE_MAX] = {"focus_s", "short_s", "long_s"};
static const int s_defaults[POMO_PHASE_MAX] = {25 * 60, 5 * 60, 15 * 60};
static const int s_max[POMO_PHASE_MAX] = {120 * 60, 60 * 60, 60 * 60};
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_timer;
static esp_timer_handle_t s_save_timer;
static pomo_state_t s_state;
static int s_seconds[POMO_PHASE_MAX];
static bool s_unsaved[POMO_PHASE_MAX];
static int64_t s_deadline_us;

static uint32_t phase_ms(pomo_phase_t phase) { return (uint32_t)s_seconds[phase] * 1000; }

static void set_phase_locked(pomo_phase_t phase)
{
    s_state.phase = phase;
    s_state.running = false;
    s_state.total_ms = phase_ms(phase);
    s_state.remaining_ms = s_state.total_ms;
}

static pomo_phase_t next_phase_locked(void)
{
    if (s_state.phase != POMO_FOCUS) return POMO_FOCUS;
    if (++s_state.focus_done >= POMO_LONG_BREAK_EVERY) {
        s_state.focus_done = 0;
        return POMO_LONG_BREAK;
    }
    return POMO_SHORT_BREAK;
}

static void expire_cb(void *arg)
{
    (void)arg;
    taskENTER_CRITICAL(&s_lock);
    if (s_state.running) {
        const pomo_phase_t finished = s_state.phase;
        set_phase_locked(next_phase_locked());
        s_state.finished = finished;
        ++s_state.finished_seq;
    }
    taskEXIT_CRITICAL(&s_lock);
}

static void save_cb(void *arg)
{
    (void)arg;
    nvs_handle_t nvs;
    if (nvs_open("pomodoro", NVS_READWRITE, &nvs) != ESP_OK) return;
    for (int i = 0; i < POMO_PHASE_MAX; ++i) {
        taskENTER_CRITICAL(&s_lock);
        const bool dirty = s_unsaved[i];
        const int seconds = s_seconds[i];
        s_unsaved[i] = false;
        taskEXIT_CRITICAL(&s_lock);
        if (dirty) nvs_set_i32(nvs, s_keys[i], seconds);
    }
    nvs_commit(nvs);
    nvs_close(nvs);
}

esp_err_t pomo_init(void)
{
    if (s_timer) return ESP_OK;
    nvs_handle_t nvs;
    const bool opened = nvs_open("pomodoro", NVS_READONLY, &nvs) == ESP_OK;
    for (int i = 0; i < POMO_PHASE_MAX; ++i) {
        int32_t seconds = 0;
        if (!opened || nvs_get_i32(nvs, s_keys[i], &seconds) != ESP_OK ||
            seconds < POMO_MIN_SECONDS || seconds > s_max[i]) seconds = s_defaults[i];
        s_seconds[i] = seconds;
    }
    if (opened) nvs_close(nvs);
    set_phase_locked(POMO_FOCUS);
    const esp_timer_create_args_t save_args = {.callback = save_cb, .name = "pomo_save"};
    const esp_timer_create_args_t timer_args = {.callback = expire_cb, .name = "pomodoro"};
    esp_err_t result = esp_timer_create(&save_args, &s_save_timer);
    if (result == ESP_OK) result = esp_timer_create(&timer_args, &s_timer);
    return result;
}

void pomo_get_state(pomo_state_t *state)
{
    if (!state) return;
    const int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_lock);
    *state = s_state;
    if (s_state.running) {
        const int64_t remaining = s_deadline_us - now;
        state->remaining_ms = remaining > 0 ? (uint32_t)(remaining / 1000) : 0;
    }
    taskEXIT_CRITICAL(&s_lock);
}

void pomo_start(void)
{
    taskENTER_CRITICAL(&s_lock);
    if (s_state.running || s_state.remaining_ms == 0) { taskEXIT_CRITICAL(&s_lock); return; }
    s_state.running = true;
    s_deadline_us = esp_timer_get_time() + (int64_t)s_state.remaining_ms * 1000;
    const uint64_t timeout = (uint64_t)s_state.remaining_ms * 1000;
    taskEXIT_CRITICAL(&s_lock);
    esp_timer_stop(s_timer);
    esp_timer_start_once(s_timer, timeout);
}

void pomo_pause(void)
{
    esp_timer_stop(s_timer);
    const int64_t now = esp_timer_get_time();
    taskENTER_CRITICAL(&s_lock);
    if (s_state.running) {
        const int64_t remaining = s_deadline_us - now;
        s_state.remaining_ms = remaining > 0 ? (uint32_t)(remaining / 1000) : 0;
        s_state.running = false;
    }
    taskEXIT_CRITICAL(&s_lock);
}

void pomo_reset(void) { esp_timer_stop(s_timer); taskENTER_CRITICAL(&s_lock); set_phase_locked(s_state.phase); taskEXIT_CRITICAL(&s_lock); }
void pomo_skip(void) { esp_timer_stop(s_timer); taskENTER_CRITICAL(&s_lock); set_phase_locked(next_phase_locked()); taskEXIT_CRITICAL(&s_lock); }
void pomo_select(pomo_phase_t phase) { if (phase >= POMO_PHASE_MAX) return; taskENTER_CRITICAL(&s_lock); if (!s_state.running) set_phase_locked(phase); taskEXIT_CRITICAL(&s_lock); }
int pomo_get_seconds(pomo_phase_t phase) { return phase < POMO_PHASE_MAX ? s_seconds[phase] : 0; }
void pomo_set_seconds(pomo_phase_t phase, int seconds)
{
    if (phase >= POMO_PHASE_MAX) return;
    if (seconds < POMO_MIN_SECONDS) seconds = POMO_MIN_SECONDS;
    if (seconds > s_max[phase]) seconds = s_max[phase];
    taskENTER_CRITICAL(&s_lock);
    const bool changed = !s_state.running && s_seconds[phase] != seconds;
    if (changed) { s_seconds[phase] = seconds; s_unsaved[phase] = true; if (s_state.phase == phase) set_phase_locked(phase); }
    taskEXIT_CRITICAL(&s_lock);
    if (changed) { esp_timer_stop(s_save_timer); esp_timer_start_once(s_save_timer, SAVE_DELAY_US); }
}
