# AGENTS.md — Technical Architecture & Modding Guide for Animated Mojang (Android ARM64)

> **Audience**: AI Coding Agents (Antigravity, Claude Code, Codex, Cursor, Copilot) and Native C++ Reverse Engineers working on Minecraft Bedrock Edition (Android ARM64).
> **Port Author & Maintainer**: **SpyGamingOG** (`spygamingog`).
> **Original Authors**: **MRUIAW** (Windows LeviLamina mod), **chara2015** (original Java mod & assets).
> **Required Mod Loader**: **LeviLamroid** (`>= v1.0.0-beta`).

---

## 1. System Overview

`AnimatedMojang` is an enterprise-grade client mod for **Minecraft Bedrock Edition (Android ARM64)** running on **LeviLauncher (LeviLaunchroid)** and powered by **LeviLamroid**. It ports the iconic PC mod by MRUIAW / chara2015 to mobile devices with 60 FPS performance, seamless sub-menu camera transitions, and an animated Minecraft title without UI distortion.

### Core Architecture
1. **Embedded OpenGL ES 3.0 3D Cave Pipeline (`CaveGlesRenderer.cpp`)**:
   - Injects into the game's GPU render thread via Global Offset Table (GOT) relocation on `glClear` (RVA `0x132893d8`).
   - Renders the authentic `normal_cave.mesh` (98,094 vertices) with baked colored lighting, animated lava UV scrolling, wall-mounted soul torches, and flame/smoke particles at a rock-solid 60 FPS.
   - Comprehensive state backup and restoration: preserves VAO, VBO, program, texture bindings, depth mask, depth test, cull face, and blend functions before yielding control back to Minecraft.
2. **Transparent Cubemap Optical Pass-Through (`AnimatedMojang_RP.mcpack`)**:
   - RenderDragon on Android ARM64 draws an internal Ancient City skybox cubemap over the framebuffer unless masked.
   - The global resource pack replaces `panorama_0.png` through `panorama_5.png`, `panorama_alternate_*.png`, and `panoramahdr_*.hdr` with 100% transparent PNGs (`RGBA(0, 0, 0, 0)`).
   - When RenderDragon draws the skybox, it samples transparent pixels, acting as an optical pass-through window that keeps the 3D OpenGL ES cave room completely visible.
3. **Hardware-Accelerated Zero-Push Title Layout**:
   - Uses Bedrock's native `aseprite_flip_book` engine in JSON UI to render the 69-frame Minecraft title animation (`animated_title.png` + `animated_title.json`).
   - **Zero Button Pushing Math**: Parent panel `title_panel_pocket` remains at exact vanilla size `[ "55%", "18.85%x" ]`, keeping all main menu buttons centered. The child `title_image` is sized to `[ "171.8%", "156.7%" ]` (`94.5%` screen width) with `offset: [ 0, "-30%" ]`, so the logo letters display at 100% full vanilla size right above the Play button with exploding debris floating upward into the top margin.
4. **Cinematic Sub-Menu Camera Navigation (0-Lag Transitions)**:
   - Camera transitions are driven by direct button click handlers (`Play`, `Settings`, `Realms`, `Dressing Room`, `Marketplace`), avoiding route pre-query stutter.
   - Menu exit hook (`0x0A567540`) and start menu open hook (`0x0A2DF314`) instantly reset the camera back to `CavePage::Main` without lag.

---

## 2. Reverse-Engineered Dobby Hook Topology (Bedrock 1.26.52.3 ARM64)

| Hook Target | RVA | Type | Purpose |
|---|---|---|---|
| `ScreenTechStackSelector::getTechStackForScreen` | `0x0974E844` | Dobby Inline | Forces `TechStack::JsonUI` (0) on menu screens |
| `StartMenuScreenController::$onOpen` | `0x0A2DF314` | Dobby Inline | Resets camera to `CavePage::Main` on return |
| `StartMenu::PlayButton` | `0x0A31344C` | Dobby Inline | Transitions camera to Worlds (`CavePage::Worlds`) |
| `StartMenu::SettingsButton` | `0x0A3171AC` | Dobby Inline | Transitions camera to Settings (`CavePage::Settings`) |
| `StartMenu::RealmsButton` | `0x0A3174DC` | Dobby Inline | Transitions camera to Realms (`CavePage::Realms`) |
| `StartMenu::DressingRoomButton` | `0x0A31815C` | Dobby Inline | Transitions camera to Profile (`CavePage::DressingRoom`) |
| `StartMenu::MarketplaceButton` | `0x0A3187A4` | Dobby Inline | Transitions camera to Marketplace (`CavePage::Marketplace`) |
| `Menu_Exit` | `0x0A567540` | Dobby Inline | Transitions camera back to MainMenu on sub-menu close |
| `ProgressBarRenderer::$render` | `0x09685F70` | Dobby Inline | Restyles loading progress bar to Crimson theme |
| `PanoramaRenderer::$render` | `0x0967FA98` | Dobby Inline | Suppresses vanilla 2D panorama renderer |
| `CubemapBackgroundScreen::$render` | `0x09B7C9E0` | Dobby Inline | Suppresses vanilla cubemap screen pass |
| `CubemapFaceSubmit` | `0x09B7B544` | Dobby Inline | Suppresses 6-face RenderDragon cubemap submission |
| `CubemapRenderer::render` | `0x09B7C0B4` | Dobby Inline | Suppresses background cubemap loop |
| `MainMenuCubemapPass` | `0x0B2DC860` | Dobby Inline | Suppresses low-level 60fps cubemap geometry pass |
| `CubemapDraw` | `0x0B29A6AC` | Dobby Inline | Suppresses direct skybox draw call |
| `glClear` (libGLESv2.so) | `0x132893d8` | GOT Relocation | Primary OpenGL ES 3.0 cave rendering hook |

---

## 3. Sub-Menu Camera Angles (Degrees & Coordinates)

Coordinates are defined in `src/scene/CaveCamera.h`:

```cpp
// Main Menu Idle
constexpr CavePose caveMenuIdle{31.0f, 13.0f, 6.0f, 21.0f, -13.0f, 0.0f};

// Worlds (Play)
constexpr CavePose caveWorlds{28.0f, 12.0f, 14.0f, 15.0f, -8.0f, 0.0f};

// Settings
constexpr CavePose caveSettings{24.0f, 14.0f, 25.0f, 30.0f, -5.0f, 0.0f};

// Realms
constexpr CavePose caveRealms{33.0f, 11.0f, 10.0f, 10.0f, -15.0f, 0.0f};

// Marketplace
constexpr CavePose caveMarketplace{20.0f, 15.0f, 35.0f, 45.0f, -2.0f, 0.0f};

// Dressing Room / Profile
constexpr CavePose caveDressingRoom{35.0f, 12.0f, 5.0f, 5.0f, -20.0f, 0.0f};
```

---

## 4. Multi-Profile Deployment Guidelines

LeviLauncher on Android maintains separate profiles:
1. `com.mojang.minecraftpe` (Default / Storage isolated)
2. `Minecraft_1.26.45.1` (Versioned Profile)
3. `_shared` (Shared storage fallback)

When deploying, `package_mod.sh` automatically:
- Copies `libAnimatedMojang.so`, `manifest.json`, `config/`, and `resources/` to both `mods/` folders.
- Ensures `"enabled": true` in both `mods_config.json` files.
- Synchronizes loose `AnimatedMojang_RP/` to all internal and external `resource_packs/` directories.
- Prepends UUID `8e4d2a10-4820-4b21-81d3-359f4d1e00a1` to all `global_resource_packs.json` files.
- Sets `dev_disableRenderMainMenuCubeMap:1` in all `options.txt` files.
- Generates `AnimatedMojang_RP.mcpack` and `AnimatedMojang_Suite_Android.zip` in `/sdcard/Download/` and `/sdcard/MG/`.

---

## 5. Building & Packaging

```bash
# Build native binary and generate all distribution artifacts
cd examples/animated-mojang-android
bash package_mod.sh
```

Artifacts produced:
- `dist/libAnimatedMojang.so`: Compiled ARM64 ELF shared library.
- `dist/AnimatedMojang_RP.mcpack`: Global resource pack.
- `dist/animated_mojang.levipack`: LeviLamina one-click mod package.
- `dist/AnimatedMojang_Suite_Android.zip`: Complete portable distribution bundle.
