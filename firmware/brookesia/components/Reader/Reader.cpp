/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "lvgl.h"
#include "esp_brookesia.hpp"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:Reader"
#include "esp_lib_utils.h"
#include "nvs.h"
#include "esp_heap_caps.h"
#include "app_fonts.h"
#include "Reader.hpp"

#define APP_NAME "Reader"

LV_IMG_DECLARE(img_app_reader);

// This board has no rotary encoder or removable-card event channel.  The
// Reader owns one storage-service lease while it is open; touch controls
// already cover every reader interaction.
static constexpr char STORAGE_DIR_BOOKS[] = STORAGE_SERVICE_MOUNT_POINT "/books";
static constexpr char STORAGE_DIR_FONTS[] = STORAGE_SERVICE_MOUNT_POINT "/fonts";
static bool storage_sd_is_mounted(void) { storage_service_info_t info = {}; return storage_service_get_info(&info) == ESP_OK && info.state == STORAGE_SERVICE_STATE_MOUNTED; }

namespace esp_brookesia::apps {

static constexpr int SCREEN = 480;
static constexpr int TEXT_TOP = 56;
static constexpr int TEXT_BOTTOM = SCREEN - 54;
static constexpr int TEXT_MARGIN = 20;
static constexpr int TAP_SIDE = SCREEN * 3 / 10;     // Taps this close to either side turn the page
static constexpr int SCAN_DEPTH = 2;
static constexpr size_t MAX_BOOKS = 500;
static constexpr size_t MAX_HISTORY = 4000;
static constexpr uint32_t TIMER_MS = 200;
static constexpr uint32_t SAVE_DELAY_MS = 2000;
static constexpr uint32_t JUMP_DELAY_MS = 400;
static constexpr uint32_t JUMP_LINE_BACK = 2048;     // A jump starts at a line start this close
static constexpr size_t FALLBACK_MAX_SIZE = 16 * 1024 * 1024;
static constexpr size_t FALLBACK_CHUNK = 64 * 1024;  // The card is free for page turns between chunks
static constexpr uint32_t FALLBACK_GLYPH_CACHE = 128;
static constexpr uint32_t FALLBACK_STOP_WAIT_MS = 3000;
static constexpr uint32_t TEXT_COLOR = 0xE9E4D8;
static constexpr uint32_t ACCENT_COLOR = 0xF2A33A;
static constexpr uint32_t HINT_COLOR = 0x8E929E;
static constexpr uint32_t CARD_COLOR = 0x1D1D25;
static constexpr uint32_t BUTTON_COLOR = 0x2C2C38;
static const char *NVS_NAMESPACE = "reader";
static const char *NVS_KEY_SIZE = "size";
static const char *NVS_KEY_LAST = "last";

/* Where a book was left, stored per book under a hash of its path */
struct SavedPosition {
    uint32_t file_size;     // A different size means the file changed; it starts over
    uint32_t pos;
    uint16_t per_mille;
    uint16_t reserved;
};

static const lv_font_t *text_font(int size)
{
    return size == 0 ? app_font_text_20() : app_font_text_30();
}

static int text_pixels(int size)
{
    return size == 0 ? 20 : 30;
}

static void position_key(const std::string &path, char *key, size_t key_size)
{
    uint32_t hash = 2166136261u;    // FNV-1a
    for (unsigned char c : path) {
        hash = (hash ^ c) * 16777619u;
    }
    snprintf(key, key_size, "b%08lx", (unsigned long)hash);
}

static bool load_position(const std::string &path, SavedPosition *saved)
{
    char key[16];
    position_key(path, key, sizeof(key));
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t size = sizeof(*saved);
    const bool ok = nvs_get_blob(nvs, key, saved, &size) == ESP_OK && size == sizeof(*saved);
    nvs_close(nvs);
    return ok;
}

static void store_position(const std::string &path, const SavedPosition &saved)
{
    char key[16];
    position_key(path, key, sizeof(key));
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_blob(nvs, key, &saved, sizeof(saved));
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

static int load_text_size(void)
{
    nvs_handle_t nvs;
    uint8_t value = 1;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, NVS_KEY_SIZE, &value);
        nvs_close(nvs);
    }
    return value > 1 ? 1 : value;
}

static void store_text_size(int size)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_SIZE, (uint8_t)size);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

/* The book open when the app last closed, which opens again with the app */
static std::string load_last_book(void)
{
    nvs_handle_t nvs;
    std::string path;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        size_t len = 0;
        if (nvs_get_str(nvs, NVS_KEY_LAST, nullptr, &len) == ESP_OK && len > 1) {
            path.resize(len);
            nvs_get_str(nvs, NVS_KEY_LAST, path.data(), &len);
            path.resize(len - 1);
        }
        nvs_close(nvs);
    }
    return path;
}

static void store_last_book(const std::string &path)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        if (path.empty()) {
            nvs_erase_key(nvs, NVS_KEY_LAST);
        } else {
            nvs_set_str(nvs, NVS_KEY_LAST, path.c_str());
        }
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

/* The first TrueType font in the fonts folder, empty when there is none */
static std::string find_fallback_font(void)
{
    DIR *d = opendir(STORAGE_DIR_FONTS);
    if (!d) {
        return std::string();
    }
    std::string path;
    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        const char *dot = strrchr(entry->d_name, '.');
        if (entry->d_name[0] != '.' && entry->d_type != DT_DIR && dot && strcasecmp(dot, ".ttf") == 0) {
            path = std::string(STORAGE_DIR_FONTS) + "/" + entry->d_name;
            break;
        }
    }
    closedir(d);
    return path;
}

static bool is_book(const char *name)
{
    const char *dot = strrchr(name, '.');
    return name[0] != '.' && dot && strcasecmp(dot, ".txt") == 0;
}

static void scan_dir(const std::string &dir, const std::string &folder, int depth, std::vector<std::string> &paths,
                     std::vector<std::string> &folders)
{
    DIR *d = opendir(dir.c_str());
    if (!d) {
        return;
    }
    std::vector<std::string> subdirs;
    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr && paths.size() < MAX_BOOKS) {
        if (entry->d_name[0] == '.') {
            continue;   // Hidden files, including the "._" companions macOS leaves on cards
        }
        if (entry->d_type == DT_DIR) {
            if (depth < SCAN_DEPTH && strcasecmp(entry->d_name, "System Volume Information") != 0) {
                subdirs.push_back(entry->d_name);
            }
        } else if (is_book(entry->d_name)) {
            paths.push_back(dir + "/" + entry->d_name);
            folders.push_back(folder);
        }
    }
    closedir(d);
    for (const std::string &sub : subdirs) {
        scan_dir(dir + "/" + sub, folder.empty() ? sub : folder + "/" + sub, depth + 1, paths, folders);
    }
}

static lv_obj_t *create_plain(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    return label;
}

static lv_obj_t *create_button(lv_obj_t *parent, int width, int height)
{
    lv_obj_t *button = create_plain(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(button, 18, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(BUTTON_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x40404F), LV_STATE_PRESSED);
    return button;
}

Reader *Reader::_instance = nullptr;

Reader *Reader::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) {
        _instance = new Reader(use_status_bar, use_navigation_bar);
    }
    return _instance;
}

Reader::Reader(bool use_status_bar, bool use_navigation_bar) :
    App(APP_NAME, &img_app_reader, true, use_status_bar, use_navigation_bar)
{
}

Reader::~Reader()
{
}

bool Reader::init(void)
{
    _file_lock = xSemaphoreCreateMutex();
    _fallback_done = xSemaphoreCreateBinary();
    ESP_UTILS_CHECK_FALSE_RETURN(_file_lock && _fallback_done, false, "Create locks failed");
    xSemaphoreGive(_fallback_done);
    return true;
}

bool Reader::run(void)
{
    ESP_UTILS_LOGD("Run");
    app_fonts_acquire();
    _text_size = load_text_size();
    _book = -1;
    _focus = 0;
    _history.clear();
    _unsaved = false;
    _jump_target = -1;
    _card_changed = false;
    if (storage_service_acquire(&_storage_lease) != ESP_OK) {
        buildUi();
        showCardState();
        return true;
    }
    buildUi();
    applyTextSize();
    _timer = lv_timer_create(timer_cb, TIMER_MS, this);

    if (!storage_sd_is_mounted()) {
        showCardState();
        return true;
    }
    scanBooks();
    // Open again on the book that was open when the app closed
    const std::string last = load_last_book();
    for (size_t i = 0; i < _books.size(); i++) {
        if (_books[i].path == last && openBook(static_cast<int>(i))) {
            return true;
        }
    }
    showShelf();
    return true;
}

/* ------------------------------------------------------------------------------------------ */
/* UI                                                                                         */
/* ------------------------------------------------------------------------------------------ */

void Reader::buildUi(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    buildShelf();
    buildReading();

    _message = create_plain(screen);
    lv_obj_set_size(_message, 320, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(_message, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_message, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(_message, 10, LV_PART_MAIN);
    lv_obj_center(_message);
    lv_obj_add_flag(_message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_message, message_event_cb, LV_EVENT_CLICKED, this);
    _message_icon = create_label(_message, &lv_font_montserrat_44, ACCENT_COLOR);
    _message_title = create_label(_message, &lv_font_montserrat_22, 0xFFFFFF);
    _message_hint = create_label(_message, app_font_text_20(), HINT_COLOR);
    lv_obj_set_width(_message_hint, 300);
    lv_label_set_long_mode(_message_hint, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(_message_hint, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    buildMenu();
}

void Reader::buildShelf(void)
{
    // A list that scrolls entries to the middle of the round screen
    _shelf = create_plain(lv_scr_act());
    lv_obj_set_size(_shelf, SCREEN, SCREEN);
    lv_obj_center(_shelf);
    lv_obj_add_flag(_shelf, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_shelf, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_shelf, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(_shelf, LV_SCROLL_SNAP_CENTER);
    lv_obj_set_flex_flow(_shelf, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_shelf, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(_shelf, 56, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_shelf, SCREEN / 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_shelf, 12, LV_PART_MAIN);
    lv_obj_add_flag(_shelf, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *header = create_label(_shelf, &lv_font_montserrat_28, 0xFFFFFF);
    lv_label_set_text(header, "Books");
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SNAPPABLE);
    lv_obj_set_style_margin_bottom(header, 6, LV_PART_MAIN);
}

void Reader::buildReading(void)
{
    _reading = create_plain(lv_scr_act());
    lv_obj_set_size(_reading, SCREEN, SCREEN);
    lv_obj_center(_reading);
    lv_obj_add_flag(_reading, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(_reading, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(_reading, reading_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(_reading, reading_event_cb, LV_EVENT_GESTURE, this);
    lv_obj_add_flag(_reading, LV_OBJ_FLAG_HIDDEN);

    _title = create_label(_reading, app_font_text_20(), HINT_COLOR);
    lv_obj_set_width(_title, 220);
    lv_obj_set_style_max_height(_title, lv_font_get_line_height(app_font_text_20()), LV_PART_MAIN);
    lv_label_set_long_mode(_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(_title, LV_ALIGN_TOP_MID, 0, 22);

    _position = create_label(_reading, &lv_font_montserrat_16, HINT_COLOR);
    lv_obj_align(_position, LV_ALIGN_BOTTOM_MID, 0, -24);
}

void Reader::buildMenu(void)
{
    _menu = create_plain(lv_scr_act());
    lv_obj_set_size(_menu, SCREEN, SCREEN);
    lv_obj_center(_menu);
    lv_obj_set_style_bg_color(_menu, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_menu, LV_OPA_70, LV_PART_MAIN);
    lv_obj_add_flag(_menu, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_menu, menu_event_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(_menu, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = create_plain(_menu);
    lv_obj_set_size(panel, 336, LV_SIZE_CONTENT);
    lv_obj_center(panel);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);  // Taps on the panel don't close the menu
    lv_obj_set_style_radius(panel, 32, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(panel, lv_color_hex(CARD_COLOR), LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, 22, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _menu_title = create_label(panel, app_font_text_20(), 0xFFFFFF);
    lv_obj_set_width(_menu_title, 280);
    lv_obj_set_style_max_height(_menu_title, lv_font_get_line_height(app_font_text_20()), LV_PART_MAIN);
    lv_label_set_long_mode(_menu_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(_menu_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    _menu_info = create_label(panel, app_font_text_20(), HINT_COLOR);

    _slider_label = create_label(panel, &lv_font_montserrat_22, 0xFFFFFF);
    _slider = lv_slider_create(panel);
    lv_obj_set_width(_slider, 260);
    lv_slider_set_range(_slider, 0, 1000);
    lv_obj_set_style_bg_color(_slider, lv_color_hex(0x3A3A48), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_slider, lv_color_hex(ACCENT_COLOR), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(_slider, lv_color_hex(ACCENT_COLOR), LV_PART_KNOB);
    lv_obj_set_style_margin_bottom(_slider, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(_slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, this);
    lv_obj_add_event_cb(_slider, slider_event_cb, LV_EVENT_RELEASED, this);

    // Text size: a small and a large "A"
    lv_obj_t *sizes = create_plain(panel);
    lv_obj_set_size(sizes, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(sizes, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(sizes, 16, LV_PART_MAIN);
    for (int i = 0; i < 2; i++) {
        _size_buttons[i] = create_button(sizes, 84, 52);
        lv_obj_set_user_data(_size_buttons[i], reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(_size_buttons[i], size_event_cb, LV_EVENT_CLICKED, this);
        lv_obj_t *label = create_label(_size_buttons[i], i == 0 ? &lv_font_montserrat_18 : &lv_font_montserrat_28,
                                       0xFFFFFF);
        lv_label_set_text(label, "A");
        lv_obj_center(label);
    }

    lv_obj_t *shelf = create_button(panel, 200, 52);
    lv_obj_add_event_cb(shelf, shelf_button_cb, LV_EVENT_CLICKED, this);
    lv_obj_t *label = create_label(shelf, &lv_font_montserrat_18, 0xFFFFFF);
    lv_label_set_text(label, LV_SYMBOL_LIST "  Bookshelf");
    lv_obj_center(label);
}

void Reader::applyTextSize(void)
{
    for (lv_obj_t *line : _lines) {
        lv_obj_delete(line);
    }
    _lines.clear();
    // The fallback is made for one size, so it is made again for the new one
    if (_fallback_font) {
        lv_tiny_ttf_destroy(_fallback_font);
        _fallback_font = nullptr;
    }
    _text_font = *text_font(_text_size);
    useFallbackFont();
    _layout.configure(&_text_font, SCREEN, TEXT_TOP, TEXT_BOTTOM, TEXT_MARGIN);
    for (size_t i = 0; i < _layout.maxLines(); i++) {
        lv_obj_t *line = create_label(_reading, &_text_font, TEXT_COLOR);
        lv_label_set_text(line, "");
        lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
        _lines.push_back(line);
    }
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(_size_buttons[i], lv_color_hex(i == _text_size ? ACCENT_COLOR : BUTTON_COLOR),
                                  LV_PART_MAIN);
    }
}

/* Starts reading the TTF from the card unless it is there already */
void Reader::loadFallbackFont(void)
{
    if (_fallback_state != FALLBACK_NONE || xSemaphoreTake(_fallback_done, 0) != pdTRUE) {
        return;
    }
    _fallback_state = FALLBACK_LOADING;
    _fallback_stop = false;
    if (xTaskCreate(fallbackLoaderTask, "reader_font", 4096, this, 1, nullptr) != pdPASS) {
        _fallback_state = FALLBACK_NONE;
        xSemaphoreGive(_fallback_done);
    }
}

/* Hands missing characters to the TTF once it is in memory */
void Reader::useFallbackFont(void)
{
    const lv_font_t *builtin = text_font(_text_size);
    if (_fallback_state == FALLBACK_READY && !_fallback_font) {
        _fallback_font = lv_tiny_ttf_create_data_ex(_fallback_data, _fallback_size, text_pixels(_text_size),
                                                    LV_FONT_KERNING_NONE, FALLBACK_GLYPH_CACHE);
        if (_fallback_font) {
            _fallback_font->fallback = builtin->fallback;
        }
    }
    _text_font.fallback = _fallback_font ? _fallback_font : builtin->fallback;
}

void Reader::dropFallbackFont(void)
{
    _fallback_stop = true;
    if (xSemaphoreTake(_fallback_done, pdMS_TO_TICKS(FALLBACK_STOP_WAIT_MS)) == pdTRUE) {
        xSemaphoreGive(_fallback_done);
    }
    _text_font.fallback = text_font(_text_size)->fallback;
    if (_fallback_font) {
        lv_tiny_ttf_destroy(_fallback_font);
        _fallback_font = nullptr;
    }
    heap_caps_free(_fallback_data);
    _fallback_data = nullptr;
    _fallback_size = 0;
    if (_fallback_state != FALLBACK_ABSENT) {
        _fallback_state = FALLBACK_NONE;
    }
}

void Reader::fallbackLoaderTask(void *arg)
{
    Reader *app = static_cast<Reader *>(arg);
    uint8_t *data = nullptr;
    size_t size = 0;
    bool ok = false;
    bool absent = false;

    xSemaphoreTake(app->_file_lock, portMAX_DELAY);
    const std::string path = storage_sd_is_mounted() ? find_fallback_font() : std::string();
    struct stat st;
    if (!path.empty() && (app->_fallback_fd = open(path.c_str(), O_RDONLY)) >= 0 && fstat(app->_fallback_fd, &st) == 0 &&
            st.st_size > 0 && (size_t)st.st_size <= FALLBACK_MAX_SIZE) {
        size = st.st_size;
        data = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
    absent = path.empty() || app->_fallback_fd < 0;
    xSemaphoreGive(app->_file_lock);

    size_t done = 0;
    while (data && done < size && !app->_fallback_stop) {
        xSemaphoreTake(app->_file_lock, portMAX_DELAY);
        // The storage callback closes the file when the card goes away
        const ssize_t got = app->_fallback_fd >= 0 ? read(app->_fallback_fd, data + done, std::min(FALLBACK_CHUNK, size - done)) : -1;
        xSemaphoreGive(app->_file_lock);
        if (got <= 0) {
            break;
        }
        done += got;
        // Let a page waiting for the card in: on the other core it would not get the lock
        // before this loop takes it again, and would wait for the whole font
        vTaskDelay(1);
    }
    ok = data && done == size;
    xSemaphoreTake(app->_file_lock, portMAX_DELAY);
    if (app->_fallback_fd >= 0) {
        ::close(app->_fallback_fd);
        app->_fallback_fd = -1;
    }
    xSemaphoreGive(app->_file_lock);

    if (ok && !app->_fallback_stop) {
        ESP_UTILS_LOGI("Fallback font %s, %u KB", path.c_str(), (unsigned)(size / 1024));
        app->_fallback_data = data;
        app->_fallback_size = size;
        app->_fallback_state = FALLBACK_READY;
    } else {
        heap_caps_free(data);
        app->_fallback_state = (absent || (!data && !path.empty())) ? FALLBACK_ABSENT : FALLBACK_NONE;
    }
    xSemaphoreGive(app->_fallback_done);
    vTaskDelete(nullptr);
}

void Reader::showMessage(const char *icon, const char *title, const char *hint)
{
    lv_label_set_text(_message_icon, icon);
    lv_label_set_text(_message_title, title);
    lv_label_set_text(_message_hint, hint ? hint : "");
    lv_obj_remove_flag(_message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_message, LV_OBJ_FLAG_CLICKABLE);
    _notice = false;
}

void Reader::hideMessage(void)
{
    lv_obj_add_flag(_message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(_message, LV_OBJ_FLAG_CLICKABLE);
    _notice = false;
}

void Reader::showNotice(const char *icon, const char *title, const char *hint)
{
    lv_obj_add_flag(_shelf, LV_OBJ_FLAG_HIDDEN);
    showMessage(icon, title, hint);
    lv_obj_add_flag(_message, LV_OBJ_FLAG_CLICKABLE);
    _notice = true;
}

void Reader::showCardState(void)
{
    lv_obj_add_flag(_shelf, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_reading, LV_OBJ_FLAG_HIDDEN);
    showMessage(LV_SYMBOL_SD_CARD, "No microSD card", "Insert a FAT32 card with TXT files in /sdcard/books.");
}

/* ------------------------------------------------------------------------------------------ */
/* Shelf                                                                                      */
/* ------------------------------------------------------------------------------------------ */

void Reader::scanBooks(void)
{
    std::vector<std::string> paths;
    std::vector<std::string> folders;
    xSemaphoreTake(_file_lock, portMAX_DELAY);
    if (storage_sd_is_mounted()) {
        scan_dir(STORAGE_DIR_BOOKS, "", 0, paths, folders);
    }
    xSemaphoreGive(_file_lock);

    _books.clear();
    for (size_t i = 0; i < paths.size(); i++) {
        const size_t slash = paths[i].rfind('/');
        std::string title = paths[i].substr(slash + 1);
        title.resize(title.size() - 4);     // ".txt"
        _books.push_back({paths[i], title, folders[i]});
    }
    // Books at the top of the folder first, then each folder in name order
    std::sort(_books.begin(), _books.end(), [](const Book & a, const Book & b) {
        if (a.folder.empty() != b.folder.empty()) {
            return a.folder.empty();
        }
        const int folder = strcasecmp(a.folder.c_str(), b.folder.c_str());
        return folder != 0 ? folder < 0 : strcasecmp(a.title.c_str(), b.title.c_str()) < 0;
    });
    ESP_UTILS_LOGI("Found %d books", (int)_books.size());

    for (lv_obj_t *item : _items) {
        lv_obj_delete(item);
    }
    _items.clear();
    for (size_t i = 0; i < _books.size(); i++) {
        lv_obj_t *item = create_plain(_shelf);
        lv_obj_set_size(item, 320, LV_SIZE_CONTENT);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SNAPPABLE);
        lv_obj_set_style_radius(item, 20, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(item, lv_color_hex(CARD_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_color(item, lv_color_hex(0x30303C), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(item, lv_color_hex(ACCENT_COLOR), LV_PART_MAIN);
        lv_obj_set_style_border_width(item, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(item, 20, LV_PART_MAIN);
        lv_obj_set_style_pad_ver(item, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_row(item, 2, LV_PART_MAIN);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_user_data(item, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_add_event_cb(item, item_event_cb, LV_EVENT_CLICKED, this);

        lv_obj_t *title = create_label(item, app_font_text_20(), 0xFFFFFF);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_max_height(title, 2 * lv_font_get_line_height(app_font_text_20()), LV_PART_MAIN);
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(title, _books[i].title.c_str());
        lv_obj_t *detail = create_label(item, app_font_text_20(), HINT_COLOR);
        lv_obj_set_width(detail, lv_pct(100));
        lv_label_set_long_mode(detail, LV_LABEL_LONG_MODE_DOTS);
        _items.push_back(item);
    }
    _focus = std::min(_focus, std::max(0, (int)_items.size() - 1));
}

void Reader::showShelf(void)
{
    lv_obj_add_flag(_reading, LV_OBJ_FLAG_HIDDEN);
    if (!storage_sd_is_mounted()) {
        showCardState();
        return;
    }
    if (_books.empty()) {
        lv_obj_add_flag(_shelf, LV_OBJ_FLAG_HIDDEN);
        showMessage(LV_SYMBOL_FILE, "No books", "Copy TXT files to the books folder on the microSD card.");
        return;
    }
    hideMessage();
    // How far each book was read
    for (size_t i = 0; i < _books.size(); i++) {
        SavedPosition saved = {};
        char text[96];
        const char *state;
        char percent[12];
        if (!load_position(_books[i].path, &saved)) {
            state = "New";
        } else if (saved.per_mille >= 1000) {
            state = "Finished";
        } else {
            snprintf(percent, sizeof(percent), "%u%%", saved.per_mille / 10);
            state = percent;
        }
        if (_books[i].folder.empty()) {
            snprintf(text, sizeof(text), "%s", state);
        } else {
            snprintf(text, sizeof(text), "%s  ·  %s", _books[i].folder.c_str(), state);
        }
        lv_label_set_text(lv_obj_get_child(_items[i], 1), text);
    }
    lv_obj_remove_flag(_shelf, LV_OBJ_FLAG_HIDDEN);
    focusItem(_focus, false);
}

void Reader::focusItem(int index, bool animate)
{
    if (_items.empty()) {
        return;
    }
    index = std::clamp(index, 0, (int)_items.size() - 1);
    lv_obj_set_style_border_width(_items[std::min(_focus, (int)_items.size() - 1)], 0, LV_PART_MAIN);
    _focus = index;
    lv_obj_set_style_border_width(_items[_focus], 2, LV_PART_MAIN);
    lv_obj_update_layout(_shelf);
    lv_obj_scroll_to_view(_items[_focus], animate ? LV_ANIM_ON : LV_ANIM_OFF);
}

/* ------------------------------------------------------------------------------------------ */
/* Reading                                                                                    */
/* ------------------------------------------------------------------------------------------ */

bool Reader::openBook(int index)
{
    const Book &book = _books[index];
    text_source_t *src = nullptr;
    saveProgress();
    xSemaphoreTake(_file_lock, portMAX_DELAY);
    text_source_close(_src);
    const esp_err_t ret = storage_sd_is_mounted() ? text_source_open(book.path.c_str(), &src) : ESP_ERR_INVALID_STATE;
    _src = (ret == ESP_OK) ? src : nullptr;
    if (_src) {
        _book_begin = text_source_begin(_src);
        _book_end = text_source_end(_src);
        ESP_UTILS_LOGI("Open %s: %s, %lu bytes", book.path.c_str(), text_encoding_name(text_source_encoding(_src)),
                       (unsigned long)_book_end);
    }
    xSemaphoreGive(_file_lock);
    if (ret != ESP_OK) {
        ESP_UTILS_LOGW("Open %s failed (%s)", book.path.c_str(), esp_err_to_name(ret));
        if (!storage_sd_is_mounted()) {
            showCardState();
        } else {
            showNotice(LV_SYMBOL_WARNING, ret == ESP_ERR_NOT_SUPPORTED ? "Not a text file" : "Can't open this book",
                       ret == ESP_ERR_NOT_SUPPORTED ? "Only plain TXT files can be read." : "Check the microSD card.");
        }
        return false;
    }

    _book = index;
    _focus = index;
    _history.clear();
    _jump_target = -1;
    uint32_t start = _book_begin;
    SavedPosition saved = {};
    if (load_position(book.path, &saved) && saved.file_size == _book_end && saved.pos > _book_begin &&
            saved.pos < _book_end) {
        start = saved.pos;
    }
    store_last_book(book.path);

    hideMessage();
    lv_obj_add_flag(_shelf, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(_title, book.title.c_str());
    lv_obj_remove_flag(_reading, LV_OBJ_FLAG_HIDDEN);
    showPage(start);
    // Characters the built-in font lacks come from the card's font once it is read
    loadFallbackFont();
    return true;
}

void Reader::closeBook(void)
{
    saveProgress();
    xSemaphoreTake(_file_lock, portMAX_DELAY);
    text_source_close(_src);
    _src = nullptr;
    xSemaphoreGive(_file_lock);
    _book = -1;
    _history.clear();
    store_last_book("");
    closeMenu();
    showShelf();
}

void Reader::renderPage(void)
{
    for (size_t i = 0; i < _lines.size(); i++) {
        if (i < _page.lines.size()) {
            lv_label_set_text(_lines[i], _page.lines[i].text.c_str());
            lv_obj_set_pos(_lines[i], _page.lines[i].x, _page.lines[i].y);
            lv_obj_remove_flag(_lines[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_lines[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (_book_end <= _book_begin) {
        showMessage(LV_SYMBOL_FILE, "Empty book", nullptr);
    } else {
        hideMessage();
    }
    updatePosition();
}

void Reader::showPage(uint32_t start)
{
    xSemaphoreTake(_file_lock, portMAX_DELAY);
    if (_src) {
        _layout.layout(_src, start, _page);
    }
    xSemaphoreGive(_file_lock);
    renderPage();
}

void Reader::turn(int pages)
{
    bool moved = false;
    xSemaphoreTake(_file_lock, portMAX_DELAY);
    for (int i = 0; i < std::abs(pages) && _src; i++) {
        if (pages > 0) {
            if (_page.last || _page.end <= _page.start) {
                break;
            }
            if (_history.size() >= MAX_HISTORY) {
                _history.erase(_history.begin());
            }
            _history.push_back(_page.start);
            _layout.layout(_src, _page.end, _page);
        } else {
            if (_page.start <= _book_begin) {
                break;
            }
            const uint32_t end = _page.start;
            uint32_t start;
            if (!_history.empty()) {
                start = _history.back();
                _history.pop_back();
            } else {
                start = _layout.previousStart(_src, end);
            }
            // Ends where the page after it starts, even when pages were laid out differently
            _layout.layout(_src, start, _page, end);
        }
        moved = true;
    }
    xSemaphoreGive(_file_lock);
    if (moved) {
        renderPage();
        _unsaved = true;
        _turned_at = lv_tick_get();
    }
}

void Reader::jumpTo(int per_mille)
{
    xSemaphoreTake(_file_lock, portMAX_DELAY);
    if (!_src) {
        xSemaphoreGive(_file_lock);
        return;
    }
    uint32_t start = _book_begin;
    if (per_mille >= 1000) {
        start = _layout.previousStart(_src, _book_end);     // The last page
    } else if (per_mille > 0) {
        const uint32_t target = _book_begin + (uint32_t)((uint64_t)(_book_end - _book_begin) * per_mille / 1000);
        bool found = false;
        start = text_source_line_start(_src, target, JUMP_LINE_BACK, &found);
        if (!found) {
            start = text_source_align(_src, target);
        }
    }
    _history.clear();
    _layout.layout(_src, start, _page);
    xSemaphoreGive(_file_lock);
    renderPage();
    _unsaved = true;
    _turned_at = lv_tick_get();
}

int Reader::perMille(void) const
{
    if (_page.last || _book_end <= _book_begin) {
        return 1000;
    }
    return (int)((uint64_t)(_page.start - _book_begin) * 1000 / (_book_end - _book_begin));
}

void Reader::updatePosition(void)
{
    char text[16];
    snprintf(text, sizeof(text), "%d%%", perMille() / 10);
    lv_label_set_text(_position, text);
}

void Reader::saveProgress(void)
{
    if (_book < 0 || !_unsaved) {
        return;
    }
    store_position(_books[_book].path, {_book_end, _page.start, static_cast<uint16_t>(perMille()), 0});
    _unsaved = false;
}

/* ------------------------------------------------------------------------------------------ */
/* Menu                                                                                       */
/* ------------------------------------------------------------------------------------------ */

bool Reader::menuOpen(void) const
{
    return _menu && !lv_obj_has_flag(_menu, LV_OBJ_FLAG_HIDDEN);
}

void Reader::openMenu(void)
{
    if (_book < 0) {
        return;
    }
    char info[48];
    const char *encoding = "";
    xSemaphoreTake(_file_lock, portMAX_DELAY);
    if (_src) {
        encoding = text_encoding_name(text_source_encoding(_src));
    }
    xSemaphoreGive(_file_lock);
    if (_book_end >= 1024 * 1024) {
        snprintf(info, sizeof(info), "%s  ·  %.1f MB", encoding, _book_end / (1024.0 * 1024.0));
    } else {
        snprintf(info, sizeof(info), "%s  ·  %lu KB", encoding, (unsigned long)((_book_end + 1023) / 1024));
    }
    lv_label_set_text(_menu_title, _books[_book].title.c_str());
    lv_label_set_text(_menu_info, info);
    lv_slider_set_value(_slider, perMille(), LV_ANIM_OFF);
    updateSlider(perMille());
    lv_obj_remove_flag(_menu, LV_OBJ_FLAG_HIDDEN);
}

void Reader::closeMenu(void)
{
    if (!menuOpen()) {
        return;
    }
    lv_obj_add_flag(_menu, LV_OBJ_FLAG_HIDDEN);
    if (_jump_target >= 0) {
        jumpTo(_jump_target);
        _jump_target = -1;
    }
}

void Reader::updateSlider(int per_mille)
{
    char text[16];
    snprintf(text, sizeof(text), "%d%%", per_mille / 10);
    lv_label_set_text(_slider_label, text);
}

/* ------------------------------------------------------------------------------------------ */
/* Events                                                                                     */
/* ------------------------------------------------------------------------------------------ */

void Reader::handleCardChange(void)
{
    if (!storage_sd_is_mounted()) {
        // The storage callback has closed the book already
        saveProgress();
        if (menuOpen()) {
            lv_obj_add_flag(_menu, LV_OBJ_FLAG_HIDDEN);
            _jump_target = -1;
        }
        showCardState();
        return;
    }
    const std::string open_path = _book >= 0 ? _books[_book].path : std::string();
    _book = -1;
    scanBooks();
    for (size_t i = 0; i < _books.size(); i++) {
        if (!open_path.empty() && _books[i].path == open_path && openBook(static_cast<int>(i))) {
            return;
        }
    }
    showShelf();
}

void Reader::timer_cb(lv_timer_t *timer)
{
    Reader *app = static_cast<Reader *>(lv_timer_get_user_data(timer));
    if (app->_card_changed) {
        app->_card_changed = false;
        app->handleCardChange();
    }
    if (app->_fallback_state == FALLBACK_READY && !app->_fallback_font) {
        app->useFallbackFont();
        if (app->_fallback_font && app->_book >= 0) {
            // Characters that were boxes have their own widths now
            app->_history.clear();
            app->showPage(app->_page.start);
        }
    }
    if (app->_jump_target >= 0 && lv_tick_elaps(app->_jump_at) >= JUMP_DELAY_MS) {
        app->jumpTo(app->_jump_target);
        app->_jump_target = -1;
    }
    if (app->_unsaved && lv_tick_elaps(app->_turned_at) >= SAVE_DELAY_MS) {
        app->saveProgress();
    }
}

void Reader::item_event_cb(lv_event_t *e)
{
    Reader *app = static_cast<Reader *>(lv_event_get_user_data(e));
    lv_obj_t *item = lv_event_get_current_target_obj(e);
    app->openBook(static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(item))));
}

void Reader::reading_event_cb(lv_event_t *e)
{
    Reader *app = static_cast<Reader *>(lv_event_get_user_data(e));
    lv_indev_t *indev = lv_indev_active();
    if (!indev || app->_book < 0) {
        return;
    }
    if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
        const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
            app->turn(dir == LV_DIR_LEFT ? 1 : -1);
            lv_indev_wait_release(indev);   // The swipe is not also a tap
        }
        return;
    }
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    if (point.x < TAP_SIDE) {
        app->turn(-1);
    } else if (point.x >= SCREEN - TAP_SIDE) {
        app->turn(1);
    } else {
        app->openMenu();
    }
}

void Reader::menu_event_cb(lv_event_t *e)
{
    Reader *app = static_cast<Reader *>(lv_event_get_user_data(e));
    // Only taps outside the panel reach the backdrop
    if (lv_event_get_target_obj(e) == app->_menu) {
        app->closeMenu();
    }
}

void Reader::message_event_cb(lv_event_t *e)
{
    Reader *app = static_cast<Reader *>(lv_event_get_user_data(e));
    if (app->_notice) {
        app->showShelf();
    }
}

void Reader::slider_event_cb(lv_event_t *e)
{
    Reader *app = static_cast<Reader *>(lv_event_get_user_data(e));
    const int value = lv_slider_get_value(app->_slider);
    app->updateSlider(value);
    app->_jump_target = value;
    app->_jump_at = lv_tick_get();
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        app->jumpTo(value);
        app->_jump_target = -1;
    }
}

void Reader::size_event_cb(lv_event_t *e)
{
    Reader *app = static_cast<Reader *>(lv_event_get_user_data(e));
    const int size = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(e))));
    if (size == app->_text_size) {
        return;
    }
    app->_text_size = size;
    store_text_size(size);
    app->applyTextSize();
    if (app->_book >= 0) {
        // Pages break differently now; the page before is found again from the text
        app->_history.clear();
        app->showPage(app->_page.start);
    }
}

void Reader::shelf_button_cb(lv_event_t *e)
{
    static_cast<Reader *>(lv_event_get_user_data(e))->closeBook();
}

bool Reader::back(void)
{
    ESP_UTILS_LOGD("Back");
    if (menuOpen()) {
        closeMenu();
    } else if (_book >= 0) {
        closeBook();
    } else if (_notice) {
        showShelf();
    } else {
        ESP_UTILS_CHECK_FALSE_RETURN(notifyCoreClosed(), false, "Notify core closed failed");
    }
    return true;
}

bool Reader::close(void)
{
    ESP_UTILS_LOGD("Close");
    // The open book stays the last one, so it opens again with the app
    saveProgress();
    if (_timer) {
        lv_timer_delete(_timer);
        _timer = nullptr;
    }
    xSemaphoreTake(_file_lock, portMAX_DELAY);
    text_source_close(_src);
    _src = nullptr;
    xSemaphoreGive(_file_lock);
    dropFallbackFont();
    // The default screen is owned by Brookesia and will be deleted by the
    // core after close() returns.  Clear this app's children now so no LVGL
    // object can retain the lazily loaded Puhui font while it is released.
    lv_obj_t *screen = _shelf != nullptr ? lv_obj_get_parent(_shelf) :
                       (_reading != nullptr ? lv_obj_get_parent(_reading) : nullptr);
    if (screen != nullptr) {
        lv_obj_clean(screen);
    }
    app_fonts_release();
    _book = -1;
    _books.clear();
    _items.clear();
    _lines.clear();
    _shelf = _reading = _title = _position = nullptr;
    _message = _message_icon = _message_title = _message_hint = nullptr;
    _menu = _menu_title = _menu_info = _slider = _slider_label = nullptr;
    _size_buttons[0] = _size_buttons[1] = nullptr;
    storage_service_release(&_storage_lease);
    return true;
}

bool Reader::pause(void)
{
    saveProgress();
    // Four megabytes of font are better left to the app in front
    dropFallbackFont();
    if (_book >= 0) {
        _history.clear();
        showPage(_page.start);
    }
    return true;
}

bool Reader::resume(void)
{
    if (_book >= 0) {
        loadFallbackFont();
    }
    return true;
}

} // namespace esp_brookesia::apps
