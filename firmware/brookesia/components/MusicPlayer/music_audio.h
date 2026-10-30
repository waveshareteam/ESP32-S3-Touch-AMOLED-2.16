/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MP3/WAV playback for the music app. Starting or resuming playback takes audio
 * over from other apps; when another app takes it, playback pauses and
 * music_audio_take_preempted() reports it once.
 */
esp_err_t music_audio_init(void);
esp_err_t music_audio_deinit(void);

/**
 * @brief  Play a file from the start
 *
 * @param  start_offset  Byte offset of the first MP3 frame. The decoder does not skip
 *                       ID3v2 tags and can get stuck on picture data inside them, so the
 *                       file is handed to it starting at this offset. 0 plays the file as is.
 */
esp_err_t music_audio_play_file(const char *path, uint32_t start_offset);
esp_err_t music_audio_resume(void);
esp_err_t music_audio_pause(void);

/**
 * @brief  Stop playback and close the file, e.g. before the card is unmounted
 */
esp_err_t music_audio_stop(void);

bool music_audio_is_playing(void);

/**
 * @brief  Position in the current file, counted from the decoded audio
 */
uint32_t music_audio_position_ms(void);

bool music_audio_take_preempted(void);

/**
 * @brief  True once after the current file played to its end
 */
bool music_audio_take_finished(void);

#ifdef __cplusplus
}
#endif
