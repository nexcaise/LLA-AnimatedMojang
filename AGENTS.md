# AGENTS.md — Technical Reference for Animated Mojang (Android ARM64)

> **Audience**: AI Coding Agents and Native C++ Reverse Engineers working on Minecraft Bedrock Edition (Android ARM64).
> **Maintainer**: **SpyGamingOG** (`spygamingog`)

---

## 1. System Architecture

`AnimatedMojang` is a native C++ client mod for **Minecraft Bedrock Edition (Android ARM64)** that renders a 60 FPS 3D cave background, animated title sequence, and smooth sub-menu camera navigation with zero resource pack dependencies.

### Core Components
1. **Multi-Segment Pattern Scanner & W^X Inline Hooking (`src/runtime/Hook.cpp`)**:
   - Enumerates all readable/executable memory mappings from `/proc/self/maps`.
   - Uses `std::memchr`-accelerated byte scanning with IDA-style wildcard patterns (`??`).
   - Automatically supports `1.26.44.3`, `1.26.45.1`, `1.26.51.1`, and `1.26.52.3`.
   - Employs anonymous `mmap` trampolines and strict `PROT_READ|PROT_WRITE` -> `PROT_READ|PROT_EXEC` transitions for W^X Android kernel compliance.
2. **Dynamic ELF GOT Resolver (`src/runtime/Hook.cpp`)**:
   - Parses `PT_DYNAMIC`, `DT_JMPREL` (`.rela.plt`), `DT_SYMTAB`, and `DT_STRTAB` via `dl_iterate_phdr`.
   - Discovers GOT slots for `glClear`, `glColorMask`, and `eglSwapBuffers` at runtime without hardcoded RVAs.
3. **OpenGL ES 3.0 Cave Pipeline (`src/scene/CaveGlesRenderer.cpp`)**:
   - Renders `normal_cave.mesh` (98,094 vertices) on the game's GPU thread during `glClear`.
   - Preserves and restores all OpenGL state (FBO binding, viewport, VAO, VBO, program, textures, blending, depth mask, color mask).
   - Compatible with offscreen render target FBOs on flagship Snapdragon 8 Gen 2/3 (Adreno 740/750).
   - Progressively preloads 1 title texture strip every 350ms across the 4.0s intro flight (`preloadTitleStrip`), eliminating animation stutter.
   - Automatically purges intermediate texture strips 0 through 7 once settling at frame 68, reclaiming ~460 MB of VRAM.
   - Pre-initializes fade shaders and VBOs during startup to eliminate 6.0s GPU allocation hitch.
   - Detects Android EGL context loss and reinitializes shaders, buffers, and textures automatically without invalid handle deletion.
4. **Title Scene Pass & Sub-Screen Isolation (`src/client/Client.cpp`)**:
   - Rendered directly in `glClear` immediately after the 3D cave background and before Bedrock's UI pass.
   - Title rendering is strictly isolated to `sIsStartScreen && currentPage == scene::CavePage::Main && !sModalDialogueActive`.
   - Filters out Bedrock's internal `/pre-start` transitional route, preventing unwanted snaps to Main and title bleed on Settings/Play screens.
   - Screen controller state (`sIsStartScreen` and `sModalDialogueActive`) completely suppresses title rendering during dialogues, popups, or sub-screens.
   - Camera transition opacity (`CaveCameraFrame::titleAlpha`) provides smooth cosine easing when navigating between screens.
5. **Lifecycle & In-World Safety (`src/client/Client.cpp`)**:
   - `InGamePlayScreen::render` sets `sIsInWorld = true` (completely disabling cave rendering during gameplay).
   - `ClientInstance::requestLeaveGame` resets `sIsInWorld = false`, safely re-enabling main menu rendering and resetting the progressive preloader.
6. **High-Refresh Display Frame Pacing (`src/client/Client.cpp`)**:
   - Sub-millisecond render threshold delivers fluid 120Hz, 144Hz, and 165Hz playback.
7. **Timeline & Smooth Fade-In (`src/animation/Timeline.h`)**:
   - `0.0s – 4.0s`: Camera flight; background preloader streams title texture strips into VRAM; buttons and title suppressed.
   - `4.0s – 6.0s`: 69-frame Minecraft title sequence played at resting title vista with zero frame drops.
   - `6.0s – 7.5s`: Intermediate strips purged; buttons smoothly fade in over 1.5s via smoothstep overlay blending.
   - `7.5s+`: Interactive idle with zero overlay pass overhead.

---

### Performance architecture (v1.0.2)
- `scene/CaveGlesRenderer`: GLES 3.00, render-thread only; background loading via `std::async` (`loadAll`, title pages).
- `scene/CaveMeshBuilder.h`: CPU mesh prep (dedup, 24-byte vertices, opaque/cutout split). GL-free, unit-testable.
- `scene/TitleAtlas.h` + `tools/bake_title_atlas.py`: baked, premultiplied title atlas (`title/baked/`). **Re-run the
  baker whenever the strips in `title/minecraft_animation/` change.** The strips are not shipped in the package.
- Never decode images or call `glDelete*` from hook threads; use `requestGlRelease()` / `requestTitleReplay()`.

---

## 2. Multi-Version Hook Reference Table

| Target Symbol | 1.26.52.3 | 1.26.51.1 | 1.26.45.1 | 1.26.44.3 | Signature / Resolution |
|---|:---:|:---:|:---:|:---:|---|
| `ScreenTechStackSelector::getTechStackForScreen` | `0x0974E844` | `0x0974E7A4` | `0x0938B714` | `0x0938B7B4` | `FD 7B BD A9 F5 0B 00 F9 F4 4F 02 A9 FD 03 00 91 F4 03 01 AA F5 03 00 AA ?? ?? ?? 94 ?? ?? ?? B4 F3 03 00 AA 00 1C 40 F9` |
| `StartMenuScreenController::$onOpen` | `0x0A2DF314` | `0x0A2DF2A4` | `0x09EBD95C` | `0x09EBD9FC` | `FF ?? ?? D1 FD 7B ?? A9 FC 6F ?? A9 FA 67 ?? A9 F8 5F ?? A9 F6 57 ?? A9 F4 4F ?? A9 FD ?? 04 91 ?? D0 3B D5` |
| `StartMenuScreenController::tick` | `0x0A2E1FF4` | `0x0A2E1F84` | `0x09EC05D8` | `0x09EC0678` | `FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF 83 15 D1 ?? D0 3B D5 F4 03 00 AA` |
| `StartMenu::_onPlayButton` | `0x0A31344C` | `0x0A3133DC` | `0x09EF3D64` | `0x09EF3E04` | `FF 83 02 D1 FD 7B 07 A9 F6 57 08 A9 F4 4F 09 A9 FD C3 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 04 80 52` |
| `StartMenu::_onSettingsButton` | `0x0A3171AC` | `0x0A31713C` | `0x09EF72C0` | `0x09EF7360` | `FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 82 04 80 52` |
| `StartMenu::_onRealmsButton` | `0x0A3174DC` | `0x0A31746C` | `0x09EF75F0` | `0x09EF7690` | `FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 C8 16 40 F9 A8 83 1F F8 13 04 40 F9` |
| `StartMenu::_onDressingRoomButton` | `0x0A31815C` | `0x0A3180EC` | `0x09EF8270` | `0x09EF8310` | `FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 05 80 52` |
| `StartMenu::_onSkinsMenuButton` | `0x0A317C1C` | `0x0A317BAC` | `0x09EF7D30` | `0x09EF7DD0` | `FF 83 03 D1 FD 7B 0B A9 F6 57 0C A9 F4 4F 0D A9 FD C3 02 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 A2 04 80 52` |
| `StartMenu::_onMarketplaceButton` | `0x0A3187A4` | `0x0A318734` | `0x09EF88A4` | `0x09EF8944` | `FF 83 03 D1 FD 7B 0B A9 F6 57 0C A9 F4 4F 0D A9 FD C3 02 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 E2 04 80 52` |
| `StartMenu::_onAchievementsButton` | `0x0A316978` | `0x0A316908` | `0x09EF69E4` | `0x09EF6A84` | `FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 56 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? C8 16 40 F9 02 05 80 52` |
| `StartMenu::_onFeedbackButton` | `0x0A314D58` | `0x0A314CE8` | `0x09EF52FC` | `0x09EF539C` | Dual Signature Pattern (`kSigFeedbackButton` / `kSigFeedbackButtonLegacy`) |
| `StartMenu::_onSignInButton` | `0x0A31972C` | `0x0A3196BC` | `0x09EF9888` | `0x09EF9928` | `FF 43 02 D1 FD 7B 06 A9 F6 57 07 A9 F4 4F 08 A9 FD 83 01 91 55 D0 3B D5 ?? ?? ?? ?? ?? ?? ?? ?? A8 16 40 F9 42 04 80 52` |
| `Menu_Exit` | `0x0A567540` | `0x0A5674D0` | `0x0A15248C` | `0x0A15252C` | `FD 7B BE A9 F3 0B 00 F9 FD 03 00 91 09 04 40 F9 F3 03 08 AA 20 6D 44 F9` |
| `ProgressBarRenderer::$render` | `0x09685F70` | `0x09685ED0` | `0x092BEC2C` | `0x092BECCC` | `FF 03 05 D1 EF 3B 0A 6D ED 33 0B 6D EB 2B 0C 6D E9 23 0D 6D FD 7B 0E A9 FC 6F 0F A9 ...` |
| `InGamePlayScreen::render` | `0x09ACC828` | `0x09ACC7A8` | `0x0974066C` | `0x0974070C` | `FF C3 01 D1 FD 7B 03 A9 F7 23 00 F9 F6 57 05 A9 F4 4F 06 A9 FD C3 00 91 57 D0 3B D5 ...` |
| `ClientInstance::requestLeaveGame` | `0x09800FF8` | `0x09800F58` | `0x0943C430` | `0x0943C4D0` | `FF 43 04 D1 FD 7B 0C A9 FC 67 0D A9 F8 5F 0E A9 F6 57 0F A9 F4 4F 10 A9 FD 03 03 91 58 D0 3B D5 ...` |
| `PanoramaRenderer::$render` | `0x0967FA98` | `0x0967F9F8` | `0x092B8DC0` | `0x092B8E60` | `ED 33 B7 6D EB 2B 01 6D E9 23 02 6D FD 7B 03 A9 FC 23 00 F9 FA 67 05 A9 F8 5F 06 A9 ...` |
| `CubemapBackgroundScreen::$render` | `0x09B7C9E0` | `0x09B7C960` | `0x097C99A4` | `0x097C9A44` | `FF 03 02 D1 FD 7B 04 A9 F7 2B 00 F9 F6 57 06 A9 F4 4F 07 A9 FD 03 01 91 56 D0 3B D5 ...` |
| `DirectoryPackAccessStrategy::getAsset` | `0x111EEE94` | `0x111EEA54` | `0x1042D360` | `0x1042D8C0` | `FD 7B BA A9 FC 0B 00 F9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF C3 31 D1 ...` |
| `ZipPackAccessStrategy::getAsset` | `0x11158670` | `0x11158230` | `0x1040D528` | `0x1040DA88` | `FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 FD 03 00 91 FF C3 33 D1 ...` |
| `glClear` (`libGLESv2.so`) | `0x132893D8` | `0x13289078` | `0x124C9688` | `0x124C9D28` | Dynamic ELF GOT Resolution (`DT_JMPREL`) |
| `glColorMask` (`libGLESv2.so`) | `0x13289288` | `0x13288F28` | `0x124C9538` | `0x124C9BD8` | Dynamic ELF GOT Resolution (`DT_JMPREL`) |
| `eglSwapBuffers` (`libEGL.so`) | `0x132C7DC0` | `0x132C7A60` | `0x12507F20` | `0x125085C0` | Dynamic ELF GOT Resolution (`DT_JMPREL`) |

---

## 3. Sub-Menu Camera Angles & Poses

Defined in `src/scene/CaveCamera.h` (all angles in degrees, zero roll):

```cpp
// Main Menu Idle
constexpr CavePose caveMenuIdle{31.0f, 13.0f, 6.0f, 21.0f, -13.0f, 0.0f};

// Sub-Menu Poses
constexpr CavePose caveWorldsPose{24.0f, 15.0f, 18.0f, -24.0f, -6.0f, 0.0f};
constexpr CavePose caveSettingsPose{26.0f, 14.0f, 20.0f, -10.0f, -8.0f, 0.0f};
constexpr CavePose caveRealmsPose{22.0f, 14.0f, 24.0f, 15.0f, -5.0f, 0.0f};
constexpr CavePose caveMarketplacePose{18.0f, 13.0f, 18.0f, 79.0f, 4.0f, 0.0f};
constexpr CavePose caveDressingRoomPose{20.0f, 13.0f, 37.0f, 42.0f, -4.0f, 0.0f};
```

---

## 4. Build & Package Commands

```bash
# Build libAnimatedMojang.so and generate dist/animated_mojang.levipack:
./package_mod.sh
```
