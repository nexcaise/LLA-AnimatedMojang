# Animated Mojang (Android ARM64)

[![Platform](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)](https://github.com/spygamingog)
[![Engine](https://img.shields.io/badge/Engine-Bedrock%20v1.26.44.3--v1.26.52.3+-blue.svg)](https://minecraft.net)
[![Architecture](https://img.shields.io/badge/Architecture-Native%20C%2B%2B-orange.svg)](https://github.com/spygamingog/Animated-Mojang-Android)
[![Package](https://img.shields.io/badge/Package-animated__mojang.levipack-blueviolet.svg)](https://github.com/spygamingog/Animated-Mojang-Android)
[![Version](https://img.shields.io/badge/Version-1.0.1-purple.svg)](https://github.com/spygamingog/Animated-Mojang-Android)

A high-performance native C++ client mod for **Minecraft Bedrock Edition (Android ARM64)** that renders a real-time 60 FPS 3D cave background, hardware-accelerated animated Minecraft title sequence, and seamless sub-menu camera transitions.

---

## Authors & Attribution

- **Original Windows x64 Mod**: Authored by **[MRUIAW](https://github.com/MRUIAW)** ([MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang)), licensed under LGPL-3.0.
- **Original Java Edition Mod**: Concept, 3D cave structure, and title assets by **[chara2015](https://github.com/chara2015)** ([chara2015/animated_mojang](https://github.com/chara2015/animated_mojang)), licensed under CC0 1.0.
- **Android ARM64 Port**: Reverse-engineered, ported, and maintained by **[SpyGamingOG](https://github.com/spygamingog)**.

---

## Visual Showcase

![Animated Mojang Main Menu Showcase](icon.png)

---

## Compatibility

| Component | Specification |
|---|---|
| **Minecraft Bedrock** | `1.26.44.3`, `1.26.45.1`, `1.26.51.1`, `1.26.52.3`, and compatible 1.26.x minor releases |
| **Platform** | Android ARM64 (`aarch64-linux-android`) |
| **Hook Engine** | Dynamic AArch64 Pattern Signatures + Dynamic ELF GOT Relocations |
| **Dependencies** | None (Self-contained Native C++ library, zero external loader runtime requirements) |
| **Resource Pack** | None (Zero RP, direct functional memory suppression) |
| **Supported Loaders**| LeviLauncher (LeviLaunchroid), PreLoader, Zygisk, or manual injection |
| **Package Format** | LeviPack (`animated_mojang.levipack`) |

---

## Key Features

- **Embedded OpenGL ES 3.0 Cave Renderer**: Renders the authentic `normal_cave.mesh` (98,094 vertices) at 60 FPS+ with baked lighting, animated lava UVs, and soul torches via dynamic GOT hooking on `glClear`. Fully compatible with offscreen render target FBOs on flagship Adreno 740/750 GPUs.
- **Hardware-Accelerated Title Animation & Progressive Streaming**: Direct 69-frame Minecraft title sequence rendered on the GPU render thread without JSON UI distortion. Texture strips are progressively streamed and uploaded during the initial 4.0s camera flight (1 strip every 350ms), eliminating frame stutters during the animation sequence.
- **Dynamic VRAM Eviction (~460 MB Reclaimed)**: Once the title sequence settles at frame 68, unused strips 0 through 7 are immediately purged from GPU VRAM, preventing out-of-memory crashes on low-to-mid-range Android devices while retaining the static resting title.
- **Start Screen & Sub-Menu Isolation**: Title visibility is strictly isolated to the main start screen (`currentPage == Main`). Internal transitional routes (`/pre-start`) are filtered out, and modal dialogues (`sModalDialogueActive`) suppress title rendering—guaranteeing that navigating to Settings, Play, or opening 'Leave Game' confirmation popups immediately and cleanly hides the title without bleed or occlusion.
- **High-Refresh Gaming Display Support**: Frame limiter tuned for fluid 120 Hz, 144 Hz, and 165 Hz refresh rates with sub-millisecond render thresholds.
- **Cinematic Timeline & Button Fade-In**:
  - `0.0s – 4.0s`: 3D camera spline flight through the cave with menu buttons hidden; background preloader streams title texture strips into VRAM.
  - `4.0s – 6.0s`: Camera settles at the title vista and plays the animated title sequence at a silky 60/120 FPS.
  - `6.0s – 7.5s`: Start screen buttons smoothly fade into view over 1.5 seconds.
  - `7.5s+`: Full interactive idle menu with zero GPU overlay overhead.
- **Sub-Menu Camera Navigation & Smooth Title Fade**: Smooth camera glides to dedicated viewpoints for Worlds, Settings, Realms, Dressing Room, and Marketplace screens, smoothly fading out the title during transitions and returning smoothly on exit.
- **Zero Resource Pack Requirement**: Suppresses vanilla panorama and Ancient City cubemap directly in memory.
- **Zero In-World Overhead**: Cave rendering automatically pauses during gameplay to preserve vanilla skies.
- **EGL Context Loss Recovery**: Automatically reinitializes shaders, buffers, and textures on Android app switch or context reload without dead handle crashes.

---

## Technical Hook Topology & Offsets

All hooks are resolved dynamically at startup via **AArch64 pattern signatures** (`hookSignature`) across all readable segments of `libminecraftpe.so`, dynamic ELF GOT parsing (`hookGotSymbol`) for graphics symbols, and audited golden RVA fallbacks:

| Target Function | Signature / Method | 1.26.52.3 | 1.26.51.1 | 1.26.45.1 | 1.26.44.3 | Type | Purpose |
|---|---|:---:|:---:|:---:|:---:|:---:|---|
| `ScreenTechStackSelector::getTechStackForScreen` | `FD 7B BD A9 F5 0B 00 F9 ...` | `0x0974E844` | `0x0974E7A4` | `0x0938B714` | `0x0938B7B4` | Signature | Enforces `TechStack::JsonUI` (0) on menu screens |
| `StartMenuScreenController::$onOpen` | `FF ?? ?? D1 FD 7B ?? A9 ...` | `0x0A2DF314` | `0x0A2DF2A4` | `0x09EBD95C` | `0x09EBD9FC` | Signature | Resets camera to Main vista upon return |
| `StartMenuScreenController::tick` | `FD 7B BA A9 FC 6F 01 A9 ...` | `0x0A2E1FF4` | `0x0A2E1F84` | `0x09EC05D8` | `0x09EC0678` | Signature | Tracks menu state and intro progression |
| `StartMenu::_onPlayButton` | `FF 83 02 D1 FD 7B 07 A9 ...` | `0x0A31344C` | `0x0A3133DC` | `0x09EF3D64` | `0x09EF3E04` | Signature | Glides camera to Worlds vista (`CavePage::Worlds`) |
| `StartMenu::_onSettingsButton` | `FF 43 02 D1 FD 7B 06 A9 ...` | `0x0A3171AC` | `0x0A31713C` | `0x09EF72C0` | `0x09EF7360` | Signature | Glides camera to Settings vista (`CavePage::Settings`) |
| `StartMenu::_onRealmsButton` | `FF 43 02 D1 FD 7B 06 A9 ...` | `0x0A3174DC` | `0x0A31746C` | `0x09EF75F0` | `0x09EF7690` | Signature | Glides camera to Realms vista (`CavePage::Realms`) |
| `StartMenu::_onDressingRoomButton` | `FF 43 02 D1 FD 7B 06 A9 ...` | `0x0A31815C` | `0x0A3180EC` | `0x09EF8270` | `0x09EF8310` | Signature | Glides camera to Profile vista (`CavePage::DressingRoom`) |
| `StartMenu::_onSkinsMenuButton` | `FF 83 03 D1 FD 7B 0B A9 ...` | `0x0A317C1C` | `0x0A317BAC` | `0x09EF7D30` | `0x09EF7DD0` | Signature | Glides camera to Skins vista |
| `StartMenu::_onMarketplaceButton` | `FF 83 03 D1 FD 7B 0B A9 ...` | `0x0A3187A4` | `0x0A318734` | `0x09EF88A4` | `0x09EF8944` | Signature | Glides camera to Marketplace vista (`CavePage::Marketplace`) |
| `StartMenu::_onAchievementsButton` | `FF 43 02 D1 FD 7B 06 A9 ...` | `0x0A316978` | `0x0A316908` | `0x09EF69E4` | `0x09EF6A84` | Signature | Camera feedback for achievements button |
| `StartMenu::_onFeedbackButton` | Dual Signature Pattern | `0x0A314D58` | `0x0A314CE8` | `0x09EF52FC` | `0x09EF539C` | Signature | Camera feedback for feedback button |
| `StartMenu::_onSignInButton` | `FF 43 02 D1 FD 7B 06 A9 ...` | `0x0A31972C` | `0x0A3196BC` | `0x09EF9888` | `0x09EF9928` | Signature | Camera feedback for sign-in button |
| `Menu_Exit` | `FD 7B BE A9 F3 0B 00 F9 ...` | `0x0A567540` | `0x0A5674D0` | `0x0A15248C` | `0x0A15252C` | Signature | Resets camera to Main vista when closing sub-menus |
| `ProgressBarRenderer::$render` | `FF 03 05 D1 EF 3B 0A 6D ...` | `0x09685F70` | `0x09685ED0` | `0x092BEC2C` | `0x092BECCC` | Signature | Restyles progress bar to Crimson theme |
| `InGamePlayScreen::render` | `FF C3 01 D1 FD 7B 03 A9 ...` | `0x09ACC828` | `0x09ACC7A8` | `0x0974066C` | `0x0974070C` | Signature | Detects in-world rendering (disables cave in-game) |
| `ClientInstance::requestLeaveGame` | `FF 43 04 D1 FD 7B 0C A9 ...` | `0x09800FF8` | `0x09800F58` | `0x0943C430` | `0x0943C4D0` | Signature | Detects world exit, restoring main menu state |
| `PanoramaRenderer::$render` | `ED 33 B7 6D EB 2B 01 6D ...` | `0x0967FA98` | `0x0967F9F8` | `0x092B8DC0` | `0x092B8E60` | Signature | Suppresses vanilla 2D panorama |
| `CubemapBackgroundScreen::$render` | `FF 03 02 D1 FD 7B 04 A9 ...` | `0x09B7C9E0` | `0x09B7C960` | `0x097C99A4` | `0x097C9A44` | Signature | Suppresses Ancient City cubemap |
| `DirectoryPackAccessStrategy::getAsset` | `FD 7B BA A9 FC 0B 00 F9 ...` | `0x111EEE94` | `0x111EEA54` | `0x1042D360` | `0x1042D8C0` | Signature | Dynamic asset interception (zero-RP textures & JsonUI) |
| `ZipPackAccessStrategy::getAsset` | `FD 7B BA A9 FC 6F 01 A9 ...` | `0x11158670` | `0x11158230` | `0x1040D528` | `0x1040DA88` | Signature | Dynamic asset interception (zero-RP textures & JsonUI) |
| `glClear` (`libGLESv2.so`) | Dynamic GOT (`DT_JMPREL`) | `0x132893D8` | `0x13289078` | `0x124C9688` | `0x124C9D28` | Dynamic GOT | 3D cave GPU injection & background capture |
| `glColorMask` (`libGLESv2.so`) | Dynamic GOT (`DT_JMPREL`) | `0x13289288` | `0x13288F28` | `0x124C9538` | `0x124C9BD8` | Dynamic GOT | Hardware color mask suppression during flight |
| `eglSwapBuffers` (`libEGL.so`) | Dynamic GOT (`DT_JMPREL`) | `0x132C7DC0` | `0x132C7A60` | `0x12507F20` | `0x125085C0` | Dynamic GOT | Smoothstep button fade-in overlay pass |

---

## Installation

### LeviLauncher (LeviLaunchroid)
1. Obtain **`animated_mojang.levipack`**.
2. Open **LeviLauncher** -> **Mods** -> **Import** -> Select `animated_mojang.levipack`.
3. Verify the mod is enabled and launch Minecraft.

---

## Configuration

Settings are saved in `mods/AnimatedMojang/config/config.json`:

| Key | Default | Description |
|---|:---:|---|
| `enabled` | `true` | Master mod toggle |
| `preferJsonMainMenu` | `false` | Use JSON UI start screen (false renders title directly via GPU) |
| `minecraftTitleAnimation` | `true` | Play the animated Minecraft title sequence |
| `caveBackground` | `true` | Render the real-time 3D OpenGL ES cave background |
| `restyleLoadingProgress` | `true` | Restyle loading bar to Crimson theme |
| `replayOnReturnToMenu` | `true` | Replay intro cinematic when returning from a world |
| `maxPresentationSeconds` | `15.0` | Intro camera flight timeout |
| `blockMenuInputDuringIntro` | `true` | Block button clicks until flight and fade-in complete |

---

## Building from Source

```bash
# Requires Android NDK r25+ (or Termux Clang) and CMake 3.22+
./package_mod.sh
```

Generates `dist/animated_mojang.levipack`.

---

## License

- Source code: **LGPL-3.0** ([MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang)).
- 3D cave model & title assets: **CC0 1.0 Universal** ([chara2015/animated_mojang](https://github.com/chara2015/animated_mojang)).
