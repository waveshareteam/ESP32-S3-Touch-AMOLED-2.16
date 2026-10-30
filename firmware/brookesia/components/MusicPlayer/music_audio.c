/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include "audio_player.h"
#include "bsp_board_extra.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "music_audio.h"

static const char *TAG = "music_audio";

static bool s_ready;
static bool s_session_acquired;
static bool s_finished;
static bool s_stopping;
static bool s_paused;
static uint64_t s_started_us;
static uint32_t s_paused_ms;
static char s_path[256];

static void on_player_event(audio_player_cb_ctx_t *ctx)
{
    if (ctx->audio_event == AUDIO_PLAYER_CALLBACK_EVENT_IDLE) {
        if (!s_stopping) {
            s_finished = true;
        }
        s_stopping = false;
        s_paused = false;
    }
}

esp_err_t music_audio_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(bsp_extra_audio_session_acquire(BSP_EXTRA_AUDIO_OWNER_MUSIC), TAG,
                        "Music audio session unavailable");
    s_session_acquired = true;
    const esp_err_t result = bsp_extra_player_init();
    if (result != ESP_OK) {
        (void)bsp_extra_audio_session_release(BSP_EXTRA_AUDIO_OWNER_MUSIC);
        s_session_acquired = false;
        return result;
    }
    bsp_extra_player_register_callback(on_player_event, NULL);
    s_ready = true;
    return ESP_OK;
}

esp_err_t music_audio_deinit(void)
{
    if (s_ready) {
        (void)bsp_extra_player_del();
        s_ready = false;
    }
    if (s_session_acquired) {
        (void)bsp_extra_audio_session_release(BSP_EXTRA_AUDIO_OWNER_MUSIC);
        s_session_acquired = false;
    }
    s_path[0] = '\0';
    s_finished = false;
    s_paused = false;
    return ESP_OK;
}

esp_err_t music_audio_play_file(const char *path, uint32_t start_offset)
{
    (void)start_offset;
    ESP_RETURN_ON_FALSE(s_ready && path, ESP_ERR_INVALID_STATE, TAG, "Music player not ready");
    const esp_err_t result = bsp_extra_player_play_file(path);
    if (result == ESP_OK) {
        strlcpy(s_path, path, sizeof(s_path));
        s_finished = false;
        s_paused = false;
        s_paused_ms = 0;
        s_started_us = esp_timer_get_time();
    }
    return result;
}

esp_err_t music_audio_resume(void)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "Music player not ready");
    const esp_err_t result = audio_player_resume();
    if (result == ESP_OK) {
        s_started_us = esp_timer_get_time();
        s_paused = false;
    }
    return result;
}

esp_err_t music_audio_pause(void)
{
    ESP_RETURN_ON_FALSE(s_ready, ESP_ERR_INVALID_STATE, TAG, "Music player not ready");
    if (!s_paused) {
        s_paused_ms = music_audio_position_ms();
    }
    const esp_err_t result = audio_player_pause();
    if (result == ESP_OK) {
        s_paused = true;
    }
    return result;
}

esp_err_t music_audio_stop(void)
{
    if (!s_ready || audio_player_get_state() == AUDIO_PLAYER_STATE_IDLE) {
        return ESP_OK;
    }
    s_stopping = true;
    return audio_player_stop();
}

bool music_audio_is_playing(void)
{
    return s_ready && !s_paused && s_path[0] != '\0' &&
           audio_player_get_state() == AUDIO_PLAYER_STATE_PLAYING;
}

uint32_t music_audio_position_ms(void)
{
    if (!s_ready) {
        return 0;
    }
    if (s_paused) {
        return s_paused_ms;
    }
    return s_paused_ms + (uint32_t)((esp_timer_get_time() - s_started_us) / 1000);
}

bool music_audio_take_preempted(void)
{
    return false;
}

bool music_audio_take_finished(void)
{
    const bool finished = s_finished;
    s_finished = false;
    return finished;
}
