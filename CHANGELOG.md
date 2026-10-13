# Changelog

All notable changes to the Android ARM64 port of **Animated Mojang** are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and release versions follow [Semantic Versioning](https://semver.org/).

---

## [1.0.8-mc26.52] - 2026-10-13

### Ported & Added (by SpyGamingOG)
- **First Public Android ARM64 Client Release** for Minecraft Bedrock 1.26.52.3 / 1.26.45.1 on **LeviLaunchroid** (LeviLamina Android).
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
  - Removed route pre-querying from `ScreenTechStackSelector` to eliminate camera stutter and transition thrashing.
- **Multi-Profile Automated Deployment**:
  - Automated deployment script `package_mod.sh` supporting both `com.mojang.minecraftpe` and `Minecraft_1.26.45.1` profiles.
  - Auto-injection of UUID `8e4d2a10-4820-4b21-81d3-359f4d1e00a1` into all `global_resource_packs.json` paths.
  - Setting `dev_disableRenderMainMenuCubeMap:1` in all `options.txt` paths.
  - Generates `.levipack`, `.mcpack`, and standalone distribution zip `AnimatedMojang_Suite_Android.zip`.

### Credits
- **Android ARM64 Port & Enhancements**: [SpyGamingOG](https://github.com/spygamingog)
- **Windows x64 LeviLamina Upstream**: [MRUIAW](https://github.com/MRUIAW/Animated-Mojang)
- **Original Java Edition Mod**: [chara2015](https://github.com/chara2015/animated_mojang)
