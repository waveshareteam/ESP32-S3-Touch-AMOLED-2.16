/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "esp_jpeg_dec.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "app_fonts.h"
#include "bsp/esp-bsp.h"
#include "now_playing_view.hpp"
// lv_image_cache_drop() is only declared in the private header in LVGL 9.5
#if __has_include("lvgl_private.h")
#   include "lvgl_private.h"
#else
#   include "src/lvgl_private.h"
#endif

static const char *TAG = "now_playing";

static constexpr uint32_t SPIN_PERIOD_MS = 50;
static constexpr int32_t SPIN_STEP = 6;             // 0.6 degree per frame, one turn in 30 s
static constexpr uint32_t VOLUME_SHOW_MS = 1500;
static constexpr int COVER_SIZE = 176;              // Multiple of 8 for the JPEG scaler
static constexpr int COVER_CENTER_Y = -68;          // Offset from the screen center
static constexpr uint32_t DEFAULT_ACCENT = 0x4F8BFF;
static constexpr uint32_t TEXT_DIM = 0xA0A3B1;

static lv_obj_t *create_plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *create_arc(lv_obj_t *parent, int size, int width, lv_color_t bg)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, size, size);
    lv_obj_center(arc);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 270);
    lv_arc_set_range(arc, 0, 1000);
    lv_arc_set_value(arc, 0);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, bg, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    return arc;
}

NowPlayingView::~NowPlayingView()
{
    destroy();
}

lv_obj_t *NowPlayingView::createButton(lv_obj_t *parent, const char *symbol, int size, Action action,
                                       const lv_font_t *font)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2B36), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_user_data(btn, reinterpret_cast<void *>(static_cast<uintptr_t>(action)));
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(icon);
    return btn;
}

void NowPlayingView::create(lv_obj_t *parent, ActionHandler handler, bool with_top_buttons)
{
    _handler = std::move(handler);
    _playing = false;

    _root = create_plain(parent);
    lv_obj_set_size(_root, lv_pct(100), lv_pct(100));

    // Track progress runs around the edge of the round panel
    _progress_arc = create_arc(_root, BSP_LCD_H_RES - 12, 6, lv_color_hex(0x22232D));

    lv_obj_t *frame = create_plain(_root);
    lv_obj_set_size(frame, COVER_SIZE, COVER_SIZE);
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, COVER_CENTER_Y);
    lv_obj_set_style_radius(frame, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_clip_corner(frame, true, LV_PART_MAIN);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0x1A1B24), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, LV_PART_MAIN);
    // Tapping the cover plays or pauses
    lv_obj_add_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(frame, reinterpret_cast<void *>(static_cast<uintptr_t>(Action::PLAY_PAUSE)));
    lv_obj_add_event_cb(frame, button_event_cb, LV_EVENT_CLICKED, this);

    // Placeholder record: grooves and a note in the middle
    for (int r = COVER_SIZE - 16; r > 60; r -= 22) {
        lv_obj_t *groove = create_plain(frame);
        lv_obj_set_size(groove, r, r);
        lv_obj_center(groove);
        lv_obj_set_style_radius(groove, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_border_width(groove, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(groove, lv_color_hex(0x2C2D3A), LV_PART_MAIN);
    }
    _disc_icon = lv_label_create(frame);
    lv_label_set_text(_disc_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(_disc_icon, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_center(_disc_icon);

    _cover_img = lv_image_create(frame);
    lv_obj_center(_cover_img);
    lv_obj_add_flag(_cover_img, LV_OBJ_FLAG_HIDDEN);

    // Title line: the song name, or the current lyric
    _title_label = lv_label_create(_root);
    lv_obj_set_width(_title_label, 340);
    lv_label_set_long_mode(_title_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(_title_label, app_font_text_30(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_align(_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(_title_label, LV_ALIGN_CENTER, 0, 52);
    lv_label_set_text(_title_label, "");

    _subtitle_label = lv_label_create(_root);
    lv_obj_set_width(_subtitle_label, 300);
    lv_label_set_long_mode(_subtitle_label, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lv_obj_set_style_text_font(_subtitle_label, app_font_text_20(), LV_PART_MAIN);
    lv_obj_set_style_text_color(_subtitle_label, lv_color_hex(TEXT_DIM), LV_PART_MAIN);
    lv_obj_set_style_text_align(_subtitle_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(_subtitle_label, LV_ALIGN_CENTER, 0, 90);
    lv_label_set_text(_subtitle_label, "");

    _time_label = lv_label_create(_root);
    lv_obj_set_style_text_font(_time_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(_time_label, lv_color_hex(TEXT_DIM), LV_PART_MAIN);
    lv_obj_align(_time_label, LV_ALIGN_CENTER, 0, 118);
    lv_label_set_text(_time_label, "");

    lv_obj_t *controls = create_plain(_root);
    lv_obj_set_size(controls, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(controls, 22, LV_PART_MAIN);
    // Above the bottom swipe area, still inside the circle
    lv_obj_align(controls, LV_ALIGN_CENTER, 0, 162);

    createButton(controls, LV_SYMBOL_PREV, 56, Action::PREV, &lv_font_montserrat_30);
    _play_btn = createButton(controls, LV_SYMBOL_PLAY, 72, Action::PLAY_PAUSE, &lv_font_montserrat_30);
    lv_obj_set_style_bg_opa(_play_btn, LV_OPA_COVER, LV_PART_MAIN);
    _play_icon = lv_obj_get_child(_play_btn, 0);
    createButton(controls, LV_SYMBOL_NEXT, 56, Action::NEXT, &lv_font_montserrat_30);

    if (with_top_buttons) {
        // Small buttons above the cover, where the circle is still wide enough
        lv_obj_t *top = create_plain(_root);
        lv_obj_set_size(top, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(top, 28, LV_PART_MAIN);
        lv_obj_align(top, LV_ALIGN_CENTER, 0, COVER_CENTER_Y - COVER_SIZE / 2 - 30);
        lv_obj_t *mode_btn = createButton(top, LV_SYMBOL_LOOP, 46, Action::MODE, &lv_font_montserrat_22);
        _mode_icon = lv_obj_get_child(mode_btn, 0);
        createButton(top, LV_SYMBOL_LIST, 46, Action::LIST, &lv_font_montserrat_22);
    }

    // Volume gauge inside the progress ring, shown briefly while the knob turns
    _volume_arc = create_arc(_root, BSP_LCD_H_RES - 40, 10, lv_color_hex(0x30313D));
    lv_arc_set_bg_angles(_volume_arc, 0, 270);
    lv_arc_set_rotation(_volume_arc, 135);
    lv_arc_set_range(_volume_arc, 0, 100);
    lv_obj_set_style_arc_color(_volume_arc, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_add_flag(_volume_arc, LV_OBJ_FLAG_HIDDEN);

    _volume_label = lv_label_create(_root);
    lv_obj_set_style_text_font(_volume_label, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(_volume_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_volume_label, lv_color_hex(0x22232D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_volume_label, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_radius(_volume_label, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(_volume_label, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_volume_label, 6, LV_PART_MAIN);
    lv_obj_align(_volume_label, LV_ALIGN_CENTER, 0, COVER_CENTER_Y);
    lv_obj_add_flag(_volume_label, LV_OBJ_FLAG_HIDDEN);

    setAccent(lv_color_hex(DEFAULT_ACCENT));

    _spin_timer = lv_timer_create(spin_timer_cb, SPIN_PERIOD_MS, this);
    _volume_hide_timer = lv_timer_create(volume_hide_timer_cb, VOLUME_SHOW_MS, this);
    lv_timer_pause(_volume_hide_timer);
}

void NowPlayingView::destroy()
{
    for (lv_timer_t **timer : {&_spin_timer, &_volume_hide_timer}) {
        if (*timer) {
            lv_timer_delete(*timer);
            *timer = nullptr;
        }
    }
    if (_cover_img) {
        lv_image_set_src(_cover_img, nullptr);
    }
    releaseCoverBuffer(0);
    releaseCoverBuffer(1);
    _root = _progress_arc = _volume_arc = _volume_label = _cover_img = _disc_icon = nullptr;
    _title_label = _subtitle_label = _time_label = _play_btn = _play_icon = _mode_icon = nullptr;
}

void NowPlayingView::setVisible(bool visible)
{
    if (visible) {
        lv_obj_remove_flag(_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
    }
}

void NowPlayingView::setTitle(const char *text)
{
    // Setting the same text would restart the scroll animation
    if (strcmp(lv_label_get_text(_title_label), text) != 0) {
        lv_label_set_text(_title_label, text);
    }
}

void NowPlayingView::setSubtitle(const char *text)
{
    if (strcmp(lv_label_get_text(_subtitle_label), text) != 0) {
        lv_label_set_text(_subtitle_label, text);
    }
}

static void format_time(char *buf, size_t len, uint32_t ms)
{
    const uint32_t s = ms / 1000;
    snprintf(buf, len, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

void NowPlayingView::setTime(uint32_t position_ms, uint32_t duration_ms)
{
    char pos[12], dur[12], text[28];
    format_time(pos, sizeof(pos), position_ms);
    if (duration_ms > 0) {
        format_time(dur, sizeof(dur), duration_ms);
        snprintf(text, sizeof(text), "%s / %s", pos, dur);
        lv_arc_set_value(_progress_arc, std::min<uint64_t>(1000, (uint64_t)position_ms * 1000 / duration_ms));
    } else {
        snprintf(text, sizeof(text), "%s", pos);
        lv_arc_set_value(_progress_arc, 0);
    }
    if (strcmp(lv_label_get_text(_time_label), text) != 0) {
        lv_label_set_text(_time_label, text);
    }
}

void NowPlayingView::setPlaying(bool playing)
{
    if (playing != _playing) {
        _playing = playing;
        lv_label_set_text(_play_icon, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
}

void NowPlayingView::setModeSymbol(const char *symbol)
{
    if (_mode_icon) {
        lv_label_set_text(_mode_icon, symbol);
    }
}

void NowPlayingView::showVolume(int percent)
{
    lv_arc_set_value(_volume_arc, percent);
    lv_label_set_text_fmt(_volume_label, LV_SYMBOL_VOLUME_MAX " %d%%", percent);
    lv_obj_remove_flag(_volume_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_volume_label, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(_volume_hide_timer);
    lv_timer_resume(_volume_hide_timer);
}

void NowPlayingView::setAccent(lv_color_t color)
{
    lv_obj_set_style_arc_color(_progress_arc, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_play_btn, color, LV_PART_MAIN);
    lv_obj_set_style_text_color(_disc_icon, color, LV_PART_MAIN);
}

void NowPlayingView::releaseCoverBuffer(int index)
{
    if (_cover_pixels[index]) {
        lv_image_cache_drop(&_cover_dsc[index]);
        heap_caps_free(_cover_pixels[index]);
        _cover_pixels[index] = nullptr;
    }
    memset(&_cover_dsc[index], 0, sizeof(_cover_dsc[index]));
}

void NowPlayingView::clearCover()
{
    lv_image_set_src(_cover_img, nullptr);
    lv_obj_add_flag(_cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_disc_icon, LV_OBJ_FLAG_HIDDEN);
    releaseCoverBuffer(0);
    releaseCoverBuffer(1);
    setAccent(lv_color_hex(DEFAULT_ACCENT));
}

// Decodes a JPEG to RGB565 at the cover size into one of the two buffers
bool NowPlayingView::decodeCover(const uint8_t *jpeg, size_t len, int index)
{
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t dec = nullptr;
    jpeg_dec_io_t io = {};
    jpeg_dec_header_info_t header = {};
    int out_len = 0;
    bool ok = false;

    io.inbuf = const_cast<uint8_t *>(jpeg);
    io.inbuf_len = (int)len;
    if (jpeg_dec_open(&config, &dec) != JPEG_ERR_OK || jpeg_dec_parse_header(dec, &io, &header) != JPEG_ERR_OK) {
        if (dec) {
            jpeg_dec_close(dec);
        }
        return false;
    }
    jpeg_dec_close(dec);
    dec = nullptr;

    // Scale down to the cover size when the image is large enough
    if (header.width >= COVER_SIZE && header.height >= COVER_SIZE) {
        config.scale.width = COVER_SIZE;
        config.scale.height = COVER_SIZE;
    }
    io = {};
    io.inbuf = const_cast<uint8_t *>(jpeg);
    io.inbuf_len = (int)len;
    uint8_t *pixels = nullptr;
    if (jpeg_dec_open(&config, &dec) == JPEG_ERR_OK && jpeg_dec_parse_header(dec, &io, &header) == JPEG_ERR_OK &&
            jpeg_dec_get_outbuf_len(dec, &out_len) == JPEG_ERR_OK && out_len > 0) {
        pixels = (uint8_t *)heap_caps_aligned_alloc(16, out_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        io.outbuf = pixels;
        ok = pixels && jpeg_dec_process(dec, &io) == JPEG_ERR_OK;
    }
    if (dec) {
        jpeg_dec_close(dec);
    }
    const int w = config.scale.width ? config.scale.width : header.width;
    const int h = config.scale.height ? config.scale.height : header.height;
    if (!ok || w <= 0 || h <= 0) {
        if (pixels) {
            heap_caps_free(pixels);
        }
        return false;
    }

    releaseCoverBuffer(index);
    _cover_pixels[index] = pixels;
    lv_image_dsc_t &dsc = _cover_dsc[index];
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.w = w;
    dsc.header.h = h;
    dsc.header.stride = w * 2;
    dsc.data_size = w * h * 2;
    dsc.data = pixels;
    return true;
}

bool NowPlayingView::setCoverJpeg(const uint8_t *jpeg, size_t len)
{
    const int next = _cover_index ^ 1;
    if (!jpeg || !len || !decodeCover(jpeg, len, next)) {
        ESP_LOGW(TAG, "Cover decode failed");
        return false;
    }

    const lv_image_dsc_t &dsc = _cover_dsc[next];
    const int w = dsc.header.w;
    const int h = dsc.header.h;
    lv_image_set_src(_cover_img, &dsc);
    lv_image_set_pivot(_cover_img, w / 2, h / 2);
    // Small images still fill the disc
    lv_image_set_scale(_cover_img, (w < COVER_SIZE) ? (256 * COVER_SIZE / w) : 256);
    lv_obj_center(_cover_img);
    lv_obj_remove_flag(_cover_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_disc_icon, LV_OBJ_FLAG_HIDDEN);
    releaseCoverBuffer(_cover_index);
    _cover_index = next;

    // Average color, brightened, keeps the ring and play button in tune with the cover
    const uint16_t *px = (const uint16_t *)dsc.data;
    uint32_t r = 0, g = 0, b = 0, n = 0;
    for (int i = 0; i < w * h; i += 7, n++) {
        r += (px[i] >> 11) & 0x1F;
        g += (px[i] >> 5) & 0x3F;
        b += px[i] & 0x1F;
    }
    lv_color_t avg = lv_color_make((r / n) << 3, (g / n) << 2, (b / n) << 3);
    lv_color_hsv_t hsv = lv_color_to_hsv(avg);
    hsv.s = std::max<uint8_t>(hsv.s, 55);
    hsv.v = std::max<uint8_t>(hsv.v, 85);
    setAccent(lv_color_hsv_to_rgb(hsv.h, hsv.s, hsv.v));
    ESP_LOGI(TAG, "Cover %dx%d", w, h);
    return true;
}

void NowPlayingView::button_event_cb(lv_event_t *e)
{
    NowPlayingView *view = static_cast<NowPlayingView *>(lv_event_get_user_data(e));
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    const Action action = static_cast<Action>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target)));
    if (view && view->_handler) {
        view->_handler(action);
    }
}

// The cover turns like a record while playing
void NowPlayingView::spin_timer_cb(lv_timer_t *timer)
{
    NowPlayingView *view = static_cast<NowPlayingView *>(lv_timer_get_user_data(timer));
    if (!view->_playing || !view->_cover_img || lv_obj_has_flag(view->_cover_img, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    view->_spin_angle = (view->_spin_angle + SPIN_STEP) % 3600;
    lv_image_set_rotation(view->_cover_img, view->_spin_angle);
}

void NowPlayingView::volume_hide_timer_cb(lv_timer_t *timer)
{
    NowPlayingView *view = static_cast<NowPlayingView *>(lv_timer_get_user_data(timer));
    lv_timer_pause(timer);
    lv_obj_add_flag(view->_volume_arc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(view->_volume_label, LV_OBJ_FLAG_HIDDEN);
}
