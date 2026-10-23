# Animated Mojang（Android ARM64 独立运行版）

[![Platform](https://img.shields.io/badge/Platform-Android%20ARM64-green.svg)](https://github.com/spygamingog)
[![Engine](https://img.shields.io/badge/Engine-Bedrock%20v1.26.44.3--v1.26.52.3-blue.svg)](https://minecraft.net)
[![Architecture](https://img.shields.io/badge/Architecture-Standalone%20Native%20C%2B%2B-orange.svg)](https://github.com/spygamingog/Animated-Mojang-Android)
[![Version](https://img.shields.io/badge/Version-1.0.0-purple.svg)](https://github.com/spygamingog/Animated-Mojang-Android)
[![README English](https://img.shields.io/badge/README-English-blue)](README.md)

运行于 Android ARM64 平台的 **我的世界基岩版** 高性能独立原生 C++ 客户端模组。

> [!NOTE]
> ### 🚀 100% 独立架构（零资源包依赖 & 零外部加载器依赖）
> **Animated Mojang v1.0.0 采用纯独立原生设计：**
> - **零资源包要求**：无需安装 `.mcpack` 资源包，亦无需修改 `global_resource_packs.json`。原版全景图及远古城市天空盒的遮罩与隐藏，全部通过原生内存函数挂钩（`PanoramaRenderer::$render` 与 `CubemapBackgroundScreen::$render`）直接处理。
> - **零外部加载器依赖**：无需安装 LeviLamroid 等辅助动态库。模组内部集成 AArch64 原生内联跳板与 GOT 重定位挂钩，直接接入基岩版底层引擎。

---

## 作者与致谢

- **Windows x64 原作**：由 **[MRUIAW](https://github.com/MRUIAW)** ([MRUIAW/Animated-Mojang](https://github.com/MRUIAW/Animated-Mojang)) 开发，遵循 LGPL-3.0 协议开源。
- **Java 版原模组**：概念设计、3D 洞穴网格与动画素材由 **[chara2015](https://github.com/chara2015)** ([chara2015/animated_mojang](https://github.com/chara2015/animated_mojang)) 创作，采用 CC0 1.0 协议。
- **Android ARM64 独立移植版**：由 **[SpyGamingOG](https://github.com/spygamingog)** 逆向分析、移植与维护。

---

## 效果展示

![Animated Mojang 主界面展示](icon.png)

*通过 OpenGL ES 3.0 直接渲染的 60 帧实时 3D 洞穴背景，配合硬件加速的动态 Minecraft 标题与零延迟子菜单视角切换。*

---

## 版本兼容性

| 组件 | 支持版本 |
|---|---|
| **我的世界基岩版** | `1.26.44.3`、`1.26.45.1`、`1.26.51.1`、`1.26.52.3` 及兼容的 1.26.x 版本 |
| **运行平台** | Android ARM64 (`aarch64-linux-android`) |
| **外部依赖** | **无**（100% 独立原生 C++） |
| **资源包需求** | **无**（函数直接屏蔽，无需资源包） |
| **支持的加载方式** | LeviLauncher (LeviLaunchroid)、PreLoader、Zygisk 或原生注入 |
| **当前版本** | `1.0.0` |

---

## 核心特性

1. **实时 60 帧 3D 洞穴 GPU 渲染引擎 (`CaveGlesRenderer`)**：
   - 通过 GOT 重定位挂钩 `glClear`（RVA `0x132893d8`），在游戏 GPU 渲染线程上直接绘制真实的 `normal_cave.mesh`（98,094 个顶点）。
   - 具备预烘焙彩色光照、动态流动熔岩 UV 滚动动画、壁挂灵魂火把以及火焰/烟雾粒子特效。
   - 自然的三次样条开场运镜，主菜单静止状态具备微幅环境呼吸晃动。

2. **直接函数级背景抑制（免资源包）**：
   - 摆脱了对透明贴图资源包（`AnimatedMojang_RP`）的依赖。
   - 内存挂钩 RenderDragon 原生渲染器（`PanoramaRenderer::$render` 位于 `0x0967FA98`，`CubemapBackgroundScreen::$render` 位于 `0x09B7C9E0`），在主菜单状态下直接静默原版天空盒。
   - **游戏世界零干扰**：精确识别游戏世界状态（通过 `InGamePlayScreen::render` 与 `ClientInstance::requestLeaveGame`），确保只在主界面渲染洞穴，进入世界后自动关闭，还原 100% 原版天空与环境。

3. **独立硬件加速标题动画**：
   - 在 GPU 渲染线程上直接合成 2D 动态标题，无需繁琐的 JSON UI 复杂替换。
   - 自动挂钩 `ui::ScreenTechStackSelector::getTechStackForScreen`（RVA `0x0974E844`），保证主界面交互顺畅。

4. **零延迟子菜单运镜切换**：
   - 为各个菜单页面量身定制的无侧倾电影级镜头视角：
     - **主界面 (Start Screen)**: `(31.0, 13.0, 6.0, yaw=21.0, pitch=-13.0)`
     - **世界列表 (Play / Worlds)**: `(24.0, 15.0, 18.0, yaw=-24.0, pitch=-6.0)`
     - **设置界面 (Settings)**: `(26.0, 14.0, 20.0, yaw=-10.0, pitch=-8.0)`
     - **Realms 界面**: `(22.0, 14.0, 24.0, yaw=15.0, pitch=-5.0)`
     - **商城界面 (Marketplace)**: `(18.0, 13.0, 18.0, yaw=79.0, pitch=4.0)`
     - **更衣室 / 档案 (Dressing Room)**: `(20.0, 13.0, 37.0, yaw=42.0, pitch=-4.0)`
   - 直接挂钩引擎按钮事件，点击瞬间平滑转动，关闭界面即刻优雅复位。

---

## 安装方法

### 方式 1：LeviLauncher (LeviLaunchroid)
1. 获取 **`animated_mojang.levipack`** 或 **`AnimatedMojang-1.0.0-android-arm64.zip`**。
2. 打开 **LeviLauncher** -> **模组** -> **导入** -> 选择 **`animated_mojang.levipack`**。
3. 确保启用 AnimatedMojang 模组。
4. 启动 Minecraft 即可！（无需安装任何资源包）。

### 方式 2：手动解压安装
将 `AnimatedMojang-1.0.0-android-arm64.zip` 解压至模组目录：
```
mods/AnimatedMojang/
├── libAnimatedMojang.so
├── manifest.json
├── icon.png
├── config/
│   └── config.json
└── resources/
    ├── data/
    │   └── normal_cave.mesh
    └── textures/
        └── animated_mojang/
```

---

## 编译指南

运行构建打包脚本即可生成全部发行文件：
```bash
./package_mod.sh
```
生成包位于 `dist/`：
- `dist/AnimatedMojang-1.0.0-android-arm64.zip`
- `dist/animated_mojang.levipack`
