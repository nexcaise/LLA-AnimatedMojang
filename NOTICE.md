# Attribution & Notices

## 1. Original Java Mod
The original concept, 3D cave structure, and Minecraft animated title assets originate from [chara2015/animated_mojang](https://github.com/chara2015/animated_mojang) (commit `ef68ffb6d6cdbdfab4db960497719416e8b7a6bc`), licensed under the **Creative Commons Zero v1.0 Universal (CC0 1.0)** public domain dedication.

## 2. Bedrock Windows x64 Upstream Mod
The Minecraft Bedrock Edition reverse-engineering, camera flight path mathematics, JSON UI flipbook structure, and Windows x64 client mod were originally authored by **[MRUIAW](https://github.com/MRUIAW)** in the repository [MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang), licensed under the **GNU Lesser General Public License v3.0 (LGPL-3.0)**.

## 3. Bedrock Android ARM64 Port
- **Port Author & Maintainer**: **[SpyGamingOG](https://github.com/spygamingog)** (`spygamingog`).
- **Target Platform**: Minecraft Bedrock Edition (Android ARM64) on **LeviLauncher (LeviLaunchroid)**.
- **Required Mod Loader**: **[LeviLamroid](https://github.com/spygamingog/LeviLamroid)** (`>= v1.0.0-beta`).
- **Android Architecture & Innovations**:
  - Embedded OpenGL ES 3.0 Cave GPU pipeline (`CaveGlesRenderer`) hooked into the game's GPU render thread via Global Offset Table (GOT) relocation on `glClear` (RVA `0x132893d8`).
  - Native vertex streaming pipeline for the 98,094-vertex `normal_cave.mesh` with baked lighting, animated scrolling lava UVs, and soul particle systems running at a rock-solid 60 FPS.
  - Hardware-accelerated 69-frame Aseprite flipbook title layout (`AnimatedMojang_RP.mcpack`) engineered with zero-push mathematical bounds to prevent main menu button displacement.
  - Multi-profile automated deployment supporting LeviLauncher profiles.

## 4. Trademarks & Copyrights
Minecraft, Mojang, and related names and logos are trademarks of Mojang AB and Microsoft Corporation. This project is an independent, non-commercial fan modification and is not affiliated with, authorized, maintained, sponsored, or endorsed by Mojang AB or Microsoft.
