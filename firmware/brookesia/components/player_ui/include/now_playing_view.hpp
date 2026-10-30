/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include "lvgl.h"

/**
 * Now-playing screen shared by the music apps, laid out for the round 466 x 466 panel:
 * a cover disc that spins while playing, a large title line, a subtitle, the time,
 * previous / play-pause / next, a progress ring along the edge and a volume gauge for
 * the knob. Optional top buttons open a track list and switch the play mode.
 *
 * All methods run in the LVGL task.
 */
class NowPlayingView {
public:
    enum class Action {
        PREV,
        PLAY_PAUSE,
        NEXT,
        LIST,
        MODE,
    };
    using ActionHandler = std::function<void(Action)>;

    ~NowPlayingView();

    void create(lv_obj_t *parent, ActionHandler handler, bool with_top_buttons);
    // Stops the timers and frees the cover; the objects belong to the parent screen
    void destroy();

    lv_obj_t *root() const
    {
        return _root;
    }
    void setVisible(bool visible);

    void setTitle(const char *text);
    void setSubtitle(const char *text);
    void setTime(uint32_t position_ms, uint32_t duration_ms);
    void setPlaying(bool playing);
    bool isPlaying() const
    {
        return _playing;
    }
    void setModeSymbol(const char *symbol);
    void showVolume(int percent);

    // Decodes a JPEG cover; the previous cover stays until the new one is ready
    bool setCoverJpeg(const uint8_t *jpeg, size_t len);
    void clearCover();

private:
    lv_obj_t *_root = nullptr;
    lv_obj_t *_progress_arc = nullptr;
    lv_obj_t *_volume_arc = nullptr;
    lv_obj_t *_volume_label = nullptr;
    lv_obj_t *_cover_img = nullptr;
    lv_obj_t *_disc_icon = nullptr;
    lv_obj_t *_title_label = nullptr;
    lv_obj_t *_subtitle_label = nullptr;
    lv_obj_t *_time_label = nullptr;
    lv_obj_t *_play_btn = nullptr;
    lv_obj_t *_play_icon = nullptr;
    lv_obj_t *_mode_icon = nullptr;
    lv_timer_t *_spin_timer = nullptr;
    lv_timer_t *_volume_hide_timer = nullptr;

    ActionHandler _handler;
    bool _playing = false;
    int32_t _spin_angle = 0;

    lv_image_dsc_t _cover_dsc[2] = {};
    uint8_t *_cover_pixels[2] = {};
    int _cover_index = 0;

    lv_obj_t *createButton(lv_obj_t *parent, const char *symbol, int size, Action action, const lv_font_t *font);
    void setAccent(lv_color_t color);
    void releaseCoverBuffer(int index);
    bool decodeCover(const uint8_t *jpeg, size_t len, int index);

    static void button_event_cb(lv_event_t *e);
    static void spin_timer_cb(lv_timer_t *timer);
    static void volume_hide_timer_cb(lv_timer_t *timer);
};
