#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <vector>

namespace asciiplay {

enum class RenderMode {
    Gray,
    ANSI256,
    TrueColor
};

enum class DitherMode {
    Off,
    Bayer2,
    Bayer4
};

struct RGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct BayerMatrix {
    const int size;
    const std::vector<float> thresholds;
};

inline constexpr std::array<RGB, 16> ANSI_BASE_COLORS{ {
    {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
    {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
    {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
    {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255}
} };

inline std::array<RGB, 256> make_xterm_palette()
{
    std::array<RGB, 256> palette{};
    for (size_t i = 0; i < 16; ++i) palette[i] = ANSI_BASE_COLORS[i];

    size_t idx = 16;
    for (int r = 0; r < 6; ++r) {
        for (int g = 0; g < 6; ++g) {
            for (int b = 0; b < 6; ++b) {
                uint8_t rr = r == 0 ? 0 : 55 + r * 40;
                uint8_t gg = g == 0 ? 0 : 55 + g * 40;
                uint8_t bb = b == 0 ? 0 : 55 + b * 40;
                palette[idx++] = {rr, gg, bb};
            }
        }
    }

    for (int i = 0; i < 24; ++i) {
        uint8_t val = static_cast<uint8_t>(8 + i * 10);
        palette[idx++] = {val, val, val};
    }

    return palette;
}

inline const std::array<RGB, 256>& xterm_palette()
{
    static const std::array<RGB, 256> palette = make_xterm_palette();
    return palette;
}

inline int xterm_index_from_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    const auto& palette = xterm_palette();
    int bestIndex = 0;
    int bestDist = std::numeric_limits<int>::max();
    for (int i = 0; i < 256; ++i) {
        int dr = static_cast<int>(palette[i].r) - r;
        int dg = static_cast<int>(palette[i].g) - g;
        int db = static_cast<int>(palette[i].b) - b;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            bestIndex = i;
        }
    }
    return bestIndex;
}

inline float luminance(uint8_t r, uint8_t g, uint8_t b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

inline float apply_gamma(float value, float gamma)
{
    value = std::clamp(value / 255.0f, 0.0f, 1.0f);
    value = std::pow(value, 1.0f / gamma);
    return std::clamp(value, 0.0f, 1.0f);
}

inline float apply_contrast(float value, float contrast)
{
    float centered = value - 0.5f;
    centered *= contrast;
    return std::clamp(centered + 0.5f, 0.0f, 1.0f);
}

inline const BayerMatrix& bayer_matrix(DitherMode mode)
{
    static const BayerMatrix off{1, {0.0f}};
    static const BayerMatrix bayer2{2, {
        0.0f / 4.0f, 2.0f / 4.0f,
        3.0f / 4.0f, 1.0f / 4.0f
    }};
    static const BayerMatrix bayer4{4, {
        0.0f / 16.0f, 8.0f / 16.0f, 2.0f / 16.0f, 10.0f / 16.0f,
        12.0f / 16.0f, 4.0f / 16.0f, 14.0f / 16.0f, 6.0f / 16.0f,
        3.0f / 16.0f, 11.0f / 16.0f, 1.0f / 16.0f, 9.0f / 16.0f,
        15.0f / 16.0f, 7.0f / 16.0f, 13.0f / 16.0f, 5.0f / 16.0f
    }};

    switch (mode) {
    case DitherMode::Bayer2:
        return bayer2;
    case DitherMode::Bayer4:
        return bayer4;
    default:
        return off;
    }
}

inline uint32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

inline RGB unpack_rgb(uint32_t value)
{
    return RGB{
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>(value & 0xFF)
    };
}

} // namespace asciiplay
