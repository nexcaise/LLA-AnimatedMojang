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
#include "mcpe/offsets.h"
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

// Standalone in-menu / in-world lifecycle state tracking (zero external loader dependencies)
std::atomic_bool sIsMainMenu = true;
std::atomic_bool sIsInWorld = false;
std::atomic_bool sIsLoading = false;
std::atomic_bool sInsideCaveRender = false;

bool isMainMenuActive() {
    return active.load() && sIsMainMenu.load() && !sIsInWorld.load() && !sIsLoading.load();
}

double nowMs() {
    return std::chrono::duration<double, std::milli>(Clock::now() - runtime.epoch).count();
}

auto& logger() {
    return my_mod::MyMod::getInstance().getLogger();
}

bool matches(std::vector<std::string> const& names, std::string const& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
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
// Reverse-Engineered Standalone Hooks (Verified 1.26.52.3 Android ARM64)
// ----------------------------------------------------------------------------

// 1. ui::ScreenTechStackSelector::getTechStackForScreen (RVA 0x0974E844)
using GetTechStackFn = int (*)(void*, std::string const&);
static GetTechStackFn sOriginalGetTechStack = nullptr;

static int Hooked_ScreenTechStackSelector_getTechStackForScreen(void* self, std::string const& name) {
    if (active.load()) {
        if (name == "/main-menu" || name == "start_screen" || name == "/pre-start"
            || name == "/start-from-template" || matches(runtime.config.menuScreenNames, name)) {
            return 0; // TechStack::JsonUI (0)
        }
    }
    if (sOriginalGetTechStack) {
        return sOriginalGetTechStack(self, name);
    }
    return 0;
}

// 2. StartMenuScreenController::$onOpen (RVA 0x0A2DF314)
using MenuOpenFn = void (*)(void*);
static MenuOpenFn sOriginalMenuOpen = nullptr;

static void Hooked_StartMenuScreenController_onOpen(void* self) {
    if (active.load()) {
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
        std::lock_guard guard(runtime.mutex);
        if (runtime.enteredMenu && runtime.config.replayOnReturnToMenu) {
            runtime.pendingReplay = true;
        }
        runtime.enteredMenu = true;
        runtime.currentPage = scene::CavePage::Main;
        logger().info("StartMenu opened: Transitioning cave camera to MainMenu idle state.");
    }
    if (sOriginalMenuOpen) {
        sOriginalMenuOpen(self);
    }
}

// 2a-2f. StartMenu Direct Button Click Hooks (Exact Verified RVAs)
using GenericButtonFn = void (*)(void*, void*, void*);

static GenericButtonFn sOriginalStartMenuPlayButton = nullptr;
static void Hooked_StartMenu_PlayButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Play button clicked -> Switching cave camera to Worlds");
        runtime.currentPage = scene::CavePage::Worlds;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
    if (sOriginalStartMenuPlayButton) {
        sOriginalStartMenuPlayButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalStartMenuSettingsButton = nullptr;
static void Hooked_StartMenu_SettingsButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Settings button clicked -> Switching cave camera to Settings");
        runtime.currentPage = scene::CavePage::Settings;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
    if (sOriginalStartMenuSettingsButton) {
        sOriginalStartMenuSettingsButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalStartMenuRealmsButton = nullptr;
static void Hooked_StartMenu_RealmsButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Realms button clicked -> Switching cave camera to Realms");
        runtime.currentPage = scene::CavePage::Realms;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
    if (sOriginalStartMenuRealmsButton) {
        sOriginalStartMenuRealmsButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalStartMenuDressingRoomButton = nullptr;
static void Hooked_StartMenu_DressingRoomButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Dressing Room button clicked -> Switching cave camera to DressingRoom");
        runtime.currentPage = scene::CavePage::DressingRoom;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
    if (sOriginalStartMenuDressingRoomButton) {
        sOriginalStartMenuDressingRoomButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalStartMenuMarketplaceButton = nullptr;
static void Hooked_StartMenu_MarketplaceButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("StartMenu: Marketplace button clicked -> Switching cave camera to Marketplace");
        runtime.currentPage = scene::CavePage::Marketplace;
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
    if (sOriginalStartMenuMarketplaceButton) {
        sOriginalStartMenuMarketplaceButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalMenuExit = nullptr;
static void Hooked_Menu_Exit(void* a0, void* a1, void* a2) {
    if (active.load() && !sIsInWorld.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("Menu Back/Exit clicked -> Transitioning cave camera back to MainMenu");
        runtime.currentPage = scene::CavePage::Main;
    }
    if (sOriginalMenuExit) {
        sOriginalMenuExit(a0, a1, a2);
    }
}

// 3. ProgressBarRenderer::$render (RVA 0x09685F70)
using ProgressRenderFn = void (*)(void*, MinecraftUIRenderContext&, void*, void*, int);
static ProgressRenderFn sOriginalProgressRender = nullptr;

static void Hooked_ProgressBarRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    if (active.load()) {
        sIsLoading.store(true);
        sIsMainMenu.store(false);
        sIsInWorld.store(false);

        if (runtime.config.restyleLoadingProgress && self) {
            auto* primary = reinterpret_cast<mce::Color*>(reinterpret_cast<uintptr_t>(self) + 0xC0);
            primary->r = 239.0f / 255.0f;
            primary->g = 50.0f / 255.0f;
            primary->b = 61.0f / 255.0f;
            primary->a = 1.0f;

            auto* secondary = reinterpret_cast<mce::Color*>(reinterpret_cast<uintptr_t>(self) + 0xD0);
            secondary->r = 71.0f / 255.0f;
            secondary->g = 29.0f / 255.0f;
            secondary->b = 34.0f / 255.0f;
            secondary->a = 1.0f;
        }
    }
    if (sOriginalProgressRender) {
        sOriginalProgressRender(self, ctx, client, owner, layer);
    }
}

// 4. InGamePlayScreen::render (RVA 0x09ACC828)
using InGamePlayScreenRenderFn = void (*)(void*, void*, const void*);
static InGamePlayScreenRenderFn sOriginalInGamePlayScreenRender = nullptr;

static void Hooked_inGamePlayScreenRender(void* self, void* screenContext, const void* frameObj) {
    if (active.load()) {
        sIsInWorld.store(true);
        sIsMainMenu.store(false);
        sIsLoading.store(false);
    }
    if (sOriginalInGamePlayScreenRender) {
        sOriginalInGamePlayScreenRender(self, screenContext, frameObj);
    }
}

// 5. InGamePlayScreen::_renderLevel (RVA 0x09ACD4C8)
using InGamePlayScreenRenderLevelFn = void (*)(void*, void*, const void*);
static InGamePlayScreenRenderLevelFn sOriginalInGamePlayScreenRenderLevel = nullptr;

static void Hooked_inGamePlayScreenRenderLevel(void* self, void* screenContext, const void* frameObj) {
    if (active.load()) {
        sIsInWorld.store(true);
    }
    if (sOriginalInGamePlayScreenRenderLevel) {
        sOriginalInGamePlayScreenRenderLevel(self, screenContext, frameObj);
    }
}

// 6. ClientInstance::requestLeaveGame (RVA 0x09800FF8)
using LeaveGameFn = void (*)(void*, bool, bool);
static LeaveGameFn sOriginalClientInstanceRequestLeaveGame = nullptr;

static void Hooked_clientInstanceRequestLeaveGame(void* self, bool b1, bool b2) {
    if (active.load()) {
        sIsInWorld.store(false);
        sIsMainMenu.store(true);
        sIsLoading.store(false);
        std::lock_guard guard(runtime.mutex);
        runtime.currentPage = scene::CavePage::Main;
        logger().info("Leave game detected -> Main menu restored.");
    }
    if (sOriginalClientInstanceRequestLeaveGame) {
        sOriginalClientInstanceRequestLeaveGame(self, b1, b2);
    }
}

// 7. PanoramaRenderer::$render (RVA 0x0967FA98)
// Direct functional suppression of vanilla panorama (zero resource pack needed)
using PanoramaRenderFn = void (*)(void*, MinecraftUIRenderContext&, void*, void*, int);
static PanoramaRenderFn sOriginalPanoramaRender = nullptr;

static void Hooked_PanoramaRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    if (isMainMenuActive() && runtime.config.caveBackground) {
        return; // Suppress vanilla panorama
    }
    if (sOriginalPanoramaRender) {
        sOriginalPanoramaRender(self, ctx, client, owner, layer);
    }
}

// 8. CubemapBackgroundScreen::$render (RVA 0x09B7C9E0)
// Direct functional suppression of vanilla cubemap background screen (zero resource pack needed)
using CubemapRenderFn = void (*)(void*, ScreenContext&, const void*);
static CubemapRenderFn sOriginalCubemapRender = nullptr;

static void Hooked_CubemapBackgroundScreen_render(void* self, ScreenContext& ctx, const void* frameObject) {
    if (isMainMenuActive() && runtime.config.caveBackground) {
        return; // Suppress vanilla cubemap background screen
    }
    if (sOriginalCubemapRender) {
        sOriginalCubemapRender(self, ctx, frameObject);
    }
}

// 9. glClear via GOT Relocation Hook (RVA 0x132893d8)
// Executed on the GPU Render Thread with active, valid EGLContext
constexpr uintptr_t GL_CLEAR_GOT_RVA = 0x132893d8;
using GlClearFn = void (*)(GLbitfield);
static GlClearFn sOriginalGlClear = nullptr;

static void Hooked_glClear(GLbitfield mask) {
    if (sInsideCaveRender.load()) {
        if (sOriginalGlClear) sOriginalGlClear(mask);
        return;
    }

    if (sOriginalGlClear) {
        sOriginalGlClear(mask);
    }

    if ((mask & GL_COLOR_BUFFER_BIT) && isMainMenuActive() && runtime.config.caveBackground) {
        GLint currentFbo = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFbo);
        if (currentFbo != 0) return;

        GLint vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, vp);
        if (vp[2] >= 800 && vp[3] >= 450) {
            const double time = nowMs();
            static double sLastRenderMs = 0.0;
            if (time - sLastRenderMs < 8.0) return;
            sLastRenderMs = time;

            sInsideCaveRender.store(true);
            {
                std::lock_guard guard(runtime.mutex);
                updatePresentation(time, runtime.enteredMenu, true);
                runtime.cameraFrame = runtime.camera.update(time, runtime.currentPage, runtime.frame);

                scene::CavePose pose = runtime.cameraFrame.pose;
                const double motionTime = runtime.cameraFrame.motionTimeMs > 0.0 ? runtime.cameraFrame.motionTimeMs : time;
                const auto view = scene::caveView(pose, motionTime);
                runtime.caveRenderer.render(static_cast<float>(vp[2]), static_cast<float>(vp[3]), view, time);

                // Animated Title rendering directly on GPU render thread (zero RP requirement)
                if (runtime.currentPage == scene::CavePage::Main && runtime.frame.titleFrame >= 0 && runtime.config.minecraftTitleAnimation) {
                    runtime.caveRenderer.renderTitle(
                        static_cast<float>(vp[2]),
                        static_cast<float>(vp[3]),
                        runtime.frame.titleFrame,
                        1.0f
                    );
                }
            }
            sInsideCaveRender.store(false);
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
    sIsMainMenu.store(true);
    sIsInWorld.store(false);
    sIsLoading.store(false);

    logger().info("Installing Animated Mojang Standalone v1.0.0 hooks (zero RP, zero Lamroid dependency)...");

    // Hook 1: Force JSON UI for start menu screens (RVA 0x0974E844)
    ll::memory::hookRva(0x0974E844,
                        reinterpret_cast<void*>(&Hooked_ScreenTechStackSelector_getTechStackForScreen),
                        reinterpret_cast<void**>(&sOriginalGetTechStack));

    // Hook 2: StartMenuScreenController::$onOpen (RVA 0x0A2DF314)
    ll::memory::hookRva(0x0A2DF314,
                        reinterpret_cast<void*>(&Hooked_StartMenuScreenController_onOpen),
                        reinterpret_cast<void**>(&sOriginalMenuOpen));

    // Hook 2a-2f: StartMenu Navigation Buttons
    ll::memory::hookRva(0x0A31344C,
                        reinterpret_cast<void*>(&Hooked_StartMenu_PlayButton),
                        reinterpret_cast<void**>(&sOriginalStartMenuPlayButton));

    ll::memory::hookRva(0x0A3171AC,
                        reinterpret_cast<void*>(&Hooked_StartMenu_SettingsButton),
                        reinterpret_cast<void**>(&sOriginalStartMenuSettingsButton));

    ll::memory::hookRva(0x0A3174DC,
                        reinterpret_cast<void*>(&Hooked_StartMenu_RealmsButton),
                        reinterpret_cast<void**>(&sOriginalStartMenuRealmsButton));

    ll::memory::hookRva(0x0A31815C,
                        reinterpret_cast<void*>(&Hooked_StartMenu_DressingRoomButton),
                        reinterpret_cast<void**>(&sOriginalStartMenuDressingRoomButton));

    ll::memory::hookRva(0x0A3187A4,
                        reinterpret_cast<void*>(&Hooked_StartMenu_MarketplaceButton),
                        reinterpret_cast<void**>(&sOriginalStartMenuMarketplaceButton));

    ll::memory::hookRva(0x0A567540,
                        reinterpret_cast<void*>(&Hooked_Menu_Exit),
                        reinterpret_cast<void**>(&sOriginalMenuExit));

    // Hook 3: Loading Progress Bar (RVA 0x09685F70)
    ll::memory::hookRva(0x09685F70,
                        reinterpret_cast<void*>(&Hooked_ProgressBarRenderer_render),
                        reinterpret_cast<void**>(&sOriginalProgressRender));

    // Hook 4: In-Game HUD Render (RVA 0x09ACC828)
    ll::memory::hookRva(0x09ACC828,
                        reinterpret_cast<void*>(&Hooked_inGamePlayScreenRender),
                        reinterpret_cast<void**>(&sOriginalInGamePlayScreenRender));

    // Hook 5: In-Game World Level Render (RVA 0x09ACD4C8)
    ll::memory::hookRva(0x09ACD4C8,
                        reinterpret_cast<void*>(&Hooked_inGamePlayScreenRenderLevel),
                        reinterpret_cast<void**>(&sOriginalInGamePlayScreenRenderLevel));

    // Hook 6: ClientInstance::requestLeaveGame (RVA 0x09800FF8)
    ll::memory::hookRva(0x09800FF8,
                        reinterpret_cast<void*>(&Hooked_clientInstanceRequestLeaveGame),
                        reinterpret_cast<void**>(&sOriginalClientInstanceRequestLeaveGame));

    // Hook 7: PanoramaRenderer::$render suppression (RVA 0x0967FA98)
    ll::memory::hookRva(0x0967FA98,
                        reinterpret_cast<void*>(&Hooked_PanoramaRenderer_render),
                        reinterpret_cast<void**>(&sOriginalPanoramaRender));

    // Hook 8: CubemapBackgroundScreen::$render suppression (RVA 0x09B7C9E0)
    ll::memory::hookRva(0x09B7C9E0,
                        reinterpret_cast<void*>(&Hooked_CubemapBackgroundScreen_render),
                        reinterpret_cast<void**>(&sOriginalCubemapRender));

    // Hook 9: glClear GOT Relocation Hook (RVA 0x132893d8)
    if (ll::memory::hookGotSlot(GL_CLEAR_GOT_RVA,
                                reinterpret_cast<void*>(&Hooked_glClear),
                                reinterpret_cast<void**>(&sOriginalGlClear))) {
        logger().info("Hooked glClear via GOT relocation at RVA 0x", std::hex, GL_CLEAR_GOT_RVA);
    }

    logger().info("Animated Mojang Standalone v1.0.0 hooks installed successfully!");
}

void stop() {
    active.store(false);
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
