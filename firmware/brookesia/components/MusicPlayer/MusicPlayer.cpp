/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:MusicPlayer"
#include "esp_lib_utils.h"
#include "app_fonts.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"
#include "music_audio.h"
#include "MusicPlayer.hpp"

#define APP_NAME "MusicPlayer"
#define FLASH_MUSIC_DIR BSP_SPIFFS_MOUNT_POINT "/music"

LV_IMG_DECLARE(img_app_musicplayer);

namespace esp_brookesia::apps {

static constexpr uint32_t TIMER_PERIOD_MS = 200;
static constexpr uint32_t RESTART_THRESHOLD_MS = 3000;  // "Previous" restarts the track after this
static constexpr uint32_t STALL_TIMEOUT_MS = 3000;      // No audio for this long while playing: skip the file
static constexpr uint32_t BG_COLOR = 0x0B0B12;
static constexpr const char *MODE_SYMBOLS[] = {LV_SYMBOL_LOOP, LV_SYMBOL_REFRESH, LV_SYMBOL_SHUFFLE};

MusicPlayer *MusicPlayer::_instance = nullptr;

MusicPlayer *MusicPlayer::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new MusicPlayer(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

MusicPlayer::MusicPlayer(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_musicplayer, true, use_status_bar, use_navigation_bar)
{
}

MusicPlayer::~MusicPlayer()
{
    _view.destroy();
}

static bool is_audio_file(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0) && name[0] != '.';
}

static std::string display_name(const std::string &path)
{
    const size_t slash = path.rfind('/');
    std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

static void add_dir(std::vector<std::string> &tracks, const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }
    std::vector<std::string> found;
    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type != DT_DIR && is_audio_file(entry->d_name)) {
            found.push_back(std::string(dir_path) + "/" + entry->d_name);
        }
    }
    closedir(dir);
    std::sort(found.begin(), found.end());
    tracks.insert(tracks.end(), found.begin(), found.end());
}

// Card tracks come first; the songs in flash are always there as a fallback
void MusicPlayer::scanLibrary(void)
{
    const std::string current = (_index >= 0 && _index < (int)_tracks.size()) ? _tracks[_index] : "";
    _tracks.clear();
    if (_storage_lease.active) {
        // Keep the product's established lowercase directory as the primary
        // location, then accept the older capitalized spelling if it contains
        // the card's library instead. FAT volumes commonly alias the two, so
        // only probe the fallback when the primary location found no tracks.
        add_dir(_tracks, STORAGE_SERVICE_MOUNT_POINT "/music");
        if (_tracks.empty()) {
            add_dir(_tracks, STORAGE_SERVICE_MOUNT_POINT "/Music");
        }
    }
    add_dir(_tracks, FLASH_MUSIC_DIR);
    ESP_UTILS_LOGI("%u tracks", (unsigned)_tracks.size());

    auto it = std::find(_tracks.begin(), _tracks.end(), current);
    _index = (it != _tracks.end()) ? (int)(it - _tracks.begin()) : -1;
}

bool MusicPlayer::run(void)
{
    ESP_UTILS_LOGD("Run");

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    app_fonts_acquire();
    if (storage_service_acquire(&_storage_lease) != ESP_OK) {
        ESP_UTILS_LOGW("microSD music unavailable; checking SPIFFS");
    }
    if (music_audio_init() != ESP_OK) {
        storage_service_release(&_storage_lease);
        app_fonts_release();
        return false;
    }

    _view.create(screen, [this](NowPlayingView::Action action) {
        onAction(action);
    }, true);
    updateModeIcon();

    _index = -1;
    _started = false;
    scanLibrary();
    if (_tracks.empty()) {
        showEmpty();
    } else {
        selectTrack(0, false);
    }

    _timer = lv_timer_create(timer_cb, TIMER_PERIOD_MS, this);
    return true;
}

void MusicPlayer::showEmpty(void)
{
    _view.setTitle("No music yet");
    _view.setSubtitle("Copy MP3 files to music/ on the microSD card");
    _view.setTime(0, 0);
    _view.setPlaying(false);
    _view.clearCover();
}

void MusicPlayer::selectTrack(int index, bool play)
{
    if (_tracks.empty()) {
        showEmpty();
        return;
    }
    _index = (index % (int)_tracks.size() + (int)_tracks.size()) % (int)_tracks.size();
    const std::string &path = _tracks[_index];

    music_meta_read(path, _meta);
    if (_meta.title.empty()) {
        _meta.title = display_name(path);
    }
    music_lyrics_load(path, _lyrics);
    _lyric_index = -1;

    if (_meta.cover_jpeg.empty() || !_view.setCoverJpeg(_meta.cover_jpeg.data(), _meta.cover_jpeg.size())) {
        _view.clearCover();
    }
    // The decoded cover is all the view needs
    std::vector<uint8_t>().swap(_meta.cover_jpeg);

    _started = false;
    _last_position = 0;
    _stall_since = lv_tick_get();
    if (play) {
        _started = music_audio_play_file(path.c_str(), _meta.audio_offset) == ESP_OK;
    } else {
        music_audio_stop();
    }
    _view.setPlaying(_started);
    updateText(true);
}

// Title line: the current lyric when there are lyrics, otherwise the song title
void MusicPlayer::updateText(bool force)
{
    if (_index < 0) {
        return;
    }
    const uint32_t position = _started ? music_audio_position_ms() : 0;
    _view.setTime(position, _meta.duration_ms);

    std::string by = _meta.artist;
    if (!_meta.album.empty()) {
        by += by.empty() ? _meta.album : "  ·  " + _meta.album;
    }
    if (_lyrics.empty()) {
        if (force) {
            _view.setTitle(_meta.title.c_str());
            _view.setSubtitle(by.empty() ? display_name(_tracks[_index]).c_str() : by.c_str());
        }
        return;
    }

    int line = -1;
    for (size_t i = 0; i < _lyrics.size() && _lyrics[i].time_ms <= position; i++) {
        line = (int)i;
    }
    if (force || line != _lyric_index) {
        _lyric_index = line;
        const bool has_line = line >= 0 && !_lyrics[line].text.empty();
        _view.setTitle(has_line ? _lyrics[line].text.c_str() : _meta.title.c_str());
        std::string subtitle = _meta.title;
        if (!_meta.artist.empty()) {
            subtitle += "  ·  " + _meta.artist;
        }
        _view.setSubtitle(subtitle.c_str());
    }
}

void MusicPlayer::playNext(bool by_user)
{
    if (_tracks.empty()) {
        return;
    }
    int next = _index + 1;
    if (_mode == PlayMode::SHUFFLE && _tracks.size() > 1) {
        do {
            next = (int)(esp_random() % _tracks.size());
        } while (next == _index);
    } else if (_mode == PlayMode::REPEAT_ONE && !by_user) {
        next = _index;
    }
    selectTrack(next, true);
}

void MusicPlayer::playPrevious(void)
{
    if (_started && music_audio_position_ms() > RESTART_THRESHOLD_MS) {
        selectTrack(_index, true);
    } else {
        selectTrack(_index - 1, true);
    }
}

void MusicPlayer::togglePlay(void)
{
    if (_index < 0) {
        return;
    }
    if (music_audio_is_playing()) {
        music_audio_pause();
        _view.setPlaying(false);
    } else if (_started) {
        _view.setPlaying(music_audio_resume() == ESP_OK);
    } else {
        selectTrack(_index, true);
    }
}

void MusicPlayer::updateModeIcon(void)
{
    _view.setModeSymbol(MODE_SYMBOLS[static_cast<int>(_mode)]);
}

void MusicPlayer::onAction(NowPlayingView::Action action)
{
    switch (action) {
    case NowPlayingView::Action::PREV:
        playPrevious();
        break;
    case NowPlayingView::Action::NEXT:
        playNext(true);
        break;
    case NowPlayingView::Action::PLAY_PAUSE:
        togglePlay();
        break;
    case NowPlayingView::Action::MODE:
        _mode = static_cast<PlayMode>((static_cast<int>(_mode) + 1) % static_cast<int>(PlayMode::MAX));
        updateModeIcon();
        break;
    case NowPlayingView::Action::LIST:
        openList();
        break;
    }
}

/* Track list: a full-screen panel over the player; tapping a song plays it */
void MusicPlayer::openList(void)
{
    if (_list_panel || _tracks.empty()) {
        return;
    }
    _list_panel = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(_list_panel);
    lv_obj_set_size(_list_panel, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_list_panel, lv_color_hex(BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_list_panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(_list_panel, LV_OBJ_FLAG_SCROLLABLE);
    // Taps outside the list must not reach the player underneath
    lv_obj_add_flag(_list_panel, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *title = lv_label_create(_list_panel);
    lv_label_set_text_fmt(title, LV_SYMBOL_AUDIO "  %u", (unsigned)_tracks.size());
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 44);

    lv_obj_t *list = lv_list_create(_list_panel);
    lv_obj_set_size(list, 340, 300);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 6);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(list, 4, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, app_font_text_20(), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *current = nullptr;
    for (size_t i = 0; i < _tracks.size(); i++) {
        lv_obj_t *btn = lv_list_add_button(list, ((int)i == _index) ? LV_SYMBOL_PLAY : nullptr,
                                           display_name(_tracks[i]).c_str());
        lv_obj_set_style_bg_color(btn, lv_color_hex(((int)i == _index) ? 0x2A2B3A : 0x16171F), LV_PART_MAIN);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x33354A), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(btn, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_user_data(btn, reinterpret_cast<void *>(i));
        lv_obj_add_event_cb(btn, list_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_t *label = lv_obj_get_child(btn, -1);
        lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
        if ((int)i == _index) {
            current = btn;
        }
    }
    if (current) {
        lv_obj_scroll_to_view(current, LV_ANIM_OFF);
    }

    lv_obj_t *close_btn = lv_button_create(_list_panel);
    lv_obj_set_size(close_btn, 56, 56);
    lv_obj_set_style_radius(close_btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x2A2B36), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(close_btn, 0, LV_PART_MAIN);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_set_user_data(close_btn, reinterpret_cast<void *>(SIZE_MAX));
    lv_obj_add_event_cb(close_btn, list_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_t *icon = lv_label_create(close_btn);
    lv_label_set_text(icon, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(icon);
}

void MusicPlayer::closeList(void)
{
    if (_list_panel) {
        lv_obj_delete_async(_list_panel);
        _list_panel = nullptr;
    }
}

void MusicPlayer::list_event_cb(lv_event_t *e)
{
    MusicPlayer *app = static_cast<MusicPlayer *>(lv_event_get_user_data(e));
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    const size_t index = reinterpret_cast<size_t>(lv_obj_get_user_data(target));
    app->closeList();
    if (index != SIZE_MAX) {
        app->selectTrack((int)index, true);
    }
}

void MusicPlayer::timer_cb(lv_timer_t *timer)
{
    MusicPlayer *app = static_cast<MusicPlayer *>(lv_timer_get_user_data(timer));
    if (music_audio_take_preempted()) {
        app->_view.setPlaying(false);
    }
    if (music_audio_take_finished() && app->_started) {
        app->playNext(false);
        return;
    }
    // A damaged or unsupported file keeps the decoder busy without producing audio
    const uint32_t position = music_audio_position_ms();
    if (!app->_started || !music_audio_is_playing() || position != app->_last_position) {
        app->_last_position = position;
        app->_stall_since = lv_tick_get();
    } else if (lv_tick_elaps(app->_stall_since) > STALL_TIMEOUT_MS) {
        ESP_UTILS_LOGW("No audio from %s, skipping it", app->_tracks[app->_index].c_str());
        app->playNext(true);
        return;
    }
    app->updateText(false);
}

bool MusicPlayer::back(void)
{
    ESP_UTILS_LOGD("Back");
    if (_list_panel) {
        closeList();
        return true;
    }
    ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    return true;
}

bool MusicPlayer::close(void)
{
    ESP_UTILS_LOGD("Close");
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    music_audio_stop();
    music_audio_deinit();
    _started = false;
    if (_list_panel != nullptr) {
        lv_obj_delete(_list_panel);
        _list_panel = nullptr;
    }
    lv_obj_t *view_root = _view.root();
    _view.destroy();
    if (view_root != nullptr && lv_obj_is_valid(view_root)) {
        lv_obj_delete(view_root);
    }
    storage_service_release(&_storage_lease);
    app_fonts_release();
    return true;
}

bool MusicPlayer::init(void)
{
    return true;
}

bool MusicPlayer::deinit(void)
{
    return true;
}

bool MusicPlayer::pause(void)
{
    return true;
}

bool MusicPlayer::resume(void)
{
    _view.setPlaying(music_audio_is_playing());
    updateText(true);
    return true;
}

} // namespace esp_brookesia::apps
