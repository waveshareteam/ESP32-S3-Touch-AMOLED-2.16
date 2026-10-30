/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TrackMeta {
    std::string title;
    std::string artist;
    std::string album;
    std::vector<uint8_t> cover_jpeg;    // Embedded front cover, empty when none
    uint32_t duration_ms = 0;           // 0 when it cannot be worked out
    uint32_t audio_offset = 0;          // First MP3 frame, after any ID3v2 tag
};

struct LyricLine {
    uint32_t time_ms;
    std::string text;
};

/**
 * @brief  Read ID3v2 tags (MP3) and the duration (MP3 or WAV)
 *
 * Text is returned as UTF-8. Fields missing from the file stay empty.
 */
void music_meta_read(const std::string &path, TrackMeta &meta);

/**
 * @brief  Load the .lrc file next to an audio file, sorted by time
 *
 * @return false when there is no .lrc file for the track in UTF-8 or GBK
 */
bool music_lyrics_load(const std::string &audio_path, std::vector<LyricLine> &lines);
