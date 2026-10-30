/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "text_codec.h"
#include "text_source.h"

/* Reads go to the card in aligned blocks of this window; a page of text needs a few KB */
#define WINDOW_SIZE     (16 * 1024)
#define BLOCK_ALIGN     512
/* Bytes that never occur inside a two-byte GBK character, so a character starts at them */
#define GBK_SAFE_BYTE(b)    ((b) < 0x40)
#define GBK_ALIGN_SCAN  1024

struct text_source {
    int fd;
    uint32_t size;
    uint32_t begin;
    text_encoding_t encoding;
    uint8_t *window;
    uint32_t window_pos;
    uint32_t window_len;
};

static bool load(text_source_t *src, uint32_t start)
{
    if (lseek(src->fd, start, SEEK_SET) < 0) {
        src->window_len = 0;
        return false;
    }
    const ssize_t got = read(src->fd, src->window, WINDOW_SIZE);
    src->window_pos = start;
    src->window_len = got > 0 ? (uint32_t)got : 0;
    return got > 0;
}

/* The bytes at pos, at least need of them unless the file ends first */
static const uint8_t *bytes_at(text_source_t *src, uint32_t pos, uint32_t need, uint32_t *avail)
{
    *avail = 0;
    if (pos >= src->size) {
        return NULL;
    }
    const uint32_t want_end = (pos + need < src->size) ? pos + need : src->size;
    if (pos < src->window_pos || want_end > src->window_pos + src->window_len) {
        uint32_t start;
        if (pos < src->window_pos) {
            // Going backwards: keep as much as possible before pos in the window
            start = (want_end > WINDOW_SIZE) ? ((want_end - WINDOW_SIZE + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1)) : 0;
        } else {
            start = pos & ~(BLOCK_ALIGN - 1);
        }
        if (!load(src, start)) {
            return NULL;
        }
    }
    if (pos >= src->window_pos + src->window_len) {
        return NULL;
    }
    *avail = src->window_pos + src->window_len - pos;
    return src->window + (pos - src->window_pos);
}

static size_t utf16_decode(const uint8_t *p, size_t len, bool le, uint32_t *cp)
{
    if (len < 2) {
        return 0;
    }
    const uint32_t u = le ? (p[0] | (p[1] << 8)) : ((p[0] << 8) | p[1]);
    if (u >= 0xD800 && u <= 0xDBFF) {
        if (len < 4) {
            return 0;
        }
        const uint32_t v = le ? (p[2] | (p[3] << 8)) : ((p[2] << 8) | p[3]);
        if (v >= 0xDC00 && v <= 0xDFFF) {
            *cp = 0x10000 + ((u - 0xD800) << 10) + (v - 0xDC00);
            return 4;
        }
        *cp = TEXT_CODEC_REPLACEMENT;
        return 2;
    }
    *cp = (u >= 0xDC00 && u <= 0xDFFF) ? TEXT_CODEC_REPLACEMENT : u;
    return 2;
}

static size_t decode(const text_source_t *src, const uint8_t *p, size_t len, uint32_t *cp)
{
    switch (src->encoding) {
    case TEXT_ENCODING_GBK:
        return text_codec_gbk_decode(p, len, cp);
    case TEXT_ENCODING_UTF16LE:
        return utf16_decode(p, len, true, cp);
    case TEXT_ENCODING_UTF16BE:
        return utf16_decode(p, len, false, cp);
    case TEXT_ENCODING_UTF8:
    default:
        return text_codec_utf8_decode(p, len, cp);
    }
}

static text_encoding_t sniff(const uint8_t *p, uint32_t len, uint32_t *begin, bool *binary)
{
    *begin = 0;
    *binary = false;
    if (len >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        *begin = 3;
        return TEXT_ENCODING_UTF8;
    }
    if (len >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        *begin = 2;
        return TEXT_ENCODING_UTF16LE;
    }
    if (len >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        *begin = 2;
        return TEXT_ENCODING_UTF16BE;
    }
    uint32_t zeros[2] = {0, 0};
    uint32_t controls = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (p[i] == 0) {
            zeros[i & 1]++;
        } else if (p[i] < 0x09 || (p[i] > 0x0D && p[i] < 0x20 && p[i] != 0x1B)) {
            controls++;
        }
    }
    // UTF-16 without a mark: Latin text leaves every other byte zero
    if (zeros[1] > len / 8 && zeros[0] < len / 64) {
        return TEXT_ENCODING_UTF16LE;
    }
    if (zeros[0] > len / 8 && zeros[1] < len / 64) {
        return TEXT_ENCODING_UTF16BE;
    }
    if (zeros[0] + zeros[1] > 0 || controls > len / 100) {
        *binary = true;
    }
    return text_codec_is_utf8(p, len) ? TEXT_ENCODING_UTF8 : TEXT_ENCODING_GBK;
}

esp_err_t text_source_open(const char *path, text_source_t **ret_src)
{
    text_source_t *src = calloc(1, sizeof(*src));
    if (!src) {
        return ESP_ERR_NO_MEM;
    }
    src->fd = open(path, O_RDONLY);
    struct stat st;
    if (src->fd < 0 || fstat(src->fd, &st) != 0) {
        text_source_close(src);
        return ESP_ERR_NOT_FOUND;
    }
    src->size = (uint32_t)st.st_size;
    src->window = heap_caps_malloc(WINDOW_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!src->window) {
        src->window = malloc(WINDOW_SIZE);
    }
    if (!src->window) {
        text_source_close(src);
        return ESP_ERR_NO_MEM;
    }
    bool binary = false;
    if (src->size > 0) {
        if (!load(src, 0)) {
            text_source_close(src);
            return ESP_FAIL;
        }
        src->encoding = sniff(src->window, src->window_len, &src->begin, &binary);
    }
    if (binary) {
        text_source_close(src);
        return ESP_ERR_NOT_SUPPORTED;
    }
    *ret_src = src;
    return ESP_OK;
}

void text_source_close(text_source_t *src)
{
    if (!src) {
        return;
    }
    if (src->fd >= 0) {
        close(src->fd);
    }
    free(src->window);
    free(src);
}

text_encoding_t text_source_encoding(const text_source_t *src)
{
    return src->encoding;
}

const char *text_encoding_name(text_encoding_t encoding)
{
    switch (encoding) {
    case TEXT_ENCODING_GBK:
        return "GBK";
    case TEXT_ENCODING_UTF16LE:
        return "UTF-16LE";
    case TEXT_ENCODING_UTF16BE:
        return "UTF-16BE";
    case TEXT_ENCODING_UTF8:
    default:
        return "UTF-8";
    }
}

uint32_t text_source_begin(const text_source_t *src)
{
    return src->begin;
}

uint32_t text_source_end(const text_source_t *src)
{
    return src->size;
}

bool text_source_next(text_source_t *src, uint32_t *pos, uint32_t *cp)
{
    uint32_t avail;
    const uint8_t *p = bytes_at(src, *pos, 4, &avail);
    if (!p) {
        return false;
    }
    size_t n = decode(src, p, avail, cp);
    if (n == 0) {
        // The file ends inside a character
        *cp = TEXT_CODEC_REPLACEMENT;
        n = avail;
    }
    *pos += (uint32_t)n;
    return true;
}

static bool is_utf16(const text_source_t *src)
{
    return src->encoding == TEXT_ENCODING_UTF16LE || src->encoding == TEXT_ENCODING_UTF16BE;
}

static uint32_t unit_at(text_source_t *src, uint32_t pos)
{
    uint32_t avail;
    const uint8_t *p = bytes_at(src, pos, is_utf16(src) ? 2 : 1, &avail);
    if (!p || avail < (is_utf16(src) ? 2u : 1u)) {
        return 0;
    }
    if (!is_utf16(src)) {
        return p[0];
    }
    return src->encoding == TEXT_ENCODING_UTF16LE ? (uint32_t)(p[0] | (p[1] << 8)) : (uint32_t)((p[0] << 8) | p[1]);
}

/* Which of two starting points reads as GBK with fewer broken characters */
static uint32_t gbk_better_start(text_source_t *src, uint32_t pos)
{
    uint32_t bad[2] = {0, 0};
    for (int k = 0; k < 2; k++) {
        uint32_t p = pos + k;
        for (int i = 0; i < 32 && p < src->size; i++) {
            uint32_t cp;
            if (!text_source_next(src, &p, &cp)) {
                break;
            }
            bad[k] += (cp == TEXT_CODEC_REPLACEMENT);
        }
    }
    return bad[1] < bad[0] ? pos + 1 : pos;
}

uint32_t text_source_align(text_source_t *src, uint32_t pos)
{
    if (pos <= src->begin) {
        return src->begin;
    }
    if (pos >= src->size) {
        return src->size;
    }
    switch (src->encoding) {
    case TEXT_ENCODING_UTF8:
        for (int i = 0; i < 3 && pos < src->size && (unit_at(src, pos) & 0xC0) == 0x80; i++) {
            pos++;
        }
        return pos;
    case TEXT_ENCODING_GBK:
        for (uint32_t p = pos; p < src->size && p < pos + GBK_ALIGN_SCAN; p++) {
            if (GBK_SAFE_BYTE(unit_at(src, p))) {
                return p;
            }
        }
        return gbk_better_start(src, pos);
    default:
        pos = src->begin + ((pos - src->begin + 1) & ~1u);
        if (pos < src->size) {
            const uint32_t u = unit_at(src, pos);
            if (u >= 0xDC00 && u <= 0xDFFF) {
                pos += 2;
            }
        }
        return pos < src->size ? pos : src->size;
    }
}

uint32_t text_source_line_start(text_source_t *src, uint32_t pos, uint32_t max_back, bool *found)
{
    const uint32_t unit = is_utf16(src) ? 2 : 1;
    *found = true;
    if (pos > src->size) {
        pos = src->size;
    }
    if (pos <= src->begin + unit) {
        return src->begin;
    }
    const uint32_t lo = (pos - src->begin > max_back) ? pos - max_back : src->begin;
    // The character just before pos may be the line break that ends its line
    for (uint32_t q = pos - 2 * unit + 1; q-- > lo;) {
        if (unit > 1 && ((q - src->begin) & 1)) {
            continue;
        }
        if (unit_at(src, q) == '\n') {
            return q + unit;
        }
        if (q == 0) {
            break;
        }
    }
    if (lo == src->begin) {
        return src->begin;
    }
    *found = false;
    return text_source_align(src, lo);
}
