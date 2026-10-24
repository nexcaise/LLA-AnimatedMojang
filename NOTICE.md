# Attribution & Notices

## English
1. **Original Java Mod**: Concept, 3D cave mesh, and Minecraft animated title assets originate from [chara2015/animated_mojang](https://github.com/chara2015/animated_mojang), licensed under **CC0 1.0 Universal**.
2. **Bedrock Windows x64 Port**: Reverse engineering, camera flight equations, and Windows x64 client integration by [MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang), licensed under **LGPL-3.0**.
3. **Bedrock Android ARM64 Port**:
   - **Author**: **SpyGamingOG** (`spygamingog`).
   - Native C++ ARM64 reverse-engineering and hooks for Bedrock `1.26.44.3` – `1.26.52.3+`.
   - Embedded OpenGL ES 3.0 cave rendering pipeline via dynamic GOT relocation on `glClear` with offscreen FBO support.
   - Hardware-accelerated title overlay pass on `eglSwapBuffers` compatible with third-party client mods and ImGui.
   - Zero-resource-pack functional background suppression and dynamic asset interception.
   - Self-contained native execution with zero companion loader runtime dependencies.
4. **Trademarks & Copyrights**: Minecraft, Mojang, and related trademarks are properties of Mojang AB / Microsoft Corporation. This project is an unofficial fan client mod and is not affiliated with or endorsed by Mojang or Microsoft.
