// Copyright (C) 2026 Soni Romanov
// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace crt {
// Monochrome tube simulation written for MU/TH/UR. Geometry, beam profile,
// phosphor texture and tone response are generated here without shader assets.
class Filter {
    struct Pixel {
        int left, right, upper, lower;
        std::array<std::uint32_t, 4> coverage;
        std::uint16_t light;
    };
    std::vector<Pixel> mapping;
    std::array<float, 256> phosphor{};

    static float beam(float position) {
        // Average three points across the output pixel to soften narrow gaps.
        float energy = 0;
        for (float offset : {-1.f / 3, 0.f, 1.f / 3}) {
            const float cycle = (position + offset) / 4.f;
            const float distance = cycle - std::floor(cycle) - .5f;
            energy += std::exp(-distance * distance / .085f);
        }
        return .66f + .47f * energy / 3.f;
    }
public:
    int width = 0, height = 0;

    void configure(int outputWidth, int outputHeight, int inputWidth, int inputHeight) {
        if (outputWidth < 1 || outputHeight < 1 || inputWidth < 1 || inputHeight < 1)
            throw std::invalid_argument("CRT dimensions must be positive");
        std::vector<Pixel> next(static_cast<size_t>(outputWidth) * outputHeight);
        for (unsigned level = 0; level < phosphor.size(); ++level) {
            // A gentle midtone lift preserves dark glass and bright lettering.
            const float intensity = level / 255.f;
            phosphor[level] = 255 * intensity * (1 + .12f * (1 - intensity));
        }
        for (int row = 0; row < outputHeight; ++row) {
            const float vertical = (2.f * row + 1) / outputHeight - 1;
            for (int column = 0; column < outputWidth; ++column) {
                const float horizontal = (2.f * column + 1) / outputWidth - 1;
                // Each axis bows more near the ends of the perpendicular axis.
                const float tubeX = horizontal * (1 + .038f * vertical * vertical);
                const float tubeY = vertical * (1 + .052f * horizontal * horizontal);
                Pixel& pixel = next[static_cast<size_t>(row) * outputWidth + column];
                if (std::abs(tubeX) > 1 || std::abs(tubeY) > 1) continue;
                const float sampleX = std::clamp((tubeX + 1) * inputWidth * .5f - .5f, 0.f, float(inputWidth - 1));
                const float sampleY = std::clamp((tubeY + 1) * inputHeight * .5f - .5f, 0.f, float(inputHeight - 1));
                pixel.left = static_cast<int>(sampleX);
                pixel.upper = static_cast<int>(sampleY);
                pixel.right = std::min(pixel.left + 1, inputWidth - 1);
                pixel.lower = std::min(pixel.upper + 1, inputHeight - 1);
                const auto across = static_cast<unsigned>((sampleX - pixel.left) * 256 + .5f);
                const auto down = static_cast<unsigned>((sampleY - pixel.upper) * 256 + .5f);
                pixel.coverage = {(256 - across) * (256 - down), across * (256 - down),
                    (256 - across) * down, across * down};
                const float edge = std::clamp(std::min((1 - std::abs(tubeX)) * outputWidth,
                    (1 - std::abs(tubeY)) * outputHeight), 0.f, 1.f);
                const float vignette = 1 - .065f * (horizontal * horizontal + vertical * vertical);
                // A green monochrome tube has no RGB triads. These faint seams
                // add phosphor structure without introducing coloured fringes.
                const float seam = column % 3 == 2 ? .96f : 1.f;
                pixel.light = static_cast<std::uint16_t>(1024 * edge * vignette * seam *
                    beam((tubeY + 1) * outputHeight * .5f) + .5f);
            }
        }
        mapping.swap(next);
        width = outputWidth; height = outputHeight;
    }

    void apply(const std::uint8_t* source, int sourceStride, std::uint8_t* destination, int destinationStride) const {
        size_t index = 0;
        for (int row = 0; row < height; ++row) {
            auto output = destination + static_cast<std::ptrdiff_t>(row) * destinationStride;
            for (int column = 0; column < width; ++column, ++index, output += 4) {
                const Pixel& pixel = mapping[index];
                output[0] = output[1] = output[2] = 0;
                output[3] = 255;
                if (!pixel.light) continue;
                const auto upper = source + static_cast<std::ptrdiff_t>(pixel.upper) * sourceStride;
                const auto lower = source + static_cast<std::ptrdiff_t>(pixel.lower) * sourceStride;
                std::uint32_t noise = static_cast<std::uint32_t>(column) * 374761393u +
                    static_cast<std::uint32_t>(row) * 668265263u + 1013904223u;
                noise = (noise ^ (noise >> 13)) * 1274126177u;
                noise ^= noise >> 16;
                const float rounding = (noise & 65535u) / 65536.f;
                for (int channel = 0; channel < 3; ++channel) {
                    const unsigned blended =
                        upper[pixel.left * 4 + channel] * pixel.coverage[0] +
                        upper[pixel.right * 4 + channel] * pixel.coverage[1] +
                        lower[pixel.left * 4 + channel] * pixel.coverage[2] +
                        lower[pixel.right * 4 + channel] * pixel.coverage[3];
                    // Keep fractional brightness through resampling and tone
                    // mapping; quantize just once, with stationary dithering.
                    const unsigned level = blended / 65536;
                    const float fraction = (blended % 65536) / 65536.f;
                    const float value = phosphor[level] +
                        (phosphor[std::min(level + 1, 255u)] - phosphor[level]) * fraction;
                    output[channel] = static_cast<std::uint8_t>(std::min(255.f,
                        value * pixel.light / 1024.f + rounding));
                }
            }
        }
    }
};
}
