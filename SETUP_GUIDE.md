# Animated Mojang for LeviLauncher Android (Setup & Distribution Guide)

## Overview
This package contains the complete, hardware-accelerated **Animated Mojang** suite for **Minecraft Bedrock Edition (Android ARM64)** running on **LeviLauncher (LeviLaunchroid)**, powered by **[LeviLamroid](https://github.com/spygamingog/LeviLamroid)**.

---

## Prerequisites: Install LeviLamroid
Before installing Animated Mojang, make sure **[LeviLamroid](https://github.com/spygamingog/LeviLamroid/releases)** (`levilamroid.levipack`) is installed as the core mod loader and enabled with order `0` in LeviLauncher.

---

## What's Included
1. **`AnimatedMojang_RP.mcpack`**:
   - Transparent panorama shield (`panorama_*.png`, `panorama_alternate_*.png`, `panoramahdr_*.hdr`).
   - Hardware-accelerated 69-frame Aseprite animated Minecraft title (`animated_title.png` + `animated_title.json`).
   - Zero-push UI layout (`ui/ui_art_assets_common.json`): Logo letters display at full 100% vanilla size right above the Play button with zero pushing of menu options.
2. **`animated_mojang.levipack`**:
   - Native C++ shared library (`libAnimatedMojang.so`) compiled for Android ARM64 (ELF64).
   - 3D Cave mesh (`normal_cave.mesh`), textures, shaders, and configuration.
   - LeviLamroid mod manifest.
3. **`AnimatedMojang-1.0.0-android-arm64.zip`**:
   - Raw loose folder format ready to drop into LeviLauncher `mods/AnimatedMojang/`.

---

## Setup Instructions

### Method A: One-Click LeviLauncher Install (Recommended)
1. In **LeviLauncher (LeviLaunchroid)**, navigate to **Mods** -> **Import** -> Select `animated_mojang.levipack`.
2. Tap `AnimatedMojang_RP.mcpack` in your file manager to import it into Minecraft (or copy `AnimatedMojang_RP` into your `resource_packs` directory).
3. In Minecraft, navigate to **Settings** -> **Global Resources** -> Activate **Animated Mojang Resources**.
4. Launch Minecraft through LeviLauncher!

### Method B: Manual Installation
1. Extract the `AnimatedMojang` folder into:
   ```
   /sdcard/Android/media/org.levimc.launcher/minecraft/<profile>/mods/AnimatedMojang/
   ```
2. Copy `AnimatedMojang_RP` folder to:
   ```
   /sdcard/Android/media/org.levimc.launcher/minecraft/<profile>/external/games/com.mojang/resource_packs/AnimatedMojang_RP/
   ```
3. In `mods_config.json`, verify `AnimatedMojang` is enabled.

---

## Verified Sub-Menu Camera Angles
- **Start Menu**: Classic torchlit cavern view with flowing lava.
- **Play Button**: Switches camera angle to the Worlds perspective.
- **Settings Button**: Switches camera angle to the Settings perspective.
- **Realms Button**: Switches camera angle to the Realms perspective.
- **Marketplace Button**: Switches camera angle to the Marketplace perspective.
- **Dressing Room / Profile**: Switches camera angle to the Dressing Room perspective.
- **Back / Exit**: Instantly returns camera to Main Menu idle state with 0 lag.
