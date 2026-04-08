#pragma once

#include <algorithm>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

namespace SpriteOps {

inline uint32_t alphaOver(uint32_t dst, uint32_t src, float opacity)
{
    float sA = (((src >> 24) & 0xFFu) / 255.f) * opacity;
    if (sA < 1e-4f) return dst;

    float sR = ((src >> 0) & 0xFFu) / 255.f;
    float sG = ((src >> 8) & 0xFFu) / 255.f;
    float sB = ((src >> 16) & 0xFFu) / 255.f;

    float dA = ((dst >> 24) & 0xFFu) / 255.f;
    float dR = ((dst >> 0) & 0xFFu) / 255.f;
    float dG = ((dst >> 8) & 0xFFu) / 255.f;
    float dB = ((dst >> 16) & 0xFFu) / 255.f;

    float outA = sA + dA * (1.f - sA);
    if (outA < 1e-6f) return 0u;
    float inv = 1.f / outA;

    float outR = (sR * sA + dR * dA * (1.f - sA)) * inv;
    float outG = (sG * sA + dG * dA * (1.f - sA)) * inv;
    float outB = (sB * sA + dB * dA * (1.f - sA)) * inv;

    auto clamp8 = [](float v) {
        return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
    };

    return (clamp8(outA) << 24) | (clamp8(outB) << 16) | (clamp8(outG) << 8) | clamp8(outR);
}

inline void floodFill(std::vector<uint32_t>& pixels, int w, int h,
                      int x, int y, uint32_t newColor)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0 || x >= w || y < 0 || y >= h) return;

    auto idx = [w](int px, int py) { return py * w + px; };
    uint32_t target = pixels[static_cast<size_t>(idx(x, y))];
    if (target == newColor) return;

    std::queue<std::pair<int, int>> q;
    q.push({x, y});

    while (!q.empty()) {
        auto [cx, cy] = q.front();
        q.pop();

        if (cx < 0 || cx >= w || cy < 0 || cy >= h) continue;
        int i = idx(cx, cy);
        if (pixels[static_cast<size_t>(i)] != target) continue;

        pixels[static_cast<size_t>(i)] = newColor;
        q.push({cx + 1, cy});
        q.push({cx - 1, cy});
        q.push({cx, cy + 1});
        q.push({cx, cy - 1});
    }
}

inline void drawLine(std::vector<uint32_t>& pixels, int w, int h,
                     int x0, int y0, int x1, int y1, uint32_t color)
{
    if (w <= 0 || h <= 0) return;
    auto inBounds = [w, h](int x, int y) { return x >= 0 && x < w && y >= 0 && y < h; };

    int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (true) {
        if (inBounds(x0, y0)) {
            pixels[static_cast<size_t>(y0 * w + x0)] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

} // namespace SpriteOps
