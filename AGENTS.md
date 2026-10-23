# AGENTS.md — Technical Architecture & Modding Guide for Animated Mojang (Android ARM64)

> **Audience**: AI Coding Agents (Antigravity, Claude Code, Codex, Cursor, Copilot) and Native C++ Reverse Engineers working on Minecraft Bedrock Edition (Android ARM64).
> **Port Maintainer**: **SpyGamingOG** (`spygamingog`).

---

## 1. System Overview

`AnimatedMojang` is a high-performance native C++ client mod for **Minecraft Bedrock Edition (Android ARM64)**. It ports the iconic PC mod by MRUIAW / chara2015 directly to mobile devices with 60 FPS performance, seamless sub-menu camera transitions, and an animated Minecraft title without UI distortion.

### Core Architecture
1. **Embedded OpenGL ES 3.0 3D Cave Pipeline (`CaveGlesRenderer.cpp`)**:
   - Injects into the game's GPU render thread via Global Offset Table (GOT) relocation on `glClear` (RVA `0x132893d8`).
   - Renders the authentic `normal_cave.mesh` (98,094 vertices) with baked colored lighting, animated lava UV scrolling, wall-mounted soul torches, and flame/smoke particles at steady 60 FPS.
   - Comprehensive state backup and restoration: preserves VAO, VBO, program, texture bindings, depth mask, depth test, cull face, and blend functions before yielding control back to Minecraft.
2. **Direct Engine Background Suppression (Zero Resource Pack)**:
   - Suppresses RenderDragon's vanilla Ancient City cubemap and panorama directly via function hooks on `PanoramaRenderer::$render` (`0x0967FA98`) and `CubemapBackgroundScreen::$render` (`0x09B7C9E0`).
   - No `.mcpack` or transparent PNG replacement textures are required.
3. **In-World Safety Guards**:
   - Tracks gameplay state via `InGamePlayScreen::render` (`0x09ACC828`), `InGamePlayScreen::_renderLevel` (`0x09ACD4C8`), and `ClientInstance::requestLeaveGame` (`0x09800FF8`).
   - Automatically shuts off cave rendering when inside a world, restoring 100% untouched vanilla sky and environment rendering.
4. **Cinematic Sub-Menu Camera Navigation**:
   - Camera transitions are driven by direct button click handlers (`Play`, `Settings`, `Realms`, `Dressing Room`, `Marketplace`), avoiding route pre-query stutter.
   - Menu exit hook (`0x0A567540`) and start menu open hook (`0x0A2DF314`) instantly reset the camera back to `CavePage::Main` without lag.

---

## 2. Reverse-Engineered Hook Topology (Bedrock 1.26.52.3 ARM64)

| Hook Target | RVA | Type | Purpose |
|---|---|---|---|
| `ScreenTechStackSelector::getTechStackForScreen` | `0x0974E844` | Inline Detour | Forces `TechStack::JsonUI` (0) on menu screens |
| `StartMenuScreenController::$onOpen` | `0x0A2DF314` | Inline Detour | Resets camera to `CavePage::Main` on return |
| `StartMenu::PlayButton` | `0x0A31344C` | Inline Detour | Transitions camera to Worlds (`CavePage::Worlds`) |
| `StartMenu::SettingsButton` | `0x0A3171AC` | Inline Detour | Transitions camera to Settings (`CavePage::Settings`) |
| `StartMenu::RealmsButton` | `0x0A3174DC` | Inline Detour | Transitions camera to Realms (`CavePage::Realms`) |
| `StartMenu::DressingRoomButton` | `0x0A31815C` | Inline Detour | Transitions camera to Profile (`CavePage::DressingRoom`) |
| `StartMenu::MarketplaceButton` | `0x0A3187A4` | Inline Detour | Transitions camera to Marketplace (`CavePage::Marketplace`) |
| `Menu_Exit` | `0x0A567540` | Inline Detour | Transitions camera back to MainMenu on sub-menu close |
| `ProgressBarRenderer::$render` | `0x09685F70` | Inline Detour | Restyles loading progress bar to Crimson theme |
| `InGamePlayScreen::render` | `0x09ACC828` | Inline Detour | Sets `sIsInWorld = true` |
| `InGamePlayScreen::_renderLevel` | `0x09ACD4C8` | Inline Detour | Sets `sIsInWorld = true` |
| `ClientInstance::requestLeaveGame` | `0x09800FF8` | Inline Detour | Resets `sIsInWorld = false`, `sIsMainMenu = true` |
| `PanoramaRenderer::$render` | `0x0967FA98` | Inline Detour | Suppresses vanilla 2D panorama renderer |
| `CubemapBackgroundScreen::$render` | `0x09B7C9E0` | Inline Detour | Suppresses vanilla cubemap screen pass |
| `glClear` (libGLESv2.so) | `0x132893d8` | GOT Relocation | Primary OpenGL ES 3.0 cave rendering hook |

---

## 3. Sub-Menu Camera Angles (Degrees & Coordinates)

Coordinates are defined in `src/scene/CaveCamera.h`:

```cpp
// Main Menu Idle
constexpr CavePose caveMenuIdle{31.0f, 13.0f, 6.0f, 21.0f, -13.0f, 0.0f};

// Sub-Menu Poses (Zero-Roll)
constexpr CavePose caveWorldsPose{24.0f, 15.0f, 18.0f, -24.0f, -6.0f, 0.0f};
constexpr CavePose caveSettingsPose{26.0f, 14.0f, 20.0f, -10.0f, -8.0f, 0.0f};
constexpr CavePose caveRealmsPose{22.0f, 14.0f, 24.0f, 15.0f, -5.0f, 0.0f};
constexpr CavePose caveMarketplacePose{18.0f, 13.0f, 18.0f, 79.0f, 4.0f, 0.0f};
constexpr CavePose caveDressingRoomPose{20.0f, 13.0f, 37.0f, 42.0f, -4.0f, 0.0f};
```
