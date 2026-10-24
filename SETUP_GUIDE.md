# Animated Mojang — Setup & Usage Guide

A setup and technical reference guide for **Animated Mojang (Android ARM64)**.

**Description**: `by SpyGamingOG`

---

## 1. Quick Start Installation

### Method A: LeviLauncher (Recommended)
1. Copy or download **`animated_mojang.levipack`**.
2. Open **LeviLauncher** -> **Mods** -> **Import** -> Select `animated_mojang.levipack`.
3. Enable the mod in the list and launch Minecraft.

### Method B: Manual Deployment
Extract `animated_mojang.levipack` (it is a standard ZIP archive) into your game or launcher directory:
```bash
# Target path structure:
<game_root>/mods/AnimatedMojang/
├── libAnimatedMojang.so
├── manifest.json
└── tooth.json
```

---

## 2. Supported Versions

- **Minecraft Bedrock 1.26.52.3**
- **Minecraft Bedrock 1.26.51.1**
- **Minecraft Bedrock 1.26.45.1**
- **Minecraft Bedrock 1.26.44.3**
- Future 1.26.x minor builds via universal AArch64 pattern signatures (`hookSignature`) and dynamic ELF GOT resolution.

---

## 3. Configuration Reference

The configuration file is automatically created at `mods/AnimatedMojang/config/config.json`:

| Parameter | Type | Default | Description |
|---|:---:|:---:|---|
| `enabled` | boolean | `true` | Master mod toggle. |
| `preferJsonMainMenu` | boolean | `false` | When false, renders the Minecraft title sequence directly via GPU. |
| `minecraftTitleAnimation` | boolean | `true` | Enables the 69-frame animated title sequence. |
| `caveBackground` | boolean | `true` | Enables real-time 3D OpenGL ES cave background rendering. |
| `restyleLoadingProgress` | boolean | `true` | Restyles the game loading progress bar to Crimson theme. |
| `replayOnReturnToMenu` | boolean | `true` | Replays the camera flight when returning from a world or sub-screen. |
| `maxPresentationSeconds` | float | `15.0` | Maximum timeout before automatically falling back to idle. |
| `blockMenuInputDuringIntro` | boolean | `true` | Blocks button clicks until camera flight and button fade-in complete. |

---

## 4. Cinematic Intro & Transition Timeline

- **0.0s – 4.0s (Camera Flight & Progressive Preload)**: 3D camera spline flight through the cave to the title viewpoint (`cameraFlightMs = 4000.0`). Start buttons and title remain hidden while 1 title texture strip is progressively uploaded to GPU VRAM every 350ms, guaranteeing zero hitching during playback.
- **4.0s – 6.0s (Title Sequence)**: Camera holds at the title viewpoint while the hardware-accelerated animated Minecraft title plays across 69 frames (`titleDelayMs = 4000.0`) at 60/120 FPS.
- **6.0s – 7.5s (Button Fade-In & VRAM Eviction)**: Unused animation strips 0 through 7 are purged from GPU VRAM to reclaim ~460 MB, retaining only resting strip 8. Start screen buttons appear and smoothly fade in over 1.5 seconds (`menuFadeMs = 1500.0`) via clean GPU background frame capture and smoothstep overlay blending.
- **7.5s+ (Interactive Idle)**: Full interactive idle state with zero GPU overlay overhead.
- **Sub-Menu Navigation**: Clicking Play, Settings, Realms, Dressing Room, or Marketplace triggers a smooth camera glide to dedicated viewpoints, while immediately hiding the title. Exiting any sub-menu smoothly glides back to Main and restores the title.

---

## 5. Building from Source

### Prerequisites
- Linux x86_64, Android Termux, or WSL2.
- Android NDK r25c+ (or clang 14+).
- CMake 3.22+ and Ninja.

### Build Command
```bash
./package_mod.sh
```
This builds `libAnimatedMojang.so`, bundles the assets, and outputs `dist/animated_mojang.levipack`.

---

## 6. Architecture Highlights

1. **Zero Resource Pack (Zero RP)**: Functional memory detours directly suppress RenderDragon's vanilla 2D panorama and Ancient City cubemap.
2. **Multi-Segment Memory Scanner**: Scans all readable and executable memory mappings in `/proc/self/maps` using `std::memchr`, eliminating issues with split ELF segments.
3. **Dynamic ELF GOT Resolution**: Discovers GOT slots for `glClear`, `glColorMask`, and `eglSwapBuffers` at runtime via `PT_DYNAMIC` / `DT_JMPREL` relocation parsing, avoiding hardcoded GOT RVAs.
4. **Adreno 740+ Offscreen FBO Compatibility**: Full preservation and restoration of active framebuffers (`GL_FRAMEBUFFER_BINDING`), ensuring 3D cave rendering succeeds on Snapdragon 8 Gen 2/3 devices (e.g., Red Magic 8 Pro) where RenderDragon uses offscreen render target FBOs.
5. **Progressive Title Texture Preloading & VRAM Eviction**: Staggers the upload of heavy 57.6 MB title texture strips across the 4.0s camera flight (1 strip every 350ms) to eliminate frame-drop hitches, then immediately deletes intermediate strips 0–7 once resting at frame 68, reclaiming ~460 MB of VRAM.
6. **Strict Start Screen & Sub-Menu Isolation**: Renders the animated title during `glClear` bound strictly to `sIsStartScreen && currentPage == Main && !sModalDialogueActive`. Internal transitional routes (`/pre-start`) are filtered out, guaranteeing that the title never bleeds into Settings, Play, or modal confirmation dialogues.
7. **High-Refresh Display Frame Pacing**: Sub-millisecond render threshold guarantees jitter-free 120 Hz, 144 Hz, and 165 Hz presentation.
8. **EGL Context Recovery & Memory Hardening**: Automatically detects Android EGL context invalidation on app switch or minimize, safely rebuilding shaders and VBOs without dead handle crashes, backed by W^X-compliant anonymous mmap trampolines.
