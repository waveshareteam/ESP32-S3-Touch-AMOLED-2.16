/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "systems/phone/esp_brookesia_phone_app.hpp"
#include "storage_service.h"
#include "page_layout.hpp"
#include "text_source.h"

namespace esp_brookesia::apps {

/**
 * @brief Reads TXT books from /sdcard/books in UTF-8, GBK or UTF-16
 *
 * The shelf lists the books with how far each was read. A book opens where it was left; the
 * a swipe or a tap at either side turns the page, a tap in the middle opens a menu with
 * the position slider, the text size and the way back to the shelf.
 */
class Reader: public systems::phone::App {
public:
    static Reader *requestInstance(bool use_status_bar = false, bool use_navigation_bar = false);
    ~Reader();

protected:
    Reader(bool use_status_bar, bool use_navigation_bar);

    bool run(void) override;
    bool back(void) override;
    bool close(void) override;
    bool init(void) override;
    bool pause(void) override;
    bool resume(void) override;

private:
    struct Book {
        std::string path;
        std::string title;      // File name without .txt
        std::string folder;     // Below the books folder, empty at the top
    };

    static Reader *_instance;

    // The card is read in the LVGL task while this is held; the storage callback takes it to
    // close the book before the card goes to USB
    SemaphoreHandle_t _file_lock = nullptr;
    storage_service_lease_t _storage_lease = {};
    text_source_t *_src = nullptr;
    volatile bool _card_changed = false;

    // Characters the built-in font lacks come from a TTF in /sdcard/fonts. A loader task reads it
    // into memory while a book is open; it is freed again when the app goes to the background.
    enum FallbackState {
        FALLBACK_NONE,      // Not loaded; loads with the next book
        FALLBACK_LOADING,
        FALLBACK_READY,     // _fallback_data holds the file
        FALLBACK_ABSENT,    // No font on the card, or it can't be used
    };
    volatile int _fallback_state = FALLBACK_NONE;
    volatile bool _fallback_stop = false;
    int _fallback_fd = -1;              // Open while loading; the storage callback closes it
    uint8_t *_fallback_data = nullptr;
    size_t _fallback_size = 0;
    SemaphoreHandle_t _fallback_done = nullptr;
    lv_font_t *_fallback_font = nullptr;
    lv_font_t _text_font = {};          // Built-in font of the text size, falling back to the TTF

    std::vector<Book> _books;
    int _book = -1;                 // Open book, -1 on the shelf
    uint32_t _book_begin = 0;       // Text range of the open book, kept while the card is away
    uint32_t _book_end = 0;
    int _focus = 0;                 // Shelf entry used for the active book
    int _text_size = 1;             // Index into the text sizes
    PageLayout _layout;
    Page _page;
    std::vector<uint32_t> _history; // Starts of the pages read before this one
    bool _unsaved = false;
    uint32_t _turned_at = 0;
    bool _notice = false;           // A message over the shelf, dismissed by a tap or back
    int _jump_target = -1;          // Per mille the menu slider asks for, -1 when none
    uint32_t _jump_at = 0;

    lv_obj_t *_shelf = nullptr;
    std::vector<lv_obj_t *> _items;
    lv_obj_t *_reading = nullptr;
    lv_obj_t *_title = nullptr;
    lv_obj_t *_position = nullptr;
    std::vector<lv_obj_t *> _lines;
    lv_obj_t *_message = nullptr;
    lv_obj_t *_message_icon = nullptr;
    lv_obj_t *_message_title = nullptr;
    lv_obj_t *_message_hint = nullptr;
    lv_obj_t *_menu = nullptr;
    lv_obj_t *_menu_title = nullptr;
    lv_obj_t *_menu_info = nullptr;
    lv_obj_t *_slider = nullptr;
    lv_obj_t *_slider_label = nullptr;
    lv_obj_t *_size_buttons[2] = {};
    lv_timer_t *_timer = nullptr;

    void buildUi(void);
    void buildShelf(void);
    void buildReading(void);
    void buildMenu(void);
    void scanBooks(void);
    void showShelf(void);
    void showCardState(void);
    void showMessage(const char *icon, const char *title, const char *hint);
    void hideMessage(void);
    void showNotice(const char *icon, const char *title, const char *hint);
    void focusItem(int index, bool animate);
    bool openBook(int index);
    void closeBook(void);
    void applyTextSize(void);
    void loadFallbackFont(void);
    void useFallbackFont(void);
    void dropFallbackFont(void);
    void showPage(uint32_t start);
    void renderPage(void);
    void turn(int pages);
    void jumpTo(int per_mille);
    void updatePosition(void);
    void openMenu(void);
    void closeMenu(void);
    bool menuOpen(void) const;
    void updateSlider(int per_mille);
    int perMille(void) const;
    void saveProgress(void);
    void handleCardChange(void);

    static void timer_cb(lv_timer_t *timer);
    static void item_event_cb(lv_event_t *e);
    static void reading_event_cb(lv_event_t *e);
    static void menu_event_cb(lv_event_t *e);
    static void message_event_cb(lv_event_t *e);
    static void slider_event_cb(lv_event_t *e);
    static void size_event_cb(lv_event_t *e);
    static void shelf_button_cb(lv_event_t *e);
    static void fallbackLoaderTask(void *arg);
};

} // namespace esp_brookesia::apps
