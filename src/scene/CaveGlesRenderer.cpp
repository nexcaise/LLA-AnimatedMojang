#include "scene/CaveGlesRenderer.h"
#include "scene/CaveModel.h"
#include "mod/MyMod.h"

#include <glm/ext/matrix_clip_space.hpp>

#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace animated_mojang::scene {

namespace {

auto& logger() {
    return my_mod::MyMod::getInstance().getLogger();
}

double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

// Background work must never compete with the game's own threads.
void lowerThreadPriority() {
    setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 10);
}

// ---------------------------------------------------------------------------------------------
// Shaders (GLSL ES 3.00). UVs are highp: the atlas is 768 texels tall and mediump (fp16) cannot
// address that many texels exactly, which made NEAREST sampling pick wrong rows near the bottom.
// ---------------------------------------------------------------------------------------------
constexpr const char* kCaveVs = R"GLSL(
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;   // rgb = baked light, a = 1 for animated lava
layout(location = 2) in vec2 a_uv;
uniform mat4 u_mvp;
uniform float u_lavaOffset;
out vec3 v_color;
out highp vec2 v_uv;
void main() {
    gl_Position = u_mvp * vec4(a_position, 1.0);
    v_color = a_color.rgb;
    v_uv = a_uv + vec2(0.0, u_lavaOffset * a_color.a);
}
)GLSL";

constexpr const char* kCaveFs = R"GLSL(
precision mediump float;
uniform mediump sampler2D u_atlas;
in vec3 v_color;
in highp vec2 v_uv;
out vec4 o_color;
void main() {
    vec4 t = texture(u_atlas, v_uv);
#ifdef CUTOUT
    if (t.a < 0.05) discard;
    o_color = vec4(t.rgb * v_color, t.a);
#else
    o_color = vec4(t.rgb * v_color, 1.0);
#endif
}
)GLSL";

// One program serves both the title (premultiplied atlas) and the full-screen fade (opaque capture).
constexpr const char* kQuadVs = R"GLSL(
layout(location = 0) in vec2 a_corner;
uniform vec4 u_rect; // NDC x, y, w, h
uniform vec4 u_uv;   // u, v, du, dv
out highp vec2 v_uv;
void main() {
    gl_Position = vec4(u_rect.xy + a_corner * u_rect.zw, 0.0, 1.0);
    v_uv = u_uv.xy + a_corner * u_uv.zw;
}
)GLSL";

constexpr const char* kQuadFs = R"GLSL(
precision mediump float;
uniform mediump sampler2D u_tex;
uniform float u_alpha;
uniform float u_opaque;
in highp vec2 v_uv;
out vec4 o_color;
void main() {
    vec4 c = texture(u_tex, v_uv);
    c.a = mix(c.a, 1.0, u_opaque);
    o_color = c * u_alpha; // premultiplied result
}
)GLSL";

// ---------------------------------------------------------------------------------------------
// GL state hygiene. The renderer shares the game's context, so everything touched is restored.
// ---------------------------------------------------------------------------------------------
class GlStateScope {
public:
    explicit GlStateScope(bool withFramebuffer = false) : mWithFbo(withFramebuffer) {
        glGetIntegerv(GL_VIEWPORT, mViewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &mProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &mVao);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &mActiveTex);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &mTex0);
        glGetIntegerv(GL_SAMPLER_BINDING, &mSampler0);
        if (mWithFbo) glGetIntegerv(GL_FRAMEBUFFER_BINDING, &mFbo);
        mDepthTest = glIsEnabled(GL_DEPTH_TEST);
        mCull = glIsEnabled(GL_CULL_FACE);
        mBlend = glIsEnabled(GL_BLEND);
        mScissor = glIsEnabled(GL_SCISSOR_TEST);
        mStencil = glIsEnabled(GL_STENCIL_TEST);
        glGetIntegerv(GL_BLEND_SRC_RGB, &mBlendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &mBlendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &mBlendSrcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &mBlendDstA);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &mBlendEqRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &mBlendEqA);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &mDepthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, mColorMask);
        glGetIntegerv(GL_DEPTH_FUNC, &mDepthFunc);
        glGetFloatv(GL_DEPTH_CLEAR_VALUE, &mDepthClear);

        // A sampler object left on unit 0 would override our texture parameters.
        glBindSampler(0, 0);
        glBlendEquation(GL_FUNC_ADD);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
    }
    ~GlStateScope() {
        if (mWithFbo) glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(mFbo));
        glViewport(mViewport[0], mViewport[1], mViewport[2], mViewport[3]);
        glUseProgram(static_cast<GLuint>(mProgram));
        glBindVertexArray(static_cast<GLuint>(mVao));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(mTex0));
        glBindSampler(0, static_cast<GLuint>(mSampler0));
        glActiveTexture(static_cast<GLenum>(mActiveTex));
        setCap(GL_DEPTH_TEST, mDepthTest);
        setCap(GL_CULL_FACE, mCull);
        setCap(GL_BLEND, mBlend);
        setCap(GL_SCISSOR_TEST, mScissor);
        setCap(GL_STENCIL_TEST, mStencil);
        glBlendFuncSeparate(mBlendSrcRgb, mBlendDstRgb, mBlendSrcA, mBlendDstA);
        glBlendEquationSeparate(mBlendEqRgb, mBlendEqA);
        glDepthMask(mDepthMask);
        glColorMask(mColorMask[0], mColorMask[1], mColorMask[2], mColorMask[3]);
        glDepthFunc(static_cast<GLenum>(mDepthFunc));
        glClearDepthf(mDepthClear);
    }
    GlStateScope(GlStateScope const&) = delete;
    GlStateScope& operator=(GlStateScope const&) = delete;

private:
    static void setCap(GLenum cap, GLboolean on) {
        if (on) glEnable(cap); else glDisable(cap);
    }
    bool mWithFbo;
    GLint mViewport[4]{};
    GLint mProgram = 0, mVao = 0, mActiveTex = 0, mTex0 = 0, mSampler0 = 0, mFbo = 0;
    GLboolean mDepthTest = GL_FALSE, mCull = GL_FALSE, mBlend = GL_FALSE, mScissor = GL_FALSE, mStencil = GL_FALSE;
    GLint mBlendSrcRgb = GL_ONE, mBlendDstRgb = GL_ZERO, mBlendSrcA = GL_ONE, mBlendDstA = GL_ZERO;
    GLint mBlendEqRgb = GL_FUNC_ADD, mBlendEqA = GL_FUNC_ADD;
    GLboolean mDepthMask = GL_TRUE, mColorMask[4]{GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint mDepthFunc = GL_LESS;
    GLfloat mDepthClear = 1.f;
};

// Texture uploads: keep unit 0's binding and make the pixel-unpack state predictable
// (a stray UNPACK_ROW_LENGTH or bound PBO from the game would corrupt or crash our uploads).
class UploadScope {
public:
    UploadScope() {
        glGetIntegerv(GL_ACTIVE_TEXTURE, &mActive);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &mTex);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &mRowLength);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &mSkipPixels);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &mSkipRows);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &mAlign);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &mPbo);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    }
    ~UploadScope() {
        glPixelStorei(GL_UNPACK_ALIGNMENT, mAlign);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, mSkipRows);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, mSkipPixels);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, mRowLength);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(mPbo));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(mTex));
        glActiveTexture(static_cast<GLenum>(mActive));
    }
    UploadScope(UploadScope const&) = delete;
    UploadScope& operator=(UploadScope const&) = delete;

private:
    GLint mActive = 0, mTex = 0, mRowLength = 0, mSkipPixels = 0, mSkipRows = 0, mAlign = 4, mPbo = 0;
};

constexpr int kTitleRowsPerBand = 256;      // 2 MB per frame while streaming (about 1.5 ms on mobile GPUs)
constexpr int kMaxTitlePagesInFlight = 2;   // bounds decoded-but-not-uploaded RAM to ~32 MB

// Placement of the resting logo (frame 68) inside the 2403x749 source frame. These numbers define the
// on-screen layout and are independent of the atlas bake scale.
constexpr float kLogoWidthPx = 1373.f, kLogoX = 518.f, kLogoY = 502.f;
constexpr float kLogoScreenWidth = 0.477f, kLogoScreenTop = 0.130f;

} // namespace

// ================================================================================================
// Lifecycle
// ================================================================================================
CaveGlesRenderer::CaveGlesRenderer() = default;

CaveGlesRenderer::~CaveGlesRenderer() {
    // Never touches GL: the destructor may run at process exit without a current context.
    for (auto& page : mPages) page.tex = 0;
}

void CaveGlesRenderer::configure(bool titleEnabled, bool keepTitleResident) {
    mTitleEnabled.store(titleEnabled);
    mKeepTitleResident.store(keepTitleResident);
}

void CaveGlesRenderer::requestTitleReplay() {
    mReplayRequested.store(true);
}

void CaveGlesRenderer::beginLoad(std::filesystem::path const& modDirectory) {
    std::lock_guard lock(mLoadMutex);
    if (mLoadStarted) return;
    mLoadStarted = true;
    mModDirectory = modDirectory;
    mStatus.store(Status::Loading);
    mLoadFuture = std::async(std::launch::async, &CaveGlesRenderer::loadAll, modDirectory);
}

CaveGlesRenderer::LoadResult CaveGlesRenderer::loadAll(std::filesystem::path modDirectory) {
    lowerThreadPriority();
    const auto started = std::chrono::steady_clock::now();
    LoadResult result;

    const auto resources = modDirectory / "resources";
    const auto meshPath = resources / "data" / "normal_cave.mesh";
    const auto atlasPath = resources / "textures" / "animated_mojang" / "cave" / "atlas.png";
    const auto titleJson = resources / "textures" / "animated_mojang" / "title" / "baked" / "title_atlas.json";

    std::vector<Vertex> raw;
    try {
        raw = loadCaveModel(meshPath);
    } catch (std::exception const& e) {
        result.error = std::string("cave mesh: ") + e.what() + " (" + meshPath.string() + ")";
        return result;
    }
    result.atlas = decodeImageRgba(atlasPath, &result.error);
    if (!result.atlas) return result;

    result.mesh = buildCaveMesh(raw, &result.atlas);
    if (result.mesh.empty()) {
        result.error = "cave mesh produced no geometry";
        return result;
    }

    TitleAtlasInfo info;
    if (parseTitleAtlas(titleJson, info, result.titleError)) result.title = std::move(info);

    logger().info("CaveGlesRenderer: assets prepared in ", static_cast<int>(msSince(started)), " ms (",
                  result.mesh.sourceVertexCount, " -> ", result.mesh.vertices.size(), " vertices, ",
                  result.mesh.opaqueIndexCount / 3, " opaque + ", result.mesh.cutoutIndexCount / 3, " cutout triangles)");
    result.ok = true;
    return result;
}

bool CaveGlesRenderer::pollCpuLoad() {
    if (mCpuReady) return true;
    std::lock_guard lock(mLoadMutex);
    if (!mLoadFuture.valid()) return false;
    if (mLoadFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;

    LoadResult result = mLoadFuture.get();
    if (!result.ok) {
        logger().error("CaveGlesRenderer: asset load failed: ", result.error);
        mStatus.store(Status::Failed);
        return false;
    }
    mMesh = std::move(result.mesh);
    mAtlasImage = std::move(result.atlas);
    mTitleInfo = std::move(result.title);
    if (mTitleInfo) {
        mPages.clear();
        mPages.resize(mTitleInfo->pages.size());
    } else if (mTitleEnabled.load()) {
        logger().warn("CaveGlesRenderer: baked title atlas unavailable (", result.titleError,
                      "); run tools/bake_title_atlas.py. The cave still renders without the title animation.");
    }
    mCpuReady = true;
    mStatus.store(Status::Ready);
    return true;
}

void CaveGlesRenderer::serviceRelease() {
    if (mReleaseRequested.load(std::memory_order_relaxed) && mReleaseRequested.exchange(false)) releaseGl(false);
}

bool CaveGlesRenderer::prepare() {
    if (mReleaseRequested.exchange(false)) releaseGl(false);
    if (mStatus.load() == Status::Failed) return true;

    const EGLContext ctx = eglGetCurrentContext();
    if (mGlReady.load() && ctx != mContext) {
        logger().warn("CaveGlesRenderer: EGL context changed (app switch / reload). Rebuilding GPU resources...");
        releaseGl(true);
    }

    if (!mGlReady.load()) {
        if (!pollCpuLoad()) return mStatus.load() == Status::Failed;
        const auto now = std::chrono::steady_clock::now();
        if (now < mNextInitAttempt) return false;
        if (!initGl()) {
            releaseGl(false);
            if (++mInitAttempts >= 3) {
                logger().error("CaveGlesRenderer: OpenGL initialization failed 3 times; cave background disabled.");
                mStatus.store(Status::Failed);
                return true;
            }
            mNextInitAttempt = now + std::chrono::milliseconds(500);
            return false;
        }
        mContext = ctx;
        mInitAttempts = 0;
        mGlReady.store(true);
        logger().info("CaveGlesRenderer: OpenGL ES cave pipeline ready.");
    }

    if (mReplayRequested.exchange(false)) {
        mTitleSettled = false;
        mPriorityPage = -1;
    }
    pumpTitleStreaming();
    return true;
}

// ================================================================================================
// GL resource creation
// ================================================================================================
GLuint CaveGlesRenderer::buildProgram(const char* vs, const char* fs, const char* defines, const char* name) {
    auto compile = [&](GLenum type, const char* body) -> GLuint {
        const std::string source = std::string("#version 300 es\n") + defines + body;
        const char* ptr = source.c_str();
        GLuint shader = glCreateShader(type);
        if (!shader) return 0;
        glShaderSource(shader, 1, &ptr, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
            std::string log(static_cast<size_t>(std::max(len, 1)), '\0');
            glGetShaderInfoLog(shader, len, nullptr, log.data());
            logger().error("Shader compile error (", name, "): ", log);
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    };

    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = v ? compile(GL_FRAGMENT_SHADER, fs) : 0;
    if (!v || !f) {
        if (v) glDeleteShader(v);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &len);
        std::string log(static_cast<size_t>(std::max(len, 1)), '\0');
        glGetProgramInfoLog(program, len, nullptr, log.data());
        logger().error("Program link error (", name, "): ", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

bool CaveGlesRenderer::initGl() {
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 8 && glGetError() != GL_NO_ERROR; ++i) {} // drop errors the game left behind

    GLint prevVao = 0, prevArray = 0, prevProgram = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArray);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    UploadScope upload;

    mProgOpaque = buildProgram(kCaveVs, kCaveFs, "", "cave-opaque");
    mProgCutout = buildProgram(kCaveVs, kCaveFs, "#define CUTOUT 1\n", "cave-cutout");
    mQuadProg = buildProgram(kQuadVs, kQuadFs, "", "quad");
    bool ok = mProgOpaque && mProgCutout && mQuadProg;

    if (ok) {
        mLocMvpOpaque = glGetUniformLocation(mProgOpaque, "u_mvp");
        mLocLavaOpaque = glGetUniformLocation(mProgOpaque, "u_lavaOffset");
        mLocMvpCutout = glGetUniformLocation(mProgCutout, "u_mvp");
        mLocLavaCutout = glGetUniformLocation(mProgCutout, "u_lavaOffset");
        glUseProgram(mProgOpaque);
        glUniform1i(glGetUniformLocation(mProgOpaque, "u_atlas"), 0);
        glUseProgram(mProgCutout);
        glUniform1i(glGetUniformLocation(mProgCutout, "u_atlas"), 0);
        glUseProgram(mQuadProg);
        glUniform1i(glGetUniformLocation(mQuadProg, "u_tex"), 0);
        mLocRect = glGetUniformLocation(mQuadProg, "u_rect");
        mLocUv = glGetUniformLocation(mQuadProg, "u_uv");
        mLocAlpha = glGetUniformLocation(mQuadProg, "u_alpha");
        mLocOpaque = glGetUniformLocation(mQuadProg, "u_opaque");
        glUseProgram(static_cast<GLuint>(prevProgram));

        // ---- cave geometry: one VAO, 24-byte vertices, 16-bit indices when they fit
        glGenVertexArrays(1, &mCaveVao);
        glGenBuffers(1, &mCaveVbo);
        glGenBuffers(1, &mCaveIbo);
        glBindVertexArray(mCaveVao);
        glBindBuffer(GL_ARRAY_BUFFER, mCaveVbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mMesh.vertices.size() * sizeof(CaveGpuVertex)),
                     mMesh.vertices.data(), GL_STATIC_DRAW);
        constexpr GLsizei stride = sizeof(CaveGpuVertex);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<const void*>(12));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(16));

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mCaveIbo); // recorded in the VAO
        if (mMesh.vertices.size() <= 65535) {
            mIndexType = GL_UNSIGNED_SHORT;
            std::vector<std::uint16_t> narrow(mMesh.indices.begin(), mMesh.indices.end());
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(narrow.size() * sizeof(std::uint16_t)),
                         narrow.data(), GL_STATIC_DRAW);
        } else {
            mIndexType = GL_UNSIGNED_INT;
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(mMesh.indices.size() * sizeof(std::uint32_t)),
                         mMesh.indices.data(), GL_STATIC_DRAW);
        }

        // ---- shared unit quad (title + fade)
        static const float kCorners[8] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f};
        glGenVertexArrays(1, &mQuadVao);
        glGenBuffers(1, &mQuadVbo);
        glBindVertexArray(mQuadVao);
        glBindBuffer(GL_ARRAY_BUFFER, mQuadVbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(kCorners), kCorners, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<const void*>(0));

        // ---- cave atlas: immutable storage, NEAREST like the original look
        glGenTextures(1, &mAtlasTex);
        glBindTexture(GL_TEXTURE_2D, mAtlasTex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, mAtlasImage.width, mAtlasImage.height);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, mAtlasImage.width, mAtlasImage.height, GL_RGBA, GL_UNSIGNED_BYTE,
                        mAtlasImage.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    glBindVertexArray(static_cast<GLuint>(prevVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArray));
    glUseProgram(static_cast<GLuint>(prevProgram));

    const GLenum err = ok ? glGetError() : GL_NO_ERROR;
    if (err != GL_NO_ERROR) {
        logger().error("CaveGlesRenderer: GL error 0x", std::hex, err, std::dec, " during initialization");
        ok = false;
    }
    if (ok) logger().info("CaveGlesRenderer: GPU resources created in ", static_cast<int>(msSince(started)), " ms");
    return ok;
}

void CaveGlesRenderer::releaseTitlePage(TitlePage& page, bool contextLost) {
    if (!contextLost && page.tex) glDeleteTextures(1, &page.tex);
    page.tex = 0;
    page.rowsUploaded = 0;
    page.image = Rgba8Image{};
    if (page.state != TitlePage::State::Decoding && page.state != TitlePage::State::Failed)
        page.state = TitlePage::State::Absent;
}

void CaveGlesRenderer::releaseGl(bool contextLost) {
    if (!contextLost) {
        if (mCaveVao) glDeleteVertexArrays(1, &mCaveVao);
        if (mCaveVbo) glDeleteBuffers(1, &mCaveVbo);
        if (mCaveIbo) glDeleteBuffers(1, &mCaveIbo);
        if (mQuadVao) glDeleteVertexArrays(1, &mQuadVao);
        if (mQuadVbo) glDeleteBuffers(1, &mQuadVbo);
        if (mAtlasTex) glDeleteTextures(1, &mAtlasTex);
        if (mFadeTex) glDeleteTextures(1, &mFadeTex);
        if (mProgOpaque) glDeleteProgram(mProgOpaque);
        if (mProgCutout) glDeleteProgram(mProgCutout);
        if (mQuadProg) glDeleteProgram(mQuadProg);
    }
    mCaveVao = mCaveVbo = mCaveIbo = mQuadVao = mQuadVbo = mAtlasTex = mFadeTex = 0;
    mProgOpaque = mProgCutout = mQuadProg = 0;
    mFadeW = mFadeH = 0;
    for (auto& page : mPages) releaseTitlePage(page, contextLost);
    mTitleSettled = false;
    mPriorityPage = -1;
    mContext = EGL_NO_CONTEXT;
    mGlReady.store(false);
}

void CaveGlesRenderer::destroy(bool contextLost) {
    releaseGl(contextLost);
}

// ================================================================================================
// Title page streaming: decode on a worker thread, upload a bounded number of rows per frame.
// ================================================================================================
bool CaveGlesRenderer::titlePagesWanted(int page) const {
    if (mTitleSettled && !mKeepTitleResident.load()) return mTitleInfo && page == mTitleInfo->restingPage();
    return true;
}

void CaveGlesRenderer::requestTitlePage(int page) {
    if (page >= 0 && page < static_cast<int>(mPages.size()) && mPages[page].state == TitlePage::State::Absent)
        mPriorityPage = page;
}

void CaveGlesRenderer::evictTitlePagesExcept(int keepPage) {
    for (int i = 0; i < static_cast<int>(mPages.size()); ++i)
        if (i != keepPage) releaseTitlePage(mPages[i], false);
}

void CaveGlesRenderer::pumpTitleStreaming() {
    if (!mTitleEnabled.load() || !mTitleInfo || mPages.empty()) return;
    using State = TitlePage::State;
    const auto& info = *mTitleInfo;

    // 1. collect finished decodes
    for (size_t i = 0; i < mPages.size(); ++i) {
        auto& page = mPages[i];
        if (page.state != State::Decoding) continue;
        if (!page.pending.valid() || page.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
        page.image = page.pending.get();
        if (page.image && page.image.width == info.pages[i].w && page.image.height == info.pages[i].h) {
            if (titlePagesWanted(static_cast<int>(i))) {
                page.state = State::Decoded;
            } else {
                page.image = Rgba8Image{};
                page.state = State::Absent; // became unnecessary while decoding
            }
        } else {
            if (!mWarnedTitle) {
                mWarnedTitle = true;
                logger().warn("CaveGlesRenderer: title page failed to decode or has unexpected size: ", info.pages[i].file);
            }
            page.image = Rgba8Image{};
            page.state = State::Failed;
        }
    }

    // 2. upload one band of the most urgent decoded page
    int uploadIdx = -1;
    if (mPriorityPage >= 0 && mPages[mPriorityPage].state == State::Decoded) uploadIdx = mPriorityPage;
    for (size_t i = 0; uploadIdx < 0 && i < mPages.size(); ++i)
        if (mPages[i].state == State::Decoded) uploadIdx = static_cast<int>(i);
    if (uploadIdx >= 0) {
        auto& page = mPages[uploadIdx];
        const int W = info.pages[uploadIdx].w, H = info.pages[uploadIdx].h;
        UploadScope scope;
        if (!page.tex) {
            glGenTextures(1, &page.tex);
            glBindTexture(GL_TEXTURE_2D, page.tex);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, page.tex);
        }
        const int rows = std::min(kTitleRowsPerBand, H - page.rowsUploaded);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, page.rowsUploaded, W, rows, GL_RGBA, GL_UNSIGNED_BYTE,
                        page.image.data() + static_cast<size_t>(page.rowsUploaded) * W * 4u);
        page.rowsUploaded += rows;
        if (page.rowsUploaded >= H) {
            page.image = Rgba8Image{};
            page.state = State::Resident;
            if (mPriorityPage == uploadIdx) mPriorityPage = -1;
        }
    }

    // 3. start the next decode (priority page first, then playback order)
    int inFlight = 0;
    for (auto const& p : mPages)
        if (p.state == State::Decoding || p.state == State::Decoded) ++inFlight;
    if (inFlight < kMaxTitlePagesInFlight) {
        int next = -1;
        if (mPriorityPage >= 0 && mPages[mPriorityPage].state == State::Absent) next = mPriorityPage;
        for (size_t i = 0; next < 0 && i < mPages.size(); ++i)
            if (mPages[i].state == State::Absent && titlePagesWanted(static_cast<int>(i))) next = static_cast<int>(i);
        if (next >= 0) {
            auto path = info.directory / info.pages[next].file;
            mPages[next].state = State::Decoding;
            mPages[next].pending = std::async(std::launch::async, [path = std::move(path)] {
                lowerThreadPriority();
                std::string error;
                Rgba8Image image = decodeImageRgba(path, &error);
                if (!image) logger().warn("CaveGlesRenderer: ", error);
                return image;
            });
        }
    }
}

// ================================================================================================
// Drawing
// ================================================================================================
bool CaveGlesRenderer::drawFrame(FrameParams const& p) {
    if (!mGlReady.load() || !(p.width > 0.f) || !(p.height > 0.f)) return false;
    GlStateScope scope; // one save/restore for cave + title + capture
    drawCave(p);
    if (p.titleFrame >= 0 && p.titleAlpha > 0.01f && mTitleInfo && mTitleEnabled.load()) drawTitle(p);
    if (p.captureBackground) captureFade(p.width, p.height);
    return true;
}

void CaveGlesRenderer::drawCave(FrameParams const& p) {
    const glm::mat4 projection = glm::perspective(glm::radians(50.0f), p.width / p.height, 0.1f, 150.0f);
    const glm::mat4 mvp = projection * p.view;
    const float lava = lavaOffsetV(p.timeMs);

    glViewport(0, 0, static_cast<GLsizei>(p.width), static_cast<GLsizei>(p.height));
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClearDepthf(1.0f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_BLEND);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mAtlasTex);
    glBindVertexArray(mCaveVao);

    const GLsizei indexSize = mIndexType == GL_UNSIGNED_SHORT ? 2 : 4;

    // Pass 1: opaque geometry (~99.6% of the cave): no discard, no blending, early-Z friendly.
    if (mMesh.opaqueIndexCount) {
        glUseProgram(mProgOpaque);
        glUniformMatrix4fv(mLocMvpOpaque, 1, GL_FALSE, &mvp[0][0]);
        glUniform1f(mLocLavaOpaque, lava);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mMesh.opaqueIndexCount), mIndexType, nullptr);
    }
    // Pass 2: the few faces that touch transparent atlas texels, drawn last so they blend over the cave.
    if (mMesh.cutoutIndexCount) {
        glUseProgram(mProgCutout);
        glUniformMatrix4fv(mLocMvpCutout, 1, GL_FALSE, &mvp[0][0]);
        glUniform1f(mLocLavaCutout, lava);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mMesh.cutoutIndexCount), mIndexType,
                       reinterpret_cast<const void*>(static_cast<uintptr_t>(mMesh.opaqueIndexCount) * indexSize));
    }
}

void CaveGlesRenderer::drawTitle(FrameParams const& p) {
    using State = TitlePage::State;
    const auto& info = *mTitleInfo;
    const int frameIndex = std::clamp(p.titleFrame, 0, static_cast<int>(info.frames.size()) - 1);
    const TitleFrame& frame = info.frames[frameIndex];
    if (frame.empty) return;

    auto& page = mPages[frame.page];
    if (page.state != State::Resident) {
        requestTitlePage(frame.page); // never decode on the render thread: skip this frame, stream it now
        return;
    }

    // Same layout rules as the original: logo is 47.7% of the screen width, top edge at 13% of the height.
    const float targetWidth = p.width * kLogoScreenWidth;
    const float scale = targetWidth / kLogoWidthPx;
    const float originX = (p.width - targetWidth) * 0.5f - kLogoX * scale;
    const float originY = p.height * kLogoScreenTop - kLogoY * scale;
    const float px = originX + static_cast<float>(frame.cx) * scale;
    const float py = originY + static_cast<float>(frame.cy) * scale;
    const float pw = static_cast<float>(frame.cw) * scale;
    const float ph = static_cast<float>(frame.ch) * scale;

    const auto& pageInfo = info.pages[frame.page];
    glViewport(0, 0, static_cast<GLsizei>(p.width), static_cast<GLsizei>(p.height));
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // atlas is premultiplied

    glUseProgram(mQuadProg);
    glUniform4f(mLocRect, px / p.width * 2.f - 1.f, 1.f - py / p.height * 2.f, pw / p.width * 2.f, -ph / p.height * 2.f);
    glUniform4f(mLocUv, static_cast<float>(frame.x) / pageInfo.w, static_cast<float>(frame.y) / pageInfo.h,
                static_cast<float>(frame.w) / pageInfo.w, static_cast<float>(frame.h) / pageInfo.h);
    glUniform1f(mLocAlpha, p.titleAlpha);
    glUniform1f(mLocOpaque, 0.f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, page.tex);
    glBindVertexArray(mQuadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Settled on the resting frame: only that page needs to stay resident (reclaims ~45 MB of VRAM).
    if (frameIndex >= info.restingFrame() && !mTitleSettled) {
        mTitleSettled = true;
        if (!mKeepTitleResident.load()) evictTitlePagesExcept(info.restingPage());
    }
}

void CaveGlesRenderer::captureFade(float w, float h) {
    const int iw = static_cast<int>(w), ih = static_cast<int>(h);
    if (iw <= 0 || ih <= 0) return;
    glActiveTexture(GL_TEXTURE0);
    if (!mFadeTex || mFadeW != iw || mFadeH != ih) {
        if (mFadeTex) glDeleteTextures(1, &mFadeTex);
        glGenTextures(1, &mFadeTex);
        glBindTexture(GL_TEXTURE_2D, mFadeTex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, iw, ih); // no pixel upload: unaffected by unpack state
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        mFadeW = iw;
        mFadeH = ih;
    } else {
        glBindTexture(GL_TEXTURE_2D, mFadeTex);
    }
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, iw, ih);
}

void CaveGlesRenderer::drawFullscreenFade(float alpha) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(mQuadProg);
    glUniform4f(mLocRect, -1.f, -1.f, 2.f, 2.f);
    glUniform4f(mLocUv, 0.f, 0.f, 1.f, 1.f);
    glUniform1f(mLocAlpha, alpha);
    glUniform1f(mLocOpaque, 1.f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mFadeTex);
    glBindVertexArray(mQuadVao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

bool CaveGlesRenderer::renderFadeOverlay(float screenWidth, float screenHeight, float overlayAlpha) {
    if (!mGlReady.load() || overlayAlpha <= 0.001f || !mFadeTex || !(screenWidth > 0.f) || !(screenHeight > 0.f))
        return false;
    GlStateScope scope(/*withFramebuffer=*/true);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, static_cast<GLsizei>(screenWidth), static_cast<GLsizei>(screenHeight));
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    drawFullscreenFade(overlayAlpha);
    return true;
}

} // namespace animated_mojang::scene
