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
#include <fstream>
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
    std::filesystem::path modDirectory;
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

// In-menu / in-world lifecycle state tracking (zero external loader dependencies)
std::atomic_bool sIsMainMenu = false;
std::atomic_bool sIsInWorld = false;
std::atomic_bool sIsLoading = true;
std::atomic_bool sInsideCaveRender = false;
std::atomic_bool sNormalLoadingComplete = false;
std::atomic_bool sIsStartScreen = true;
std::atomic_bool sModalDialogueActive = false;
std::atomic<double> sLastStartMenuTickMs = 0.0;

bool isMainMenuActive() {
    return active.load() && sNormalLoadingComplete.load() && sIsMainMenu.load() && !sIsInWorld.load() && !sIsLoading.load();
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

#include <link.h>
#include <elf.h>

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
        if (lowerName == "/pre-start" || lowerName.find("pre-start") != std::string::npos) {
            if (sOriginalGetTechStack) {
                return sOriginalGetTechStack(self, name);
            }
            return 0;
        }

        const bool isStart = (lowerName == "start_screen" || lowerName == "/main-menu"
            || lowerName == "main_menu_screen"
            || matches(runtime.config.menuScreenNames, name) || matches(runtime.config.menuScreenNames, lowerName));

        const bool isModal = (lowerName.find("dialog") != std::string::npos
            || lowerName.find("modal") != std::string::npos
            || lowerName.find("popup") != std::string::npos
            || lowerName.find("quit") != std::string::npos
            || lowerName.find("leave") != std::string::npos
            || lowerName.find("exit") != std::string::npos
            || lowerName.find("alert") != std::string::npos
            || lowerName.find("prompt") != std::string::npos
            || lowerName.find("confirm") != std::string::npos);

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
        if (page.has_value()) {
            std::lock_guard guard(runtime.mutex);
            if (runtime.currentPage != *page) {
                logger().info("[AnimatedMojang] Screen change detected: ", name, " -> Page ", static_cast<int>(*page));
                runtime.currentPage = *page;
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
        }
        if (isStart || name == "/start-from-template") {
            return 0; // TechStack::JsonUI (0)
        }
    }
    if (sOriginalGetTechStack) {
        return sOriginalGetTechStack(self, name);
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Resource Folder & 3D Cave Loading (Zero RP, Direct Engine Functions)
// ----------------------------------------------------------------------------
static void onNormalLoadingComplete() {
    if (sNormalLoadingComplete.exchange(true)) {
        return;
    }
    logger().info("Normal loading complete. Loading 3D cave model and animated title assets directly...");

    // Load cave 3D mesh model, vertex buffers, and shaders directly into OpenGL
    animated_mojang::client::prepareCave(runtime.modDirectory);

    logger().info("3D cave and animated title assets loaded successfully right after normal loading completed!");
}

// ----------------------------------------------------------------------------
// Option B: Deferred Start Menu Construction & Screen Transition Architecture
// ----------------------------------------------------------------------------
// Timeline:
// 0.0s - 4.0s: Pure camera flight through cave (no title, no buttons)
// 4.0s - 6.0s: Camera reaches caveTitle vista, title appears and animates (no buttons)
// 6.0s+: Title animation complete, Start Menu screen is constructed & options displayed!
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
        logger().info("[AnimatedMojang] Option B (6.0s reached): Constructing Start Menu screen and displaying options!");
        if (sOriginalMenuOpen) {
            sOriginalMenuOpen(self);
        }
    }
}

static void Hooked_StartMenuScreenController_onOpen(void* self) {
    if (active.load()) {
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
        sIsStartScreen.store(true);
        sModalDialogueActive.store(false);

        // Right after normal loading is complete, load files from resource folder, then load cave
        onNormalLoadingComplete();

        std::lock_guard guard(runtime.mutex);
        if (runtime.enteredMenu && runtime.config.replayOnReturnToMenu) {
            runtime.pendingReplay = true;
            runtime.caveRenderer.resetTitlePreload();
        }
        runtime.enteredMenu = true;

        // Option B: If we are in the intro flight (< 6.0s), defer construction of start screen controls
        if (runtime.frame.phase == Phase::MenuOpening && runtime.frame.elapsedMs < 6000.0) {
            sPendingMenuOpenSelf = self;
            sMenuOpenConstructed.store(false);
            logger().info("[AnimatedMojang] Option B: Deferring Start Menu construction until 6.0s (after cave flight & animated title)...");
            return;
        }

        sMenuOpenConstructed.store(true);
        if (runtime.currentPage != scene::CavePage::Main) {
            logger().info("[AnimatedMojang] StartMenu onOpen: Camera smoothly returning to initial Main vista (caveTitle).");
            runtime.currentPage = scene::CavePage::Main;
            sIsStartScreen.store(true);
            sModalDialogueActive.store(false);
        }
    }
    if (sOriginalMenuOpen) {
        sOriginalMenuOpen(self);
    }
}

// 2b. StartMenuScreenController::$tick
using MenuTickFn = void (*)(void*);
static MenuTickFn sOriginalMenuTick = nullptr;
static std::atomic<bool> sFirstMenuTick = false;

static void Hooked_StartMenuScreenController_tick(void* self) {
    if (active.load()) {
        sLastStartMenuTickMs.store(nowMs());

        // Update Start Screen state:
        // Only mark start screen as active when currentPage is Main and no modal dialogue is active.
        if (runtime.currentPage == scene::CavePage::Main && !sModalDialogueActive.load()) {
            sIsStartScreen.store(true);
        } else {
            sIsStartScreen.store(false);
        }

        if (!sFirstMenuTick.exchange(true) || sIsLoading.load()) {
            sIsMainMenu.store(true);
            sIsInWorld.store(false);
            sIsLoading.store(false);
            onNormalLoadingComplete();
        }

        // Option B: Check if 6.0s has arrived to construct the deferred Start Menu
        if (sPendingMenuOpenSelf != nullptr) {
            if (runtime.frame.phase == Phase::MenuIdle || runtime.frame.elapsedMs >= 6000.0) {
                triggerPendingStartMenuConstruction();
            } else {
                return; // Do not tick unconstructed menu controls before 6.0s
            }
        }
    }
    if (sOriginalMenuTick) {
        sOriginalMenuTick(self);
    }
}

// 2c-2k. StartMenu Direct Button Click Hooks (With prominent logging and smooth transitions)
using GenericButtonFn = void (*)(void*, void*, void*);

static GenericButtonFn sOriginalStartMenuPlayButton = nullptr;
static void Hooked_StartMenu_PlayButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        if (runtime.config.blockMenuInputDuringIntro && runtime.frame.phase == Phase::MenuOpening && runtime.frame.menuAlpha < 0.95f) {
            return;
        }
        std::lock_guard guard(runtime.mutex);
        logger().info("[AnimatedMojang] Button Clicked: Play -> Camera transitioning to Worlds vista");
        runtime.currentPage = scene::CavePage::Worlds;
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
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
        if (runtime.config.blockMenuInputDuringIntro && runtime.frame.phase == Phase::MenuOpening && runtime.frame.menuAlpha < 0.95f) {
            return;
        }
        std::lock_guard guard(runtime.mutex);
        logger().info("[AnimatedMojang] Button Clicked: Settings -> Camera transitioning to Settings vista");
        runtime.currentPage = scene::CavePage::Settings;
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
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
        if (runtime.config.blockMenuInputDuringIntro && runtime.frame.phase == Phase::MenuOpening && runtime.frame.menuAlpha < 0.95f) {
            return;
        }
        std::lock_guard guard(runtime.mutex);
        logger().info("[AnimatedMojang] Button Clicked: Realms -> Camera transitioning to Realms vista");
        runtime.currentPage = scene::CavePage::Realms;
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
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
        if (runtime.config.blockMenuInputDuringIntro && runtime.frame.phase == Phase::MenuOpening && runtime.frame.menuAlpha < 0.95f) {
            return;
        }
        std::lock_guard guard(runtime.mutex);
        logger().info("[AnimatedMojang] Button Clicked: Dressing Room (DressingRoomButton) -> Camera transitioning to Dressing Room vista");
        runtime.currentPage = scene::CavePage::DressingRoom;
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
    if (sOriginalStartMenuDressingRoomButton) {
        sOriginalStartMenuDressingRoomButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalStartMenuSkinsMenuButton = nullptr;
static void Hooked_StartMenu_SkinsMenuButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        if (runtime.config.blockMenuInputDuringIntro && runtime.frame.phase == Phase::MenuOpening && runtime.frame.menuAlpha < 0.95f) {
            return;
        }
        std::lock_guard guard(runtime.mutex);
        logger().info("[AnimatedMojang] Button Clicked: Dressing Room / Skins (SkinsMenuButton) -> Camera transitioning to Dressing Room vista");
        runtime.currentPage = scene::CavePage::DressingRoom;
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
    if (sOriginalStartMenuSkinsMenuButton) {
        sOriginalStartMenuSkinsMenuButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalStartMenuMarketplaceButton = nullptr;
static void Hooked_StartMenu_MarketplaceButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        if (runtime.config.blockMenuInputDuringIntro && runtime.frame.phase == Phase::MenuOpening && runtime.frame.menuAlpha < 0.95f) {
            return;
        }
        std::lock_guard guard(runtime.mutex);
        logger().info("[AnimatedMojang] Button Clicked: Marketplace -> Camera transitioning to Marketplace vista");
        runtime.currentPage = scene::CavePage::Marketplace;
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(true);
        sIsInWorld.store(false);
        sIsLoading.store(false);
        if (runtime.enteredMenu) {
            runtime.timeline.skip();
        }
    }
    if (sOriginalStartMenuMarketplaceButton) {
        sOriginalStartMenuMarketplaceButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalStartMenuAchievementsButton = nullptr;
static void Hooked_StartMenu_AchievementsButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        sModalDialogueActive.store(true);
        logger().info("[AnimatedMojang] Button Clicked: Achievements -> Modal dialogue active");
    }
    if (sOriginalStartMenuAchievementsButton) {
        sOriginalStartMenuAchievementsButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalStartMenuFeedbackButton = nullptr;
static void Hooked_StartMenu_FeedbackButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        sModalDialogueActive.store(true);
        logger().info("[AnimatedMojang] Button Clicked: Feedback -> Modal dialogue active");
    }
    if (sOriginalStartMenuFeedbackButton) {
        sOriginalStartMenuFeedbackButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalStartMenuSignInButton = nullptr;
static void Hooked_StartMenu_SignInButton(void* a0, void* a1, void* a2) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        sModalDialogueActive.store(true);
        logger().info("[AnimatedMojang] Button Clicked: Sign In -> Modal dialogue active");
    }
    if (sOriginalStartMenuSignInButton) {
        sOriginalStartMenuSignInButton(a0, a1, a2);
    }
}

static GenericButtonFn sOriginalMenuExit = nullptr;
static void Hooked_Menu_Exit(void* a0, void* a1, void* a2) {
    if (active.load() && !sIsInWorld.load()) {
        std::lock_guard guard(runtime.mutex);
        logger().info("[AnimatedMojang] Button Clicked: Back / Exit (button.menu_exit) -> Returning camera to Main Menu vista");
        runtime.currentPage = scene::CavePage::Main;
        sIsStartScreen.store(true);
        sModalDialogueActive.store(false);
        sIsMainMenu.store(true);
    }
    if (sOriginalMenuExit) {
        sOriginalMenuExit(a0, a1, a2);
    }
}

// 3. ProgressBarRenderer::$render
using ProgressRenderFn = void (*)(void*, MinecraftUIRenderContext&, void*, void*, int);
static ProgressRenderFn sOriginalProgressRender = nullptr;

static void Hooked_ProgressBarRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    if (active.load()) {
        // Only mark loading during initial cold boot before normal loading completes.
        // In-menu progress bars (Marketplace loading, Dressing Room skin sync, Realms) must
        // NOT disrupt cave rendering or freeze camera interpolation.
        if (!sNormalLoadingComplete.load()) {
            sIsLoading.store(true);
            sIsMainMenu.store(false);
            sIsInWorld.store(false);
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
        sIsStartScreen.store(false);
        sModalDialogueActive.store(false);
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

[[maybe_unused]] static void Hooked_inGamePlayScreenRenderLevel(void* self, void* screenContext, const void* frameObj) {
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
        sIsStartScreen.store(true);
        sModalDialogueActive.store(false);
        std::lock_guard guard(runtime.mutex);
        runtime.currentPage = scene::CavePage::Main;
        logger().info("[AnimatedMojang] Leave game detected -> Main menu restored.");
    }
    if (sOriginalClientInstanceRequestLeaveGame) {
        sOriginalClientInstanceRequestLeaveGame(self, b1, b2);
    }
}

// 7. PanoramaRenderer::$render
using PanoramaRenderFn = void (*)(void*, MinecraftUIRenderContext&, void*, void*, int);
static PanoramaRenderFn sOriginalPanoramaRender = nullptr;

static void Hooked_PanoramaRenderer_render(void* self, MinecraftUIRenderContext& ctx, void* client, void* owner, int layer) {
    if (isMainMenuActive()) {
        if (runtime.config.caveBackground) {
            return; // Suppress vanilla panorama
        }
    }
    if (sOriginalPanoramaRender) {
        sOriginalPanoramaRender(self, ctx, client, owner, layer);
    }
}

// 8. CubemapBackgroundScreen::$render
using CubemapRenderFn = void (*)(void*, ScreenContext&, const void*);
static CubemapRenderFn sOriginalCubemapRender = nullptr;

static void Hooked_CubemapBackgroundScreen_render(void* self, ScreenContext& ctx, const void* frameObject) {
    if (active.load()) {
        if (runtime.currentPage == scene::CavePage::Main && !sModalDialogueActive.load()) {
            sIsStartScreen.store(true);
        }
        if (sPendingMenuOpenSelf != nullptr && (runtime.frame.phase == Phase::MenuIdle || runtime.frame.elapsedMs >= 6000.0)) {
            triggerPendingStartMenuConstruction();
        }
    }
    if (isMainMenuActive() && runtime.config.caveBackground) {
        return; // Suppress vanilla cubemap background screen
    }
    if (sOriginalCubemapRender) {
        sOriginalCubemapRender(self, ctx, frameObject);
    }
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

// 9. glClear via GOT Relocation Hook
// Executed on the GPU Render Thread with active, valid EGLContext
using GlClearFn = void (*)(GLbitfield);
static GlClearFn sOriginalGlClear = nullptr;

// 9b. glColorMask via GOT Relocation Hook
// Hardware-level GPU color write protection:
// During the first 6.0s on the Start Menu, completely suppresses rendering of all 2D UI buttons and panels.
// At 6.0s+ (after the cave flight & animated title complete), enables color writes so start screen buttons appear.
using GlColorMaskFn = void (*)(GLboolean, GLboolean, GLboolean, GLboolean);
static GlColorMaskFn sOriginalGlColorMask = nullptr;
static std::atomic<bool> sSuppressUiDrawing = false;

static void Hooked_glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) {
    if (sSuppressUiDrawing.load()) {
        if (sOriginalGlColorMask) {
            sOriginalGlColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        }
        return;
    }
    if (sOriginalGlColorMask) {
        sOriginalGlColorMask(r, g, b, a);
    }
}

static void Hooked_glClear(GLbitfield mask) {
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
        if (vp[2] >= 400 && vp[3] >= 300) {
            // Ensure normal loading complete has fired when start menu is up
            if (!sNormalLoadingComplete.load()) {
                if (sIsMainMenu.load() || !sIsLoading.load()) {
                    onNormalLoadingComplete();
                }
            }

            if (isMainMenuActive()) {
                const double time = nowMs();
                static double sLastRenderMs = 0.0;
                if (time - sLastRenderMs < 1.0) return; // Smooth support for 120Hz/144Hz/165Hz gaming displays
                sLastRenderMs = time;

                sInsideCaveRender.store(true);
                {
                    std::lock_guard guard(runtime.mutex);
                    updatePresentation(time, runtime.enteredMenu, true);
                    runtime.cameraFrame = runtime.camera.update(time, runtime.currentPage, runtime.frame);

                    // Ensure color mask is enabled while drawing the 3D cave
                    if (sOriginalGlColorMask) {
                        sOriginalGlColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    }

                    scene::CavePose pose = runtime.cameraFrame.pose;
                    const double motionTime = runtime.cameraFrame.motionTimeMs > 0.0 ? runtime.cameraFrame.motionTimeMs : time;
                    const auto view = scene::caveView(pose, motionTime);
                    runtime.caveRenderer.render(static_cast<float>(vp[2]), static_cast<float>(vp[3]), view, time);

                    // Animated Title rendering directly on GPU render thread (zero RP requirement)
                    // Renders during 4.0s - 6.0s animation and remains stationed as full title at 6.0s+
                    // STRICT SCREEN ISOLATION: ONLY rendered on the main start screen when no modal dialogue (like leave game) is active
                    // (disappears immediately on Settings, Play, or any sub-screen).
                    const bool isStartScreenActive = sIsStartScreen.load()
                        && !sModalDialogueActive.load()
                        && (runtime.currentPage == scene::CavePage::Main);

                    if (isStartScreenActive
                        && runtime.frame.titleFrame >= 0 && runtime.config.minecraftTitleAnimation) {
                        float effectiveTitleAlpha = runtime.cameraFrame.titleAlpha;
                        if (runtime.frame.phase == Phase::MenuOpening && runtime.frame.elapsedMs < 6000.0) {
                            effectiveTitleAlpha = (runtime.frame.elapsedMs >= 4000.0) ? 1.0f : 0.0f;
                        }
                        if (effectiveTitleAlpha > 0.01f) {
                            runtime.caveRenderer.renderTitle(
                                static_cast<float>(vp[2]),
                                static_cast<float>(vp[3]),
                                runtime.frame.titleFrame,
                                effectiveTitleAlpha
                            );
                        }
                    }

                    // During 6.0s - 7.5s on Main Start Screen, capture clean background before Bedrock UI pass
                    if (runtime.currentPage == scene::CavePage::Main && runtime.frame.phase == Phase::MenuOpening
                        && runtime.frame.elapsedMs >= 6000.0 && runtime.frame.elapsedMs < 7500.0) {
                        runtime.caveRenderer.captureFadeBackground(static_cast<float>(vp[2]), static_cast<float>(vp[3]));
                    }

                    // During 0.0s - 6.0s on the main start screen, suppress UI button rendering completely!
                    if (runtime.currentPage == scene::CavePage::Main && runtime.frame.phase == Phase::MenuOpening && runtime.frame.elapsedMs < 6000.0) {
                        sSuppressUiDrawing.store(true);
                        if (sOriginalGlColorMask) {
                            sOriginalGlColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                        }
                    } else {
                        sSuppressUiDrawing.store(false);
                        if (sOriginalGlColorMask) {
                            sOriginalGlColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                        }
                    }
                }
                sInsideCaveRender.store(false);
            }
        }
    }
}

// 10. eglSwapBuffers via GOT Relocation Hook
using EglSwapBuffersFn = EGLBoolean (*)(EGLDisplay, EGLSurface);
static EglSwapBuffersFn sOriginalEglSwapBuffers = nullptr;

static EGLBoolean Hooked_eglSwapBuffers(EGLDisplay dpy, EGLSurface surf) {
    if (sSuppressUiDrawing.load()) {
        if (sOriginalGlColorMask) {
            sOriginalGlColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        }
    }

    if (active.load() && !sIsInWorld.load()) {
        std::lock_guard guard(runtime.mutex);

        // 1. Slow fade-in of start screen buttons over 1.5s (from 6.0s to 7.5s)
        if (runtime.currentPage == scene::CavePage::Main && runtime.frame.phase == Phase::MenuOpening
            && runtime.frame.elapsedMs >= 6000.0 && runtime.frame.elapsedMs < 7500.0) {
            const float progress = std::clamp(static_cast<float>((runtime.frame.elapsedMs - 6000.0) / 1500.0), 0.0f, 1.0f);
            const float smoothProgress = progress * progress * (3.0f - 2.0f * progress); // smoothstep
            const float overlayAlpha = 1.0f - smoothProgress;
            GLint vp[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_VIEWPORT, vp);
            if (vp[2] > 0 && vp[3] > 0) {
                runtime.caveRenderer.renderFadeOverlay(static_cast<float>(vp[2]), static_cast<float>(vp[3]), overlayAlpha);
            }
        }
    }

    if (sOriginalEglSwapBuffers) {
        return sOriginalEglSwapBuffers(dpy, surf);
    }
    return EGL_TRUE;
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
        runtime.modDirectory = modDirectory;
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
    }

    active.store(true);
    sIsMainMenu.store(false);
    sIsInWorld.store(false);
    sIsLoading.store(true);
    sNormalLoadingComplete.store(false);

    sOffsets = detectOffsets();
    auto const& offsets = sOffsets;
    logger().info("Installing Animated Mojang v1.0.0 hooks across Bedrock versions via Pattern Signatures (detected: ", offsets.versionStr, ")...");

    // ========================================================================
    // Multi-Version Pattern Signatures (Bedrock 1.26.44.3 – 1.26.52.3+)
    // Hardcoded RVAs are retained as reference comments per version.
    // ========================================================================

    // Hook 1: Force JSON UI for start menu screens
    // RVAs: 1.26.52.3: 0x0974E844 | 1.26.51.1: 0x0974E7A4 | 1.26.45.1: 0x0938B714 | 1.26.44.3: 0x0938B7B4
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.getTechStackForScreen, ...);
    static constexpr std::string_view kSigGetTechStack =
        "FD 7B BD A9 F5 0B 00 F9 F4 4F 02 A9 FD 03 00 91 F4 03 01 AA F5 03 00 AA ?? ?? ?? 94 20 01 00 B4 F3 03 00 AA 00 1C 40 F9 40 01 00 B4";
    ll::memory::hookSignature(std::string(kSigGetTechStack),
                              reinterpret_cast<void*>(&Hooked_ScreenTechStackSelector_getTechStackForScreen),
                              reinterpret_cast<void**>(&sOriginalGetTechStack),
                              offsets.getTechStackForScreen);

    // Hook 2: StartMenuScreenController::$onOpen
    // RVAs: 1.26.52.3: 0x0A2DF314 | 1.26.51.1: 0x0A2DF2A4 | 1.26.45.1: 0x09EBD95C | 1.26.44.3: 0x09EBD9FC
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.startMenuOnOpen, ...);
    static constexpr std::string_view kSigStartMenuOnOpen =
        "FF ?? ?? D1 FD 7B ?? A9 FC 6F ?? A9 FA 67 ?? A9 F8 5F ?? A9 F6 57 ?? A9 F4 4F ?? A9 FD ?? 04 91 ?? D0 3B D5 D8 1B 98 52 F3 03 00 AA ?? 17 40 F9 B8 D5 BB 72 A8 03 1F F8 ?? ?? E0 97 60 F2 44 F9";
    ll::memory::hookSignature(std::string(kSigStartMenuOnOpen),
                              reinterpret_cast<void*>(&Hooked_StartMenuScreenController_onOpen),
                              reinterpret_cast<void**>(&sOriginalMenuOpen),
                              offsets.startMenuOnOpen);

    // Hook 2b: StartMenuScreenController::$tick
    // RVAs: 1.26.52.3: 0x0A2E1FF4 | 1.26.51.1: 0x0A2E1F84 | 1.26.45.1: 0x09EC05D8 | 1.26.44.3: 0x09EC0678
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.startMenuTick, ...);
    static constexpr std::string_view kSigStartMenuTick =
        "FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF 83 15 D1 57 D0 3B D5 F4 03 00 AA";
    ll::memory::hookSignature(std::string(kSigStartMenuTick),
                              reinterpret_cast<void*>(&Hooked_StartMenuScreenController_tick),
                              reinterpret_cast<void**>(&sOriginalMenuTick),
                              offsets.startMenuTick);

    // Hook 2c-2k: StartMenu Navigation Buttons (With camera glides and log feedback)
    // Play Button
    // RVAs: 1.26.52.3: 0x0A31344C | 1.26.51.1: 0x0A3133DC | 1.26.45.1: 0x09EF3D64 | 1.26.44.3: 0x09EF3E04
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.playButton, ...);
    static constexpr std::string_view kSigPlayButton =
        "FF 83 02 D1 FD 7B 07 A9 F6 57 08 A9 F4 4F 09 A9 FD C3 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 04 80 52 A8 83 1F F8 13 04 40 F9";
    ll::memory::hookSignature(std::string(kSigPlayButton),
                              reinterpret_cast<void*>(&Hooked_StartMenu_PlayButton),
                              reinterpret_cast<void**>(&sOriginalStartMenuPlayButton),
                              offsets.playButton);

    // Settings Button
    // RVAs: 1.26.52.3: 0x0A3171AC | 1.26.51.1: 0x0A31713C | 1.26.45.1: 0x09EF72C0 | 1.26.44.3: 0x09EF7360
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.settingsButton, ...);
    static constexpr std::string_view kSigSettingsButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 82 04 80 52 A8 83 1F F8 13 04 40 F9";
    ll::memory::hookSignature(std::string(kSigSettingsButton),
                              reinterpret_cast<void*>(&Hooked_StartMenu_SettingsButton),
                              reinterpret_cast<void**>(&sOriginalStartMenuSettingsButton),
                              offsets.settingsButton);

    // Realms Button
    // RVAs: 1.26.52.3: 0x0A3174DC | 1.26.51.1: 0x0A31746C | 1.26.45.1: 0x09EF75F0 | 1.26.44.3: 0x09EF7690
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.realmsButton, ...);
    static constexpr std::string_view kSigRealmsButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 C8 16 40 F9 A8 83 1F F8 13 04 40 F9 E0 03 13 AA";
    ll::memory::hookSignature(std::string(kSigRealmsButton),
                              reinterpret_cast<void*>(&Hooked_StartMenu_RealmsButton),
                              reinterpret_cast<void**>(&sOriginalStartMenuRealmsButton),
                              offsets.realmsButton);

    // Dressing Room Button
    // RVAs: 1.26.52.3: 0x0A31815C | 1.26.51.1: 0x0A3180EC | 1.26.45.1: 0x09EF8270 | 1.26.44.3: 0x09EF8310
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.dressingRoomButton, ...);
    static constexpr std::string_view kSigDressingRoomButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 05 80 52 A8 83 1F F8 13 04 40 F9 E0 23 00 91 74 6E 44 F9 ?? ?? ?? ?? 00 E4 00 6F 08 F0 A7 52 E8 43 00 B9 E0 03 01 AD E1 23 00 91 E2 83 00 91 E0 03 14 AA ?? ?? ?? ?? F4 1B 40 F9 94 02 00 B5";
    ll::memory::hookSignature(std::string(kSigDressingRoomButton),
                              reinterpret_cast<void*>(&Hooked_StartMenu_DressingRoomButton),
                              reinterpret_cast<void**>(&sOriginalStartMenuDressingRoomButton),
                              offsets.dressingRoomButton);

    // Skins Menu Button
    // RVAs: 1.26.52.3: 0x0A317C1C | 1.26.51.1: 0x0A317BAC | 1.26.45.1: 0x09EF7D30 | 1.26.44.3: 0x09EF7DD0
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.skinsMenuButton, ...);
    static constexpr std::string_view kSigSkinsMenuButton =
        "FF 83 03 D1 FD 7B 0B A9 F6 57 0C A9 F4 4F 0D A9 FD C3 02 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 A2 04 80 52 A8 83 1F F8 13 04 40 F9";
    ll::memory::hookSignature(std::string(kSigSkinsMenuButton),
                              reinterpret_cast<void*>(&Hooked_StartMenu_SkinsMenuButton),
                              reinterpret_cast<void**>(&sOriginalStartMenuSkinsMenuButton),
                              offsets.skinsMenuButton);

    // Marketplace Button
    // RVAs: 1.26.52.3: 0x0A3187A4 | 1.26.51.1: 0x0A318734 | 1.26.45.1: 0x09EF88A4 | 1.26.44.3: 0x09EF8944
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.marketplaceButton, ...);
    static constexpr std::string_view kSigMarketplaceButton =
        "FF 83 03 D1 FD 7B 0B A9 F6 57 0C A9 F4 4F 0D A9 FD C3 02 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 E2 04 80 52 A8 83 1F F8 13 04 40 F9";
    ll::memory::hookSignature(std::string(kSigMarketplaceButton),
                              reinterpret_cast<void*>(&Hooked_StartMenu_MarketplaceButton),
                              reinterpret_cast<void**>(&sOriginalStartMenuMarketplaceButton),
                              offsets.marketplaceButton);

    // Achievements Button
    // RVAs: 1.26.52.3: 0x0A316978 | 1.26.51.1: 0x0A316908 | 1.26.45.1: 0x09EF69E4 | 1.26.44.3: 0x09EF6A84
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.achievementsButton, ...);
    static constexpr std::string_view kSigAchievementsButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 05 80 52 A8 83 1F F8 13 04 40 F9 E0 23 00 91 74 6E 44 F9 ?? ?? ?? ?? 00 E4 00 6F 08 F0 A7 52 E8 43 00 B9 E0 03 01 AD E1 23 00 91 E2 83 00 91 E0 03 14 AA ?? ?? ?? ?? F4 1B 40 F9 34 09 00 B5";
    ll::memory::hookSignature(std::string(kSigAchievementsButton),
                              reinterpret_cast<void*>(&Hooked_StartMenu_AchievementsButton),
                              reinterpret_cast<void**>(&sOriginalStartMenuAchievementsButton),
                              offsets.achievementsButton);

    // Feedback Button
    // RVAs: 1.26.52.3: 0x0A314D58 | 1.26.51.1: 0x0A314CE8 | 1.26.45.1: 0x09EF52FC | 1.26.44.3: 0x09EF539C
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.feedbackButton, ...);
    static constexpr std::string_view kSigFeedbackButton =
        "FF 43 07 D1 FD 7B 17 A9 FC 6F 18 A9 FA 67 19 A9 F8 5F 1A A9 F6 57 1B A9 F4 4F 1C A9 FD C3 05 91 57 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? E8 16 40 F9 82 04 80 52";
    static constexpr std::string_view kSigFeedbackButtonLegacy =
        "FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF 03 07 D1 57 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? E8 16 40 F9 82 04 80 52 A8 83 1F F8 14 04 40 F9 A0 03 01 D1";
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
                         reinterpret_cast<void*>(&Hooked_StartMenu_FeedbackButton),
                         reinterpret_cast<void**>(&sOriginalStartMenuFeedbackButton));
    } else {
        ll::memory::hookRva(offsets.feedbackButton,
                            reinterpret_cast<void*>(&Hooked_StartMenu_FeedbackButton),
                            reinterpret_cast<void**>(&sOriginalStartMenuFeedbackButton));
    }

    // Sign-In Button
    // RVAs: 1.26.52.3: 0x0A31972C | 1.26.51.1: 0x0A3196BC | 1.26.45.1: 0x09EF9888 | 1.26.44.3: 0x09EF9928
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.signInButton, ...);
    static constexpr std::string_view kSigSignInButton =
        "FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 55 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? A8 16 40 F9 42 04 80 52 A8 83 1F F8 13 04 40 F9";
    ll::memory::hookSignature(std::string(kSigSignInButton),
                              reinterpret_cast<void*>(&Hooked_StartMenu_SignInButton),
                              reinterpret_cast<void**>(&sOriginalStartMenuSignInButton),
                              offsets.signInButton);

    // Menu Exit / Back Button
    // RVAs: 1.26.52.3: 0x0A567540 | 1.26.51.1: 0x0A5674D0 | 1.26.45.1: 0x0A15248C | 1.26.44.3: 0x0A15252C
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.menuExit, ...);
    static constexpr std::string_view kSigMenuExit =
        "FD 7B BE A9 F3 0B 00 F9 FD 03 00 91 09 04 40 F9 F3 03 08 AA 20 6D 44 F9 ?? ?? ?? 97 ?? ?? ?? ?? ?? ?? ?? 91 ?? ?? ?? ?? ?? ?? ?? 91 1F 00 00 72 0A 02 80 52 4B 03 80 52";
    ll::memory::hookSignature(std::string(kSigMenuExit),
                              reinterpret_cast<void*>(&Hooked_Menu_Exit),
                              reinterpret_cast<void**>(&sOriginalMenuExit),
                              offsets.menuExit);

    // Hook 3: Loading Progress Bar
    // RVAs: 1.26.52.3: 0x09685F70 | 1.26.51.1: 0x09685ED0 | 1.26.45.1: 0x092BEC2C | 1.26.44.3: 0x092BECCC
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.progressBarRender, ...);
    static constexpr std::string_view kSigProgressBar =
        "FF 03 05 D1 EF 3B 0A 6D ED 33 0B 6D EB 2B 0C 6D E9 23 0D 6D FD 7B 0E A9 FC 6F 0F A9 FA 67 10 A9 F8 5F 11 A9 F6 57 12 A9 F4 4F 13 A9 FD 83 03 91 48 D0 3B D5 F5 03 00 AA E0 03 03 AA E8 0F 00 F9";
    ll::memory::hookSignature(std::string(kSigProgressBar),
                              reinterpret_cast<void*>(&Hooked_ProgressBarRenderer_render),
                              reinterpret_cast<void**>(&sOriginalProgressRender),
                              offsets.progressBarRender);

    // Hook 4: In-Game HUD Render
    // RVAs: 1.26.52.3: 0x09ACC828 | 1.26.51.1: 0x09ACC7A8 | 1.26.45.1: 0x0974066C | 1.26.44.3: 0x0974070C
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.inGamePlayScreenRender, ...);
    static constexpr std::string_view kSigInGameHud =
        "FF C3 01 D1 FD 7B 03 A9 F7 23 00 F9 F6 57 05 A9 F4 4F 06 A9 FD C3 00 91 57 D0 3B D5 F4 03 02 AA F3 03 01 AA E8 16 40 F9 F5 03 00 AA A8 83 1F F8 ?? ?? ?? ?? ?? ?? ?? 91 08 FD DF 08 88 10 00 36";
    ll::memory::hookSignature(std::string(kSigInGameHud),
                              reinterpret_cast<void*>(&Hooked_inGamePlayScreenRender),
                              reinterpret_cast<void**>(&sOriginalInGamePlayScreenRender),
                              offsets.inGamePlayScreenRender);

    // Hook 5: ClientInstance::requestLeaveGame
    // RVAs: 1.26.52.3: 0x09800FF8 | 1.26.51.1: 0x09800F58 | 1.26.45.1: 0x0943C430 | 1.26.44.3: 0x0943C4D0
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.clientInstanceRequestLeaveGame, ...);
    static constexpr std::string_view kSigLeaveGame =
        "FF 43 04 D1 FD 7B 0C A9 FC 67 0D A9 F8 5F 0E A9 F6 57 0F A9 F4 4F 10 A9 FD 03 03 91 58 D0 3B D5 F7 03 02 2A F6 03 00 AA";
    ll::memory::hookSignature(std::string(kSigLeaveGame),
                              reinterpret_cast<void*>(&Hooked_clientInstanceRequestLeaveGame),
                              reinterpret_cast<void**>(&sOriginalClientInstanceRequestLeaveGame),
                              offsets.clientInstanceRequestLeaveGame);

    // Hook 6: PanoramaRenderer::$render suppression
    // RVAs: 1.26.52.3: 0x0967FA98 | 1.26.51.1: 0x0967F9F8 | 1.26.45.1: 0x092B8DC0 | 1.26.44.3: 0x092B8E60
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.panoramaRender, ...);
    static constexpr std::string_view kSigPanorama =
        "ED 33 B7 6D EB 2B 01 6D E9 23 02 6D FD 7B 03 A9 FC 23 00 F9 FA 67 05 A9 F8 5F 06 A9 F6 57 07 A9 F4 4F 08 A9 FD C3 00 91 FF 83 13 D1 59 D0 3B D5";
    ll::memory::hookSignature(std::string(kSigPanorama),
                              reinterpret_cast<void*>(&Hooked_PanoramaRenderer_render),
                              reinterpret_cast<void**>(&sOriginalPanoramaRender),
                              offsets.panoramaRender);

    // Hook 7: CubemapBackgroundScreen::$render suppression
    // RVAs: 1.26.52.3: 0x09B7C9E0 | 1.26.51.1: 0x09B7C960 | 1.26.45.1: 0x097C99A4 | 1.26.44.3: 0x097C9A44
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.cubemapBackgroundRender, ...);
    static constexpr std::string_view kSigCubemap =
        "FF 03 02 D1 FD 7B 04 A9 F7 2B 00 F9 F6 57 06 A9 F4 4F 07 A9 FD 03 01 91 56 D0 3B D5 D7 1B 98 52 F4 03 00 AA C8 16 40 F9";
    ll::memory::hookSignature(std::string(kSigCubemap),
                              reinterpret_cast<void*>(&Hooked_CubemapBackgroundScreen_render),
                              reinterpret_cast<void**>(&sOriginalCubemapRender),
                              offsets.cubemapBackgroundRender);

    // Hook 8: DirectoryPackAccessStrategy & ZipPackAccessStrategy (Zero RP Asset Interception)
    // DirectoryPack RVAs: 1.26.52.3: 0x111EEE94 | 1.26.51.1: 0x111EEA54 | 1.26.45.1: 0x1042D360 | 1.26.44.3: 0x1042D8C0
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.directoryPackGetAsset, ...);
    static constexpr std::string_view kSigDirectoryPack =
        "FD 7B BA A9 FC 0B 00 F9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF C3 31 D1 59 D0 3B D5 F3 03 02 AA";
    ll::memory::hookSignature(std::string(kSigDirectoryPack),
                              reinterpret_cast<void*>(&Hooked_DirectoryPack_getAsset),
                              reinterpret_cast<void**>(&sOriginalDirectoryPackGetAsset),
                              offsets.directoryPackGetAsset);

    // ZipPack RVAs: 1.26.52.3: 0x11158670 | 1.26.51.1: 0x11158230 | 1.26.45.1: 0x1040D528 | 1.26.44.3: 0x1040DA88
    // [RVA Hook - Commented Out]: ll::memory::hookRva(offsets.zipPackGetAsset, ...);
    static constexpr std::string_view kSigZipPack =
        "FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF C3 33 D1 59 D0 3B D5 F4 03 02 AA";
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
