#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace animated_mojang::scene {

// Bit set in Vertex::texture for faces that sample the animated lava strip.
inline constexpr std::uint32_t animatedLavaFlag = 0x10000;

// Cave atlas layout: 256x768, lava occupies a 16px-per-frame strip, 32 frames.
inline constexpr float caveAtlasWidth  = 256.f;
inline constexpr float caveAtlasHeight = 768.f;
inline constexpr int   lavaFrameCount  = 32;
inline constexpr float lavaFramePx     = 16.f;

inline unsigned lavaFrame(double timeMs) {
    return static_cast<unsigned>(std::fmod(std::floor(std::max(0.0, timeMs) / 300.0), double(lavaFrameCount)));
}
// V offset (in UV space) for the current lava animation frame.
inline float lavaOffsetV(double timeMs) {
    return static_cast<float>(lavaFrame(timeMs)) * (lavaFramePx / caveAtlasHeight);
}

} // namespace animated_mojang::scene
