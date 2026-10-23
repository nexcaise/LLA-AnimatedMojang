# Attribution & Notices (素材与开源说明)

## English
1. **Original Java Mod**: The concept, 3D cave structure, and Minecraft animated title assets originate from [chara2015/animated_mojang](https://github.com/chara2015/animated_mojang) (commit `ef68ffb6d6cdbdfab4db960497719416e8b7a6bc`), licensed under **CC0 1.0 Universal**.
2. **Bedrock Windows x64 Port**: Reverse engineering, Bedrock camera flight path equations, and Windows x64 LeviLamina client integration were implemented by [MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang), licensed under **LGPL-3.0**.
3. **Bedrock Android ARM64 Standalone Port**:
   - **Author**: **SpyGamingOG** (`spygamingog`).
   - Native C++ ARM64 standalone hook reverse-engineering for Bedrock 1.26.52.3 / 1.26.45.1.
   - Embedded OpenGL ES 3.0 Cave GPU pipeline (`CaveGlesRenderer`) hooking into `glClear` via GOT relocation (`0x132893d8`).
   - Pure functional background suppression without external resource packs (`AnimatedMojang_RP`).
   - Pure standalone execution without external mod loader libraries (LeviLamroid).
4. **Trademarks & Copyrights**: Minecraft, Mojang, and related trademarks are properties of Mojang AB / Microsoft Corporation. This project is an unofficial fan client mod and is not affiliated with or endorsed by Mojang or Microsoft.

---

## 简体中文
1. **原版 Java 模组**：Minecraft 标题动画及 3D 洞穴素材来自 [chara2015/animated_mojang](https://github.com/chara2015/animated_mojang)（固定提交 `ef68ffb6d6cdbdfab4db960497719416e8b7a6bc`），采用 **CC0 1.0 Universal**。
2. **基岩版 Windows x64 原作**：基岩版客户端运镜算法、数学建模与 LeviLamina 基础流程由 [MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang) 实现，采用 **LGPL-3.0** 开源。
3. **基岩版 Android ARM64 独立移植版**：
   - **移植作者**：**SpyGamingOG** (`spygamingog`)。
   - 针对基岩版 1.26.52.3 / 1.26.45.1 的 Android ARM64 原生 C++ 独立跳板与 GOT 挂钩逆向适配。
   - 嵌入式 OpenGL ES 3.0 3D 洞穴 GPU 渲染管线（通过 GOT 表挂钩 `glClear` RVA `0x132893d8` 实现 60 帧流畅渲染）。
   - 纯函数级背景抑制，完全免去资源包（`AnimatedMojang_RP`）。
   - 纯独立原生运行，完全免去外部加载器依赖（LeviLamroid）。
4. **商标声明**：Minecraft 及相关名称、标识属于 Mojang AB 及微软公司。本项目为非官方客户端模组。
