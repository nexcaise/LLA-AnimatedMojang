#include "client/Client.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "animation/Timeline.h"
#include "ll/api/Logger.h"
#include "ll/api/memory/Hook.h"
#include "mod/MyMod.h"
#include "scene/CaveCamera.h"
#include "scene/CaveGlesRenderer.h"

// Opaque engine types: only ever passed through, never dereferenced.
class MinecraftUIRenderContext;
class ScreenContext;

namespace animated_mojang::client {
namespace {

using Clock = std::chrono::steady_clock;

// Presentation timeline (ms since the intro started)
constexpr double kTitleStartMs = 4000.0;     // title animation begins
constexpr double kUiRevealMs = 6000.0;       // buttons start fading in
constexpr double kUiFadeEndMs = 7500.0;      // ...and are fully visible
constexpr double kAssetWaitTimeoutMs = 4000.0;

struct Runtime {
    Config config;
    Timeline timeline;
    std::filesystem::path modDirectory;
    Clock::time_point epoch = Clock::now();
    std::recursive_mutex mutex; // guards timeline / camera / config-derived state below
    scene::CaveGlesRenderer caveRenderer;
    scene::CaveCameraController camera;
    scene::CaveCameraFrame cameraFrame;
    bool enteredMenu = false;
    bool pendingReplay = false;
    bool warnedAssetTimeout = false;
    double firstMenuRenderMs = -1;
    Frame frame;
};

Runtime runtime;
std::atomic_bool active = false;
std::atomic_bool skipRequested = false;
std::atomic_bool skipAllowed = false;

// Lifecycle flags written by engine hooks (main thread) and read on the render thread.
std::atomic_bool sIsMainMenu = false;
std::atomic_bool sIsInWorld = false;
std::atomic_bool sIsLoading = true;
std::atomic_bool sInsideCaveRender = false;
std::atomic_bool sNormalLoadingComplete = false;
std::atomic_bool sIsStartScreen = true;
std::atomic_bool sModalDialogueActive = false;
std::atomic<double> sLastStartMenuTickMs = 0.0;
std::atomic_bool sSuppressUiDrawing = false;

// Lock-free snapshot of presentation state for hooks that must not take the mutex.
std::atomic<scene::CavePage> sCurrentPage{scene::CavePage::Main};
std::atomic<Phase> sPhase{Phase::Waiting};
std::atomic<double> sElapsedMs{0.0};
std::atomic<float> sMenuAlpha{0.f};

bool isMainMenuActive() {
    return active.load() && sNormalLoadingComplete.load() && sIsMainMenu.load() && !sIsInWorld.load()
        && !sIsLoading.load() && !runtime.caveRenderer.failed();
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

// Caller holds runtime.mutex.
void updatePresentation(double time, bool foregroundMenu, bool ready) {
    if (foregroundMenu && runtime.pendingReplay) {
        runtime.pendingReplay = false;
        runtime.timeline = Timeline{Timing{.maxPresentationMs = runtime.config.maxPresentationSeconds * 1000.0}};
        runtime.camera.replay();
        runtime.firstMenuRenderMs = -1;
        runtime.warnedAssetTimeout = false;
    }
    if (foregroundMenu) {
        runtime.enteredMenu = true;
        if (runtime.firstMenuRenderMs < 0) runtime.firstMenuRenderMs = time;
    }
    if (skipRequested.exchange(false)) runtime.timeline.skip();
    runtime.frame = runtime.timeline.update(time, foregroundMenu, ready, runtime.config.minecraftTitleAnimation);
    skipAllowed.store(foregroundMenu && runtime.frame.phase != Phase::MenuIdle);
    sPhase.store(runtime.frame.phase);
    sElapsedMs.store(runtime.frame.elapsedMs);
    sMenuAlpha.store(runtime.frame.menuAlpha);
}

// ----------------------------------------------------------------------------
// Multi-Version Offset Table (1.26.44.3, 1.26.45.1, 1.26.51.1, 1.26.52.3)
// ----------------------------------------------------------------------------
struct VersionOffsets {
    const char* versionStr;
    uintptr_t getTechStackForScreen;
    uintptr_t startMenuOnOpen;
    uintptr_t startMenuTick;
    uintptr_t playButton;
    uintptr_t settingsButton;
    uintptr_t realmsButton;
    uintptr_t dressingRoomButton;
    uintptr_t skinsMenuButton;
    uintptr_t marketplaceButton;
    uintptr_t achievementsButton;
    uintptr_t feedbackButton;
    uintptr_t signInButton;
    uintptr_t menuExit;
    uintptr_t progressBarRender;
    uintptr_t inGamePlayScreenRender;
    uintptr_t clientInstanceRequestLeaveGame;
    uintptr_t panoramaRender;
    uintptr_t cubemapBackgroundRender;
    uintptr_t directoryPackGetAsset;
    uintptr_t zipPackGetAsset;
    uintptr_t glClearGotRva;
    uintptr_t glColorMaskGotRva;
    uintptr_t eglSwapBuffersGotRva;
    size_t uiControlAlphaOffset;
    size_t uiControlParentOffset;
};

static const VersionOffsets kOffsets_1_26_52_3 = {
    "1.26.52.3",
    0x0974E844, 0x0A2DF314, 0x0A2E1FF4,
    0x0A31344C, 0x0A3171AC, 0x0A3174DC, 0x0A31815C, 0x0A317C1C, 0x0A3187A4,
    0x0A316978, 0x0A314D58, 0x0A31972C,
    0x0A567540,
    0x09685F70,
    0x09ACC828, 0x09800FF8,
    0x0967FA98, 0x09B7C9E0,
    0x111EEE94, 0x11158670,
    0x132893d8, 0x13289288, 0x132c7dc0,
    0x58, 0x128
};

static const VersionOffsets kOffsets_1_26_51_1 = {
    "1.26.51.1",
    0x0974E7A4, 0x0A2DF2A4, 0x0A2E1F84,
    0x0A3133DC, 0x0A31713C, 0x0A31746C, 0x0A3180EC, 0x0A317BAC, 0x0A318734,
    0x0A316908, 0x0A314CE8, 0x0A3196BC,
    0x0A5674D0,
    0x09685ED0,
    0x09ACC7A8, 0x09800F58,
    0x0967F9F8, 0x09B7C960,
    0x111EEA54, 0x11158230,
    0x13289078, 0x13288F28, 0x132C7A60,
    0x58, 0x128
};

static const VersionOffsets kOffsets_1_26_45_1 = {
    "1.26.45.1",
    0x0938B714, 0x09EBD95C, 0x09EC05D8,
    0x09EF3D64, 0x09EF72C0, 0x09EF75F0, 0x09EF8270, 0x09EF7D30, 0x09EF88A4,
    0x09EF69E4, 0x09EF52FC, 0x09EF9888,
    0x0A15248C,
    0x092BEC2C,
    0x0974066C, 0x0943C430,
    0x092B8DC0, 0x097C99A4,
    0x1042D360, 0x1040D528,
    0x124C9688, 0x124C9538, 0x12507F20,
    0x180, 0x8
};

static const VersionOffsets kOffsets_1_26_44_3 = {
    "1.26.44.3",
    0x0938B7B4, 0x09EBD9FC, 0x09EC0678,
    0x09EF3E04, 0x09EF7360, 0x09EF7690, 0x09EF8310, 0x09EF7DD0, 0x09EF8944,
    0x09EF6A84, 0x09EF539C, 0x09EF9928,
    0x0A15252C,
    0x092BECCC,
    0x0974070C, 0x0943C4D0,
    0x092B8E60, 0x097C9A44,
    0x1042D8C0, 0x1040DA88,
    0x124C9D28, 0x124C9BD8, 0x125085C0,
    0x180, 0x8
};

static VersionOffsets sOffsets = {};

static VersionOffsets detectOffsets() {
    std::string buildId;
    dl_iterate_phdr([](struct dl_phdr_info* info, size_t, void* data) -> int {
        auto* outId = static_cast<std::string*>(data);
        if (!info->dlpi_name || !strstr(info->dlpi_name, "libminecraftpe.so")) {
            return 0;
        }
        for (int i = 0; i < info->dlpi_phnum; ++i) {
            if (info->dlpi_phdr[i].p_type == PT_NOTE) {
                const uint8_t* notePtr = reinterpret_cast<const uint8_t*>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
                size_t remaining = info->dlpi_phdr[i].p_memsz;
                while (remaining >= sizeof(Elf64_Nhdr)) {
                    const auto* nhdr = reinterpret_cast<const Elf64_Nhdr*>(notePtr);
                    size_t nameLen = (nhdr->n_namesz + 3) & ~3;
                    size_t descLen = (nhdr->n_descsz + 3) & ~3;
                    if (sizeof(Elf64_Nhdr) + nameLen + descLen > remaining) break;
                    const char* name = reinterpret_cast<const char*>(notePtr + sizeof(Elf64_Nhdr));
                    if (nhdr->n_type == NT_GNU_BUILD_ID && nhdr->n_namesz == 4 && memcmp(name, "GNU", 3) == 0) {
                        const uint8_t* desc = notePtr + sizeof(Elf64_Nhdr) + nameLen;
                        char hex[64];
                        for (size_t b = 0; b < nhdr->n_descsz && b < 32; ++b) {
                            snprintf(hex + b * 2, sizeof(hex) - b * 2, "%02x", desc[b]);
                        }
                        *outId = hex;
                        return 1;
                    }
                    notePtr += sizeof(Elf64_Nhdr) + nameLen + descLen;
                    remaining -= sizeof(Elf64_Nhdr) + nameLen + descLen;
                }
            }
        }
        return 0;
    }, &buildId);

    if (!buildId.empty()) {
        logger().info("Discovered libminecraftpe.so GNU Build ID: ", buildId);
        if (buildId.rfind("b480c79a", 0) == 0) {
            return kOffsets_1_26_44_3;
        } else if (buildId.rfind("56de9eed", 0) == 0) {
            return kOffsets_1_26_52_3;
        } else if (buildId.rfind("868e275c", 0) == 0 || buildId.rfind("86895c02", 0) == 0) {
            return kOffsets_1_26_45_1;
        } else if (buildId.rfind("712509dc", 0) == 0) {
            return kOffsets_1_26_51_1;
        }
    }

    FILE* fp = fopen("/proc/self/maps", "r");
    if (fp) {
        char line[512];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "libminecraftpe.so")) {
                if (strstr(line, "1.26.44")) { fclose(fp); return kOffsets_1_26_44_3; }
                if (strstr(line, "1.26.45")) { fclose(fp); return kOffsets_1_26_45_1; }
                if (strstr(line, "1.26.51")) { fclose(fp); return kOffsets_1_26_51_1; }
                if (strstr(line, "1.26.52")) { fclose(fp); return kOffsets_1_26_52_3; }
            }
        }
        fclose(fp);
    }
    return kOffsets_1_26_52_3; // Default fallback
}


// ----------------------------------------------------------------------------
// Reverse-Engineered Native Hooks (Multi-Version Android ARM64)
// ----------------------------------------------------------------------------

// 1. ui::ScreenTechStackSelector::getTechStackForScreen
using GetTechStackFn = int (*)(void*, std::string const&);
static GetTechStackFn sOriginalGetTechStack = nullptr;

static int Hooked_ScreenTechStackSelector_getTechStackForScreen(void* self, std::string const& name) {
    if (active.load()) {
        std::string lowerName = name;
        for (char& c : lowerName) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

        // Internal engine transition route between menus: ignore and do not reset start screen state
        if (lowerName.find("pre-start") != std::string::npos) {
            return sOriginalGetTechStack ? sOriginalGetTechStack(self, name) : 0;
        }

        const bool isStart = (lowerName == "start_screen" || lowerName == "/main-menu"
            || lowerName == "main_menu_screen"
            || matches(runtime.config.menuScreenNames, name) || matches(runtime.config.menuScreenNames, lowerName));

        static constexpr const char* kModalWords[] = {"dialog", "modal", "popup", "quit", "leave",
                                                      "exit", "alert", "prompt", "confirm"};
        bool isModal = false;
        for (const char* word : kModalWords) isModal = isModal || lowerName.find(word) != std::string::npos;

        if (isModal) {
            sModalDialogueActive.store(true);
            sIsStartScreen.store(false);
            logger().info("[AnimatedMojang] Modal/dialogue screen detected: ", name, " -> title suppressed");
        } else if (isStart) {
            sIsStartScreen.store(true);
            sModalDialogueActive.store(false);
        } else {
            sIsStartScreen.store(false);
            sModalDialogueActive.store(false);
        }

        auto page = scene::cavePageForRoute(name);
        if (!page) page = scene::cavePageForScreen(name);
        if (page.has_value() && sCurrentPage.load() != *page) {
            logger().info("[AnimatedMojang] Screen change detected: ", name, " -> Page ", static_cast<int>(*page));
            sCurrentPage.store(*page);
            sIsMainMenu.store(true);
            sIsInWorld.store(false);
            sIsLoading.store(false);
            if (*page == scene::CavePage::Main) {
                sIsStartScreen.store(true);
                sModalDialogueActive.store(false);
            } else {
                sIsStartScreen.store(false);
            }
        }
        if (isStart || name == "/start-from-template") {
            return 0; // TechStack::JsonUI (0)
        }
    }
    return sOriginalGetTechStack ? sOriginalGetTechStack(self, name) : 0;
}

// ----------------------------------------------------------------------------
// Asset loading: all file IO / decoding happens on a background thread (see CaveGlesRenderer).
// ----------------------------------------------------------------------------
static void onNormalLoadingComplete() {
    if (sNormalLoadingComplete.exchange(true)) return;
    logger().info("Normal loading complete; cave assets are prepared in the background.");
    prepareCave(runtime.modDirectory); // idempotent: usually already running since mod start
}

// ----------------------------------------------------------------------------
// Deferred Start Menu Construction
// 0.0s - 4.0s: pure camera flight; 4.0s - 6.0s: title animation; 6.0s+: start menu controls constructed
// ----------------------------------------------------------------------------
static void* sPendingMenuOpenSelf = nullptr;
static std::atomic<bool> sMenuOpenConstructed = false;

// 2. StartMenuScreenController::$onOpen
using MenuOpenFn = void (*)(void*);
static MenuOpenFn sOriginalMenuOpen = nullptr;

static void triggerPendingStartMenuConstruction() {
    if (sPendingMenuOpenSelf) {
        void* self = sPendingMenuOpenSelf;
        sPendingMenuOpenSelf = nullptr;
        sMenuOpenConstructed.store(true);
        logger().info("[AnimatedMojang] Constructing deferred Start Menu screen and displaying options.");
        if (sOriginalMenuOpen) sOriginalMenuOpen(self);
    }
}

static bool introFinishedForMenu() {
    return sPhase.load() == Phase::MenuIdle || sElapsedMs.load() >= kUiRevealMs;
}

static void Hooked_StartMenuScreenController_onOpen(void* self) {
    if (active.load()) {
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
        sIsStartScreen.store(true);
        sModalDialogueActive.store(false);
        onNormalLoadingComplete();

        std::lock_guard guard(runtime.mutex);
        if (runtime.enteredMenu && runtime.config.replayOnReturnToMenu) {
            runtime.pendingReplay = true;
            runtime.caveRenderer.requestTitleReplay();
        }
        runtime.enteredMenu = true;

        if (sPhase.load() == Phase::MenuOpening && sElapsedMs.load() < kUiRevealMs) {
            sPendingMenuOpenSelf = self;
            sMenuOpenConstructed.store(false);
            logger().info("[AnimatedMojang] Deferring Start Menu construction until the intro reaches 6.0s...");
            return;
        }

        sMenuOpenConstructed.store(true);
        if (sCurrentPage.load() != scene::CavePage::Main) {
            logger().info("[AnimatedMojang] StartMenu onOpen: camera returning to the Main vista.");
            sCurrentPage.store(scene::CavePage::Main);
            sIsStartScreen.store(true);
            sModalDialogueActive.store(false);
        }
    }
    if (sOriginalMenuOpen) sOriginalMenuOpen(self);
}

// 2b. StartMenuScreenController::$tick
using MenuTickFn = void (*)(void*);
static MenuTickFn sOriginalMenuTick = nullptr;
static std::atomic<bool> sFirstMenuTick = false;

static void Hooked_StartMenuScreenController_tick(void* self) {
    if (active.load()) {
        sLastStartMenuTickMs.store(nowMs());
        sIsStartScreen.store(sCurrentPage.load() == scene::CavePage::Main && !sModalDialogueActive.load());

        if (!sFirstMenuTick.exchange(true) || sIsLoading.load()) {
            sIsMainMenu.store(true);
            sIsInWorld.store(false);
            sIsLoading.store(false);
            onNormalLoadingComplete();
        }

        if (sPendingMenuOpenSelf != nullptr) {
            if (introFinishedForMenu()) triggerPendingStartMenuConstruction();
            else return; // do not tick unconstructed menu controls before 6.0s
        }
    }
    if (sOriginalMenuTick) sOriginalMenuTick(self);
}

// 2c-2k. StartMenu button hooks. One template instance per button instead of nine copies.
using GenericButtonFn = void (*)(void*, void*, void*);

struct ButtonSpec {
    const char* label;
    bool navigates;           // true: glide the camera to `page`; false: opens a modal dialogue
    scene::CavePage page;
};
static constexpr ButtonSpec kButtons[] = {
    {"Play -> Worlds vista", true, scene::CavePage::Worlds},
    {"Settings -> Settings vista", true, scene::CavePage::Settings},
    {"Realms -> Realms vista", true, scene::CavePage::Realms},
    {"Dressing Room -> Dressing Room vista", true, scene::CavePage::DressingRoom},
    {"Skins -> Dressing Room vista", true, scene::CavePage::DressingRoom},
    {"Marketplace -> Marketplace vista", true, scene::CavePage::Marketplace},
    {"Achievements -> modal dialogue", false, scene::CavePage::Main},
    {"Feedback -> modal dialogue", false, scene::CavePage::Main},
    {"Sign In -> modal dialogue", false, scene::CavePage::Main},
};

// Returns false when the click must be swallowed (input is blocked during the intro).
static bool onStartMenuButton(ButtonSpec const& spec) {
    if (!active.load()) return true;
    if (spec.navigates && runtime.config.blockMenuInputDuringIntro && sPhase.load() == Phase::MenuOpening
        && sMenuAlpha.load() < 0.95f) {
        return false;
    }
    std::lock_guard guard(runtime.mutex);
    logger().info("[AnimatedMojang] Button clicked: ", spec.label);
    if (spec.navigates) {
        sCurrentPage.store(spec.page);
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
        if (runtime.enteredMenu) runtime.timeline.skip();
    } else {
        sModalDialogueActive.store(true);
    }
    return true;
}

template <int Id>
struct ButtonHook {
    static inline GenericButtonFn original = nullptr;
    static void hooked(void* a0, void* a1, void* a2) {
        if (!onStartMenuButton(kButtons[Id])) return;
        if (original) original(a0, a1, a2);
    }
};

static GenericButtonFn sOriginalMenuExit = nullptr;
static void Hooked_Menu_Exit(void* a0, void* a1, void* a2) {
    if (active.load() && !sIsInWorld.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("[AnimatedMojang] Back / Exit pressed -> camera returns to the Main vista");
        sCurrentPage.store(scene::CavePage::Main);
        sIsStartScreen.store(true);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(true);
    }
    if (sOriginalMenuExit) sOriginalMenuExit(a0, a1, a2);
}

// 3. ProgressBarRenderer::$render
using ProgressRenderFn = void (*)(void*, MinecraftUIRenderContext&, void*, void*, int);
static ProgressRenderFn sOriginalProgressRender = nullptr;

static void Hooked_ProgressBarRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    // Only the initial cold-boot loading bar counts. In-menu bars (Marketplace, Realms, skin sync)
    // must not disturb the cave or the camera interpolation.
    if (active.load() && !sNormalLoadingComplete.load()) {
        sIsLoading.store(true);
        sIsMainMenu.store(false);
        sIsInWorld.store(false);
    }
    if (sOriginalProgressRender) sOriginalProgressRender(self, ctx, client, owner, layer);
}

// 4. InGamePlayScreen::render
using InGamePlayScreenRenderFn = void (*)(void*, void*, const void*);
static InGamePlayScreenRenderFn sOriginalInGamePlayScreenRender = nullptr;

static void Hooked_inGamePlayScreenRender(void* self, void* screenContext, const void* frameObj) {
    if (active.load()) {
        sIsInWorld.store(true);
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(false);
        sIsLoading.store(false);
    }
    if (sOriginalInGamePlayScreenRender) sOriginalInGamePlayScreenRender(self, screenContext, frameObj);
}

// 6. ClientInstance::requestLeaveGame
using LeaveGameFn = void (*)(void*, bool, bool);
static LeaveGameFn sOriginalClientInstanceRequestLeaveGame = nullptr;

static void Hooked_clientInstanceRequestLeaveGame(void* self, bool b1, bool b2) {
    if (active.load()) {
        sIsInWorld.store(false);
        sIsMainMenu.store(true);
        sIsLoading.store(false);
        sIsStartScreen.store(true);
        sModalDialogueActive.store(false);
        sCurrentPage.store(scene::CavePage::Main);
        logger().info("[AnimatedMojang] Leave game detected -> Main menu restored.");
    }
    if (sOriginalClientInstanceRequestLeaveGame) sOriginalClientInstanceRequestLeaveGame(self, b1, b2);
}

// 7. PanoramaRenderer::$render
using PanoramaRenderFn = void (*)(void*, MinecraftUIRenderContext&, void*, void*, int);
static PanoramaRenderFn sOriginalPanoramaRender = nullptr;

static void Hooked_PanoramaRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    if (isMainMenuActive() && runtime.config.caveBackground) return; // suppress vanilla panorama
    if (sOriginalPanoramaRender) sOriginalPanoramaRender(self, ctx, client, owner, layer);
}

// 8. CubemapBackgroundScreen::$render
using CubemapRenderFn = void (*)(void*, ScreenContext&, const void*);
static CubemapRenderFn sOriginalCubemapRender = nullptr;

static void Hooked_CubemapBackgroundScreen_render(void* self, ScreenContext& ctx, const void* frameObject) {
    if (active.load()) {
        if (sCurrentPage.load() == scene::CavePage::Main && !sModalDialogueActive.load()) sIsStartScreen.store(true);
        if (sPendingMenuOpenSelf != nullptr && introFinishedForMenu()) triggerPendingStartMenuConstruction();
    }
    if (isMainMenuActive() && runtime.config.caveBackground) return; // suppress vanilla cubemap background
    if (sOriginalCubemapRender) sOriginalCubemapRender(self, ctx, frameObject);
}

// 8b. Resource Pack Asset Interception (Zero RP Requirement)
// Transparent 16x16 PNG to suppress vanilla static title logo and cubemap/panorama textures
static const uint8_t kTransparentPng[75] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x10,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0xf3, 0xff, 0x61, 0x00, 0x00, 0x00,
    0x12, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x18, 0x05, 0xa3,
    0x60, 0x14, 0x8c, 0x02, 0x08, 0x00, 0x00, 0x04, 0x10, 0x00, 0x01, 0x55,
    0x37, 0x5a, 0xd0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82
};

static bool safeExtractPath(const void* pathPtr, std::string& outPath) noexcept {
    if (!pathPtr) return false;
    try {
        const auto* s = reinterpret_cast<const std::string*>(pathPtr);
        size_t sz = s->size();
        if (sz > 0 && sz < 1024) {
            const char* data = s->data();
            if (data) {
                outPath.assign(data, sz);
                return true;
            }
        }
    } catch (...) {}
    return false;
}

using PackGetAssetFn = bool (*)(void*, void*, std::string&, bool);
static PackGetAssetFn sOriginalDirectoryPackGetAsset = nullptr;
static PackGetAssetFn sOriginalZipPackGetAsset = nullptr;

static bool shouldInterceptAsset(const std::string& p) {
    if (p.find("textures/ui/title") != std::string::npos) return true;
    if (p.find("textures/ui/panorama") != std::string::npos) return true;
    return false;
}

static bool handlePackAsset(void* self, void* pathPtr, std::string& outBuffer, bool loadMode, PackGetAssetFn orig) noexcept {
    std::string path;
    safeExtractPath(pathPtr, path);

    // Suppress vanilla static title and cubemap/panorama textures with transparent 16x16 PNG
    // Zero Resource Pack requirement - purely intercepts in-memory asset queries
    if (!path.empty() && shouldInterceptAsset(path)) {
        outBuffer.assign(reinterpret_cast<const char*>(kTransparentPng), sizeof(kTransparentPng));
        return true;
    }

    if (orig) {
        return orig(self, pathPtr, outBuffer, loadMode);
    }
    return false;
}

static bool Hooked_DirectoryPack_getAsset(void* self, void* pathPtr, std::string& outBuffer, bool loadMode) noexcept {
    return handlePackAsset(self, pathPtr, outBuffer, loadMode, sOriginalDirectoryPackGetAsset);
}

static bool Hooked_ZipPack_getAsset(void* self, void* pathPtr, std::string& outBuffer, bool loadMode) noexcept {
    return handlePackAsset(self, pathPtr, outBuffer, loadMode, sOriginalZipPackGetAsset);
}

// 9. glColorMask via GOT Relocation Hook
// During the first 6.0s on the Start Menu, all UI color writes are masked off so the buttons stay hidden.
using GlColorMaskFn = void (*)(GLboolean, GLboolean, GLboolean, GLboolean);
static GlColorMaskFn sOriginalGlColorMask = nullptr;

static void Hooked_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    if (sSuppressUiDrawing.load()) {
        if (sOriginalGlColorMask) sOriginalGlColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        return;
    }
    if (sOriginalGlColorMask) sOriginalGlColorMask(r, g, b, a);
}

// One complete menu frame, executed on the GL thread right after the game cleared its target.
static void renderMenuFrame(float width, float height, double time) {
    sInsideCaveRender.store(true);
    auto& R = runtime;

    // GL object creation + bounded texture streaming; never blocks on disk or decoding.
    const bool rendererReady = R.caveRenderer.prepare();

    Frame frame;
    scene::CaveCameraFrame cam;
    scene::CavePage page;
    bool entered, timedOut;
    {
        std::lock_guard guard(R.mutex);
        timedOut = R.firstMenuRenderMs >= 0 && time - R.firstMenuRenderMs > kAssetWaitTimeoutMs;
        entered = R.enteredMenu;
        if (!rendererReady && timedOut) {
            if (!R.warnedAssetTimeout) {
                R.warnedAssetTimeout = true;
                logger().warn("[AnimatedMojang] Cave assets did not become ready within 4s; showing the plain menu.");
            }
        } else {
            updatePresentation(time, R.enteredMenu, rendererReady);
            page = sCurrentPage.load();
            R.cameraFrame = R.camera.update(time, page, R.frame);
            frame = R.frame;
            cam = R.cameraFrame;
        }
    }
    if (!rendererReady && timedOut) { // give the user a usable menu instead of a black screen
        sSuppressUiDrawing.store(false);
        if (sOriginalGlColorMask) sOriginalGlColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        sInsideCaveRender.store(false);
        return;
    }

    if (sOriginalGlColorMask) sOriginalGlColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    scene::CaveGlesRenderer::FrameParams params;
    params.width = width;
    params.height = height;
    params.timeMs = time;
    const double motionTime = cam.motionTimeMs > 0.0 ? cam.motionTimeMs : time;
    params.view = scene::caveView(cam.pose, motionTime);

    // Title: only on the main start screen, never over dialogues or sub-screens.
    const bool isStartScreenActive = sIsStartScreen.load() && !sModalDialogueActive.load() && page == scene::CavePage::Main;
    if (isStartScreenActive && frame.titleFrame >= 0 && R.config.minecraftTitleAnimation) {
        float alpha = cam.titleAlpha;
        if (frame.phase == Phase::MenuOpening && frame.elapsedMs < kUiRevealMs)
            alpha = frame.elapsedMs >= kTitleStartMs ? 1.0f : 0.0f;
        if (alpha > 0.01f) {
            params.titleFrame = frame.titleFrame;
            params.titleAlpha = alpha;
        }
    }
    // While buttons fade in (6.0s - 7.5s) keep a clean copy of cave + title for the overlay pass.
    params.captureBackground = page == scene::CavePage::Main && frame.phase == Phase::MenuOpening
        && frame.elapsedMs >= kUiRevealMs && frame.elapsedMs < kUiFadeEndMs;

    R.caveRenderer.drawFrame(params);

    // During the intro suppress all UI color writes (buttons hidden); afterwards let the UI through.
    const bool hideUi = page == scene::CavePage::Main
        && ((frame.phase == Phase::MenuOpening && frame.elapsedMs < kUiRevealMs) || (frame.phase == Phase::Waiting && entered));
    sSuppressUiDrawing.store(hideUi);
    if (sOriginalGlColorMask) sOriginalGlColorMask(hideUi ? GL_FALSE : GL_TRUE, hideUi ? GL_FALSE : GL_TRUE,
                                                   hideUi ? GL_FALSE : GL_TRUE, hideUi ? GL_FALSE : GL_TRUE);
    sInsideCaveRender.store(false);
}

// 10. glClear via GOT Relocation Hook (GL render thread, valid EGLContext)
using GlClearFn = void (*)(GLbitfield);
static GlClearFn sOriginalGlClear = nullptr;

static void Hooked_glClear(GLbitfield mask) {
    if (sOriginalGlClear) sOriginalGlClear(mask);

    // Cheapest checks first: in the world this hook runs several times per frame.
    if (sInsideCaveRender.load(std::memory_order_relaxed)) return;
    runtime.caveRenderer.serviceRelease(); // honours stop() requests on the thread that owns the context
    if (!(mask & GL_COLOR_BUFFER_BIT) || !active.load() || !runtime.config.caveBackground) return;

    if (!sNormalLoadingComplete.load() && (sIsMainMenu.load() || !sIsLoading.load())) onNormalLoadingComplete();

    if (!isMainMenuActive()) {
        // Never leave the UI masked after leaving the menu / when the cave is unusable.
        if (sSuppressUiDrawing.load(std::memory_order_relaxed)) sSuppressUiDrawing.store(false);
        return;
    }

    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] < 400 || vp[3] < 300) return; // ignore small offscreen targets

    const double time = nowMs();
    static double sLastRenderMs = 0.0;
    if (time - sLastRenderMs < 1.0) return; // several clears per frame: draw once per burst (works at 120/144/165 Hz)
    sLastRenderMs = time;

    renderMenuFrame(static_cast<float>(vp[2]), static_cast<float>(vp[3]), time);
}

// 11. eglSwapBuffers via GOT Relocation Hook
using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
static EglSwapBuffersFn sOriginalEglSwapBuffers = nullptr;

static EGLBoolean Hooked_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    if (sSuppressUiDrawing.load() && sOriginalGlColorMask) sOriginalGlColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Slow fade-in of the start screen buttons (6.0s -> 7.5s): lock-free, uses the published snapshot.
    if (active.load() && !sIsInWorld.load() && sCurrentPage.load() == scene::CavePage::Main
        && sPhase.load() == Phase::MenuOpening) {
        const double elapsed = sElapsedMs.load();
        if (elapsed >= kUiRevealMs && elapsed < kUiFadeEndMs) {
            const float progress = std::clamp(static_cast<float>((elapsed - kUiRevealMs) / (kUiFadeEndMs - kUiRevealMs)), 0.0f, 1.0f);
            const float smooth = progress * progress * (3.0f - 2.0f * progress);
            GLint vp[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_VIEWPORT, vp);
            if (vp[2] > 0 && vp[3] > 0)
                runtime.caveRenderer.renderFadeOverlay(static_cast<float>(vp[2]), static_cast<float>(vp[3]), 1.0f - smooth);
        }
    }
    return sOriginalEglSwapBuffers ? sOriginalEglSwapBuffers(dpy, surf) : EGL_TRUE;
}

// Hooks are installed exactly once per process. Re-installing after stop()/start() would make the new
// trampoline point at our own detour and recurse forever.
static void installHooks() {
    auto const& offsets = sOffsets;
    logger().info("Installing Animated Mojang hooks via pattern signatures (detected: ", offsets.versionStr, ")...");

    static constexpr std::string_view kSigGetTechStack =
        "FD 7B BD A9 F5 0B 00 F9 F4 4F 02 A9 FD 03 00 91 F4 03 01 AA F5 03 00 AA ?? ?? ?? 94 20 01 00 B4 F3 03 00 AA 00 1C 40 F9 40 01 00 B4";
    static constexpr std::string_view kSigStartMenuOnOpen =
        "FF ?? ?? D1 FD 7B ?? A9 FC 6F ?? A9 FA 67 ?? A9 F8 5F ?? A9 F6 57 ?? A9 F4 4F ?? A9 FD ?? 04 91 ?? D0 3B D5 D8 1B 98 52 F3 03 00 AA ?? 17 40 F9 B8 D5 BB 72 A8 03 1F F8 ?? ?? E0 97 60 F2 44 F9";
    static constexpr std::string_view kSigStartMenuTick =
        "FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF 83 15 D1 57 D0 3B D5 F4 03 00 AA";
    static constexpr std::string_view kSigPlayButton =
        "FF 83 02 D1 FD 7B 07 A9 F6 57 08 A9 F4 4F 09 A9 FD C3 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 04 80 52 A8 83 1F F8 13 04 40 F9";
    static constexpr std::string_view kSigSettingsButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 82 04 80 52 A8 83 1F F8 13 04 40 F9";
    static constexpr std::string_view kSigRealmsButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 C8 16 40 F9 A8 83 1F F8 13 04 40 F9 E0 03 13 AA";
    static constexpr std::string_view kSigDressingRoomButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 05 80 52 A8 83 1F F8 13 04 40 F9 E0 23 00 91 74 6E 44 F9 ?? ?? ?? ?? 00 E4 00 6F 08 F0 A7 52 E8 43 00 B9 E0 03 01 AD E1 23 00 91 E2 83 00 91 E0 03 14 AA ?? ?? ?? ?? F4 1B 40 F9 94 02 00 B5";
    static constexpr std::string_view kSigSkinsMenuButton =
        "FF 83 03 D1 FD 7B 0B A9 F6 57 0C A9 F4 4F 0D A9 FD C3 02 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 A2 04 80 52 A8 83 1F F8 13 04 40 F9";
    static constexpr std::string_view kSigMarketplaceButton =
        "FF 83 03 D1 FD 7B 0B A9 F6 57 0C A9 F4 4F 0D A9 FD C3 02 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 E2 04 80 52 A8 83 1F F8 13 04 40 F9";
    static constexpr std::string_view kSigAchievementsButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 05 80 52 A8 83 1F F8 13 04 40 F9 E0 23 00 91 74 6E 44 F9 ?? ?? ?? ?? 00 E4 00 6F 08 F0 A7 52 E8 43 00 B9 E0 03 01 AD E1 23 00 91 E2 83 00 91 E0 03 14 AA ?? ?? ?? ?? F4 1B 40 F9 34 09 00 B5";
    static constexpr std::string_view kSigFeedbackButton =
        "FF 43 07 D1 FD 7B 17 A9 FC 6F 18 A9 FA 67 19 A9 F8 5F 1A A9 F6 57 1B A9 F4 4F 1C A9 FD C3 05 91 57 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? E8 16 40 F9 82 04 80 52";
    static constexpr std::string_view kSigFeedbackButtonLegacy =
        "FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF 03 07 D1 57 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? E8 16 40 F9 82 04 80 52 A8 83 1F F8 14 04 40 F9 A0 03 01 D1";
    static constexpr std::string_view kSigSignInButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 55 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? A8 16 40 F9 42 04 80 52 A8 83 1F F8 13 04 40 F9";
    static constexpr std::string_view kSigMenuExit =
        "FD 7B BE A9 F3 0B 00 F9 FD 03 00 91 09 04 40 F9 F3 03 08 AA 20 6D 44 F9 ?? ?? ?? 97 ?? ?? ?? ?? ?? ?? ?? 91 ?? ?? ?? ?? ?? ?? ?? 91 1F 00 00 72 0A 02 80 52 4B 03 80 52";
    static constexpr std::string_view kSigProgressBar =
        "FF 03 05 D1 EF 3B 0A 6D ED 33 0B 6D EB 2B 0C 6D E9 23 0D 6D FD 7B 0E A9 FC 6F 0F A9 FA 67 10 A9 F8 5F 11 A9 F6 57 12 A9 F4 4F 13 A9 FD 83 03 91 48 D0 3B D5 F5 03 00 AA E0 03 03 AA E8 0F 00 F9";
    static constexpr std::string_view kSigInGameHud =
        "FF C3 01 D1 FD 7B 03 A9 F7 23 00 F9 F6 57 05 A9 F4 4F 06 A9 FD C3 00 91 57 D0 3B D5 F4 03 02 AA F3 03 01 AA E8 16 40 F9 F5 03 00 AA A8 83 1F F8 ?? ?? ?? ?? ?? ?? ?? 91 08 FD DF 08 88 10 00 36";
    static constexpr std::string_view kSigLeaveGame =
        "FF 43 04 D1 FD 7B 0C A9 FC 67 0D A9 F8 5F 0E A9 F6 57 0F A9 F4 4F 10 A9 FD 03 03 91 58 D0 3B D5 F7 03 02 2A F6 03 00 AA";
    static constexpr std::string_view kSigPanorama =
        "ED 33 B7 6D EB 2B 01 6D E9 23 02 6D FD 7B 03 A9 FC 23 00 F9 FA 67 05 A9 F8 5F 06 A9 F6 57 07 A9 F4 4F 08 A9 FD C3 00 91 FF 83 13 D1 59 D0 3B D5";
    static constexpr std::string_view kSigCubemap =
        "FF 03 02 D1 FD 7B 04 A9 F7 2B 00 F9 F6 57 06 A9 F4 4F 07 A9 FD 03 01 91 56 D0 3B D5 D7 1B 98 52 F4 03 00 AA C8 16 40 F9";
    static constexpr std::string_view kSigDirectoryPack =
        "FD 7B BA A9 FC 0B 00 F9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF C3 31 D1 59 D0 3B D5 F3 03 02 AA";
    static constexpr std::string_view kSigZipPack =
        "FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF C3 33 D1 59 D0 3B D5 F4 03 02 AA";

    ll::memory::precacheSignatures({
        std::string(kSigGetTechStack),
        std::string(kSigStartMenuOnOpen),
        std::string(kSigStartMenuTick),
        std::string(kSigPlayButton),
        std::string(kSigSettingsButton),
        std::string(kSigRealmsButton),
        std::string(kSigDressingRoomButton),
        std::string(kSigSkinsMenuButton),
        std::string(kSigMarketplaceButton),
        std::string(kSigAchievementsButton),
        std::string(kSigFeedbackButton),
        std::string(kSigFeedbackButtonLegacy),
        std::string(kSigSignInButton),
        std::string(kSigMenuExit),
        std::string(kSigProgressBar),
        std::string(kSigInGameHud),
        std::string(kSigLeaveGame),
        std::string(kSigPanorama),
        std::string(kSigCubemap),
        std::string(kSigDirectoryPack),
        std::string(kSigZipPack)
    });

    // Hook 1: Force JSON UI for start menu screens
    // RVAs: 1.26.52.3: 0x0974E844 | 1.26.51.1: 0x0974E7A4 | 1.26.45.1: 0x0938B714 | 1.26.44.3: 0x0938B7B4
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.getTechStackForScreen, ...);
    ll::memory::hookSignature(std::string(kSigGetTechStack),
                              reinterpret_cast<void*>(&Hooked_ScreenTechStackSelector_getTechStackForScreen),
                              reinterpret_cast<void**>(&sOriginalGetTechStack),
                              offsets.getTechStackForScreen);

    // Hook 2: StartMenuScreenController::$onOpen
    // RVAs: 1.26.52.3: 0x0A2DF314 | 1.26.51.1: 0x0A2DF2A4 | 1.26.45.1: 0x09EBD95C | 1.26.44.3: 0x09EBD9FC
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.startMenuOnOpen, ...);
    ll::memory::hookSignature(std::string(kSigStartMenuOnOpen),
                              reinterpret_cast<void*>(&Hooked_StartMenuScreenController_onOpen),
                              reinterpret_cast<void**>(&sOriginalMenuOpen),
                              offsets.startMenuOnOpen);

    // Hook 2b: StartMenuScreenController::$tick
    // RVAs: 1.26.52.3: 0x0A2E1FF4 | 1.26.51.1: 0x0A2E1F84 | 1.26.45.1: 0x09EC05D8 | 1.26.44.3: 0x09EC0678
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.startMenuTick, ...);
    ll::memory::hookSignature(std::string(kSigStartMenuTick),
                              reinterpret_cast<void*>(&Hooked_StartMenuScreenController_tick),
                              reinterpret_cast<void**>(&sOriginalMenuTick),
                              offsets.startMenuTick);

    // Hook 2c-2k: StartMenu Navigation Buttons (With camera glides and log feedback)
    // Play Button
    // RVAs: 1.26.52.3: 0x0A31344C | 1.26.51.1: 0x0A3133DC | 1.26.45.1: 0x09EF3D64 | 1.26.44.3: 0x09EF3E04
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.playButton, ...);
    ll::memory::hookSignature(std::string(kSigPlayButton),
                              reinterpret_cast<void*>(&ButtonHook<0>::hooked),
                              reinterpret_cast<void**>(&ButtonHook<0>::original),
                              offsets.playButton);

    // Settings Button
    // RVAs: 1.26.52.3: 0x0A3171AC | 1.26.51.1: 0x0A31713C | 1.26.45.1: 0x09EF72C0 | 1.26.44.3: 0x09EF7360
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.settingsButton, ...);
    ll::memory::hookSignature(std::string(kSigSettingsButton),
                              reinterpret_cast<void*>(&ButtonHook<1>::hooked),
                              reinterpret_cast<void**>(&ButtonHook<1>::original),
                              offsets.settingsButton);

    // Realms Button
    // RVAs: 1.26.52.3: 0x0A3174DC | 1.26.51.1: 0x0A31746C | 1.26.45.1: 0x09EF75F0 | 1.26.44.3: 0x09EF7690
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.realmsButton, ...);
    ll::memory::hookSignature(std::string(kSigRealmsButton),
                              reinterpret_cast<void*>(&ButtonHook<2>::hooked),
                              reinterpret_cast<void**>(&ButtonHook<2>::original),
                              offsets.realmsButton);

    // Dressing Room Button
    // RVAs: 1.26.52.3: 0x0A31815C | 1.26.51.1: 0x0A3180EC | 1.26.45.1: 0x09EF8270 | 1.26.44.3: 0x09EF8310
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.dressingRoomButton, ...);
    ll::memory::hookSignature(std::string(kSigDressingRoomButton),
                              reinterpret_cast<void*>(&ButtonHook<3>::hooked),
                              reinterpret_cast<void**>(&ButtonHook<3>::original),
                              offsets.dressingRoomButton);

    // Skins Menu Button
    // RVAs: 1.26.52.3: 0x0A317C1C | 1.26.51.1: 0x0A317BAC | 1.26.45.1: 0x09EF7D30 | 1.26.44.3: 0x09EF7DD0
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.skinsMenuButton, ...);
    ll::memory::hookSignature(std::string(kSigSkinsMenuButton),
                              reinterpret_cast<void*>(&ButtonHook<4>::hooked),
                              reinterpret_cast<void**>(&ButtonHook<4>::original),
                              offsets.skinsMenuButton);

    // Marketplace Button
    // RVAs: 1.26.52.3: 0x0A3187A4 | 1.26.51.1: 0x0A318734 | 1.26.45.1: 0x09EF88A4 | 1.26.44.3: 0x09EF8944
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.marketplaceButton, ...);
    ll::memory::hookSignature(std::string(kSigMarketplaceButton),
                              reinterpret_cast<void*>(&ButtonHook<5>::hooked),
                              reinterpret_cast<void**>(&ButtonHook<5>::original),
                              offsets.marketplaceButton);

    // Achievements Button
    // RVAs: 1.26.52.3: 0x0A316978 | 1.26.51.1: 0x0A316908 | 1.26.45.1: 0x09EF69E4 | 1.26.44.3: 0x09EF6A84
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.achievementsButton, ...);
    ll::memory::hookSignature(std::string(kSigAchievementsButton),
                              reinterpret_cast<void*>(&ButtonHook<6>::hooked),
                              reinterpret_cast<void**>(&ButtonHook<6>::original),
                              offsets.achievementsButton);

    // Feedback Button
    // RVAs: 1.26.52.3: 0x0A314D58 | 1.26.51.1: 0x0A314CE8 | 1.26.45.1: 0x09EF52FC | 1.26.44.3: 0x09EF539C
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.feedbackButton, ...);
    uintptr_t fbAddr = ll::memory::findSignature(std::string(kSigFeedbackButton));
    if (!fbAddr) {
        fbAddr = ll::memory::findSignature(std::string(kSigFeedbackButtonLegacy));
    }
    if (fbAddr) {
        uintptr_t base = ll::memory::getMinecraftBase();
        uintptr_t rva = base ? (fbAddr - base) : fbAddr;
        char buf[96];
        snprintf(buf, sizeof(buf), "[AnimatedMojang] Hooked signature at %p (RVA 0x%lx)", reinterpret_cast<void*>(fbAddr), rva);
        logger().info(buf);
        ll::memory::hook(reinterpret_cast<void*>(fbAddr),
                         reinterpret_cast<void*>(&ButtonHook<7>::hooked),
                         reinterpret_cast<void**>(&ButtonHook<7>::original));
    } else {
        ll::memory::hookRva(offsets.feedbackButton,
                            reinterpret_cast<void*>(&ButtonHook<7>::hooked),
                            reinterpret_cast<void**>(&ButtonHook<7>::original));
    }

    // Sign-In Button
    // RVAs: 1.26.52.3: 0x0A31972C | 1.26.51.1: 0x0A3196BC | 1.26.45.1: 0x09EF9888 | 1.26.44.3: 0x09EF9928
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.signInButton, ...);
    ll::memory::hookSignature(std::string(kSigSignInButton),
                              reinterpret_cast<void*>(&ButtonHook<8>::hooked),
                              reinterpret_cast<void**>(&ButtonHook<8>::original),
                              offsets.signInButton);

    // Menu Exit / Back Button
    // RVAs: 1.26.52.3: 0x0A567540 | 1.26.51.1: 0x0A5674D0 | 1.26.45.1: 0x0A15248C | 1.26.44.3: 0x0A15252C
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.menuExit, ...);
    ll::memory::hookSignature(std::string(kSigMenuExit),
                              reinterpret_cast<void*>(&Hooked_Menu_Exit),
                              reinterpret_cast<void**>(&sOriginalMenuExit),
                              offsets.menuExit);

    // Hook 3: Loading Progress Bar
    // RVAs: 1.26.52.3: 0x09685F70 | 1.26.51.1: 0x09685ED0 | 1.26.45.1: 0x092BEC2C | 1.26.44.3: 0x092BECCC
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.progressBarRender, ...);
    ll::memory::hookSignature(std::string(kSigProgressBar),
                              reinterpret_cast<void*>(&Hooked_ProgressBarRenderer_render),
                              reinterpret_cast<void**>(&sOriginalProgressRender),
                              offsets.progressBarRender);

    // Hook 4: In-Game HUD Render
    // RVAs: 1.26.52.3: 0x09ACC828 | 1.26.51.1: 0x09ACC7A8 | 1.26.45.1: 0x0974066C | 1.26.44.3: 0x0974070C
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.inGamePlayScreenRender, ...);
    ll::memory::hookSignature(std::string(kSigInGameHud),
                              reinterpret_cast<void*>(&Hooked_inGamePlayScreenRender),
                              reinterpret_cast<void**>(&sOriginalInGamePlayScreenRender),
                              offsets.inGamePlayScreenRender);

    // Hook 5: ClientInstance::requestLeaveGame
    // RVAs: 1.26.52.3: 0x09800FF8 | 1.26.51.1: 0x09800F58 | 1.26.45.1: 0x0943C430 | 1.26.44.3: 0x0943C4D0
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.clientInstanceRequestLeaveGame, ...);
    ll::memory::hookSignature(std::string(kSigLeaveGame),
                              reinterpret_cast<void*>(&Hooked_clientInstanceRequestLeaveGame),
                              reinterpret_cast<void**>(&sOriginalClientInstanceRequestLeaveGame),
                              offsets.clientInstanceRequestLeaveGame);

    // Hook 6: PanoramaRenderer::$render suppression
    // RVAs: 1.26.52.3: 0x0967FA98 | 1.26.51.1: 0x0967F9F8 | 1.26.45.1: 0x092B8DC0 | 1.26.44.3: 0x092B8E60
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.panoramaRender, ...);
    ll::memory::hookSignature(std::string(kSigPanorama),
                              reinterpret_cast<void*>(&Hooked_PanoramaRenderer_render),
                              reinterpret_cast<void**>(&sOriginalPanoramaRender),
                              offsets.panoramaRender);

    // Hook 7: CubemapBackgroundScreen::$render suppression
    // RVAs: 1.26.52.3: 0x09B7C9E0 | 1.26.51.1: 0x09B7C960 | 1.26.45.1: 0x097C99A4 | 1.26.44.3: 0x097C9A44
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.cubemapBackgroundRender, ...);
    ll::memory::hookSignature(std::string(kSigCubemap),
                              reinterpret_cast<void*>(&Hooked_CubemapBackgroundScreen_render),
                              reinterpret_cast<void**>(&sOriginalCubemapRender),
                              offsets.cubemapBackgroundRender);

    // Hook 8: DirectoryPackAccessStrategy & ZipPackAccessStrategy (Zero RP Asset Interception)
    // DirectoryPack RVAs: 1.26.52.3: 0x111EEE94 | 1.26.51.1: 0x111EEA54 | 1.26.45.1: 0x1042D360 | 1.26.44.3: 0x1042D8C0
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.directoryPackGetAsset, ...);
    ll::memory::hookSignature(std::string(kSigDirectoryPack),
                              reinterpret_cast<void*>(&Hooked_DirectoryPack_getAsset),
                              reinterpret_cast<void**>(&sOriginalDirectoryPackGetAsset),
                              offsets.directoryPackGetAsset);

    // ZipPack RVAs: 1.26.52.3: 0x11158670 | 1.26.51.1: 0x11158230 | 1.26.45.1: 0x1040D528 | 1.26.44.3: 0x1040DA88
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.zipPackGetAsset, ...);
    ll::memory::hookSignature(std::string(kSigZipPack),
                              reinterpret_cast<void*>(&Hooked_ZipPack_getAsset),
                              reinterpret_cast<void**>(&sOriginalZipPackGetAsset),
                              offsets.zipPackGetAsset);

    // ========================================================================
    // Dynamic ELF GOT Relocation Hooks (Auto-resolves across all versions)
    // Hardcoded GOT RVAs are retained as reference comments per version.
    // ========================================================================

    // Hook 9: glClear GOT Relocation Hook
    // GOT RVAs: 1.26.52.3: 0x132893d8 | 1.26.51.1: 0x13289078 | 1.26.45.1: 0x124C9688 | 1.26.44.3: 0x124C9D28
    // [RVA Hook - Commented Out]: ll::memory::hookGotSlot(offsets.glClearGotRva, ...);
    ll::memory::hookGotSymbol("glClear",
                              reinterpret_cast<void*>(&Hooked_glClear),
                              reinterpret_cast<void**>(&sOriginalGlClear),
                              offsets.glClearGotRva);

    // Hook 9b: glColorMask GOT Relocation Hook
    // GOT RVAs: 1.26.52.3: 0x13289288 | 1.26.51.1: 0x13288f28 | 1.26.45.1: 0x124c9538 | 1.26.44.3: 0x124c9bd8
    // [RVA Hook - Commented Out]: ll::memory::hookGotSlot(offsets.glColorMaskGotRva, ...);
    ll::memory::hookGotSymbol("glColorMask",
                              reinterpret_cast<void*>(&Hooked_glColorMask),
                              reinterpret_cast<void**>(&sOriginalGlColorMask),
                              offsets.glColorMaskGotRva);

    // Hook 10: eglSwapBuffers GOT Relocation Hook
    // GOT RVAs: 1.26.52.3: 0x132c7dc0 | 1.26.51.1: 0x132c7a60 | 1.26.45.1: 0x12507f20 | 1.26.44.3: 0x125085c0
    // [RVA Hook - Commented Out]: ll::memory::hookGotSlot(offsets.eglSwapBuffersGotRva, ...);
    ll::memory::hookGotSymbol("eglSwapBuffers",
                              reinterpret_cast<void*>(&Hooked_eglSwapBuffers),
                              reinterpret_cast<void**>(&sOriginalEglSwapBuffers),
                              offsets.eglSwapBuffersGotRva);

    logger().info("Animated Mojang v1.0.0 hooks installed successfully!");
}

static bool sHooksInstalled = false;

} // namespace

void prepareCave(std::filesystem::path const& modDirectory) {
    runtime.caveRenderer.beginLoad(modDirectory);
}

void start(Config const& config, std::filesystem::path const& modDirectory) {
    if (active.load() || !config.enabled) return;

    {
        std::lock_guard guard(runtime.mutex);
        runtime.config = config;
        runtime.modDirectory = modDirectory;
        runtime.epoch = Clock::now();
        runtime.timeline = Timeline{Timing{.maxPresentationMs = config.maxPresentationSeconds * 1000.0}};
        runtime.frame = {};
        runtime.camera.reset();
        runtime.cameraFrame = {};
        runtime.enteredMenu = runtime.pendingReplay = runtime.warnedAssetTimeout = false;
        runtime.firstMenuRenderMs = -1;
        sCurrentPage.store(scene::CavePage::Main);
        sPhase.store(Phase::Waiting);
        sElapsedMs.store(0.0);
        sMenuAlpha.store(0.f);
    }
    runtime.caveRenderer.configure(config.minecraftTitleAnimation, config.replayOnReturnToMenu);

    active.store(true);
    sIsMainMenu.store(false);
    sIsInWorld.store(false);
    sIsLoading.store(true);
    sNormalLoadingComplete.store(false);

    // CPU-only work (mesh, atlas, title metadata) starts right away on a low-priority thread so the
    // assets are ready before the first menu frame; GL objects are created on the render thread.
    prepareCave(modDirectory);

    if (!sHooksInstalled) {
        sHooksInstalled = true;
        sOffsets = detectOffsets();
        installHooks();
    }
}

void stop() {
    active.store(false);
    skipRequested.store(false);
    skipAllowed.store(false);
    sSuppressUiDrawing.store(false);
    // The menu might still be waiting for the deferred construction; never leave it unbuilt.
    triggerPendingStartMenuConstruction();
    // GL objects can only be freed on the render thread: ask it to do so at its next glClear.
    runtime.caveRenderer.requestGlRelease();
}

} // namespace animated_mojang::client
