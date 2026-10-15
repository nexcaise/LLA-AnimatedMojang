#pragma once

#include <cstdint>

namespace mce {

struct Color {
    float r{1.0f};
    float g{1.0f};
    float b{1.0f};
    float a{1.0f};

    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    constexpr Color(int r_, int g_, int b_, int a_ = 255)
        : r(r_ / 255.0f), g(g_ / 255.0f), b(b_ / 255.0f), a(a_ / 255.0f) {}

    static const Color WHITE;
    static const Color BLACK;
};

inline constexpr Color Color::WHITE{1.0f, 1.0f, 1.0f, 1.0f};
inline constexpr Color Color::BLACK{0.0f, 0.0f, 0.0f, 1.0f};

} // namespace mce
