#include "scene/CaveGlesRenderer.h"
#include "scene/CaveModel.h"
#include "mod/MyMod.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cmath>
#include <iostream>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace animated_mojang::scene {

namespace {

auto& logger() {
    return my_mod::MyMod::getInstance().getLogger();
}

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    if (!shader) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            std::string infoLog(infoLen, '\0');
            glGetShaderInfoLog(shader, infoLen, nullptr, infoLog.data());
            logger().error("Shader compile error: ", infoLog);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

CaveGlesRenderer::CaveGlesRenderer() = default;

CaveGlesRenderer::~CaveGlesRenderer() {
    destroy();
}

void CaveGlesRenderer::destroy() {
    if (mVbo) {
        glDeleteBuffers(1, &mVbo);
        mVbo = 0;
    }
    if (mAtlasTexture) {
        glDeleteTextures(1, &mAtlasTexture);
        mAtlasTexture = 0;
    }
    if (mProgram) {
        glDeleteProgram(mProgram);
        mProgram = 0;
    }
    if (mTitleVbo) {
        glDeleteBuffers(1, &mTitleVbo);
        mTitleVbo = 0;
    }
    for (auto& tex : mTitleTextures) {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
            tex = 0;
        }
    }
    if (mTitleProgram) {
        glDeleteProgram(mTitleProgram);
        mTitleProgram = 0;
    }
    mGlReady = false;
    mGlFailed = false;
}

bool CaveGlesRenderer::loadTitleAnimation(const std::filesystem::path& titleDir) {
    mTitleDir = titleDir;
    logger().info("CaveGlesRenderer: Configured title animation directory: ", mTitleDir.string());
    return std::filesystem::exists(mTitleDir);
}


bool CaveGlesRenderer::loadModel(const std::filesystem::path& meshPath, const std::filesystem::path& atlasPath) {
    mAtlasPath = atlasPath;
    try {
        std::vector<Vertex> raw = loadCaveModel(meshPath);
        mVertices.clear();
        mVertices.reserve(raw.size());
        for (const auto& v : raw) {
            float flags = (v.texture & animatedLavaFlag) ? 1.0f : 0.0f;
            mVertices.push_back({
                v.position.x, v.position.y, v.position.z,
                v.r, v.g, v.b,
                v.u, v.v,
                flags
            });
        }
        logger().info("CaveGlesRenderer: Loaded ", mVertices.size(), " vertices into CPU staging buffer.");
        return true;
    } catch (const std::exception& e) {
        logger().error("CaveGlesRenderer::loadModel failed: ", e.what());
        mVertices.clear();
        return false;
    }
}

bool CaveGlesRenderer::initShaders() {
    static const char* kVertexShaderSource =
        "attribute vec3 a_position;\n"
        "attribute vec3 a_color;\n"
        "attribute vec2 a_uv;\n"
        "attribute float a_flags;\n"
        "\n"
        "uniform mat4 u_mvp;\n"
        "uniform float u_lavaOffset;\n"
        "\n"
        "varying vec3 v_color;\n"
        "varying vec2 v_uv;\n"
        "\n"
        "void main() {\n"
        "    gl_Position = u_mvp * vec4(a_position, 1.0);\n"
        "    v_color = a_color;\n"
        "    vec2 uv = a_uv;\n"
        "    if (a_flags > 0.5) {\n"
        "        uv.y += u_lavaOffset;\n"
        "    }\n"
        "    v_uv = uv;\n"
        "}\n";

    static const char* kFragmentShaderSource =
        "precision mediump float;\n"
        "\n"
        "varying vec3 v_color;\n"
        "varying vec2 v_uv;\n"
        "\n"
        "uniform sampler2D u_atlas;\n"
        "\n"
        "void main() {\n"
        "    vec4 texColor = texture2D(u_atlas, v_uv);\n"
        "    vec4 finalColor = vec4(texColor.rgb * v_color, texColor.a);\n"
        "    if (finalColor.a < 0.05) {\n"
        "        discard;\n"
        "    }\n"
        "    gl_FragColor = finalColor;\n"
        "}\n";

    GLuint vert = compileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    if (!vert) return false;

    GLuint frag = compileShader(GL_FRAGMENT_SHADER, kFragmentShaderSource);
    if (!frag) {
        glDeleteShader(vert);
        return false;
    }

    mProgram = glCreateProgram();
    glAttachShader(mProgram, vert);
    glAttachShader(mProgram, frag);
    glLinkProgram(mProgram);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint linked = 0;
    glGetProgramiv(mProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(mProgram, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            std::string infoLog(infoLen, '\0');
            glGetProgramInfoLog(mProgram, infoLen, nullptr, infoLog.data());
            logger().error("Program link error: ", infoLog);
        }
        glDeleteProgram(mProgram);
        mProgram = 0;
        return false;
    }

    mLocMvp = glGetUniformLocation(mProgram, "u_mvp");
    mLocLavaOffset = glGetUniformLocation(mProgram, "u_lavaOffset");
    mLocAtlas = glGetUniformLocation(mProgram, "u_atlas");

    mAttrPos = glGetAttribLocation(mProgram, "a_position");
    mAttrColor = glGetAttribLocation(mProgram, "a_color");
    mAttrUv = glGetAttribLocation(mProgram, "a_uv");
    mAttrFlags = glGetAttribLocation(mProgram, "a_flags");

    return true;
}

bool CaveGlesRenderer::initVbo() {
    if (mVertices.empty()) return false;
    glGenBuffers(1, &mVbo);
    if (!mVbo) return false;

    glBindBuffer(GL_ARRAY_BUFFER, mVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(mVertices.size() * sizeof(GpuVertex)),
                 mVertices.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    logger().info("CaveGlesRenderer: Uploaded ", mVertices.size(), " vertices to GPU VBO ", mVbo);
    return true;
}

bool CaveGlesRenderer::initTexture() {
    if (!std::filesystem::exists(mAtlasPath)) {
        logger().warn("CaveGlesRenderer: Atlas path does not exist: ", mAtlasPath.string());
        return false;
    }

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load(mAtlasPath.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        logger().warn("CaveGlesRenderer: Failed to decode atlas with stb_image: ", mAtlasPath.string());
        return false;
    }

    glGenTextures(1, &mAtlasTexture);
    glBindTexture(GL_TEXTURE_2D, mAtlasTexture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);

    stbi_image_free(pixels);
    logger().info("CaveGlesRenderer: Uploaded atlas texture (", width, "x", height, ") to GPU texture ", mAtlasTexture);
    return true;
}

bool CaveGlesRenderer::ensureGlInitialized() {
    if (mGlReady) return true;
    if (mGlFailed) return false;

    logger().info("CaveGlesRenderer: Initializing OpenGL ES resources on render thread...");

    if (!initShaders()) {
        logger().error("CaveGlesRenderer: Shader initialization failed.");
        mGlFailed = true;
        return false;
    }

    if (!initVbo()) {
        logger().error("CaveGlesRenderer: VBO initialization failed.");
        mGlFailed = true;
        return false;
    }

    if (!initTexture()) {
        logger().error("CaveGlesRenderer: Texture initialization failed.");
        mGlFailed = true;
        return false;
    }

    if (!mTitleDir.empty() && std::filesystem::exists(mTitleDir)) {
        if (!initTitleShaders() || !initTitleVbo()) {
            logger().warn("CaveGlesRenderer: Title GPU shaders or VBO initialization failed.");
        }
    }

    mGlReady = true;
    logger().info("CaveGlesRenderer: 100% OpenGL ES 3D Cave GPU pipeline ready!");
    return true;
}

bool CaveGlesRenderer::initTitleShaders() {
    static const char* kTitleVertexShader =
        "attribute vec2 a_pos;\n"
        "attribute vec2 a_uv;\n"
        "uniform mat4 u_ortho;\n"
        "varying vec2 v_uv;\n"
        "void main() {\n"
        "    gl_Position = u_ortho * vec4(a_pos, 0.0, 1.0);\n"
        "    v_uv = a_uv;\n"
        "}\n";

    static const char* kTitleFragmentShader =
        "precision mediump float;\n"
        "varying vec2 v_uv;\n"
        "uniform sampler2D u_titleTex;\n"
        "uniform float u_alpha;\n"
        "void main() {\n"
        "    vec4 col = texture2D(u_titleTex, v_uv);\n"
        "    if (col.a < 0.05) discard;\n"
        "    gl_FragColor = vec4(col.rgb, col.a * u_alpha);\n"
        "}\n";

    GLuint vert = compileShader(GL_VERTEX_SHADER, kTitleVertexShader);
    if (!vert) return false;

    GLuint frag = compileShader(GL_FRAGMENT_SHADER, kTitleFragmentShader);
    if (!frag) {
        glDeleteShader(vert);
        return false;
    }

    mTitleProgram = glCreateProgram();
    glAttachShader(mTitleProgram, vert);
    glAttachShader(mTitleProgram, frag);
    glLinkProgram(mTitleProgram);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint linked = 0;
    glGetProgramiv(mTitleProgram, GL_LINK_STATUS, &linked);
    if (!linked) {
        glDeleteProgram(mTitleProgram);
        mTitleProgram = 0;
        return false;
    }

    mLocTitleOrtho = glGetUniformLocation(mTitleProgram, "u_ortho");
    mLocTitleAlpha = glGetUniformLocation(mTitleProgram, "u_alpha");
    mLocTitleTex = glGetUniformLocation(mTitleProgram, "u_titleTex");

    mAttrTitlePos = glGetAttribLocation(mTitleProgram, "a_pos");
    mAttrTitleUv = glGetAttribLocation(mTitleProgram, "a_uv");

    return true;
}

bool CaveGlesRenderer::initTitleVbo() {
    glGenBuffers(1, &mTitleVbo);
    return mTitleVbo != 0;
}

bool CaveGlesRenderer::renderTitle(float screenWidth, float screenHeight, int titleFrame, float titleAlpha) {
    if (titleFrame < 0 || titleAlpha <= 0.01f || mTitleDir.empty()) return false;
    if (!mTitleProgram || !mTitleVbo) {
        if (!initTitleShaders() || !initTitleVbo()) return false;
    }

    int frame = std::clamp(titleFrame, 0, 68);
    int stripIndex = frame / 8; // 0 to 8
    int subFrame = frame % 8;   // 0 to 7

    // On-demand GPU upload of the required title strip (minecraft_0.png .. minecraft_8.png)
    if (mTitleTextures[stripIndex] == 0) {
        auto stripPath = mTitleDir / ("minecraft_" + std::to_string(stripIndex) + ".png");
        if (!std::filesystem::exists(stripPath)) {
            static bool sWarnedMissing = false;
            if (!sWarnedMissing) {
                sWarnedMissing = true;
                logger().warn("CaveGlesRenderer: Missing title strip file: ", stripPath.string());
            }
            return false;
        }

        int width = 0, height = 0, channels = 0;
        unsigned char* pixels = stbi_load(stripPath.c_str(), &width, &height, &channels, 4);
        if (!pixels) {
            logger().warn("CaveGlesRenderer: Failed to decode title strip: ", stripPath.string());
            return false;
        }

        glGenTextures(1, &mTitleTextures[stripIndex]);
        glBindTexture(GL_TEXTURE_2D, mTitleTextures[stripIndex]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);

        stbi_image_free(pixels);
        logger().info("CaveGlesRenderer: Uploaded title strip ", stripIndex, " (", width, "x", height, ") -> GPU tex ", mTitleTextures[stripIndex]);
    }

    GLuint curTex = mTitleTextures[stripIndex];
    if (curTex == 0) return false;

    // Memory optimization: once animation reaches frame 68 (resting strip 8), purge strips 0..7 from VRAM
    if (stripIndex == 8) {
        for (int i = 0; i < 8; ++i) {
            if (mTitleTextures[i] != 0) {
                glDeleteTextures(1, &mTitleTextures[i]);
                mTitleTextures[i] = 0;
            }
        }
    }

    static bool sLoggedFirstTitle = false;
    if (!sLoggedFirstTitle) {
        sLoggedFirstTitle = true;
        logger().info("renderTitle: Active and rendering frame ", titleFrame,
                      " (Viewport: ", screenWidth, "x", screenHeight, ", tex: ", curTex, ")");
    }

    static int sLastLoggedFrame = -1;
    if (frame != sLastLoggedFrame && (frame == 0 || frame == 34 || frame == 68)) {
        sLastLoggedFrame = frame;
        logger().info("renderTitle: Playing frame ", frame, " (Viewport: ", screenWidth, "x", screenHeight, ")");
    }

    float u0 = 0.0f;
    float u1 = 1.0f;
    float v0 = static_cast<float>(subFrame) / 8.0f;
    float v1 = static_cast<float>(subFrame + 1) / 8.0f;

    // Upstream 1:1 mathematical titlePlacement derivation:
    // targetWidth matches vanilla Minecraft title on screen (~47.7% of screen width)
    // resting logo in frame 68 has width 1373, height 245, positioned at x0=518, y0=502 inside 2403x749 frame
    float targetWidth = screenWidth * 0.477f;
    float targetX = (screenWidth - targetWidth) * 0.5f;
    // Vanilla logo resting position: top of logo at ~13.0% screen height (Y=140 on 1080p),
    // bottom of logo at Y=350, perfectly positioned above the Play button (Y=388)!
    float targetY = screenHeight * 0.130f;

    const float scale = targetWidth / 1373.0f;
    float w = 2403.0f * scale;
    float h = 749.0f * scale;
    float x = targetX - 518.0f * scale;
    float y = targetY - 502.0f * scale;

    struct TitleVertex {
        float x, y;
        float u, v;
    };

    TitleVertex quad[6] = {
        { x,     y,     u0, v0 },
        { x + w, y,     u1, v0 },
        { x + w, y + h, u1, v1 },
        { x,     y,     u0, v0 },
        { x + w, y + h, u1, v1 },
        { x,     y + h, u0, v1 }
    };

    GLint prevVao = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glBindVertexArray(0);
    GLint prevProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLint prevVbo = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVbo);
    GLint prevActiveTex = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    GLint prevTex2D = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2D);
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLint prevBlendSrc = 0, prevBlendDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(mTitleProgram);

    glm::mat4 ortho = glm::ortho(0.0f, screenWidth, screenHeight, 0.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(mLocTitleOrtho, 1, GL_FALSE, &ortho[0][0]);
    glUniform1f(mLocTitleAlpha, titleAlpha);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, curTex);
    glUniform1i(mLocTitleTex, 0);

    glBindBuffer(GL_ARRAY_BUFFER, mTitleVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);

    constexpr GLsizei stride = sizeof(TitleVertex);
    if (mAttrTitlePos >= 0) {
        glEnableVertexAttribArray(mAttrTitlePos);
        glVertexAttribPointer(mAttrTitlePos, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    }
    if (mAttrTitleUv >= 0) {
        glEnableVertexAttribArray(mAttrTitleUv);
        glVertexAttribPointer(mAttrTitleUv, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(2 * sizeof(float)));
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (mAttrTitlePos >= 0) glDisableVertexAttribArray(mAttrTitlePos);
    if (mAttrTitleUv >= 0) glDisableVertexAttribArray(mAttrTitleUv);

    glBindBuffer(GL_ARRAY_BUFFER, prevVbo);
    glBindVertexArray(prevVao);
    glActiveTexture(prevActiveTex);
    glBindTexture(GL_TEXTURE_2D, prevTex2D);
    glUseProgram(prevProgram);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(prevBlendSrc, prevBlendDst);

    return true;
}

bool CaveGlesRenderer::render(float screenWidth, float screenHeight, const glm::mat4& viewMatrix, double timeMs) {
    if (!ensureGlInitialized()) return false;
    if (mVertices.empty()) return false;

    // 1. Calculate perspective projection and MVP matrix
    glm::mat4 projection = glm::perspective(glm::radians(50.0f), screenWidth / screenHeight, 0.1f, 150.0f);
    glm::mat4 mvp = projection * viewMatrix;

    // 2. Compute lava frame UV scrolling offset
    float lavaOffset = static_cast<float>(std::fmod(std::floor(std::max(0.0, timeMs) / 300.0), 32.0)) * (16.0f / 768.0f);

    // 3. Preserve preceding OpenGL ES state
    GLint prevViewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevVao = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glBindVertexArray(0);
    GLint prevProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLint prevVbo = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVbo);
    GLint prevActiveTex = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    GLint prevTex2D = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2D);
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLint prevBlendSrc = 0, prevBlendDst = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDst);
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    // 4. Configure GL state for 3D Cave room
    glViewport(0, 0, static_cast<GLsizei>(screenWidth), static_cast<GLsizei>(screenHeight));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 5. Bind program & upload uniforms
    glUseProgram(mProgram);
    glUniformMatrix4fv(mLocMvp, 1, GL_FALSE, &mvp[0][0]);
    glUniform1f(mLocLavaOffset, lavaOffset);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mAtlasTexture);
    glUniform1i(mLocAtlas, 0);

    // 6. Bind VBO and configure vertex attributes
    glBindBuffer(GL_ARRAY_BUFFER, mVbo);

    constexpr GLsizei stride = sizeof(GpuVertex);
    if (mAttrPos >= 0) {
        glEnableVertexAttribArray(mAttrPos);
        glVertexAttribPointer(mAttrPos, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    }
    if (mAttrColor >= 0) {
        glEnableVertexAttribArray(mAttrColor);
        glVertexAttribPointer(mAttrColor, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(3 * sizeof(float)));
    }
    if (mAttrUv >= 0) {
        glEnableVertexAttribArray(mAttrUv);
        glVertexAttribPointer(mAttrUv, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(6 * sizeof(float)));
    }
    if (mAttrFlags >= 0) {
        glEnableVertexAttribArray(mAttrFlags);
        glVertexAttribPointer(mAttrFlags, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(8 * sizeof(float)));
    }

    // 7. Execute GPU hardware draw call
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mVertices.size()));

    // 8. Clean up vertex attribute state
    if (mAttrPos >= 0) glDisableVertexAttribArray(mAttrPos);
    if (mAttrColor >= 0) glDisableVertexAttribArray(mAttrColor);
    if (mAttrUv >= 0) glDisableVertexAttribArray(mAttrUv);
    if (mAttrFlags >= 0) glDisableVertexAttribArray(mAttrFlags);

    // 9. Restore prior OpenGL ES state for UI pass
    glBindBuffer(GL_ARRAY_BUFFER, prevVbo);
    glBindVertexArray(prevVao);
    glActiveTexture(prevActiveTex);
    glBindTexture(GL_TEXTURE_2D, prevTex2D);
    glUseProgram(prevProgram);

    glDepthMask(prevDepthMask);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFunc(prevBlendSrc, prevBlendDst);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);

    return true;
}

} // namespace animated_mojang::scene
