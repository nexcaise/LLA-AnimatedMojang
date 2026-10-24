# Changelog

All notable changes to **Animated Mojang (Android ARM64)** are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and follows [Semantic Versioning](https://semver.org/).

## [1.0.1] - 2026-10-24

### Fixed
- **Title Visibility on Start Screen**: Fixed a critical bug where the animated title failed to appear at 4.0s due to an expired cubemap heartbeat check (`sLastCubemapRenderMs`); removed the invalid heartbeat check, guaranteeing that `renderTitle` renders reliably on the main Start Screen (`sIsStartScreen && currentPage == Main`).
- **Progressive Title Texture Preloading (Zero Animation Lag)**: Resolved the 250–350ms frame hitches during the 2.0s title sequence by progressively loading and uploading 1 texture strip every 350ms during the initial 4.0s camera flight; this prevents simultaneous multi-hundred-megabyte decoding spikes on the main thread and guarantees all 9 texture strips are GPU-resident before frame 0 appears.
- **Dynamic VRAM Eviction (Saves ~460 MB VRAM)**: Once the title sequence settles on the resting frame (frame 68 on strip 8) at 6.0s, strips 0 through 7 are immediately evicted from GPU memory via `glDeleteTextures()`, preventing out-of-memory (OOM) crashes on low-to-mid-range Android devices.
- **Sub-Screen Isolation & Route Filter (/pre-start)**: Fixed the title lingering/remaining visible when navigating to Settings or Play screens. Filtered out Bedrock's internal `/pre-start` transitional route from resetting the page to Main, ensuring `sIsStartScreen` is set to `false` and the title immediately vanishes when navigating to Settings, Play, or other sub-screens.
- **Intro Replay Pipeline**: Synchronized replay triggers (`onOpen` and `requestLeaveGame`) with progressive preloader resets (`resetTitlePreload()`), ensuring clean texture reloading during replayed flights without memory leaks or spikes.
- **High-Refresh Display Frame Pacing**: Lowered frame throttle threshold from 8.0ms to <1.0ms, providing fluid 120Hz, 144Hz, and 165Hz rendering on gaming screens.
- **6.0s GPU Allocation Hitch**: Pre-initialized fade shaders and VBOs during startup in `ensureGlInitialized()`, eliminating the 1-frame GPU compilation stall when start screen buttons begin fading in.
- **W^X Memory Hardening**: Upgraded AArch64 inline hook trampoline allocator to use anonymous `mmap` memory with strict RW -> RX lifecycle transitions for hardened Android kernels.
- **EGL Context Loss Resilience**: Implemented `destroy(bool contextLost)` to safely bypass invalid `glDelete*` calls when Android destroys the EGL context during app switching or screen locks.

## [1.0.0] - 2026-10-24

### Added
- **Multi-Version Support**: Dynamic engine hooking across Minecraft Bedrock `1.26.44.3`, `1.26.45.1`, `1.26.51.1`, and `1.26.52.3` via AArch64 IDA pattern signatures (`hookSignature`).
- **Dynamic ELF GOT Resolution**: Automatic discovery and hooking of `glClear`, `glColorMask`, and `eglSwapBuffers` at runtime via `DT_JMPREL` relocation parsing.
- **Embedded 3D Cave GPU Engine**: Real-time 60 FPS OpenGL ES 3.0 renderer for `normal_cave.mesh` with baked lighting, animated lava UVs, and soul torches.
- **Hardware-Accelerated Title Animation**: Direct 69-frame Minecraft title sequence rendered on the GPU thread without JSON UI distortion.
- **Cinematic Timeline & Fade-In**: 4.0s camera intro flight, 2.0s title sequence, and smooth 1.5s start screen button fade-in with input protection.
- **Sub-Menu Camera Navigation**: Smooth camera glides to dedicated vistas for Worlds, Settings, Realms, Dressing Room, and Marketplace screens.
- **Zero Resource Pack Architecture**: Native memory suppression of vanilla 2D panorama and Ancient City cubemap.
- **LeviPack Distribution**: Self-contained `animated_mojang.levipack` package for one-tap import in LeviLauncher.

### Fixed
- **1.26.45.1 Launch Crash**: Added missing `InGamePlayScreen::render` and `ClientInstance::requestLeaveGame` version offsets, preventing incorrect fallback patching into startup virtual dispatch.
- **Multi-Segment Scanner**: Updated `/proc/self/maps` scanner to enumerate all readable/executable segments of `libminecraftpe.so`, fixing scan truncation beyond 134 MB.
- **Button Handler Signatures**: Wildcarded 8-byte AArch64 `ADRP + ADD` literal loads across button handlers for resilient matching across minor versions.
- **EGL Context Recovery**: Added automatic GPU shader, VBO, and texture reconstruction on Android app minimize or context loss.
- **In-World Interference**: Paused cave rendering while in gameplay via `InGamePlayScreen::render` and `ClientInstance::requestLeaveGame` hooks to preserve vanilla skies.
