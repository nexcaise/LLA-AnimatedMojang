#include "client/Client.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>
#include <array>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "animation/Timeline.h"
#include "ll/api/Logger.h"
#include "ll/api/memory/Hook.h"
#include "levilamroid/LeviLamroid.h"
#include "ll/api/ui/UI.h"
#include "ll/api/render/Render.h"
#include "mod/MyMod.h"
#include "scene/NativeScene.h"
#include "scene/CaveCamera.h"
#include "scene/CaveGlesRenderer.h"
#include "scene/AssetRefresh.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/ScreenRenderer.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"

namespace animated_mojang::client {
namespace {

using Clock = std::chrono::steady_clock;

struct Runtime {
    Config config;
    Timeline timeline;
    std::filesystem::path resources;
    Clock::time_point epoch = Clock::now();
    std::vector<ResourceLocation> locations;
    std::vector<ResourceLocation> caveLocations;
    std::vector<std::unique_ptr<mce::TexturePtr>> textures;
    std::array<scene::AssetRefresh, 9> titleRefresh;
    std::weak_ptr<mce::TextureGroup> textureGroup;
    std::recursive_mutex mutex;
    scene::NativeScene nativeScene;
    scene::CaveGlesRenderer caveRenderer;
    scene::CaveCameraController camera;
    scene::CaveCameraFrame cameraFrame;
    scene::CavePage currentPage = scene::CavePage::Main;
    bool enteredMenu = false;
    bool pendingReplay = false;
    bool assetsTimedOut = false;
    bool warnedTitle = false;
    double firstMenuRenderMs = -1;
    Frame frame;
};

Runtime runtime;
std::atomic_bool active = false;
std::atomic_bool skipRequested = false;
std::atomic_bool skipAllowed = false;

struct RenderScope {
    bool menu = false;
    bool bootProgress = false;
    Frame frame;
    glm::vec2 size{0.0f, 0.0f};
    bool backgroundDrawn = false;
};

thread_local RenderScope* currentScope = nullptr;

class ScopedRender {
    RenderScope* previous;
public:
    explicit ScopedRender(RenderScope& value) : previous(currentScope) { currentScope = &value; }
    ~ScopedRender() { currentScope = previous; }
};

double nowMs() {
    return std::chrono::duration<double, std::milli>(Clock::now() - runtime.epoch).count();
}

auto& logger() {
    return my_mod::MyMod::getInstance().getLogger();
}

void updatePresentation(double time, bool foregroundMenu, bool ready) {
    if (foregroundMenu && runtime.pendingReplay) {
        runtime.pendingReplay = false;
        runtime.timeline = Timeline{Timing{.maxPresentationMs = runtime.config.maxPresentationSeconds * 1000.0}};
        runtime.camera.replay();
        runtime.firstMenuRenderMs = -1;
        runtime.assetsTimedOut = runtime.warnedTitle = false;
    }
    if (foregroundMenu) {
        runtime.enteredMenu = true;
        if (runtime.firstMenuRenderMs < 0) {
            runtime.firstMenuRenderMs = time;
        }
    }
    if (skipRequested.exchange(false)) runtime.timeline.skip();
    runtime.frame = runtime.timeline.update(time, foregroundMenu, ready, runtime.config.minecraftTitleAnimation);
    skipAllowed.store(foregroundMenu && runtime.frame.phase != Phase::MenuIdle);
}

// ----------------------------------------------------------------------------
// High-Level Human Callbacks (Driven by LeviLamroid Core Engine)
// ----------------------------------------------------------------------------

void onStartMenuOpen() {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        if (runtime.enteredMenu && runtime.config.replayOnReturnToMenu) {
            runtime.pendingReplay = true;
        }
        runtime.enteredMenu = true;
        runtime.currentPage = scene::CavePage::Main;
        logger().info("StartMenu opened: Transitioning cave camera to MainMenu idle state.");
    }
}

void onPlayButtonClicked() {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Play button clicked -> Switching cave camera to Worlds");
        runtime.currentPage = scene::CavePage::Worlds;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
}

void onSettingsButtonClicked() {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Settings button clicked -> Switching cave camera to Settings");
        runtime.currentPage = scene::CavePage::Settings;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
}

void onRealmsButtonClicked() {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Realms button clicked -> Switching cave camera to Realms");
        runtime.currentPage = scene::CavePage::Realms;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
}

void onDressingRoomButtonClicked() {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Dressing Room button clicked -> Switching cave camera to DressingRoom");
        runtime.currentPage = scene::CavePage::DressingRoom;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
}

void onMarketplaceButtonClicked() {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Marketplace button clicked -> Switching cave camera to Marketplace");
        runtime.currentPage = scene::CavePage::Marketplace;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
}

void onMenuExit() {
    if (active.load() && ll::isMainMenu() && !ll::isInWorld()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("Menu Back/Exit clicked -> Transitioning cave camera back to MainMenu");
        runtime.currentPage = scene::CavePage::Main;
    }
}

void onPostClear(int width, int height) {
    if (width >= 800 && height >= 450 && active.load() && runtime.config.caveBackground && runtime.enteredMenu && ll::isMainMenu() && !ll::isInWorld() && !ll::isLoading()) {
        const double time = nowMs();
        static double sLastRenderMs = 0.0;
        // Throttle cave rendering to at most once per 8ms (caps at 120 FPS, prevents duplicate clears per frame)
        if (time - sLastRenderMs < 8.0) {
            return;
        }
        sLastRenderMs = time;

        static uint32_t sRenderCount = 0;
        if (++sRenderCount % 180 == 1) {
            logger().info("[GPU Render Thread] Cave post-clear render pass active (", width, "x", height, ")");
        }
        std::lock_guard guard(runtime.mutex);
        updatePresentation(time, runtime.enteredMenu, true);
        runtime.cameraFrame = runtime.camera.update(time, runtime.currentPage, runtime.frame);

        scene::CavePose pose = runtime.cameraFrame.pose;
        const double motionTime = runtime.cameraFrame.motionTimeMs > 0.0 ? runtime.cameraFrame.motionTimeMs : time;
        const auto view = scene::caveView(pose, motionTime);
        runtime.caveRenderer.render(static_cast<float>(width), static_cast<float>(height), view, time);

        // Animated Title rendering directly on GPU render thread (fallback when not using JSON UI)
        if (runtime.currentPage == scene::CavePage::Main && runtime.frame.titleFrame >= 0 && runtime.config.minecraftTitleAnimation && !runtime.config.preferJsonMainMenu) {
            runtime.caveRenderer.renderTitle(
                static_cast<float>(width),
                static_cast<float>(height),
                runtime.frame.titleFrame,
                1.0f
            );
        }
    }
}


} // namespace

void prepareCave(std::filesystem::path const& modDirectory) {
    std::lock_guard guard(runtime.mutex);
    auto meshPath = modDirectory / "resources" / "data" / "normal_cave.mesh";
    auto atlasPath = modDirectory / "resources" / "textures" / "animated_mojang" / "cave" / "atlas.png";
    auto titleDir = modDirectory / "resources" / "textures" / "animated_mojang" / "title" / "minecraft_animation";
    runtime.caveRenderer.loadModel(meshPath, atlasPath);
    runtime.caveRenderer.loadTitleAnimation(titleDir);
    runtime.nativeScene.load(meshPath);
}

void start(Config const& config, std::filesystem::path const& modDirectory) {
    if (active.load() || !config.enabled) return;

    {
        std::lock_guard guard(runtime.mutex);
        runtime.config = config;
        runtime.resources = std::filesystem::absolute(modDirectory / "resources");
        runtime.epoch = Clock::now();
        runtime.timeline = Timeline{Timing{.maxPresentationMs = config.maxPresentationSeconds * 1000.0}};
        runtime.frame = {};
        runtime.camera.reset();
        runtime.cameraFrame = {};
        runtime.textures.clear();
        runtime.textureGroup.reset();
        runtime.titleRefresh = {};
        runtime.nativeScene.reset();
        runtime.nativeScene.setTextureSource({}, nullptr);
        runtime.locations.clear();
        runtime.caveLocations.clear();

        runtime.assetsTimedOut = runtime.enteredMenu = runtime.pendingReplay = false;
        runtime.warnedTitle = false;
        runtime.firstMenuRenderMs = -1;

        if (runtime.config.caveBackground) {
            auto meshPath = modDirectory / "resources" / "data" / "normal_cave.mesh";
            auto atlasPath = modDirectory / "resources" / "textures" / "animated_mojang" / "cave" / "atlas.png";
            auto titleDir = modDirectory / "resources" / "textures" / "animated_mojang" / "title" / "minecraft_animation";
            runtime.caveRenderer.loadModel(meshPath, atlasPath);
            runtime.caveRenderer.loadTitleAnimation(titleDir);
            runtime.nativeScene.load(meshPath);
        }
    }

    active.store(true);
    logger().info("Binding Animated Mojang to LeviLamroid human engine APIs...");

    // 1. Force JSON UI for start menu screens via LeviLamroid
    ll::ui::forceJsonUIForScreen("/main-menu");
    ll::ui::forceJsonUIForScreen("start_screen");
    ll::ui::forceJsonUIForScreen("/pre-start");
    ll::ui::forceJsonUIForScreen("/start-from-template");
    for (const auto& name : runtime.config.menuScreenNames) {
        ll::ui::forceJsonUIForScreen(name);
    }

    // 2. Register UI navigation callbacks
    ll::ui::onStartMenuOpen(&onStartMenuOpen);
    ll::ui::onPlayButtonClicked(&onPlayButtonClicked);
    ll::ui::onSettingsButtonClicked(&onSettingsButtonClicked);
    ll::ui::onRealmsButtonClicked(&onRealmsButtonClicked);
    ll::ui::onDressingRoomButtonClicked(&onDressingRoomButtonClicked);
    ll::ui::onMarketplaceButtonClicked(&onMarketplaceButtonClicked);
    ll::ui::onMenuExit(&onMenuExit);

    // 3. Configure background suppression via LeviLamroid (active only in menu, never in world)
    ll::render::suppressVanillaBackground(runtime.config.caveBackground);

    // 4. Register post-clear GPU rendering callback for 3D cave
    ll::render::registerPostClearCallback(&onPostClear);

    logger().info("Animated Mojang successfully bound to LeviLamroid with zero hardcoded hooks!");
}

void stop() {
    active.store(false);
    ll::render::suppressVanillaBackground(false);
    skipRequested.store(false);
    skipAllowed.store(false);
    std::lock_guard guard(runtime.mutex);
    runtime.caveRenderer.destroy();
    runtime.textures.clear();
    runtime.textureGroup.reset();
    runtime.nativeScene.reset();
    runtime.nativeScene.setTextureSource({}, nullptr);
}

} // namespace animated_mojang::client
