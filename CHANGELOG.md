# Changelog

All notable changes to the Android ARM64 port of **Animated Mojang** are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and release versions follow [Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-10-23

### Direct Native C++ Re-Architecture
- **Native C++ Engine Integration**: Direct engine integration without companion loader dependencies. The mod contains its own built-in AArch64 inline detours, GOT relocation hooker, and game lifecycle state engine.
- **Purged Resource Pack (RP) Requirement**: Removed all requirements for `AnimatedMojang_RP` resource packs and eliminated all `global_resource_packs.json` file writes.
- **Direct Engine Functional Background Suppression**:
  - Directly hooks `PanoramaRenderer::$render` (RVA `0x0967FA98`) and `CubemapBackgroundScreen::$render` (RVA `0x09B7C9E0`) to suppress the vanilla Ancient City cubemap cleanly in main menus without requiring optical pass-through transparent PNGs.
- **Embedded OpenGL ES 3.0 Real-Time 3D Cave GPU Engine**:
  - Directly hooks GPU `glClear` via GOT relocation slot (RVA `0x132893d8`).
  - Renders the 98,094-vertex `normal_cave.mesh` with baked lighting, animated lava, soul torches, and particles at 60 FPS.
  - Automatically suppresses rendering when `!isMainMenuActive()`, guaranteeing 100% untouched vanilla sky and world rendering in-game.
- **Hardware-Accelerated Animated Title Sequence**:
  - Direct 2D title animation sequence rendered on the GPU render thread without requiring JSON UI modifications.
- **Instant Sub-Menu Camera Navigation**:
  - Tailored zero-roll camera vistas for Play/Worlds, Settings, Realms, Dressing Room, Marketplace, and Main Start screens.
- **LeviPack Packaging**:
  - Generates `animated_mojang.levipack` for one-tap import in LeviLauncher.
