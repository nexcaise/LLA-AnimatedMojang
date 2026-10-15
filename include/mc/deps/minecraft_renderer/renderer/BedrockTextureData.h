#pragma once

#include <memory>
#include <cstdint>

enum class IsMissingTexture : int {
    No = 0,
    Yes = 1
};

enum class TextureLoadState : unsigned {
    Unloaded = 0,
    LoadedBit = 1
};

struct BedrockTextureData {
    IsMissingTexture mIsMissingTexture{IsMissingTexture::No};
    TextureLoadState mTextureLoadState{TextureLoadState::LoadedBit};
    std::shared_ptr<void> mClientTexture{nullptr};
};

namespace mce {

struct TexturePtr {
    std::shared_ptr<BedrockTextureData> mClientTexture;
};

} // namespace mce
