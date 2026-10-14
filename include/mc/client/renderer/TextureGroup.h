#pragma once

#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"

namespace cg {
enum class TextureSetLayerType : int { Color = 0, Normal = 1, Height = 2 };
}

namespace mce {

class TextureGroup {
public:
    virtual ~TextureGroup() = default;
    virtual TexturePtr getTexture(const ResourceLocation& loc, bool fallback = false);
    virtual TexturePtr getTexture(
        const ResourceLocation& loc,
        bool fallback,
        std::optional<int> type,
        cg::TextureSetLayerType layer = cg::TextureSetLayerType::Color
    );
    virtual void unloadTexture(const ResourceLocation& loc);
};

} // namespace mce

