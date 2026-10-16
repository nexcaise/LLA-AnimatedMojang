# Animated Mojang (Android ARM64)

[![Platform](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)](https://github.com/spygamingog)
[![Engine](https://img.shields.io/badge/Engine-Bedrock%20v1.26.44.3--v1.26.52.3-blue.svg)](https://minecraft.net)
[![Mod Loader](https://img.shields.io/badge/Loader-LeviLamroid%20%3E%3Dv1.0.1--beta-orange.svg)](https://github.com/spygamingog/LeviLamroid)
[![Version](https://img.shields.io/badge/Version-1.0.2-purple.svg)](https://github.com/spygamingog/Animated-Mojang-Android/releases)

A high-performance native C++ client mod for **Minecraft Bedrock Edition (Android ARM64)** running on **LeviLauncher (LeviLaunchroid)**.

> [!IMPORTANT]
> ### ⚠️ Mod Loader Requirement
> **Animated Mojang Android requires [LeviLamroid](https://github.com/spygamingog/LeviLamroid) (`>= v1.0.1-beta`) installed and enabled in LeviLauncher.**  
> LeviLamroid provides the central AArch64 hook engine, runtime version detection, and GLES render loop interception that powers this mod.

---

## Authors & Attribution

- **Original Windows x64 Mod**: Authored by **[MRUIAW](https://github.com/MRUIAW)** ([MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang)), licensed under LGPL-3.0.
- **Original Java Edition Mod**: Original concept, 3D cave structure, and title assets by **[chara2015](https://github.com/chara2015)** ([chara2015/animated_mojang](https://github.com/chara2015/animated_mojang)), licensed under CC0 1.0.
- **Android ARM64 Port**: Reverse-engineered, ported, and maintained by **[SpyGamingOG](https://github.com/spygamingog)**.

---

## Visual Showcase

![Animated Mojang Main Menu Showcase](icon.png)

*Real-time 60 FPS 3D cave background rendered directly via OpenGL ES 3.0, paired with the hardware-accelerated 69-frame animated Minecraft title and instant sub-menu camera navigation.*

---

## Compatibility

| Component | Supported Version |
|---|---|
| **Minecraft Bedrock** | `1.26.44.3`, `1.26.45.1`, `1.26.51.1`, `1.26.52.3`, and compatible 1.26.x releases |
| **Platform** | Android ARM64 (`aarch64-linux-android`) |
| **Required Loader** | **[LeviLamroid](https://github.com/spygamingog/LeviLamroid)** (`>= v1.0.1-beta`) |
| **Target Launcher** | **LeviLauncher (LeviLaunchroid)** |
| **Version** | `1.0.2` |

---

## Key Features

1. **Real-Time 60 FPS 3D Cave GPU Engine (`CaveGlesRenderer`)**:
   - Renders the authentic `normal_cave.mesh` (98,094 vertices) directly on the game's GPU render thread via LeviLamroid's `ll::render::registerPostClearCallback`.
   - Features baked colored lighting, animated scrolling lava, wall-mounted soul torches, and flame/smoke particle effects.
   - Smooth cubic-bezier spline opening flight with subtle ambient idle camera swaying.
   - **Zero In-World Interference**: Automatically checks `ll::isMainMenu() && !ll::isInWorld()` to guarantee the cave only renders when idling on the main menus and never inside gameplay, preserving 100% of the native vanilla sky, clouds, and dimensions.

2. **Full-Width Hardware-Accelerated 69-Frame Minecraft Title**:
   - 69-frame Aseprite animated flipbook (`animated_title.png` + `animated_title.json`) rendered via Bedrock's native JSON UI engine.
   - Forces JSON UI stack seamlessly using LeviLamroid's `ll::ui::forceJsonUIForScreen`.
   - **Zero-Push Layout Architecture**: Parent panel `title_panel_pocket` remains at exact vanilla size `[ "55%", "18.85%x" ]`, ensuring that menu buttons remain perfectly centered. Child `title_image` scales to `[ "171.8%", "156.7%" ]` with `-30%` vertical offset, displaying the logo at 100% full vanilla size right above the Play button with exploding debris floating upward into the top margin.

3. **Instant, Lag-Free Sub-Menu Camera Navigation**:
   - Distinct, cinematic camera perspectives tailored for every menu screen with zero sideways roll tilt:
     - **Main Start Screen**: `(31.0, 13.0, 6.0, yaw=21.0, pitch=-13.0)`
     - **Worlds / Play Screen**: `(24.0, 15.0, 18.0, yaw=-24.0, pitch=-6.0)`
     - **Settings Screen**: `(26.0, 14.0, 20.0, yaw=-10.0, pitch=-8.0)`
     - **Realms Screen**: `(22.0, 14.0, 24.0, yaw=15.0, pitch=-5.0)`
     - **Marketplace Screen**: `(18.0, 13.0, 18.0, yaw=79.0, pitch=4.0)`
     - **Dressing Room / Profile Screen**: `(20.0, 13.0, 37.0, yaw=42.0, pitch=-4.0)`
   - Driven by LeviLamroid's high-level human UI callbacks (`ll::ui::onPlayButtonClicked`, `ll::ui::onSettingsButtonClicked`, `ll::ui::onMenuExit`, etc.) with zero hardcoded hex RVAs and smooth cubic return-to-center glides.
   > [!TIP]
   > **Recommended: Pair with ForceCloseOreUI**  
   > For the cleanest and most consistent visual experience, pairing Animated Mojang with **ForceCloseOreUI** is highly recommended. Disabling opaque OreUI web shells across screens like Dressing Room and Marketplace allows the continuous 3D cave background, real-time lighting, and cinematic camera flight paths to remain fully visible without opaque web panel obstructions.

4. **Transparent Cubemap Optical Pass-Through (`AnimatedMojang_RP.mcpack`)**:
   - Overrides RenderDragon's default cubemap panorama textures with transparent assets, while LeviLamroid's `ll::render::suppressVanillaBackground` prevents vanilla cubemap rendering in menus.

---

## Installation

### Step 1: Ensure LeviLamroid Is Installed
Before installing Animated Mojang, make sure **[LeviLamroid](https://github.com/spygamingog/LeviLamroid/releases)** (`levilamroid.levipack`) is installed and enabled with order `0` in LeviLauncher.

### Step 2: Install Animated Mojang
1. Download **`animated_mojang.levipack`** and **`AnimatedMojang_RP.mcpack`** from the [Latest Release](https://github.com/spygamingog/Animated-Mojang-Android/releases).
2. Open **LeviLauncher** -> **Mods** tab -> **Import** -> Select **`animated_mojang.levipack`**.
3. Import **`AnimatedMojang_RP.mcpack`** into Minecraft:
   - Tap `AnimatedMojang_RP.mcpack` in your file manager to open it in Minecraft, OR copy the folder to your `resource_packs` directory.
4. In Minecraft, navigate to **Settings** -> **Global Resources** and activate **Animated Mojang Resources**.
5. Launch Minecraft through LeviLauncher!

---

## Configuration

Edit `mods/AnimatedMojang/config/config.json`:

| Option | Default | Description |
|---|---|---|
| `enabled` | `true` | Enable or disable the mod |
| `preferJsonMainMenu` | `true` | Use JSON UI flipbook for title rendering |
| `minecraftTitleAnimation` | `true` | Play the 69-frame title animation sequence |
| `caveBackground` | `true` | Render the real-time 3D OpenGL ES cave background |
| `restyleLoadingProgress` | `true` | Recolor loading progress bars to Crimson theme |
| `replayOnReturnToMenu` | `false` | Replay the intro flight upon returning to main menu |
| `blockMenuInputDuringIntro` | `true` | Prevent accidental button clicks during the intro flight |
| `maxPresentationSeconds` | `15` | Intro timeout safeguard (5–60 seconds) |

---

## Building from Source

### Prerequisites
- Android NDK r25+ / Clang 17+
- CMake 3.22+
- Python 3.10+
- [LeviLamroid](https://github.com/spygamingog/LeviLamroid) SDK headers

### Build & Package Command
```bash
bash package_mod.sh
```

The script builds `libAnimatedMojang.so`, creates `AnimatedMojang_RP.mcpack`, packages `animated_mojang.levipack`, and generates the all-in-one suite zip `AnimatedMojang_Suite_Android.zip` in `dist/`.

---

## Credits & Attribution

- **Original Windows x64 Mod**: [MRUIAW](https://github.com/MRUIAW/Animated-Mojang)
- **Original Java Edition Mod**: [chara2015](https://github.com/chara2015/animated_mojang)
- **Android ARM64 Port**: [SpyGamingOG](https://github.com/spygamingog)
- **Mod Loader**: [LeviLamroid](https://github.com/spygamingog/LeviLamroid)
- **Minecraft**: All game assets and trademarks belong to Mojang AB / Microsoft.
