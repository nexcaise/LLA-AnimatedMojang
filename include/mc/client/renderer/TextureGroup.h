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

    virtual void loadTexturesAsync(std::span<ResourceLocation> /*locs*/, std::optional<int> = std::nullopt) {}

    virtual TexturePtr getTexture(const ResourceLocation& /*loc*/, bool /*fallback*/,
                                  std::optional<int> = std::nullopt,
                                  cg::TextureSetLayerType = cg::TextureSetLayerType::Color) {
        TexturePtr ptr;
        ptr.mClientTexture = std::make_shared<BedrockTextureData>();
        return ptr;
    }
};

} // namespace mce
