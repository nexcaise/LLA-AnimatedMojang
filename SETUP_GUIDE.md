# Animated Mojang Standalone Edition (Setup & Architecture Guide)

## Overview
This package contains the complete, hardware-accelerated **Animated Mojang Standalone Edition** for **Minecraft Bedrock Edition (Android ARM64)**.

It requires **zero resource packs** and **zero companion mod loaders**.

---

## What's Included
1. **`animated_mojang.levipack`** (2.5 MB):
   - Native C++ shared library (`libAnimatedMojang.so`) compiled for Android ARM64 (ELF64).
   - 3D Cave mesh (`normal_cave.mesh`), textures, shaders, and configuration.
   - Self-contained manifest for LeviLauncher.
2. **`AnimatedMojang-1.0.0-android-arm64.zip`** (2.5 MB):
   - Raw loose folder format ready to extract into `mods/AnimatedMojang/`.

---

## Installation

### Method A: LeviLauncher (LeviLaunchroid)
1. Open **LeviLauncher** -> **Mods** tab -> **Import** -> Select `animated_mojang.levipack`.
2. Ensure the mod toggle is **Enabled**.
3. Launch Minecraft.

### Method B: Manual Extraction
1. Extract `AnimatedMojang-1.0.0-android-arm64.zip` into:
   `/sdcard/Android/media/org.levimc.launcher/minecraft/<profile>/mods/AnimatedMojang/`
2. Launch Minecraft.

---

## Technical Hook Topology (Android ARM64)
- **JSON UI Screen Enforcement**: RVA `0x0974E844` (`ScreenTechStackSelector::getTechStackForScreen`)
- **Main Menu Open**: RVA `0x0A2DF314` (`StartMenuScreenController::$onOpen`)
- **Navigation Buttons**:
  - Play: RVA `0x0A31344C`
  - Settings: RVA `0x0A3171AC`
  - Realms: RVA `0x0A3174DC`
  - Dressing Room: RVA `0x0A31815C`
  - Marketplace: RVA `0x0A3187A4`
  - Menu Exit / Back: RVA `0x0A567540`
- **Panorama Suppression**: RVA `0x0967FA98` (`PanoramaRenderer::$render`)
- **Cubemap Background Screen Suppression**: RVA `0x09B7C9E0` (`CubemapBackgroundScreen::$render`)
- **In-Game State Detection**:
  - InGame HUD: RVA `0x09ACC828` (`InGamePlayScreen::render`)
  - World Render: RVA `0x09ACD4C8` (`InGamePlayScreen::_renderLevel`)
  - Leave Game: RVA `0x09800FF8` (`ClientInstance::requestLeaveGame`)
- **3D Cave & Title GPU Render**: GOT Relocation slot RVA `0x132893d8` (`glClear`)
