# Animated Mojang (Android ARM64)

[![Platform](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)](https://github.com/spygamingog)
[![Engine](https://img.shields.io/badge/Engine-Bedrock%20v1.26.44.3--v1.26.52.3-blue.svg)](https://minecraft.net)
[![Architecture](https://img.shields.io/badge/Architecture-Native%20C%2B%2B-orange.svg)](https://github.com/spygamingog/Animated-Mojang-Android)
[![Package](https://img.shields.io/badge/Package-animated__mojang.levipack-blueviolet.svg)](https://github.com/spygamingog/Animated-Mojang-Android)
[![Version](https://img.shields.io/badge/Version-1.0.0-purple.svg)](https://github.com/spygamingog/Animated-Mojang-Android)

A high-performance native C++ client mod for **Minecraft Bedrock Edition (Android ARM64)**.

> [!NOTE]
> ### 🚀 Direct Native Architecture (Zero RP & Zero External Loader Dependency)
> **Animated Mojang v1.0.0 features direct engine integration:**
> - **Zero Resource Pack (RP) Requirement**: No `.mcpack` or resource pack installation is needed. All vanilla panorama and Ancient City cubemap suppressions are handled directly through native engine function hooks (`PanoramaRenderer::$render`, `CubemapBackgroundScreen::$render`).
> - **Zero Companion Loader Dependency**: No companion mod loaders or external helper libraries (like LeviLamroid) are required. The mod directly hooks native Bedrock engine functions via internal AArch64 detours and GOT relocation.

---

## Authors & Attribution

- **Original Windows x64 Mod**: Authored by **[MRUIAW](https://github.com/MRUIAW)** ([MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang)), licensed under LGPL-3.0.
- **Original Java Edition Mod**: Concept, 3D cave structure, and title assets by **[chara2015](https://github.com/chara2015)** ([chara2015/animated_mojang](https://github.com/chara2015/animated_mojang)), licensed under CC0 1.0.
- **Android ARM64 Port**: Reverse-engineered, ported, and maintained by **[SpyGamingOG](https://github.com/spygamingog)**.

---

## Visual Showcase

![Animated Mojang Main Menu Showcase](icon.png)

*Real-time 60 FPS 3D cave background rendered directly via OpenGL ES 3.0, paired with hardware-accelerated animated Minecraft title sequence and seamless sub-menu camera transitions.*

---

## Compatibility

| Component | Supported Version |
|---|---|
| **Minecraft Bedrock** | `1.26.44.3`, `1.26.45.1`, `1.26.51.1`, `1.26.52.3`, and compatible 1.26.x releases |
| **Platform** | Android ARM64 (`aarch64-linux-android`) |
| **Dependencies** | **None** (Native C++) |
| **Resource Pack** | **None** (Direct functional suppression, no RP needed) |
| **Loaders Supported** | LeviLauncher (LeviLaunchroid), PreLoader, Zygisk, or direct native injection |
| **Package Format** | `.levipack` (`animated_mojang.levipack`) |
| **Version** | `1.0.0` |

---

## Key Features

1. **Embedded Real-Time 60 FPS 3D Cave GPU Engine (`CaveGlesRenderer`)**:
   - Injects into the game's GPU render thread via Global Offset Table (GOT) relocation on `glClear` (RVA `0x132893d8`).
   - Renders authentic `normal_cave.mesh` (98,094 vertices) with baked colored lighting, animated lava UV scrolling, wall-mounted soul torches, and flame/smoke particles at steady 60 FPS.
   - Comprehensive state backup and restoration: preserves VAO, VBO, program, texture bindings, depth mask, depth test, cull face, and blend functions before yielding control back to Minecraft.

2. **Direct Functional Background Suppression (Zero Resource Pack)**:
   - Eliminates the requirement for transparent panorama resource packs (`AnimatedMojang_RP`).
   - Hooks RenderDragon's native background renderers (`PanoramaRenderer::$render` at `0x0967FA98`, `CubemapBackgroundScreen::$render` at `0x09B7C9E0`) to suppress the vanilla Ancient City cubemap directly in memory.
   - **Zero In-World Interference**: Tracks game state (`isMainMenuActive()`) across `InGamePlayScreen::render` and `ClientInstance::requestLeaveGame`, ensuring the cave only renders when idling on the main menus and never inside gameplay, preserving 100% vanilla sky and dimensions.

3. **Hardware-Accelerated Title Animation**:
   - Renders the 2D title animation sequence directly on the GPU render thread without requiring JSON UI modifications or external resource packs.
   - Hooks `ui::ScreenTechStackSelector::getTechStackForScreen` (RVA `0x0974E844`) to enforce JSON UI for seamless menu presentation.

4. **Instant Sub-Menu Camera Navigation**:
   - Distinct, cinematic camera perspectives tailored for every menu screen with zero sideways roll tilt:
     - **Main Start Screen**: `(31.0, 13.0, 6.0, yaw=21.0, pitch=-13.0)`
     - **Worlds / Play Screen**: `(24.0, 15.0, 18.0, yaw=-24.0, pitch=-6.0)`
     - **Settings Screen**: `(26.0, 14.0, 20.0, yaw=-10.0, pitch=-8.0)`
     - **Realms Screen**: `(22.0, 14.0, 24.0, yaw=15.0, pitch=-5.0)`
     - **Marketplace Screen**: `(18.0, 13.0, 18.0, yaw=79.0, pitch=4.0)`
     - **Dressing Room Screen**: `(20.0, 13.0, 37.0, yaw=42.0, pitch=-4.0)`
   - Hooked directly to engine button handlers (Play, Settings, Realms, Dressing Room, Marketplace, Menu Exit) for zero-lag camera glides.

---

## Installation

### LeviLauncher (LeviLaunchroid)
1. Get **`animated_mojang.levipack`**.
2. Open **LeviLauncher** -> **Mods** tab -> **Import** -> Select **`animated_mojang.levipack`**.
3. Ensure AnimatedMojang is enabled.
4. Launch Minecraft. That's it! (Zero resource pack required).

---

## Configuration

Edit `mods/AnimatedMojang/config/config.json`:

| Option | Default | Description |
|---|---|---|
| `enabled` | `true` | Enable or disable the mod |
| `preferJsonMainMenu` | `true` | Use JSON UI start menu mode |
| `minecraftTitleAnimation` | `true` | Play the animated Minecraft title sequence |
| `caveBackground` | `true` | Render the real-time 3D OpenGL ES cave background |
| `restyleLoadingProgress` | `true` | Restyle progress bar to Crimson theme |
| `replayOnReturnToMenu` | `true` | Replay the intro cinematic when returning from gameplay |
| `maxPresentationSeconds` | `15.0` | Maximum opening flight duration |

---

## Building from Source

### Prerequisites
- Clang compiler for AArch64 (Android NDK r25+ or Termux clang++)
- CMake 3.22+

### Build Steps
```bash
# Build native shared library and generate .levipack:
./package_mod.sh
```
The output package is generated at:
- `dist/animated_mojang.levipack`

---

## License

- Source code licensed under **LGPL-3.0** (inherited from [MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang)).
- 3D cave model and animation assets by **chara2015** under **CC0 1.0 Universal**.
