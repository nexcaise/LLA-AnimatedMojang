#pragma once

#include "mod/Config.h"
#include "ll/api/Logger.h"
#include <filesystem>

namespace my_mod {

class MyMod {
private:
    animated_mojang::Config mConfig;
    std::filesystem::path mModDir{"/sdcard/Android/data/com.mojang.minecraftpe/files/levilamina/mods/AnimatedMojang"};
    std::filesystem::path mConfigDir{"/sdcard/Android/data/com.mojang.minecraftpe/files/levilamina/mods/AnimatedMojang/config"};
    ll::Logger mLogger{"AnimatedMojang"};

public:
    static MyMod& getInstance();

    bool load();
    bool enable();
    bool disable();
    bool unload();

    const std::filesystem::path& getModDir() const { return mModDir; }
    const std::filesystem::path& getConfigDir() const { return mConfigDir; }
    void setModDir(std::filesystem::path const& dir) { mModDir = dir; mConfigDir = dir / "config"; }

    ll::Logger& getLogger() { return mLogger; }
    const ll::Logger& getLogger() const { return mLogger; }
    MyMod& getSelf() { return *this; }
};

} // namespace my_mod
