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
#include "lowlamina/api/mcpe/Offsets.h"
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

struct BackgroundFrame {
    bool inside = false;
    bool native3DDrawn = false;
    bool presentationUpdated = false;
    bool pageResolved = false;
};

thread_local BackgroundFrame backgroundFrame;

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
// Reverse-Engineered Dobby Hooks (Verified for Bedrock 1.26.52.3 ARM64)
// ----------------------------------------------------------------------------

// 1. ui::ScreenTechStackSelector::getTechStackForScreen (RVA 0x0974E844)
using GetTechStackFn = int (*)(void*, std::string const&);
GetTechStackFn sOriginalGetTechStack = nullptr;

int Hooked_ScreenTechStackSelector_getTechStackForScreen(void* self, std::string const& name) {
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
MenuOpenFn sOriginalMenuOpen = nullptr;

void Hooked_StartMenuScreenController_onOpen(void* self) {
    if (active.load()) {
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

// 2a. StartMenu Direct Button Click Hooks (Exact Verified RVAs)
using GenericButtonFn = void (*)(void*, void*, void*);

GenericButtonFn sOriginalStartMenuPlayButton = nullptr;
void Hooked_StartMenu_PlayButton(void* a0, void* a1, void* a2) {
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

GenericButtonFn sOriginalStartMenuSettingsButton = nullptr;
void Hooked_StartMenu_SettingsButton(void* a0, void* a1, void* a2) {
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

GenericButtonFn sOriginalStartMenuRealmsButton = nullptr;
void Hooked_StartMenu_RealmsButton(void* a0, void* a1, void* a2) {
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

GenericButtonFn sOriginalStartMenuDressingRoomButton = nullptr;
void Hooked_StartMenu_DressingRoomButton(void* a0, void* a1, void* a2) {
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

GenericButtonFn sOriginalStartMenuMarketplaceButton = nullptr;
void Hooked_StartMenu_MarketplaceButton(void* a0, void* a1, void* a2) {
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

GenericButtonFn sOriginalMenuExit = nullptr;
void Hooked_Menu_Exit(void* a0, void* a1, void* a2) {
    if (active.load()) {
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
ProgressRenderFn sOriginalProgressRender = nullptr;

void Hooked_ProgressBarRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    if (active.load() && runtime.config.restyleLoadingProgress && self) {
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
    if (sOriginalProgressRender) {
        sOriginalProgressRender(self, ctx, client, owner, layer);
    }
}

// 4. PanoramaRenderer::$render (RVA 0x0967FA98)
using PanoramaRenderFn = void (*)(void*, MinecraftUIRenderContext&, void*, void*, int);
PanoramaRenderFn sOriginalPanoramaRender = nullptr;

void Hooked_PanoramaRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    if (active.load() && runtime.config.caveBackground) {
        static uint32_t sDropPano = 0;
        if (++sDropPano % 180 == 1) {
            logger().info("Hooked_PanoramaRenderer_render (0x0967FA98): Suppressed vanilla panorama rendering!");
        }
        return; // Suppress vanilla panorama rendering completely!
    }
    if (sOriginalPanoramaRender) {
        sOriginalPanoramaRender(self, ctx, client, owner, layer);
    }
}

// 5. CubemapBackgroundScreen::$render (RVA 0x09B7C9E0)
using CubemapRenderFn = void (*)(void*, ScreenContext&, const void*);
CubemapRenderFn sOriginalCubemapRender = nullptr;

void Hooked_CubemapBackgroundScreen_render(void* self, ScreenContext& ctx, const void* frameObject) {
    if (active.load() && runtime.config.caveBackground) {
        static uint32_t sDropScreen = 0;
        if (++sDropScreen % 180 == 1) {
            logger().info("Hooked_CubemapBackgroundScreen_render (0x09B7C9E0): Suppressed vanilla cubemap background screen!");
        }
        return; // Suppress vanilla cubemap background screen completely!
    }
    if (sOriginalCubemapRender) {
        sOriginalCubemapRender(self, ctx, frameObject);
    }
}

// 6. True Engine Cubemap Face Submission Suppression (RVA 0x09B7B544)
using CubemapFaceSubmitFn = void (*)(void*, void*, void*, void*);
CubemapFaceSubmitFn sOriginalCubemapFaceSubmit = nullptr;

void Hooked_CubemapFaceSubmit(void* a1, void* a2, void* a3, void* a4) {
    if (active.load() && runtime.config.caveBackground) {
        static uint32_t sDropFace = 0;
        if (++sDropFace % 360 == 1) {
            logger().info("Hooked_CubemapFaceSubmit (0x09B7B544): Suppressed RenderDragon 6-face Ancient City cubemap submission!");
        }
        return; // Suppress RenderDragon 6-face Ancient City cubemap submission completely!
    }
    if (sOriginalCubemapFaceSubmit) {
        sOriginalCubemapFaceSubmit(a1, a2, a3, a4);
    }
}

// 7. Full CubemapRenderer Render Loop Suppression (RVA 0x09B7C0B4)
using CubemapRendererFn = void (*)(void*, void*, void*, void*);
CubemapRendererFn sOriginalCubemapRenderer = nullptr;

void Hooked_CubemapRenderer_render(void* a1, void* a2, void* a3, void* a4) {
    if (active.load() && runtime.config.caveBackground) {
        static uint32_t sDropRender = 0;
        if (++sDropRender % 180 == 1) {
            logger().info("Hooked_CubemapRenderer_render (0x09B7C0B4): Suppressed cubemap loop render pass!");
        }
        return; // Suppress entire cubemap render loop!
    }
    if (sOriginalCubemapRenderer) {
        sOriginalCubemapRenderer(a1, a2, a3, a4);
    }
}

// 7b. Low-Level RenderDragon MainMenu Cubemap Pass (RVA 0x0B2DC860)
// This is the continuous 60fps cubemap submission function that evaluates Option 284
// (dev_disableRenderMainMenuCubeMap) and calls 0x0B29A6AC to draw the rotating Ancient City skybox.
using MainMenuCubemapPassFn = void (*)(void*, void*, void*, void*, void*, void*);
MainMenuCubemapPassFn sOriginalMainMenuCubemapPass = nullptr;

void Hooked_MainMenuCubemapPass(void* a0, void* a1, void* a2, void* a3, void* a4, void* a5) {
    if (active.load() && runtime.config.caveBackground) {
        static uint32_t sDropPass = 0;
        if (++sDropPass % 180 == 1) {
            logger().info("Hooked_MainMenuCubemapPass (0x0B2DC860): Suppressed RenderDragon 60fps Ancient City cubemap pass!");
        }
        return; // Early return prevents 0x0B29A6AC from painting the cubemap over the 3D cave!
    }
    if (sOriginalMainMenuCubemapPass) {
        sOriginalMainMenuCubemapPass(a0, a1, a2, a3, a4, a5);
    }
}

// 7c. Direct RenderDragon Cubemap Geometry Submission (RVA 0x0B29A6AC)
using CubemapDrawFn = void (*)(void*, void*, void*, uint32_t, float, float);
CubemapDrawFn sOriginalCubemapDraw = nullptr;

void Hooked_CubemapDraw(void* a0, void* a1, void* a2, uint32_t a3, float s0, float s1) {
    if (active.load() && runtime.config.caveBackground) {
        return; // Suppress direct skybox cubemap geometry drawing completely!
    }
    if (sOriginalCubemapDraw) {
        sOriginalCubemapDraw(a0, a1, a2, a3, s0, s1);
    }
}

// 8. GOT (Global Offset Table) Relocation Hook for glClear (RVA 0x132893d8)
// Executed on the GPU Render Thread with 100% active, valid EGLContext!
constexpr uintptr_t GL_CLEAR_GOT_RVA = 0x132893d8;
using GlClearFn = void (*)(GLbitfield);
GlClearFn sOriginalGlClear = nullptr;
static std::atomic<bool> sInsideCaveRender = false;

static bool hookGotSlot(uintptr_t gotRva, void* hookFunc, void** originalFunc) {
    uintptr_t base = ll::memory::getMinecraftBase();
    if (!base) return false;

    uintptr_t target = base + gotRva;
    long pageSize = sysconf(_SC_PAGESIZE);
    uintptr_t pageStart = target & ~(pageSize - 1);

    if (mprotect(reinterpret_cast<void*>(pageStart), pageSize, PROT_READ | PROT_WRITE) != 0) {
        logger().error("Failed to mprotect GOT page at 0x", std::hex, pageStart);
        return false;
    }

    auto* slot = reinterpret_cast<void**>(target);
    if (originalFunc) {
        *originalFunc = *slot;
    }
    *slot = hookFunc;

    logger().info("Installed GOT hook at RVA 0x", std::hex, gotRva,
                  " (Original: ", originalFunc ? *originalFunc : nullptr, " -> New: ", hookFunc, ")");
    return true;
}

void Hooked_glClear(GLbitfield mask) {
    if (sInsideCaveRender.load()) {
        if (sOriginalGlClear) sOriginalGlClear(mask);
        return;
    }

    if (sOriginalGlClear) {
        sOriginalGlClear(mask);
    }

    if ((mask & GL_COLOR_BUFFER_BIT) && active.load() && runtime.config.caveBackground) {
        GLint vp[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, vp);
        // Intercept primary full-screen framebuffer pass
        if (vp[2] >= 800 && vp[3] >= 450) {
            static uint32_t sRenderCount = 0;
            if (++sRenderCount % 180 == 1) {
                logger().info("[GPU Render Thread] glClear intercepted! Viewport: ", vp[2], "x", vp[3],
                              " EGLContext: ", eglGetCurrentContext());
            }
            sInsideCaveRender.store(true);
            {
                std::lock_guard guard(runtime.mutex);
                const double time = nowMs();
                updatePresentation(time, runtime.enteredMenu, true);
                runtime.cameraFrame = runtime.camera.update(time, runtime.currentPage, runtime.frame);

                scene::CavePose pose = runtime.cameraFrame.pose;
                const double motionTime = runtime.cameraFrame.motionTimeMs > 0.0 ? runtime.cameraFrame.motionTimeMs : time;
                const auto view = scene::caveView(pose, motionTime);
                runtime.caveRenderer.render(static_cast<float>(vp[2]), static_cast<float>(vp[3]), view, time);

                // Animated Title rendering directly on GPU render thread (fallback when not using JSON UI)
                if (runtime.currentPage == scene::CavePage::Main && runtime.frame.titleFrame >= 0 && runtime.config.minecraftTitleAnimation && !runtime.config.preferJsonMainMenu) {
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
    const auto& offsets = lowlamina::mcpe::getOffsets();
    std::string ver = lowlamina::mcpe::getDetectedVersion();
    logger().info("Installing Animated Mojang hooks for Minecraft Bedrock ", ver, "...");

    if (offsets.getTechStackForScreen) {
        ll::memory::hookRva(offsets.getTechStackForScreen,
                            reinterpret_cast<void*>(&Hooked_ScreenTechStackSelector_getTechStackForScreen),
                            reinterpret_cast<void**>(&sOriginalGetTechStack));
    }

    if (offsets.startMenuOnOpen) {
        ll::memory::hookRva(offsets.startMenuOnOpen,
                            reinterpret_cast<void*>(&Hooked_StartMenuScreenController_onOpen),
                            reinterpret_cast<void**>(&sOriginalMenuOpen));
    }

    if (offsets.playButton) {
        ll::memory::hookRva(offsets.playButton,
                            reinterpret_cast<void*>(&Hooked_StartMenu_PlayButton),
                            reinterpret_cast<void**>(&sOriginalStartMenuPlayButton));
    }

    if (offsets.settingsButton) {
        ll::memory::hookRva(offsets.settingsButton,
                            reinterpret_cast<void*>(&Hooked_StartMenu_SettingsButton),
                            reinterpret_cast<void**>(&sOriginalStartMenuSettingsButton));
    }

    if (offsets.realmsButton) {
        ll::memory::hookRva(offsets.realmsButton,
                            reinterpret_cast<void*>(&Hooked_StartMenu_RealmsButton),
                            reinterpret_cast<void**>(&sOriginalStartMenuRealmsButton));
    }

    if (offsets.dressingRoomButton) {
        ll::memory::hookRva(offsets.dressingRoomButton,
                            reinterpret_cast<void*>(&Hooked_StartMenu_DressingRoomButton),
                            reinterpret_cast<void**>(&sOriginalStartMenuDressingRoomButton));
    }

    if (offsets.marketplaceButton) {
        ll::memory::hookRva(offsets.marketplaceButton,
                            reinterpret_cast<void*>(&Hooked_StartMenu_MarketplaceButton),
                            reinterpret_cast<void**>(&sOriginalStartMenuMarketplaceButton));
    }

    if (offsets.menuExit) {
        ll::memory::hookRva(offsets.menuExit,
                            reinterpret_cast<void*>(&Hooked_Menu_Exit),
                            reinterpret_cast<void**>(&sOriginalMenuExit));
    }

    if (offsets.progressBarRender) {
        ll::memory::hookRva(offsets.progressBarRender,
                            reinterpret_cast<void*>(&Hooked_ProgressBarRenderer_render),
                            reinterpret_cast<void**>(&sOriginalProgressRender));
    }

    if (offsets.panoramaRender) {
        ll::memory::hookRva(offsets.panoramaRender,
                            reinterpret_cast<void*>(&Hooked_PanoramaRenderer_render),
                            reinterpret_cast<void**>(&sOriginalPanoramaRender));
    }

    if (offsets.cubemapBackgroundRender) {
        ll::memory::hookRva(offsets.cubemapBackgroundRender,
                            reinterpret_cast<void*>(&Hooked_CubemapBackgroundScreen_render),
                            reinterpret_cast<void**>(&sOriginalCubemapRender));
    }

    if (offsets.cubemapFaceSubmit) {
        ll::memory::hookRva(offsets.cubemapFaceSubmit,
                            reinterpret_cast<void*>(&Hooked_CubemapFaceSubmit),
                            reinterpret_cast<void**>(&sOriginalCubemapFaceSubmit));
    }

    if (offsets.cubemapRendererRender) {
        ll::memory::hookRva(offsets.cubemapRendererRender,
                            reinterpret_cast<void*>(&Hooked_CubemapRenderer_render),
                            reinterpret_cast<void**>(&sOriginalCubemapRenderer));
    }

    if (offsets.mainMenuCubemapPass) {
        ll::memory::hookRva(offsets.mainMenuCubemapPass,
                            reinterpret_cast<void*>(&Hooked_MainMenuCubemapPass),
                            reinterpret_cast<void**>(&sOriginalMainMenuCubemapPass));
    }

    if (offsets.cubemapDraw) {
        ll::memory::hookRva(offsets.cubemapDraw,
                            reinterpret_cast<void*>(&Hooked_CubemapDraw),
                            reinterpret_cast<void**>(&sOriginalCubemapDraw));
    }

    if (offsets.glClearGotRva) {
        if (hookGotSlot(offsets.glClearGotRva,
                        reinterpret_cast<void*>(&Hooked_glClear),
                        reinterpret_cast<void**>(&sOriginalGlClear))) {
            logger().info("Hooked glClear via GOT relocation at RVA 0x", std::hex, offsets.glClearGotRva);
        }
    }

    logger().info("Animated Mojang hooks successfully installed!");
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
