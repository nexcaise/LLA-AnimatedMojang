# Changelog

All notable changes to the Android ARM64 port of **Animated Mojang** are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and release versions follow [Semantic Versioning](https://semver.org/).

## [1.0.2] - 2026-10-16

### Added
- **Multi-Version Bedrock Support**: Native verified support across Minecraft Bedrock **1.26.44.3**, **1.26.45.1**, **1.26.51.1**, and **1.26.52.3** via LeviLamroid `v1.0.1-beta`.
- **Dynamic Profile & Resource Discovery**: Build automation and runtime configuration discovery now dynamically enumerate all active LeviLauncher profiles under `/sdcard/Android/media/org.levimc.launcher/minecraft/*/`, removing all hardcoded profile paths.

### Fixed & Improved
- **Loading Screen Cave Initialization Timing**: Prevented the 3D cave background and camera flight from triggering while Minecraft is still loading resources on the progress bar screen. The camera flight now starts cleanly at the exact moment the main menu appears, ensuring the full 4.0s cinematic entrance flight is experienced without premature triggering.
- **Sub-Menu Camera Transitions**: Fixed frame stutter and lag across JSON UI menus (Play, Settings, Realms) by eliminating duplicate GPU cave passes during off-screen UI sub-clears. Transition duration extended to a silky-smooth 900 ms cubic ease curve across all 5 sub-menu options (pairing with ForceCloseOreUI is recommended to remove opaque web overlays on Dressing Room and Marketplace for the best visual experience).
- **Zero-Roll Cinematic Camera Vistas**: Refined camera orientations for all sub-menus (Play, Settings, Realms, Dressing Room, Marketplace), eliminating sideways roll tilt and framing wide, illuminated cavern vistas.
- **Return-to-Main Camera Restoration**: Closing or pressing Back on any sub-menu screen now smoothly glides the camera back to the main menu resting position (`caveTitle`) over 900 ms via synchronized `StartMenuScreenController::tick` return detection.
- **Camera Flight Transition Smoothing**: Fixed a visual stutter where the 3D cave entrance flight snapped to the resting position in mid-motion, ensuring the camera completes its full 4.0s cinematic flight and decelerates smoothly.

---

## [1.0.1] - 2026-10-15


### Fixed & Improved
- **Zero Hardcoded Hooks Architecture**: Completely purged all 18 hardcoded RVAs and memory hooks from the client mod. The mod now connects purely to LeviLamroid's high-level human APIs (`ll::ui::forceJsonUIForScreen`, `ll::ui::onStartMenuOpen`, `ll::ui::onPlayButtonClicked`, `ll::render::suppressVanillaBackground`, `ll::render::registerPostClearCallback`, `ll::isInWorld()`).
- **Launcher Dynamic Linkage Fix**: Client headers now resolve loader symbols dynamically at runtime via `dlsym`, eliminating the `cannot locate symbol "ll_ui_force_json_ui"` load failure under LeviLauncher when mods are loaded with `RTLD_LOCAL`.
- **In-World Skybox Restoration**: Fixed 3D cave background rendering inside gameplay worlds. The mod now queries `ll::isMainMenu() && !ll::isInWorld()`, ensuring the cave background is active strictly on main menu screens and automatically disabling the post-clear pass to restore 100% vanilla skybox and RenderDragon environment rendering when in a world.
- **Resource Pack Reload Crash Fix**: Eliminated the SIGSEGV crash when closing the Global Resources screen or reloading texture packs by removing unsafe raw memory writes.
- **Dynamic Menu Context Switching**: Returning to the title screen from in-game or sub-menus automatically re-engages the 3D cave background and camera motion smoothly.

---

## [1.0.0] - 2026-10-15

### Initial Android ARM64 Release (Ported by SpyGamingOG)
- **First Public Android Release**: Ported the Windows x64 LeviLamina mod by **MRUIAW** to Android ARM64 (`aarch64-linux-android`) on **LeviLauncher (LeviLaunchroid)**.
- **Powered by LeviLamroid**: Built on top of **[LeviLamroid](https://github.com/spygamingog/LeviLamroid)** (`>= v1.0.0-beta`) for centralized hook management, runtime version detection, and lifecycle dispatch.
- **Embedded OpenGL ES 3.0 Real-Time 3D Cave Engine**:
  - Native vertex streaming pipeline (`CaveGlesRenderer.cpp`) hooked into the GPU render thread via GOT relocation on `glClear` (RVA `0x132893d8`).
  - Renders 98,094-vertex `normal_cave.mesh` with baked colored lighting, animated scrolling lava, soul torches, and flame/smoke particles at steady 60 FPS.
  - Full hardware depth testing (`GL_LEQUAL`), blending (`GL_SRC_ALPHA`, `GL_ONE_MINUS_SRC_ALPHA`), and automatic state preservation.
- **Hardware-Accelerated 69-Frame Minecraft Title Animation**:
  - Global resource pack (`AnimatedMojang_RP.mcpack`) featuring 4096×1440 Aseprite atlas and metadata.
  - Plays the iconic exploding stone and soul particle animation at 25 FPS, freezing smoothly on frame 68.
  - Zero-push layout architecture: parent panel `title_panel_pocket` stays at vanilla `[ "55%", "18.85%x" ]` while child `title_image` scales to `[ "171.8%", "156.7%" ]` with `-30%` vertical offset, displaying the logo at 100% vanilla size without pushing any main menu buttons down.
- **Cinematic Sub-Menu Camera Navigation (0-Lag Transitions)**:
  - Custom camera angles for all main menus:
    - **Main Start Screen**: `(31.0, 13.0, 6.0, yaw=21.0, pitch=-13.0)`
    - **Worlds (Play) Screen**: `(28.0, 12.0, 14.0, yaw=15.0, pitch=-8.0)`
    - **Settings Screen**: `(24.0, 14.0, 25.0, yaw=30.0, pitch=-5.0)`
    - **Realms Screen**: `(33.0, 11.0, 10.0, yaw=10.0, pitch=-15.0)`
    - **Marketplace Screen**: `(20.0, 15.0, 35.0, yaw=45.0, pitch=-2.0)`
    - **Dressing Room / Profile Screen**: `(35.0, 12.0, 5.0, yaw=5.0, pitch=-20.0)`
  - Instantaneous return to Main Menu camera on menu close/exit via hook RVA `0x0A567540`.
- **Multi-Profile Automated Deployment**:
  - Automated packaging into `.levipack`, `.mcpack`, and standalone distribution zip `AnimatedMojang_Suite_Android.zip`.
  - Auto-deployment to active LeviLauncher profiles.

### Credits
- **Original Windows x64 Mod**: [MRUIAW](https://github.com/MRUIAW/Animated-Mojang)
- **Original Java Edition Mod**: [chara2015](https://github.com/chara2015/animated_mojang)
- **Android ARM64 Port**: [SpyGamingOG](https://github.com/spygamingog)
- **Mod Loader Runtime**: [LeviLamroid](https://github.com/spygamingog/LeviLamroid)
