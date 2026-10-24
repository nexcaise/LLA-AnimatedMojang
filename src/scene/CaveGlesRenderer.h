#pragma once

#include "scene/CaveMeshBuilder.h"
#include "scene/ImageDecode.h"
#include "scene/TitleAtlas.h"

#include <GLES3/gl3.h>
#include <EGL/egl.h>
#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <mutex>
#include <optional>
#include <vector>

namespace animated_mojang::scene {

// OpenGL ES 3.0 renderer for the cave background, the animated title and the UI fade-in.
//
// Threading model
//   * beginLoad / configure / requestTitleReplay / requestGlRelease / failed / ready: any thread
//   * everything else: the render thread that owns the current EGL context (i.e. inside the glClear hook)
// All file IO, PNG decoding, mesh de-duplication and alpha classification run on background threads;
// the render thread only issues GL calls and never decodes an image.
class CaveGlesRenderer {
public:
    struct FrameParams {
        float width = 0, height = 0;
        glm::mat4 view{1.f};
        double timeMs = 0;
        int titleFrame = -1;     // < 0: no title
        float titleAlpha = 1.f;
        bool captureBackground = false; // copy the finished cave+title into the fade texture
    };

    CaveGlesRenderer();
    ~CaveGlesRenderer();
    CaveGlesRenderer(CaveGlesRenderer const&) = delete;
    CaveGlesRenderer& operator=(CaveGlesRenderer const&) = delete;

    // ---- any thread -----------------------------------------------------------------------
    void beginLoad(std::filesystem::path const& modDirectory); // idempotent
    void configure(bool titleEnabled, bool keepTitleResident);
    void requestTitleReplay();
    void requestGlRelease() { mReleaseRequested.store(true); }
    // Render thread: frees GL objects if a release was requested from another thread (mod disable).
    void serviceRelease();
    bool failed() const { return mStatus.load() == Status::Failed; }
    bool ready() const { return mGlReady.load(); }

    // ---- render thread ----------------------------------------------------------------------
    // Creates GL resources once the background load finished and streams title pages.
    // Returns true when the presentation may start (renderer is drawable, or has permanently failed).
    bool prepare();
    bool drawFrame(FrameParams const& params);
    bool renderFadeOverlay(float screenWidth, float screenHeight, float overlayAlpha);
    void destroy(bool contextLost);

private:
    enum class Status : int { Idle, Loading, Ready, Failed };

    struct LoadResult {
        bool ok = false;
        std::string error;
        CaveMeshCpu mesh;
        Rgba8Image atlas;
        std::optional<TitleAtlasInfo> title;
        std::string titleError;
    };
    static LoadResult loadAll(std::filesystem::path modDirectory);

    struct TitlePage {
        enum class State { Absent, Decoding, Decoded, Resident, Failed };
        State state = State::Absent;
        GLuint tex = 0;
        std::future<Rgba8Image> pending;
        Rgba8Image image;
        int rowsUploaded = 0;
    };

    // --- GL setup
    bool pollCpuLoad();
    bool initGl();
    GLuint buildProgram(const char* vs, const char* fs, const char* defines, const char* name);
    void releaseGl(bool contextLost);

    // --- drawing
    void drawCave(FrameParams const& p);
    void drawTitle(FrameParams const& p);
    void captureFade(float w, float h);
    void drawFullscreenFade(float alpha);

    // --- title streaming
    void pumpTitleStreaming();
    void requestTitlePage(int page);
    void evictTitlePagesExcept(int keepPage);
    void releaseTitlePage(TitlePage& page, bool contextLost);
    bool titlePagesWanted(int page) const;

    // lifecycle
    std::atomic<Status> mStatus{Status::Idle};
    std::atomic<bool> mGlReady{false};
    std::atomic<bool> mReleaseRequested{false};
    std::atomic<bool> mReplayRequested{false};
    std::atomic<bool> mTitleEnabled{true};
    std::atomic<bool> mKeepTitleResident{false};
    std::mutex mLoadMutex;
    std::future<LoadResult> mLoadFuture;
    std::filesystem::path mModDirectory;
    bool mLoadStarted = false;

    // CPU copies (kept so a lost EGL context can be rebuilt without touching the disk)
    CaveMeshCpu mMesh;
    Rgba8Image mAtlasImage;
    std::optional<TitleAtlasInfo> mTitleInfo;
    bool mCpuReady = false;

    // GL objects: cave
    EGLContext mContext = EGL_NO_CONTEXT;
    GLuint mProgOpaque = 0, mProgCutout = 0;
    GLint mLocMvpOpaque = -1, mLocLavaOpaque = -1, mLocMvpCutout = -1, mLocLavaCutout = -1;
    GLuint mCaveVao = 0, mCaveVbo = 0, mCaveIbo = 0, mAtlasTex = 0;
    GLenum mIndexType = GL_UNSIGNED_SHORT;
    // GL objects: title + fade (shared quad program)
    GLuint mQuadProg = 0, mQuadVao = 0, mQuadVbo = 0;
    GLint mLocRect = -1, mLocUv = -1, mLocAlpha = -1, mLocOpaque = -1;
    GLuint mFadeTex = 0;
    int mFadeW = 0, mFadeH = 0;

    // title streaming state
    std::vector<TitlePage> mPages;
    bool mTitleSettled = false;
    int mPriorityPage = -1;
    bool mWarnedTitle = false;

    // GL init retry policy
    int mInitAttempts = 0;
    std::chrono::steady_clock::time_point mNextInitAttempt{};
};

} // namespace animated_mojang::scene
