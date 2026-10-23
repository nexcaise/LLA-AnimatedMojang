#include "mod/MyMod.h"
#include "client/Client.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <dlfcn.h>
#include <nlohmann/json.hpp>

namespace my_mod {

MyMod& MyMod::getInstance() {
    static MyMod instance;
    return instance;
}

bool MyMod::load() {
    getLogger().info("Initializing Animated Mojang on Android ARM64 (LeviLaunchroid)...");

    // Discover base directory dynamically
    std::vector<std::filesystem::path> candidates;
    Dl_info dlinfo;
    if (dladdr(reinterpret_cast<void*>(&MyMod::getInstance), &dlinfo) && dlinfo.dli_fname) {
        candidates.push_back(std::filesystem::path(dlinfo.dli_fname).parent_path());
    }
    candidates.push_back("/sdcard/Android/media/org.levimc.launcher/minecraft/com.mojang.minecraftpe/mods/AnimatedMojang");
    candidates.push_back("/sdcard/MG/AnimatedMojang");
    candidates.push_back("/sdcard/Download/AnimatedMojang");
    candidates.push_back("/data/data/org.levimc.launcher/cache/native_mods/com.mojang.minecraftpe/mods/AnimatedMojang");
    candidates.push_back("/sdcard/Android/data/com.mojang.minecraftpe/files/mods/AnimatedMojang");
    candidates.push_back("/sdcard/Android/data/com.mojang.minecraftpe/files/levilamina/mods/AnimatedMojang");

    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate / "resources" / "data" / "normal_cave.mesh")) {
            setModDir(candidate);
            getLogger().info("Resolved mod directory: ", candidate.string());
            break;
        }
    }

    // Check for local mod resources directory
    auto cfgPath = getConfigDir() / "config.json";
    if (std::filesystem::exists(cfgPath)) {
        try {
            std::ifstream f(cfgPath);
            nlohmann::json j = nlohmann::json::parse(f);
            mConfig.enabled = j.value("enabled", true);
            mConfig.preferJsonMainMenu = j.value("preferJsonMainMenu", true);
            mConfig.caveBackground = j.value("caveBackground", true);
            mConfig.minecraftTitleAnimation = j.value("minecraftTitleAnimation", true);
            mConfig.maxPresentationSeconds = j.value("maxPresentationSeconds", 15.0);
            if (j.contains("menuScreenNames")) {
                mConfig.menuScreenNames = j["menuScreenNames"].get<std::vector<std::string>>();
            }
        } catch (...) {
            getLogger().warn("Failed to parse config.json, using defaults.");
        }
    }


    animated_mojang::client::start(mConfig, getModDir());
    if (mConfig.enabled && mConfig.caveBackground) {
        animated_mojang::client::prepareCave(getModDir());
    }

    getLogger().info("Animated Mojang loaded and enabled successfully for Bedrock!");
    return true;
}

bool MyMod::enable() {
    animated_mojang::client::start(mConfig, getModDir());
    return true;
}

bool MyMod::disable() {
    animated_mojang::client::stop();
    return true;
}

bool MyMod::unload() {
    animated_mojang::client::stop();
    return true;
}

} // namespace my_mod

// ----------------------------------------------------------------------------
// Exported Lifecycle Entrypoints for LeviLaunchroid & LeviLamina
// ----------------------------------------------------------------------------

struct PLModRegistration {
    void* instance;
    bool (*load)(void* instance, void* context);
    bool (*enable)(void* instance, void* context);
    bool (*disable)(void* instance, void* context);
    bool (*unload)(void* instance, void* context);
};

static bool pl_load_shim(void*, void*) {
    return my_mod::MyMod::getInstance().load();
}
static bool pl_enable_shim(void*, void*) {
    return my_mod::MyMod::getInstance().enable();
}
static bool pl_disable_shim(void*, void*) {
    return my_mod::MyMod::getInstance().disable();
}
static bool pl_unload_shim(void*, void*) {
    return my_mod::MyMod::getInstance().unload();
}

static PLModRegistration sPLRegistration = {
    &my_mod::MyMod::getInstance(),
    pl_load_shim,
    pl_enable_shim,
    pl_disable_shim,
    pl_unload_shim
};

extern "C" {

#if defined(__GNUC__)
#define MOD_EXPORT __attribute__((visibility("default")))
#else
#define MOD_EXPORT
#endif

// LeviLauncher / LeviLaunchroid PreLoader ABI Entrypoint
MOD_EXPORT const void* PLGetModRegistration() {
    my_mod::MyMod::getInstance().load();
    my_mod::MyMod::getInstance().enable();
    return &sPLRegistration;
}

// LeviLamina Compatibility Entrypoints
MOD_EXPORT bool ll_mod_load(void* /*modContext*/) {
    return my_mod::MyMod::getInstance().load();
}

MOD_EXPORT bool ll_mod_enable(void* /*modContext*/) {
    return my_mod::MyMod::getInstance().enable();
}

MOD_EXPORT bool ll_mod_disable(void* /*modContext*/) {
    return my_mod::MyMod::getInstance().disable();
}

// Android Dynamic Linker Entrypoint
using jint = int32_t;
MOD_EXPORT jint JNI_OnLoad(void* /*vm*/, void* /*reserved*/) {
    my_mod::MyMod::getInstance().load();
    my_mod::MyMod::getInstance().enable();
    return 0x00010006;
}

// ----------------------------------------------------------------------------
// NDK libc++ Fallback Symbols for older/differing Android dynamic linkers
// Prevents "dlopen failed: cannot locate symbol _ZNSt6__ndk113__hash_memoryEPKvm"
// ----------------------------------------------------------------------------
MOD_EXPORT size_t _ZNSt6__ndk113__hash_memoryEPKvm(const void* ptr, size_t len) {
    const auto* data = static_cast<const unsigned char*>(ptr);
    size_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

MOD_EXPORT size_t _ZNSt6__ndk112__next_primeEm(size_t n) {
    static const size_t primes[] = {
        53UL, 97UL, 193UL, 389UL, 769UL, 1543UL, 3079UL, 6151UL, 12289UL, 24593UL,
        49157UL, 98317UL, 196613UL, 393241UL, 786433UL, 1572869UL, 3145739UL,
        6291469UL, 12582917UL, 25165843UL, 50331653UL, 100663319UL, 201326611UL,
        402653189UL, 805306457UL, 1610612741UL, 3221225473UL, 4294967291UL
    };
    for (size_t p : primes) {
        if (p >= n) return p;
    }
    return n | 1;
}

}

