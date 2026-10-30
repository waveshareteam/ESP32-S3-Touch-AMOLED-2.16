/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "music_meta.hpp"
#include "text_codec.h"

static const char *TAG = "music_meta";

static constexpr size_t MAX_TAG_SIZE = 2 * 1024 * 1024;     // Covers above this are skipped
static constexpr size_t MAX_LRC_SIZE = 256 * 1024;
static constexpr size_t FRAME_SCAN_SIZE = 16 * 1024;

struct PsramFree {
    void operator()(uint8_t *p) const
    {
        heap_caps_free(p);
    }
};
using Buffer = std::unique_ptr<uint8_t, PsramFree>;

static Buffer alloc_buffer(size_t size)
{
    return Buffer(static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

/* ---------------------------------------------------------------------------- */
/* Text                                                                          */

static bool is_valid_utf8(const uint8_t *p, size_t n)
{
    size_t i = 0;
    while (i < n) {
        const uint8_t c = p[i];
        size_t extra;
        if (c < 0x80) {
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            extra = 3;
        } else {
            return false;
        }
        if (extra && i + extra >= n) {
            return false;
        }
        for (size_t k = 1; k <= extra; k++) {
            if ((p[i + k] & 0xC0) != 0x80) {
                return false;
            }
        }
        i += extra + 1;
    }
    return true;
}

static void append_utf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80) {
        out += (char)cp;
    } else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

static std::string from_utf16(const uint8_t *p, size_t n, bool big_endian)
{
    std::string out;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t u = big_endian ? (p[i] << 8 | p[i + 1]) : (p[i + 1] << 8 | p[i]);
        if (u == 0) {
            break;
        }
        if (u >= 0xD800 && u < 0xDC00 && i + 3 < n) {
            const uint32_t lo = big_endian ? (p[i + 2] << 8 | p[i + 3]) : (p[i + 3] << 8 | p[i + 2]);
            u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        append_utf8(out, u);
    }
    return out;
}

/* Whether text that is not UTF-8 reads as GBK: every byte above 0x7F starts a pair GBK assigns */
static bool looks_gbk(const uint8_t *p, size_t n)
{
    bool wide = false;
    for (size_t i = 0; i < n; i++) {
        if (p[i] < 0x80) {
            continue;
        }
        if (i + 1 >= n || text_codec_gbk(p[i], p[i + 1]) == 0) {
            return false;
        }
        wide = true;
        i++;
    }
    return wide;
}

static std::string from_gbk(const uint8_t *p, size_t n)
{
    // A two-byte GBK character becomes at most three bytes of UTF-8
    std::string out(n * 3 / 2 + 4, '\0');
    out.resize(text_codec_gbk_to_utf8(p, n, out.data(), out.size()));
    return out;
}

/* Latin-1 text that is really UTF-8 or GBK (common on Chinese files) is read as that */
static std::string from_latin1(const uint8_t *p, size_t n)
{
    n = strnlen(reinterpret_cast<const char *>(p), n);
    if (is_valid_utf8(p, n)) {
        return std::string(reinterpret_cast<const char *>(p), n);
    }
    if (looks_gbk(p, n)) {
        return from_gbk(p, n);
    }
    std::string out;
    for (size_t i = 0; i < n; i++) {
        append_utf8(out, p[i]);
    }
    return out;
}

/* ID3v2 text: one encoding byte, then the text */
static std::string decode_text(const uint8_t *p, size_t n, uint8_t encoding)
{
    switch (encoding) {
    case 1:
        if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
            return from_utf16(p + 2, n - 2, true);
        }
        if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
            return from_utf16(p + 2, n - 2, false);
        }
        return from_utf16(p, n, false);
    case 2:
        return from_utf16(p, n, true);
    case 3:
        return std::string(reinterpret_cast<const char *>(p), strnlen(reinterpret_cast<const char *>(p), n));
    default:
        return from_latin1(p, n);
    }
}

static void trim(std::string &s)
{
    while (!s.empty() && (unsigned char)s.back() <= ' ') {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && (unsigned char)s[start] <= ' ') {
        start++;
    }
    s.erase(0, start);
}

/* ---------------------------------------------------------------------------- */
/* ID3v2                                                                          */

static uint32_t syncsafe(const uint8_t *p)
{
    return (p[0] & 0x7F) << 21 | (p[1] & 0x7F) << 14 | (p[2] & 0x7F) << 7 | (p[3] & 0x7F);
}

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

/* APIC: encoding, MIME (Latin-1), picture type, description, image data */
static void read_apic(const uint8_t *p, size_t n, TrackMeta &meta)
{
    if (n < 4) {
        return;
    }
    const uint8_t encoding = p[0];
    size_t i = 1;
    while (i < n && p[i]) {
        i++;
    }
    i += 1;     // MIME terminator
    const uint8_t type = (i < n) ? p[i] : 0;
    i += 1;
    if (encoding == 1 || encoding == 2) {
        while (i + 1 < n && (p[i] || p[i + 1])) {
            i += 2;
        }
        i += 2;
    } else {
        while (i < n && p[i]) {
            i++;
        }
        i += 1;
    }
    if (i + 2 >= n || p[i] != 0xFF || p[i + 1] != 0xD8) {
        return;     // Only JPEG covers are shown
    }
    // Prefer the front cover (type 3), otherwise keep the first picture
    if (meta.cover_jpeg.empty() || type == 3) {
        meta.cover_jpeg.assign(p + i, p + n);
    }
}

/* Returns the size of the tag including its header, 0 when there is none */
static size_t read_id3v2(FILE *fp, TrackMeta &meta)
{
    uint8_t header[10];
    if (fseek(fp, 0, SEEK_SET) != 0 || fread(header, 1, sizeof(header), fp) != sizeof(header) ||
            memcmp(header, "ID3", 3) != 0) {
        return 0;
    }
    const uint8_t version = header[3];
    const uint8_t flags = header[5];
    const size_t size = syncsafe(header + 6);
    const size_t total = 10 + size + ((flags & 0x10) ? 10 : 0);
    if (version < 3 || version > 4 || size > MAX_TAG_SIZE) {
        return total;
    }
    Buffer tag = alloc_buffer(size);
    if (!tag || fread(tag.get(), 1, size, fp) != size) {
        return total;
    }
    const uint8_t *p = tag.get();
    size_t pos = 0;
    if (flags & 0x40) {
        // Extended header: v2.4 counts itself, v2.3 does not
        pos = (version == 4) ? syncsafe(p) : be32(p) + 4;
    }
    while (pos + 10 <= size && p[pos] != 0) {
        const uint8_t *frame = p + pos;
        const size_t frame_size = (version == 4) ? syncsafe(frame + 4) : be32(frame + 4);
        const uint8_t *data = frame + 10;
        if (frame_size == 0 || pos + 10 + frame_size > size) {
            break;
        }
        if (!memcmp(frame, "TIT2", 4)) {
            meta.title = decode_text(data + 1, frame_size - 1, data[0]);
        } else if (!memcmp(frame, "TPE1", 4)) {
            meta.artist = decode_text(data + 1, frame_size - 1, data[0]);
        } else if (!memcmp(frame, "TALB", 4)) {
            meta.album = decode_text(data + 1, frame_size - 1, data[0]);
        } else if (!memcmp(frame, "APIC", 4)) {
            read_apic(data, frame_size, meta);
        }
        pos += 10 + frame_size;
    }
    trim(meta.title);
    trim(meta.artist);
    trim(meta.album);
    return total;
}

/* ---------------------------------------------------------------------------- */
/* Duration                                                                       */

/* Finds the first MPEG audio frame after the tag and works out the duration */
static uint32_t mp3_duration_ms(FILE *fp, size_t audio_start, size_t file_size, uint32_t &frame_offset)
{
    static const uint16_t BITRATE_V1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const uint16_t BITRATE_V2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const uint32_t RATE[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};

    Buffer buf = alloc_buffer(FRAME_SCAN_SIZE);
    if (!buf || fseek(fp, audio_start, SEEK_SET) != 0) {
        return 0;
    }
    const size_t n = fread(buf.get(), 1, FRAME_SCAN_SIZE, fp);
    const uint8_t *p = buf.get();
    for (size_t i = 0; i + 4 < n; i++) {
        if (p[i] != 0xFF || (p[i + 1] & 0xE0) != 0xE0) {
            continue;
        }
        const int version_bits = (p[i + 1] >> 3) & 3;  // 0: 2.5, 2: 2, 3: 1
        const int layer_bits = (p[i + 1] >> 1) & 3;    // 1: layer III
        const int bitrate_idx = p[i + 2] >> 4;
        const int rate_idx = (p[i + 2] >> 2) & 3;
        if (version_bits == 1 || layer_bits != 1 || bitrate_idx == 0 || bitrate_idx == 15 || rate_idx == 3) {
            continue;
        }
        const bool v1 = version_bits == 3;
        const int rate_row = v1 ? 0 : (version_bits == 2 ? 1 : 2);
        const uint32_t sample_rate = RATE[rate_row][rate_idx];
        const uint32_t samples_per_frame = v1 ? 1152 : 576;
        const uint32_t bitrate = (v1 ? BITRATE_V1 : BITRATE_V2)[bitrate_idx] * 1000;
        const bool mono = ((p[i + 3] >> 6) & 3) == 3;
        frame_offset = (uint32_t)(audio_start + i);

        // VBR files carry the frame count in a Xing/Info or VBRI header in the first frame
        const size_t xing = i + 4 + (v1 ? (mono ? 17 : 32) : (mono ? 9 : 17));
        if (xing + 12 <= n && (!memcmp(p + xing, "Xing", 4) || !memcmp(p + xing, "Info", 4))) {
            if (be32(p + xing + 4) & 1) {
                const uint32_t frames = be32(p + xing + 8);
                return (uint32_t)((uint64_t)frames * samples_per_frame * 1000 / sample_rate);
            }
        }
        if (i + 36 + 18 <= n && !memcmp(p + i + 36, "VBRI", 4)) {
            const uint32_t frames = be32(p + i + 36 + 14);
            return (uint32_t)((uint64_t)frames * samples_per_frame * 1000 / sample_rate);
        }
        // Constant bit rate
        const size_t audio_bytes = file_size - (audio_start + i);
        return (uint32_t)((uint64_t)audio_bytes * 8 * 1000 / bitrate);
    }
    return 0;
}

static uint32_t wav_duration_ms(FILE *fp)
{
    uint8_t riff[12];
    if (fseek(fp, 0, SEEK_SET) != 0 || fread(riff, 1, sizeof(riff), fp) != sizeof(riff) ||
            memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        return 0;
    }
    uint32_t byte_rate = 0;
    uint8_t chunk[8];
    while (fread(chunk, 1, sizeof(chunk), fp) == sizeof(chunk)) {
        const uint32_t size = chunk[4] | chunk[5] << 8 | chunk[6] << 16 | (uint32_t)chunk[7] << 24;
        if (!memcmp(chunk, "fmt ", 4) && size >= 16) {
            uint8_t fmt[16];
            if (fread(fmt, 1, sizeof(fmt), fp) != sizeof(fmt)) {
                return 0;
            }
            byte_rate = fmt[8] | fmt[9] << 8 | fmt[10] << 16 | (uint32_t)fmt[11] << 24;
            fseek(fp, (long)(size - 16 + (size & 1)), SEEK_CUR);
        } else if (!memcmp(chunk, "data", 4)) {
            return byte_rate ? (uint32_t)((uint64_t)size * 1000 / byte_rate) : 0;
        } else if (fseek(fp, (long)(size + (size & 1)), SEEK_CUR) != 0) {
            return 0;
        }
    }
    return 0;
}

static bool has_extension(const std::string &path, const char *ext)
{
    const size_t dot = path.rfind('.');
    return dot != std::string::npos && strcasecmp(path.c_str() + dot, ext) == 0;
}

void music_meta_read(const std::string &path, TrackMeta &meta)
{
    meta = TrackMeta();
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        ESP_LOGW(TAG, "Open %s failed", path.c_str());
        return;
    }
    struct stat st = {};
    stat(path.c_str(), &st);
    if (has_extension(path, ".wav")) {
        meta.duration_ms = wav_duration_ms(fp);
    } else {
        const size_t tag_size = read_id3v2(fp, meta);
        meta.duration_ms = mp3_duration_ms(fp, tag_size, (size_t)st.st_size, meta.audio_offset);
    }
    fclose(fp);
}

/* ---------------------------------------------------------------------------- */
/* LRC lyrics                                                                     */

/* Parses "mm:ss", "mm:ss.xx" or "mm:ss.xxx" inside brackets; false for [ar:...] tags */
static bool parse_time(const char *s, size_t n, uint32_t &ms)
{
    char buf[16];
    if (n == 0 || n >= sizeof(buf) || !isdigit((unsigned char)s[0])) {
        return false;
    }
    memcpy(buf, s, n);
    buf[n] = '\0';
    char *end = nullptr;
    const long minutes = strtol(buf, &end, 10);
    if (*end != ':') {
        return false;
    }
    const double seconds = strtod(end + 1, &end);
    if (*end != '\0' && *end != ':') {
        return false;
    }
    ms = (uint32_t)(minutes * 60000 + seconds * 1000);
    return true;
}

bool music_lyrics_load(const std::string &audio_path, std::vector<LyricLine> &lines)
{
    lines.clear();
    const size_t dot = audio_path.rfind('.');
    const std::string lrc_path = audio_path.substr(0, dot) + ".lrc";
    FILE *fp = fopen(lrc_path.c_str(), "rb");
    if (!fp) {
        return false;
    }
    Buffer buf = alloc_buffer(MAX_LRC_SIZE + 1);
    size_t n = buf ? fread(buf.get(), 1, MAX_LRC_SIZE, fp) : 0;
    fclose(fp);
    if (n == 0) {
        return false;
    }
    uint8_t *p = buf.get();
    p[n] = '\0';
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        p += 3;
        n -= 3;
    }
    std::string converted;
    if (!is_valid_utf8(p, n)) {
        if (!looks_gbk(p, n)) {
            ESP_LOGW(TAG, "%s is neither UTF-8 nor GBK, lyrics skipped", lrc_path.c_str());
            return false;
        }
        converted = from_gbk(p, n);
        p = reinterpret_cast<uint8_t *>(converted.data());
        n = converted.size();
    }

    const char *text = reinterpret_cast<const char *>(p);
    const char *end_all = text + n;
    while (text < end_all) {
        const char *eol = static_cast<const char *>(memchr(text, '\n', end_all - text));
        const char *line_end = eol ? eol : end_all;
        // A line may carry several time tags before its text
        std::vector<uint32_t> times;
        const char *cur = text;
        while (cur < line_end && *cur == '[') {
            const char *close = static_cast<const char *>(memchr(cur, ']', line_end - cur));
            if (!close) {
                break;
            }
            uint32_t ms;
            if (parse_time(cur + 1, close - cur - 1, ms)) {
                times.push_back(ms);
            }
            cur = close + 1;
        }
        if (!times.empty()) {
            std::string lyric(cur, line_end);
            trim(lyric);
            for (uint32_t t : times) {
                lines.push_back({t, lyric});
            }
        }
        text = line_end + 1;
    }
    std::stable_sort(lines.begin(), lines.end(), [](const LyricLine & a, const LyricLine & b) {
        return a.time_ms < b.time_ms;
    });
    ESP_LOGI(TAG, "Lyrics: %u lines", (unsigned)lines.size());
    return !lines.empty();
}
