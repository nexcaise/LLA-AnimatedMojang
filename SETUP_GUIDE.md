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

- **0.0s – 4.0s (Camera Flight)**: 3D camera spline flight through the cave to the title viewpoint (`cameraFlightMs = 4000.0`). Start buttons and title remain hidden.
- **4.0s – 6.0s (Title Sequence)**: Camera holds at the title viewpoint while the hardware-accelerated animated Minecraft title plays across 69 frames (`titleDelayMs = 4000.0`).
- **6.0s – 7.5s (Button Fade-In)**: Start screen buttons appear and smoothly fade in over 1.5 seconds (`menuFadeMs = 1500.0`) via clean GPU background frame capture and smoothstep overlay blending.
- **7.5s+ (Interactive Idle)**: Full interactive idle state with zero GPU overlay overhead.
- **Sub-Menu Navigation**: Clicking Play, Settings, Realms, Dressing Room, or Marketplace triggers a smooth camera glide to dedicated viewpoints. Exiting any sub-menu smoothly glides back to Main.

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
4. **EGL Context Recovery**: Automatically detects Android EGL context invalidation on app switch or minimize, rebuilding shaders, VBOs, and textures seamlessly.
