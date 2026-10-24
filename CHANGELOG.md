# Changelog

All notable changes to **Animated Mojang (Android ARM64)** are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and follows [Semantic Versioning](https://semver.org/).

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
