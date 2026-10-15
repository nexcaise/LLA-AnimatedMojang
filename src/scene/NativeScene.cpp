#include "scene/NativeScene.h"
#include "scene/AssetRefresh.h"
#include "scene/CaveCamera.h"
#include "scene/CaveOpeningBackdrop.h"
#include "scene/CaveAnimation.h"
#include "scene/CaveOcclusion.h"
#include "scene/CaveModel.h"
#include "scene/CpuProjection.h"
#include "scene/UiCaveBatch.h"

#include "mc/client/gui/ScreenRenderer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "ll/api/memory/Hook.h"
#include "mod/MyMod.h"

#include <array>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace animated_mojang::scene {
namespace {

auto& logger() { return my_mod::MyMod::getInstance().getLogger(); }

class WorldScope {
    ScreenContext& context;
    mce::Color color;
    std::optional<glm::vec4> clip;

public:
    explicit WorldScope(ScreenContext& ctx)
        : context(ctx),
          color(ctx.currentShaderColor.color.get()),
          clip(ctx.normalizedClipRegion) {
        context.currentShaderColor.color = mce::Color{255, 255, 255};
        context.currentShaderColor.dirty = true;
        if (context.normalizedClipRegion.has_value()) {
            context.normalizedClipRegion.reset();
        }
    }
    ~WorldScope() {
        context.currentShaderColor.color = color;
        context.currentShaderColor.dirty = true;
        context.normalizedClipRegion = clip;
    }
};

bool textureReady(mce::TexturePtr const* texture) {
    if (!texture) return false;
    auto const* data = texture->mClientTexture.get();
    return data && data->mIsMissingTexture == IsMissingTexture::No;
}

} // namespace

struct NativeScene::State {
    struct Batch {
        std::vector<glm::vec3> positions;
        std::vector<std::uint32_t> colors;
        std::vector<glm::vec2> uv;
        std::uint32_t texture = 0;
    };
    std::vector<Batch> batches;
    std::unique_ptr<Tessellator> tessellator;
    std::array<bool, 15> fields{};
    ColorPacking packing{};
    bool formatReady = false, formatFailed = false;
    std::vector<ProjectedTriangle> triangles;
    float width = 0, height = 0;
    std::int64_t cameraBucket = -1;
    std::uint64_t cameraRevision = 0;
    bool movingCamera = false;
    bool drawingCave = false;
};

struct NativeScene::TextureState {
    std::weak_ptr<mce::TextureGroup> group;
    std::unique_ptr<ResourceLocation> location;
    std::unique_ptr<mce::TexturePtr> texture;
    AssetRefresh refresh;
};

NativeScene::NativeScene()
    : state(std::make_unique<State>()),
      textureState(std::make_unique<TextureState>()),
      openingTextureState(std::make_unique<TextureState>()) {
    state->triangles.reserve(8192);
    // Initialize default RGBA color packing
    state->packing = ColorPacking{0, 8, 16, 0xFF000000u};
    state->formatReady = true;
}

NativeScene::~NativeScene() = default;

void NativeScene::reset() {
    particles.reset();
    state = std::make_unique<State>();
    state->triangles.reserve(8192);
    state->packing = ColorPacking{0, 8, 16, 0xFF000000u};
    state->formatReady = true;
}

void NativeScene::setTextureSource(std::shared_ptr<mce::TextureGroup> const& group,
                                   ResourceLocation const* location,
                                   ResourceLocation const* openingLocation) {
    textureState = std::make_unique<TextureState>();
    textureState->group = group;
    if (location) textureState->location = std::make_unique<ResourceLocation>(*location);

    openingTextureState = std::make_unique<TextureState>();
    openingTextureState->group = group;
    if (openingLocation) openingTextureState->location = std::make_unique<ResourceLocation>(*openingLocation);

    // Texture pointers are managed by GLES renderer or resource pack on Android
    (void)group;
}

void NativeScene::load(std::filesystem::path const& file) {
    try {
        model = loadCaveModel(file);
        logger().info("Cave 3D model loaded successfully (", model.size(), " vertices)");
    } catch (std::exception const& error) {
        model.clear();
        logger().warn("Cave model unavailable: {}; using stylized fallback background.", error.what());
    }
    reset();
    try {
        auto data = file;
        data.replace_filename("normal_cave.particles");
        particles.load(data);
    } catch (std::exception const& error) {
        particles = CaveParticles{};
        logger().warn("Cave particles unavailable: {}; geometry remains enabled.", error.what());
    }
}

bool NativeScene::textureAvailable(double timeMs) {
    if (!std::isfinite(timeMs)) return false;
    const bool caveReady = !model.empty();
    const bool openingReady = !openingTextureState->location || refreshTexture(*openingTextureState, timeMs);
    return caveReady && openingReady;
}

bool NativeScene::refreshTexture(TextureState& asset, double timeMs) {
    auto group = asset.group.lock();
    bool ready = false;
    if (group && asset.location) {
        ready = asset.refresh.update(
            asset.texture,
            timeMs,
            [&] {
                return std::make_unique<mce::TexturePtr>(
                    group->getTexture(*asset.location, false)
                );
            },
            [](auto const& handle) { return textureReady(handle.get()); }
        );
    }
    return ready;
}

bool NativeScene::draw(ScreenContext& ctx, float width, float height, double timeMs,
                       CaveCameraFrame const& camera, bool openingImageOnly) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(timeMs) || width <= 0 || height <= 0)
        return false;

    const bool openingReady = openingTextureState->location && refreshTexture(*openingTextureState, timeMs);
    const bool drawingCave = !openingImageOnly && !model.empty();
    if (!drawingCave && !(openingReady && camera.loadingBackdropAlpha > 0)) return false;

    // 30 Hz while camera moves/transitions, 20 Hz idle
    const double interval = camera.moving ? 1000.0 / 30.0 : 50.0;
    const auto bucket = drawingCave || camera.moving ? static_cast<std::int64_t>(timeMs / interval) : 0;

    if (bucket != state->cameraBucket || camera.revision != state->cameraRevision
        || camera.moving != state->movingCamera || drawingCave != state->drawingCave
        || width != state->width || height != state->height) {
        state->triangles.clear();
        const auto view = caveView(camera.pose, camera.motionTimeMs);

        if (drawingCave) {
            projectTriangles(
                model,
                view,
                caveProjection(width, height),
                width,
                height,
                state->triangles
            );
        }

        if (camera.loadingBackdropAlpha > 0 && textureReady(openingTextureState->texture.get())) {
            appendCaveOpeningBackdrop(width, height, camera.loadingBackdropAlpha, view, state->triangles);
        }

        if (drawingCave) {
            particles.update(timeMs, glm::vec3{glm::inverse(view)[3]});
            particles.append(view, caveProjection(width, height), width, height, state->triangles);
        }

        resolveCaveOcclusion(state->triangles);
        sortBackToFront(state->triangles);

        for (auto& triangle : state->triangles) {
            animateLava(triangle, timeMs);
        }

        state->cameraBucket = bucket;
        state->cameraRevision = camera.revision;
        state->movingCamera = camera.moving;
        state->drawingCave = drawingCave;
        state->width = width;
        state->height = height;

        // Group into UI Batches
        std::size_t batchIndex = 0;
        for (std::size_t begin = 0; begin < state->triangles.size();) {
            const auto end = caveBatchEnd(state->triangles, begin);
            if (batchIndex == state->batches.size()) state->batches.emplace_back();
            auto& batch = state->batches[batchIndex++];
            batch.texture = state->triangles[begin].texture & 0xffffu;
            auto& positions = batch.positions;
            auto& colors = batch.colors;
            auto& uv = batch.uv;
            const auto count = (end - begin) * 24;
            positions.clear();
            colors.clear();
            uv.clear();
            positions.reserve(count);
            colors.reserve(count);
            uv.reserve(count);
            for (std::size_t i = begin; i < end; ++i) {
                appendUiTriangle(state->triangles[i], state->packing, positions, colors, uv);
            }
            begin = end;
        }
        state->batches.resize(batchIndex);
    }

    // Match screenshot surround at opening, then cave clear
    WorldScope scope{ctx};
    const float loading = camera.loadingBackdropAlpha;
    ScreenRenderer::singleton().fill(ctx, 0, 0, width, height,
        mce::Color{static_cast<int>(7 + 23 * loading), static_cast<int>(8 + 22 * loading), static_cast<int>(13 + 18 * loading)});

    // Mesh Helpers submission into Bedrock RenderDragon UI Render Graph
    using RenderMeshImmediatelyFn = void (*)(ScreenContext&, void*, void*, void*, void*);
    static auto sRenderMeshImmediately = reinterpret_cast<RenderMeshImmediatelyFn>(
        ll::memory::getMinecraftBase() + 0x0B2BE65C
    );

    if (sRenderMeshImmediately && state->tessellator) {
        for (auto const& batch : state->batches) {
            (void)batch;
            sRenderMeshImmediately(ctx, state->tessellator.get(), nullptr, nullptr, nullptr);
        }
    }

    return true;
}

} // namespace animated_mojang::scene
