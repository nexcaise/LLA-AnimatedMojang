# Attribution & Notices

## English
1. **Original Java Mod**: The concept, 3D cave structure, and Minecraft animated title assets originate from [chara2015/animated_mojang](https://github.com/chara2015/animated_mojang) (commit `ef68ffb6d6cdbdfab4db960497719416e8b7a6bc`), licensed under **CC0 1.0 Universal**.
2. **Bedrock Windows x64 Port**: Reverse engineering, Bedrock camera flight path equations, and Windows x64 LeviLamina client integration were implemented by [MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang), licensed under **LGPL-3.0**.
3. **Bedrock Android ARM64 Port**:
   - **Author**: **SpyGamingOG** (`spygamingog`).
   - Native C++ ARM64 hook reverse-engineering for Bedrock 1.26.52.3 / 1.26.45.1.
   - Embedded OpenGL ES 3.0 Cave GPU pipeline (`CaveGlesRenderer`) hooking into `glClear` via GOT relocation (`0x132893d8`).
   - Pure functional background suppression without external resource packs (`AnimatedMojang_RP`).
   - Direct native execution without companion mod loader libraries (LeviLamroid).
4. **Trademarks & Copyrights**: Minecraft, Mojang, and related trademarks are properties of Mojang AB / Microsoft Corporation. This project is an unofficial fan client mod and is not affiliated with or endorsed by Mojang or Microsoft.
