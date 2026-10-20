// Copyright (C) 2026 Soni Romanov
// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace display {
// A soft phosphor halo and a faint glass tint, applied before the CRT filter.
class Glass {
    std::vector<float> horizontal;
public:
    void apply(std::uint8_t* pixels, int stride, int w, int h) {
        horizontal.resize(static_cast<size_t>(w) * h);
        constexpr int radius = 6;
        constexpr float weights[] = {1, .94f, .78f, .57f, .37f, .21f, .11f};
        constexpr float total = 6.96f;
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            float sum = 0;
            for (int i = -radius; i <= radius; ++i)
                sum += pixels[y * stride + std::clamp(x + i, 0, w - 1) * 4 + 1] * weights[std::abs(i)];
            horizontal[y * w + x] = sum / total;
        }
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            float halo = 0;
            for (int i = -radius; i <= radius; ++i)
                halo += horizontal[std::clamp(y + i, 0, h - 1) * w + x] * weights[std::abs(i)];
            halo *= .28f / total;
            const float nx = 2.f * x / w - 1, ny = 2.f * y / h - 1;
            const float center = std::max(0.f, 1 - .55f * nx * nx - .65f * ny * ny);
            // Bottle-green glass, gently lifted at the center (BGR order).
            const float background[] = {12 + 10 * center, 21 + 15 * center, 4 + 4 * center};
            const float tint[] = {.32f, 1.f, .72f};
            auto p = pixels + y * stride + x * 4;
            // Stable sub-level dithering breaks up the visible rings produced
            // when a very dark, smooth gradient is reduced to 8-bit colour.
            std::uint32_t noise = static_cast<std::uint32_t>(x) * 374761393u +
                static_cast<std::uint32_t>(y) * 668265263u;
            noise = (noise ^ (noise >> 13)) * 1274126177u;
            noise ^= noise >> 16;
            const float rounding = (noise & 65535u) / 65536.f;
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<std::uint8_t>(std::min(255.f, p[c] + background[c] + halo * tint[c] + rounding));
        }
    }
};

// Original molded housing with a recessed tube, raised rim and diffuse reflections.
class Frame {
    struct Reflection { int x, y, sample; float fx, fy, gain; };
    static constexpr int rw = 160, rh = 90;
    int width = 0, height = 0;
    std::vector<std::uint8_t> base;
    std::vector<Reflection> reflections;
    std::vector<float> downsampled, horizontal, blurred;
    static std::vector<float> diffusionKernel(int radius, float sigma) {
        std::vector<float> kernel(2 * radius + 1);
        float total = 0;
        for (int i = -radius; i <= radius; ++i) {
            kernel[i + radius] = std::exp(-float(i * i) / (2 * sigma * sigma));
            total += kernel[i + radius];
        }
        for (float& weight : kernel) weight /= total;
        return kernel;
    }
public:
    void configure(int w, int h, int sw, int sh) {
        width = w; height = h;
        base.assign(static_cast<size_t>(w) * h * 4, 0);
        reflections.clear();
        downsampled.resize(rw * rh * 3); horizontal.resize(downsampled.size()); blurred.resize(downsampled.size());
        const int left = (w - sw) / 2, top = (h - sh) / 2;
        const float bevel = sh * .11f, radius = std::max(3.f, sh * .028f);
        auto roundedBox = [](float x, float y, float halfW, float halfH, float r) {
            const float qx = std::abs(x) - halfW + r, qy = std::abs(y) - halfH + r;
            const float ax = std::max(qx, 0.f), ay = std::max(qy, 0.f);
            return std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.f) - r;
        };
        auto ease = [](float value) {
            value = std::clamp(value, 0.f, 1.f);
            return value * value * (3 - 2 * value);
        };
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
            const float px = x - left + .5f, py = y - top + .5f;
            const float cx = px - sw * .5f, cy = py - sh * .5f;
            const float d = roundedBox(cx, cy, sw * .5f, sh * .5f, radius);
            // The lower sill is deeper; outer corners stay tight rather than
            // expanding into a rounded picture frame as the rim gets thicker.
            const float outer = roundedBox(cx, cy - bevel * .055f,
                sw * .5f + bevel, sh * .5f + bevel * 1.055f, sh * .045f);
            auto p = base.data() + (y * w + x) * 4;
            if (d < -.5f) continue;
            // Locate this pixel on a family of true rounded rectangles. Blending
            // inner/outer distance fields pinches the intermediate corner arcs.
            float t = outer >= 0 ? 1 + outer / bevel : 0;
            if (d > 0 && outer < 0) {
                float low = 0, high = 1;
                for (int step = 0; step < 14; ++step) {
                    const float middle = (low + high) * .5f;
                    const float contour = roundedBox(cx, cy - bevel * .055f * middle,
                        sw * .5f + bevel * middle, sh * .5f + bevel * 1.055f * middle,
                        radius + (sh * .045f - radius) * middle);
                    if (contour > 0) low = middle; else high = middle;
                }
                t = (low + high) * .5f;
            }
            const float contourT = std::clamp(t, 0.f, 1.f);
            const float contourRadius = radius + (sh * .045f - radius) * contourT;
            const float normalX = std::max(0.f, std::abs(cx) - (sw * .5f + bevel * contourT - contourRadius));
            const float normalY = std::max(0.f, std::abs(cy - bevel * .055f * contourT) -
                (sh * .5f + bevel * 1.055f * contourT - contourRadius));
            // Follow each circular arc's normal so light rolls smoothly between faces.
            const float sideWeight = normalX * normalX / std::max(normalX * normalX + normalY * normalY, .0001f);
            const float sideLight = cx < 0 ? .91f : .73f;
            const float verticalLight = cy < 0 ? .59f : 1.16f;
            const float wallLight = verticalLight + (sideLight - verticalLight) * sideWeight;
            const float outerLight = 1.035f - .125f * std::clamp(cy / (sh * .5f), -1.f, 1.f);
            float shade;
            if (t < .08f) shade = .16f + .12f * ease(t / .08f);
            else if (t < .22f) shade = .28f + (wallLight * .78f - .28f) * ease((t - .08f) / .14f);
            else if (t < .66f) shade = wallLight * (.78f + .25f * ease((t - .22f) / .44f));
            else if (t < .73f) shade = wallLight * (1.03f - .23f * ease((t - .66f) / .07f));
            else if (t < .83f) shade = wallLight * .80f + (outerLight - wallLight * .80f) * ease((t - .73f) / .10f);
            else if (t < .93f) shade = outerLight;
            else if (t < 1.f) shade = outerLight + (.44f - outerLight) * ease((t - .93f) / .07f);
            else {
                // Contact shadow separates the raised housing from the outer panel.
                shade = .79f - .53f * std::exp(-std::max(outer, 0.f) / (sh * .009f));
            }
            const auto grainHash = static_cast<std::uint32_t>(x) * 1973u ^ static_cast<std::uint32_t>(y) * 9277u;
            const float grain = (static_cast<float>((grainHash * 26699u) & 255u) / 255.f - .5f) * .018f;
            shade += grain;
            p[0] = static_cast<std::uint8_t>(43 * shade);
            p[1] = static_cast<std::uint8_t>(73 * shade);
            p[2] = static_cast<std::uint8_t>(68 * shade);
            p[3] = static_cast<std::uint8_t>(255 * std::clamp(d + .5f, 0.f, 1.f));
            if (t <= .06f || t >= .76f) continue;
            float u = std::clamp(px / sw, 0.f, 1.f), v = std::clamp(py / sh, 0.f, 1.f);
            // A shallow reflection stretches the diffused screen light across
            // the face instead of compressing individual text rows into stripes.
            const float depth = .08f + t * .28f;
            const float sideSample = px < sw * .5f ? depth : 1 - depth;
            const float verticalSample = py < sh * .5f ? depth : 1 - depth;
            u += (sideSample - u) * sideWeight;
            v += (verticalSample - v) * (1 - sideWeight);
            const float sampleX = std::clamp(u * (rw - 1), 0.f, float(rw - 1));
            const float sampleY = std::clamp(v * (rh - 1), 0.f, float(rh - 1));
            const int sx = std::min(static_cast<int>(sampleX), rw - 2);
            const int sy = std::min(static_cast<int>(sampleY), rh - 2);
            // Diffuse light reaches the recess, fading before the raised outer rim.
            const float envelope = ease((t - .06f) / .18f) * (1 - ease((t - .48f) / .28f));
            reflections.push_back({x, y, (sy * rw + sx) * 3, sampleX - sx, sampleY - sy,
                1.3f * envelope * (1 - .20f * t)});
        }
    }
    void apply(const std::uint8_t* src, int srcStride, int sw, int sh, std::uint8_t* dst, int stride) {
        // Area downsampling keeps moving text from sparkling in the reflection.
        for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x) {
            const int x0 = x * sw / rw, x1 = std::max(x0 + 1, (x + 1) * sw / rw);
            const int y0 = y * sh / rh, y1 = std::max(y0 + 1, (y + 1) * sh / rh);
            for (int c = 0; c < 3; ++c) {
                float sum = 0;
                for (int sy = y0; sy < y1; ++sy) for (int sx = x0; sx < x1; ++sx)
                    sum += src[sy * srcStride + sx * 4 + c];
                downsampled[(y * rw + x) * 3 + c] = sum / ((x1 - x0) * (y1 - y0));
            }
        }
        // Broad Gaussian diffusion merges text lines into gradual pools of light.
        // Normalized kernels keep brightness stable on a uniformly lit screen.
        static const auto kernelX = diffusionKernel(30, 10.f);
        static const auto kernelY = diffusionKernel(24, 8.f);
        for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x) for (int c = 0; c < 3; ++c) {
            float sum = 0;
            for (int i = -30; i <= 30; ++i)
                sum += downsampled[(y * rw + std::clamp(x + i, 0, rw - 1)) * 3 + c] * kernelX[i + 30];
            horizontal[(y * rw + x) * 3 + c] = sum;
        }
        for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x) for (int c = 0; c < 3; ++c) {
            float sum = 0;
            for (int i = -24; i <= 24; ++i)
                sum += horizontal[(std::clamp(y + i, 0, rh - 1) * rw + x) * 3 + c] * kernelY[i + 24];
            blurred[(y * rw + x) * 3 + c] = sum;
        }
        for (int y = 0; y < height; ++y)
            std::copy_n(base.data() + y * width * 4, width * 4, dst + y * stride);
        for (const auto& tap : reflections) {
            auto p = dst + tap.y * stride + tap.x * 4;
            for (int c = 0; c < 3; ++c) {
                const auto a = blurred.data() + tap.sample + c;
                const float top = a[0] + (a[3] - a[0]) * tap.fx;
                const float bottom = a[rw * 3] + (a[rw * 3 + 3] - a[rw * 3]) * tap.fx;
                const float light = std::max(0.f, top + (bottom - top) * tap.fy - 5);
                p[c] = static_cast<std::uint8_t>(std::min(255.f, p[c] + light * tap.gain));
            }
        }
    }
};
}
