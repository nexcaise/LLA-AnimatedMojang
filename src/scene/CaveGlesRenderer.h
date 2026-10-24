#pragma once

#include <filesystem>
#include <vector>
#include <GLES3/gl3.h>
#include <glm/glm.hpp>
#include "scene/MeshVertex.h"
#include "scene/CaveAnimation.h"

namespace animated_mojang::scene {

class CaveGlesRenderer {
public:
    CaveGlesRenderer();
    ~CaveGlesRenderer();

    // Load mesh data on CPU side from binary .mesh file
    bool loadModel(const std::filesystem::path& meshPath, const std::filesystem::path& atlasPath);

    // Title animation directory (containing minecraft_0.png .. minecraft_8.png)
    bool loadTitleAnimation(const std::filesystem::path& titleDir);

    // Initialize GL resources on render thread (shaders, VBO, texture)
    bool ensureGlInitialized();

    // Render the 3D cave on active EGL render context
    bool render(float screenWidth, float screenHeight, const glm::mat4& viewMatrix, double timeMs);

    // Render the 2D animated title on active EGL render context
    bool renderTitle(float screenWidth, float screenHeight, int titleFrame, float titleAlpha = 1.0f);

    // Progressive preloader: loads 1 title texture strip at a time to eliminate animation hitching without freezing
    bool preloadTitleStrip(int stripIndex);
    bool preloadTitleTextures();
    void resetTitlePreload() {
        mNextStripToPreload = 0;
        mLastPreloadTimeMs = 0.0;
    }

    // Capture clean background (Cave + Title) before UI buttons render
    bool captureFadeBackground(float screenWidth, float screenHeight);

    // Render clean background overlay over UI to fade buttons in slowly
    bool renderFadeOverlay(float screenWidth, float screenHeight, float overlayAlpha);

    void destroy(bool contextLost = false);

    bool isModelLoaded() const { return !mVertices.empty(); }
    size_t getVertexCount() const { return mVertices.size(); }

private:
    struct GpuVertex {
        float x, y, z;
        float r, g, b;
        float u, v;
        float flags; // 1.0f if animated lava, 0.0f otherwise
    };

    std::vector<GpuVertex> mVertices;
    std::filesystem::path mAtlasPath;
    std::filesystem::path mTitleDir;

    GLuint mProgram = 0;
    GLuint mVbo = 0;
    GLuint mAtlasTexture = 0;

    GLint mLocMvp = -1;
    GLint mLocLavaOffset = -1;
    GLint mLocAtlas = -1;

    GLint mAttrPos = -1;
    GLint mAttrColor = -1;
    GLint mAttrUv = -1;
    GLint mAttrFlags = -1;

    // Title 2D rendering resources (9 upstream strip images, minecraft_0.png .. minecraft_8.png)
    GLuint mTitleProgram = 0;
    GLuint mTitleVbo = 0;
    std::array<GLuint, 9> mTitleTextures{};
    GLint mLocTitleOrtho = -1;
    GLint mLocTitleAlpha = -1;
    GLint mLocTitleTex = -1;
    GLint mAttrTitlePos = -1;
    GLint mAttrTitleUv = -1;

    // Fade Overlay 2D rendering resources (for slow button fade-in)
    GLuint mFadeTexture = 0;
    GLuint mFadeProgram = 0;
    GLuint mFadeVbo = 0;
    int mFadeTexWidth = 0;
    int mFadeTexHeight = 0;
    GLint mLocFadeAlpha = -1;
    GLint mLocFadeTex = -1;
    GLint mAttrFadePos = -1;
    GLint mAttrFadeUv = -1;

    bool mGlReady = false;
    bool mGlFailed = false;
    int mNextStripToPreload = 0;
    double mLastPreloadTimeMs = 0.0;

    bool initShaders();
    bool initVbo();
    bool initTexture();

    bool initTitleShaders();
    bool initTitleVbo();

    bool initFadeShaders();
    bool initFadeVbo();
};

} // namespace animated_mojang::scene

