# legacy/cpu_ui_renderer

Earlier prototype that projected the cave on the CPU and drew it through Bedrock's `Tessellator`/`ScreenRenderer`.
It is **not compiled** (the shipping renderer is `src/scene/CaveGlesRenderer.cpp`); before this cleanup it was still
loaded at startup (mesh + particles parsed twice) although `NativeScene::draw()` was never called. Kept for reference
(e.g. `CaveParticles.h` for a future GPU particle pass).
