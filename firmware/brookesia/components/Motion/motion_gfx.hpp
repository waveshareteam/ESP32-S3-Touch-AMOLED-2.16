/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

/* Antialiased drawing into an RGB565 canvas, by coverage: shapes are filled where they cover a
 * pixel and blended at their edges, so they stay smooth at any position. */
namespace esp_brookesia::apps::motion_gfx {

constexpr uint32_t OPAQUE = 255;

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t hex(uint32_t c)
{
    return rgb((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

/* fg over bg with alpha 0-255, per channel at full RGB565 precision */
inline uint16_t blend(uint16_t bg, uint16_t fg, uint32_t alpha)
{
    if (alpha >= OPAQUE) {
        return fg;
    }
    if (alpha == 0) {
        return bg;
    }
    // Green moves to the top half so all three channels scale in one multiply
    const uint32_t a = (alpha + 4) >> 3;   // 0-32
    const uint32_t bx = (bg | (static_cast<uint32_t>(bg) << 16)) & 0x07E0F81F;
    const uint32_t fx = (fg | (static_cast<uint32_t>(fg) << 16)) & 0x07E0F81F;
    const uint32_t mix = (bx + (((fx - bx) * a) >> 5)) & 0x07E0F81F;
    return static_cast<uint16_t>(mix | (mix >> 16));
}

inline uint16_t mix(uint16_t a, uint16_t b, float t)
{
    return blend(a, b, static_cast<uint32_t>(std::clamp(t, 0.0f, 1.0f) * OPAQUE));
}

/* An 8-bit colour quantized with a 4x4 ordered dither, so smooth gradients show no bands in RGB565 */
inline uint16_t dither(float r, float g, float b, int x, int y)
{
    static constexpr uint8_t BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const float t = (BAYER[y & 3][x & 3] + 0.5f) / 16.0f;
    const int r5 = std::clamp(static_cast<int>(r * 31 / 255 + t), 0, 31);
    const int g6 = std::clamp(static_cast<int>(g * 63 / 255 + t), 0, 63);
    const int b5 = std::clamp(static_cast<int>(b * 31 / 255 + t), 0, 31);
    return static_cast<uint16_t>(r5 << 11 | g6 << 5 | b5);
}

class Canvas {
public:
    Canvas(uint16_t *pixels, int width, int height) : _px(pixels), _w(width), _h(height)
    {
    }

    uint16_t *pixels() const
    {
        return _px;
    }

    void plot(int x, int y, uint16_t c, uint32_t alpha)
    {
        if (x >= 0 && y >= 0 && x < _w && y < _h) {
            uint16_t *p = &_px[y * _w + x];
            *p = blend(*p, c, alpha);
        }
    }

    void span(int y, int x0, int x1, uint16_t c, uint32_t alpha)
    {
        if (y < 0 || y >= _h) {
            return;
        }
        x0 = std::max(x0, 0);
        x1 = std::min(x1, _w - 1);
        uint16_t *p = &_px[y * _w];
        if (alpha >= OPAQUE) {
            for (int x = x0; x <= x1; x++) {
                p[x] = c;
            }
        } else {
            for (int x = x0; x <= x1; x++) {
                p[x] = blend(p[x], c, alpha);
            }
        }
    }

    /* Filled circle; alpha scales the whole disc */
    void disc(float cx, float cy, float r, uint16_t c, uint32_t alpha = OPAQUE)
    {
        const int y0 = static_cast<int>(std::floor(cy - r - 1));
        const int y1 = static_cast<int>(std::ceil(cy + r + 1));
        const float outer = (r + 0.5f) * (r + 0.5f);
        const float inner = r > 0.5f ? (r - 0.5f) * (r - 0.5f) : 0;
        for (int y = y0; y <= y1; y++) {
            const float dy = y + 0.5f - cy;
            const float dy2 = dy * dy;
            if (dy2 >= outer) {
                continue;
            }
            const float half = std::sqrt(outer - dy2);
            const float in_half = dy2 < inner ? std::sqrt(inner - dy2) : -1.0f;
            const int xa = static_cast<int>(std::floor(cx - half));
            const int xb = static_cast<int>(std::ceil(cx + half));
            const int ia = in_half >= 0 ? static_cast<int>(std::ceil(cx - in_half)) : xb + 1;
            const int ib = in_half >= 0 ? static_cast<int>(std::floor(cx + in_half)) - 1 : xb;
            if (ia <= ib) {
                span(y, ia, ib, c, alpha);
            }
            for (int x = xa; x <= xb; x++) {
                if (x >= ia && x <= ib) {
                    continue;
                }
                const float dx = x + 0.5f - cx;
                const float cover = std::clamp(r + 0.5f - std::sqrt(dx * dx + dy2), 0.0f, 1.0f);
                plot(x, y, c, static_cast<uint32_t>(cover * alpha));
            }
        }
    }

    /* Circle outline of width w centred on radius r */
    void ring(float cx, float cy, float r, float w, uint16_t c, uint32_t alpha = OPAQUE)
    {
        const float ro = r + w / 2 + 0.5f;
        const float ri = std::max(0.0f, r - w / 2 - 0.5f);
        const int y0 = static_cast<int>(std::floor(cy - ro));
        const int y1 = static_cast<int>(std::ceil(cy + ro));
        for (int y = y0; y <= y1; y++) {
            const float dy = y + 0.5f - cy;
            const float dy2 = dy * dy;
            if (dy2 >= ro * ro) {
                continue;
            }
            const float half = std::sqrt(ro * ro - dy2);
            const float hole = dy2 < ri * ri ? std::sqrt(ri * ri - dy2) : -1.0f;
            const int xa = static_cast<int>(std::floor(cx - half));
            const int xb = static_cast<int>(std::ceil(cx + half));
            for (int x = xa; x <= xb; x++) {
                const float dx = x + 0.5f - cx;
                if (hole >= 0 && std::fabs(dx) < hole - 1) {
                    x = static_cast<int>(std::floor(cx + hole - 1));   // Skip the inside of the ring
                    continue;
                }
                const float d = std::sqrt(dx * dx + dy2);
                const float cover = std::clamp(w / 2 + 0.5f - std::fabs(d - r), 0.0f, 1.0f);
                if (cover > 0) {
                    plot(x, y, c, static_cast<uint32_t>(cover * alpha));
                }
            }
        }
    }

    /* Line of width w with round ends */
    void line(float x0, float y0, float x1, float y1, float w, uint16_t c, uint32_t alpha = OPAQUE)
    {
        const float h = w / 2;
        const int bx0 = static_cast<int>(std::floor(std::min(x0, x1) - h - 1));
        const int bx1 = static_cast<int>(std::ceil(std::max(x0, x1) + h + 1));
        const int by0 = static_cast<int>(std::floor(std::min(y0, y1) - h - 1));
        const int by1 = static_cast<int>(std::ceil(std::max(y0, y1) + h + 1));
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float len2 = dx * dx + dy * dy;
        const float len = std::sqrt(len2);
        // Squared distances first: only pixels on the edge need a square root
        const float out2 = (h + 0.5f) * (h + 0.5f);
        const float in2 = h > 0.5f ? (h - 0.5f) * (h - 0.5f) : -1.0f;
        // Unit normal: a row only needs the pixels this close to the line through both ends
        const float nx = len > 0 ? -dy / len : 0;
        const float ny = len > 0 ? dx / len : 1;
        for (int y = std::max(by0, 0); y <= std::min(by1, _h - 1); y++) {
            const float py = y + 0.5f;
            int xa = std::max(bx0, 0);
            int xb = std::min(bx1, _w - 1);
            if (std::fabs(nx) > 0.05f) {
                const float xc = x0 - (py - y0) * ny / nx;
                const float reach = (h + 1.5f) / std::fabs(nx);
                xa = std::max(xa, static_cast<int>(std::floor(xc - reach)));
                xb = std::min(xb, static_cast<int>(std::ceil(xc + reach)));
            }
            for (int x = xa; x <= xb; x++) {
                const float px = x + 0.5f;
                float t = len2 > 0 ? ((px - x0) * dx + (py - y0) * dy) / len2 : 0;
                t = std::clamp(t, 0.0f, 1.0f);
                const float ex = px - (x0 + t * dx);
                const float ey = py - (y0 + t * dy);
                const float d2 = ex * ex + ey * ey;
                if (d2 >= out2) {
                    continue;
                }
                const float cover = d2 <= in2 ? 1.0f : std::clamp(h + 0.5f - std::sqrt(d2), 0.0f, 1.0f);
                plot(x, y, c, static_cast<uint32_t>(cover * alpha));
            }
        }
    }

    /* Filled triangle with antialiased edges */
    void triangle(float ax, float ay, float bx, float by, float cx, float cy, uint16_t c, uint32_t alpha = OPAQUE)
    {
        // Wind counter-clockwise in screen space so every edge has the inside on its left
        if ((bx - ax) * (cy - ay) - (by - ay) * (cx - ax) < 0) {
            std::swap(bx, cx);
            std::swap(by, cy);
        }
        const float ex[3] = {ax, bx, cx};
        const float ey[3] = {ay, by, cy};
        const int x0 = static_cast<int>(std::floor(std::min({ax, bx, cx}))) - 1;
        const int x1 = static_cast<int>(std::ceil(std::max({ax, bx, cx}))) + 1;
        const int y0 = static_cast<int>(std::floor(std::min({ay, by, cy}))) - 1;
        const int y1 = static_cast<int>(std::ceil(std::max({ay, by, cy}))) + 1;
        for (int y = std::max(y0, 0); y <= std::min(y1, _h - 1); y++) {
            for (int x = std::max(x0, 0); x <= std::min(x1, _w - 1); x++) {
                float cover = 1.0f;
                for (int i = 0; i < 3; i++) {
                    const int j = (i + 1) % 3;
                    const float nx = ey[j] - ey[i];
                    const float ny = ex[i] - ex[j];
                    const float len = std::sqrt(nx * nx + ny * ny);
                    if (len <= 0) {
                        continue;
                    }
                    // Signed distance of the pixel centre from the edge, positive inside
                    const float d = -((x + 0.5f - ex[i]) * nx + (y + 0.5f - ey[i]) * ny) / len;
                    cover = std::min(cover, std::clamp(d + 0.5f, 0.0f, 1.0f));
                }
                if (cover > 0) {
                    plot(x, y, c, static_cast<uint32_t>(cover * alpha));
                }
            }
        }
    }

    /* Draws a sprite of colours and alphas with its centre at (cx, cy), at whole-pixel positions */
    void sprite(const uint16_t *colors, const uint8_t *alphas, int size, float cx, float cy, uint32_t alpha = OPAQUE)
    {
        const int left = static_cast<int>(std::lround(cx)) - size / 2;
        const int top = static_cast<int>(std::lround(cy)) - size / 2;
        for (int sy = 0; sy < size; sy++) {
            const int y = top + sy;
            if (y < 0 || y >= _h) {
                continue;
            }
            uint16_t *row = &_px[y * _w];
            for (int sx = 0; sx < size; sx++) {
                const int x = left + sx;
                const uint32_t a = alphas[sy * size + sx] * alpha / OPAQUE;
                if (a && x >= 0 && x < _w) {
                    row[x] = blend(row[x], colors[sy * size + sx], a);
                }
            }
        }
    }

private:
    uint16_t *_px;
    int _w;
    int _h;
};

} // namespace esp_brookesia::apps::motion_gfx
