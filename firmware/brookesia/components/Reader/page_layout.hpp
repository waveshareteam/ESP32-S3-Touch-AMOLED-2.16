/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "lvgl.h"
#include "text_source.h"

namespace esp_brookesia::apps {

/* One line of a page, as UTF-8 at its place on the screen */
struct PageLine {
    std::string text;
    uint32_t pos = 0;       // Where the line's text starts in the file
    int16_t x = 0;
    int16_t y = 0;
};

struct Page {
    uint32_t start = 0;
    uint32_t end = 0;       // Where the next page starts
    bool last = false;      // The text ends on this page
    std::vector<PageLine> lines;
};

/**
 * @brief Breaks text into pages of lines that follow the round screen
 *
 * Every line is as wide as the circle at its height. Chinese breaks between any two characters,
 * other text between words; closing punctuation and spaces may hang past the end of a line
 * rather than start the next one.
 */
class PageLayout {
public:
    /**
     * @param top, bottom  Vertical band for the text
     * @param margin       Gap between the text and the edge of the circle
     */
    void configure(const lv_font_t *font, int screen, int top, int bottom, int margin);

    const lv_font_t *font() const
    {
        return _font;
    }

    size_t maxLines() const
    {
        return _slots.size();
    }

    /**
     * @brief Lay out the page that starts at start, stopping before limit
     */
    void layout(text_source_t *src, uint32_t start, Page &page, uint32_t limit = UINT32_MAX) const;

    /**
     * @brief Start of the fullest page that ends where start begins
     *
     *        Pages are laid out again from the paragraphs before start, so after a jump the page
     *        before need not be one of the pages seen on the way forward.
     */
    uint32_t previousStart(text_source_t *src, uint32_t start) const;

private:
    struct Slot {
        int16_t x;
        int16_t y;
        int16_t width;
    };

    const lv_font_t *_font = nullptr;
    std::vector<Slot> _slots;
};

} // namespace esp_brookesia::apps
