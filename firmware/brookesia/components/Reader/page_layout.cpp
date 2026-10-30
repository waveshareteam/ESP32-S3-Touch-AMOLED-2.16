/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cmath>
#include "text_codec.h"
#include "page_layout.hpp"

namespace esp_brookesia::apps {

/* How far back to look for the paragraphs before a page, and how much of them to lay out */
static constexpr uint32_t PARAGRAPH_MAX_BACK = 8 * 1024;
static constexpr uint32_t PREVIOUS_TEXT_BYTES = 2 * 1024;
static constexpr int MAX_HANGING = 2;
static constexpr int TAB_SPACES = 4;
/* Blank text this short at the end of a file doesn't get a page of its own */
static constexpr int TRAILING_BLANK_MAX = 64;

static bool is_space(uint32_t c)
{
    return c == ' ' || c == 0x3000;
}

static bool is_blank(uint32_t c)
{
    return is_space(c) || c == '\n' || c == '\r' || c == '\t';
}

/* Chinese, Japanese and Korean text, which may break between any two characters */
static bool is_wide(uint32_t c)
{
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xAC00 && c <= 0xD7AF) || (c >= 0xF900 && c <= 0xFAFF) ||
           (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFFEF) || (c >= 0x20000 && c <= 0x3FFFF) ||
           (c >= 0x1F300 && c <= 0x1FAFF);
}

/* Closing punctuation, which stays on the line before */
static bool no_line_start(uint32_t c)
{
    switch (c) {
    case ',': case '.': case ';': case ':': case '!': case '?': case ')': case ']': case '}': case '%':
    case 0x2019: case 0x201D: case 0x2026: case 0x2014: case 0x00B7:
    case 0x3001: case 0x3002: case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011: case 0x3015:
    case 0x30FC: case 0xFF01: case 0xFF09: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF1F:
    case 0xFF5E:
        return true;
    default:
        return false;
    }
}

/* Opening punctuation, which stays with the text after it */
static bool no_line_end(uint32_t c)
{
    switch (c) {
    case '(': case '[': case '{': case 0x2018: case 0x201C:
    case 0x3008: case 0x300A: case 0x300C: case 0x300E: case 0x3010: case 0x3014: case 0xFF08:
        return true;
    default:
        return false;
    }
}

/* Whether a line may end between a and b */
static bool can_break(uint32_t a, uint32_t b)
{
    if (no_line_start(b) || no_line_end(a) || is_space(b)) {
        return false;
    }
    return is_space(a) || a == '-' || is_wide(a) || is_wide(b);
}

static void append_utf8(std::string &text, uint32_t cp)
{
    char utf8[4];
    text.append(utf8, text_codec_utf8_encode(cp, utf8));
}

void PageLayout::configure(const lv_font_t *font, int screen, int top, int bottom, int margin)
{
    _font = font;
    _slots.clear();
    const int line_height = lv_font_get_line_height(font);
    const int step = line_height + std::max(2, line_height / 16);
    const int count = std::max(1, (bottom - top + step - line_height) / step);
    const int y0 = top + (bottom - top - ((count - 1) * step + line_height)) / 2;
    const float centre = screen / 2.0f;
    const float radius = centre - margin;
    for (int i = 0; i < count; i++) {
        const int y = y0 + i * step;
        // The end of the line box farther from the centre limits the width
        const float d = std::max(std::fabs(y - centre), std::fabs(y + line_height - centre));
        if (d >= radius) {
            continue;
        }
        const int width = static_cast<int>(2 * std::sqrt(radius * radius - d * d));
        _slots.push_back({static_cast<int16_t>(centre - width / 2), static_cast<int16_t>(y), static_cast<int16_t>(width)});
    }
}

void PageLayout::layout(text_source_t *src, uint32_t start, Page &page, uint32_t limit) const
{
    page.start = start;
    page.lines.clear();
    const uint32_t end = std::min(limit, text_source_end(src));
    uint32_t pos = start;
    bool wrapped = false;   // The line goes on with a paragraph the line before broke
    bool ended = false;     // No more text could be read
    const int space_width = lv_font_get_glyph_width(_font, ' ', 0);

    // A page doesn't open with the blank lines between two paragraphs
    while (pos > text_source_begin(src) && pos < end) {
        uint32_t next = pos;
        uint32_t cp;
        if (!text_source_next(src, &next, &cp) || (cp != '\n' && cp != '\r')) {
            break;
        }
        pos = next;
    }

    for (const Slot &slot : _slots) {
        if (pos >= end || ended) {
            break;
        }
        PageLine line;
        line.pos = pos;
        line.x = slot.x;
        line.y = slot.y;
        int width = 0;
        uint32_t prev = 0;
        bool has_break = false;
        uint32_t break_pos = 0;
        size_t break_len = 0;
        int hanging = 0;
        bool newline = false;

        while (pos < end) {
            uint32_t next = pos;
            uint32_t cp;
            if (!text_source_next(src, &next, &cp)) {
                ended = true;
                break;
            }
            if (cp == '\n') {
                pos = next;
                newline = true;
                break;
            }
            if (cp == '\r' || cp == 0xFEFF || cp == 0x200B) {
                pos = next;
                continue;
            }
            if (wrapped && prev == 0 && cp == ' ') {
                pos = next;     // A wrapped line doesn't start with the space it broke at
                line.pos = pos;
                continue;
            }
            int w;
            const bool tab = cp == '\t';
            if (tab) {
                cp = ' ';
                w = space_width * TAB_SPACES;
            } else {
                if (cp < 0x20) {
                    cp = ' ';
                }
                w = lv_font_get_glyph_width(_font, cp, 0);
            }
            if (width + w > slot.width && prev != 0) {
                if (cp == ' ' || (no_line_start(cp) && hanging < MAX_HANGING)) {
                    hanging++;  // Spaces and closing punctuation hang past the edge
                } else {
                    if (!can_break(prev, cp) && has_break) {
                        line.text.resize(break_len);
                        pos = break_pos;
                    }
                    break;
                }
            }
            if (prev != 0 && can_break(prev, cp)) {
                has_break = true;
                break_pos = pos;
                break_len = line.text.size();
            }
            if (tab) {
                line.text.append(TAB_SPACES, ' ');
            } else {
                append_utf8(line.text, cp);
            }
            width += w;
            prev = cp;
            pos = next;
        }
        wrapped = !newline;
        page.lines.push_back(std::move(line));
    }
    page.end = pos;
    page.last = ended || pos >= text_source_end(src);
    if (!page.last && pos < end) {
        // Only line breaks and spaces left: this is the last page
        uint32_t next = pos;
        uint32_t cp = 0;
        int count = 0;
        while (count < TRAILING_BLANK_MAX && text_source_next(src, &next, &cp) && is_blank(cp)) {
            count++;
        }
        if (next >= text_source_end(src) && count < TRAILING_BLANK_MAX && is_blank(cp)) {
            page.end = text_source_end(src);
            page.last = true;
        }
    }
}

uint32_t PageLayout::previousStart(text_source_t *src, uint32_t start) const
{
    const uint32_t begin = text_source_begin(src);
    if (start <= begin) {
        return begin;
    }
    // Whole paragraphs, enough of them to fill more than a page
    uint32_t from = start;
    while (from > begin && start - from < PREVIOUS_TEXT_BYTES) {
        bool found = true;
        const uint32_t line = text_source_line_start(src, from, PARAGRAPH_MAX_BACK, &found);
        if (line >= from) {
            break;
        }
        from = line;
        if (!found) {
            break;
        }
    }
    // Pages from there until one reaches start
    Page page;
    std::vector<uint32_t> lines_before;     // Line starts of the page before that one
    uint32_t pos = from;
    uint32_t last = from;
    while (pos < start) {
        layout(src, pos, page, start);
        if (page.end >= start || page.end <= pos) {
            last = pos;
            break;
        }
        lines_before.clear();
        for (const PageLine &line : page.lines) {
            lines_before.push_back(line.pos);
        }
        pos = page.end;
    }
    // That page may be short; take whole lines from the page before while it still fits
    for (auto it = lines_before.rbegin(); it != lines_before.rend(); ++it) {
        if (*it >= last) {
            continue;
        }
        layout(src, *it, page, start);
        if (page.end < start) {
            break;
        }
        last = *it;
    }
    return last;
}

} // namespace esp_brookesia::apps
