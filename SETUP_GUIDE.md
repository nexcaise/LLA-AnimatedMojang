# Animated Mojang for LeviLauncher Android (Setup & Distribution Guide)

## Overview
This package contains the complete, hardware-accelerated **Animated Mojang** suite for **Minecraft Bedrock Edition (Android ARM64)** running on **LeviLaunchroid** (LeviLamina Android).

---

## What's Included
1. **`AnimatedMojang_RP.mcpack`** (881 KB):
   - Transparent panorama shield (`panorama_*.png`, `panorama_alternate_*.png`, `panoramahdr_*.hdr`).
   - Hardware-accelerated 69-frame Aseprite animated Minecraft title (`animated_title.png` + `animated_title.json`).
   - Zero-push UI layout (`ui/ui_art_assets_common.json`): Logo letters display at full 100% vanilla size right above the Play button with zero pushing of menu options.
2. **`animated_mojang.levipack`** (2.6 MB):
   - Native C++ shared library (`libAnimatedMojang.so`) compiled for Android ARM64 (ELF64).
   - 3D Cave mesh (`normal_cave.mesh`), textures, shaders, and configuration.
   - LeviLamina mod manifest.
3. **`AnimatedMojang-1.26.52.3-android-arm64.zip`** (2.6 MB):
   - Raw loose folder format ready to drop into LeviLauncher `mods/AnimatedMojang/`.

---

## One-Click Setup Instructions

### Method A: Automated Deployment (Active Device)
If running on the current device, `package_mod.sh` has already:
1. Deployed `libAnimatedMojang.so` and assets to:
   - `/sdcard/Android/media/org.levimc.launcher/minecraft/com.mojang.minecraftpe/mods/AnimatedMojang/`
   - `/sdcard/Android/media/org.levimc.launcher/minecraft/Minecraft_1.26.45.1/mods/AnimatedMojang/`
2. Enabled the mod in `mods_config.json` for both profiles.
3. Synchronized loose `AnimatedMojang_RP` to all `resource_packs/` directories.
4. Registered UUID `8e4d2a10-4820-4b21-81d3-359f4d1e00a1` in all `global_resource_packs.json` paths.
5. Set `dev_disableRenderMainMenuCubeMap:1` in all `options.txt` files.

👉 **Simply open LeviLauncher and launch Minecraft!**

---

### Method B: Installing on Another Device

1. **Copy the Files**:
   - Transfer `AnimatedMojang_Suite_Android.zip` (found in `/sdcard/Download/` or `/sdcard/MG/`) to the new device and extract it.
2. **Install the C++ Mod**:
   - Open LeviLauncher -> **Mods** tab -> **Import** -> Select `animated_mojang.levipack` (or copy the `AnimatedMojang` folder from `AnimatedMojang-1.26.52.3-android-arm64.zip` into `.../minecraft/<profile>/mods/`).
   - Ensure the mod toggle is **Enabled** in LeviLauncher.
3. **Install the Resource Pack**:
   - Tap `AnimatedMojang_RP.mcpack` in your file manager to open it with Minecraft/LeviLauncher, OR copy the loose folder `AnimatedMojang_RP` to:
     `/sdcard/Android/media/org.levimc.launcher/minecraft/<profile>/external/games/com.mojang/resource_packs/AnimatedMojang_RP/`
   - Go to **Settings** -> **Global Resources** -> Activate **Animated Mojang Resources**.

---

## Verified Sub-Menu Camera Angles
- **Start Menu**: Classic torchlit cavern view with flowing lava.
- **Play Button**: Switches camera angle to the Worlds perspective.
- **Settings Button**: Switches camera angle to the Settings perspective.
- **Realms Button**: Switches camera angle to the Realms perspective.
- **Marketplace Button**: Switches camera angle to the Marketplace perspective.
- **Dressing Room / Profile**: Switches camera angle to the Dressing Room perspective.
- **Back / Exit**: Instantly returns camera to Main Menu idle state with 0 lag.
