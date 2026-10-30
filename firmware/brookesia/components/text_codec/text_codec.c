/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "text_codec.h"
#include "ff.h"

uint32_t text_codec_gbk(uint8_t lead, uint8_t trail)
{
    if (lead < 0x81 || lead == 0xFF || trail < 0x40 || trail == 0xFF) {
        return 0;
    }

    // ESP-IDF's FatFs build already carries the selected OEM table.  The
    // project fixes it to CP936 in sdkconfig.defaults, so reuse that tested
    // GBK-to-Unicode converter instead of embedding a second 200 KiB table.
    return ff_oem2uni(((WCHAR)lead << 8) | trail, 936);
}

size_t text_codec_gbk_decode(const uint8_t *data, size_t len, uint32_t *cp)
{
    if (len == 0) {
        return 0;
    }
    if (data[0] < 0x80) {
        *cp = data[0];
        return 1;
    }
    if (data[0] == 0x80) {
        *cp = 0x20AC;
        return 1;
    }
    if (len < 2) {
        return 0;
    }
    if (data[1] >= '0' && data[1] <= '9') {
        if (len < 4) {
            return 0;
        }
        *cp = TEXT_CODEC_REPLACEMENT;
        return 4;
    }
    const uint32_t unicode = text_codec_gbk(data[0], data[1]);
    if (unicode == 0) {
        *cp = TEXT_CODEC_REPLACEMENT;
        return 1;
    }
    *cp = unicode;
    return 2;
}

size_t text_codec_utf8_decode(const uint8_t *data, size_t len, uint32_t *cp)
{
    if (len == 0) return 0;
    const uint8_t b = data[0];
    size_t n;
    uint32_t u;
    uint32_t min;
    if (b < 0x80) { *cp = b; return 1; }
    if ((b & 0xE0) == 0xC0) { n = 2; u = b & 0x1F; min = 0x80; }
    else if ((b & 0xF0) == 0xE0) { n = 3; u = b & 0x0F; min = 0x800; }
    else if ((b & 0xF8) == 0xF0) { n = 4; u = b & 0x07; min = 0x10000; }
    else { *cp = TEXT_CODEC_REPLACEMENT; return 1; }
    for (size_t i = 1; i < n; ++i) {
        if (i >= len) return 0;
        if ((data[i] & 0xC0) != 0x80) { *cp = TEXT_CODEC_REPLACEMENT; return 1; }
        u = (u << 6) | (data[i] & 0x3F);
    }
    if (u < min || u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) {
        *cp = TEXT_CODEC_REPLACEMENT;
        return 1;
    }
    *cp = u;
    return n;
}

size_t text_codec_utf8_encode(uint32_t cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) { out[0] = (char)(0xC0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    if (cp < 0x10000) { out[0] = (char)(0xE0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[2] = (char)(0x80 | (cp & 0x3F)); return 3; }
    out[0] = (char)(0xF0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[3] = (char)(0x80 | (cp & 0x3F)); return 4;
}

bool text_codec_is_utf8(const uint8_t *data, size_t len)
{
    for (size_t pos = 0; pos < len;) {
        uint32_t cp;
        const size_t used = text_codec_utf8_decode(data + pos, len - pos, &cp);
        if (used == 0) return true;
        if (cp == TEXT_CODEC_REPLACEMENT && used == 1) return false;
        pos += used;
    }
    return true;
}

size_t text_codec_gbk_to_utf8(const uint8_t *data, size_t len, char *out, size_t out_size)
{
    size_t pos = 0;
    size_t used = 0;
    if (out_size == 0) return 0;
    while (pos < len) {
        uint32_t cp;
        const size_t consumed = text_codec_gbk_decode(data + pos, len - pos, &cp);
        if (consumed == 0) break;
        char utf8[4];
        const size_t encoded = text_codec_utf8_encode(cp, utf8);
        if (used + encoded >= out_size) break;
        for (size_t i = 0; i < encoded; ++i) out[used++] = utf8[i];
        pos += consumed;
    }
    out[used] = '\0';
    return used;
}
