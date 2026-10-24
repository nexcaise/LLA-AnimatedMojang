#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace animated_mojang::scene {

struct ImagePixelsFree {
    void operator()(std::uint8_t* p) const noexcept;
};

// Decoded RGBA8 image, row 0 = top. Move-only; owns the decoder's buffer (no extra copy).
struct Rgba8Image {
    int width = 0;
    int height = 0;
    std::unique_ptr<std::uint8_t, ImagePixelsFree> pixels;

    explicit operator bool() const { return pixels && width > 0 && height > 0; }
    const std::uint8_t* data() const { return pixels.get(); }
    std::size_t byteSize() const { return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u; }
};

// Thread-safe (no shared state). Returns an empty image and fills `error` on failure.
Rgba8Image decodeImageRgba(std::filesystem::path const& path, std::string* error = nullptr);

} // namespace animated_mojang::scene
