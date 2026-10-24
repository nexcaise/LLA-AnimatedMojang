#include "scene/ImageDecode.h"

// The only translation unit that carries the stb_image implementation.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_GIF
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_TGA
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image.h"
#pragma GCC diagnostic pop

#include <system_error>

namespace animated_mojang::scene {

void ImagePixelsFree::operator()(std::uint8_t* p) const noexcept {
    stbi_image_free(p);
}

Rgba8Image decodeImageRgba(std::filesystem::path const& path, std::string* error) {
    Rgba8Image image;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        if (error) *error = "file not found: " + path.string();
        return image;
    }
    int w = 0, h = 0, channels = 0;
    std::uint8_t* px = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!px) {
        if (error) *error = "decode failed: " + path.string();
        return image;
    }
    image.width = w;
    image.height = h;
    image.pixels.reset(px);
    return image;
}

} // namespace animated_mojang::scene
