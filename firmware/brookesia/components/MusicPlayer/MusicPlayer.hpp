/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>
#include <vector>
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "storage_service.h"
#include "now_playing_view.hpp"
#include "music_meta.hpp"

namespace esp_brookesia::apps {

/**
 * @brief Player for MP3 and WAV files in flash (spiffs/music) and on the microSD card (music/)
 *
 * Shows ID3 titles and covers, follows .lrc lyrics next to the file, plays the list in
 * order, repeats one track or shuffles, with touch controls for playback and queue selection.
 */
class MusicPlayer: public systems::phone::App {
public:
    static MusicPlayer *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~MusicPlayer();

    using systems::phone::App::startRecordResource;
    using systems::phone::App::endRecordResource;

protected:
    MusicPlayer(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;
    bool deinit(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    enum class PlayMode {
        LOOP_ALL = 0,
        REPEAT_ONE,
        SHUFFLE,
        MAX,
    };

    static MusicPlayer *_instance;

    NowPlayingView _view;
    lv_obj_t *_list_panel = nullptr;
    lv_timer_t *_timer = nullptr;

    std::vector<std::string> _tracks;
    int _index = -1;
    bool _started = false;              // The current track has been sent to the decoder
    PlayMode _mode = PlayMode::LOOP_ALL;
    TrackMeta _meta;
    std::vector<LyricLine> _lyrics;
    int _lyric_index = -1;
    // Decoder watchdog: a track whose position stops moving is skipped
    uint32_t _last_position = 0;
    uint32_t _stall_since = 0;

    storage_service_lease_t _storage_lease{};

    void scanLibrary(void);
    void selectTrack(int index, bool play);
    void playNext(bool by_user);
    void playPrevious(void);
    void togglePlay(void);
    void showEmpty(void);
    void updateText(bool force);
    void updateModeIcon(void);
    void onAction(NowPlayingView::Action action);
    void openList(void);
    void closeList(void);

    static void timer_cb(lv_timer_t *timer);
    static void list_event_cb(lv_event_t *e);
};

} // namespace esp_brookesia::apps
