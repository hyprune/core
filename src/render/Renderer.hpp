#pragma once
#include "GPUTimer.hpp"
#include "Graphics.hpp"
#include "OverlayStyle.hpp"
#include "Resolution.hpp"
#include "interaction/Tools.hpp"
#include "surfaces/Surface.hpp"
#include "world/World.hpp"
#include <GLES3/gl3.h>
#include <chrono>
#include <deque>
#include <functional>
namespace hyprune::render {
class Renderer {
  public:
    ~Renderer() = default;                             // Owner explicitly destroys with its EGL context current.
    void prepare(std::shared_ptr<const world::World>); // Offline synchronous convenience.
    void beginPrepare(std::shared_ptr<const world::World>);
    bool continuePrepare(double milliseconds = 2, size_t byteBudget = 2 * 1024 * 1024);
    void cancelPrepare(); // All upload methods require owner GL context, outside frame.

    void draw(GLuint target, int width, int height, const sim::Camera &, std::span<const surfaces::Surface>,
              bool typing);
    void destroy();
    void drawLoading(GLuint target, int width, int height);
    void drawHUD(GLuint target, int width, int height);
    void collisionChanged(); // Deferred owner tick after editor transforms.
    // Optional prepared, display-encoded opaque world colour. Native depth,
    // window buffers, glass, prompts and debug UI remain in this GL owner.
    GLuint externalWorldFbo = 0;
    bool worldOnly = false;
    bool warmShaders = true; // Offline benchmark can disable; runtime always warms.
    world::Json offloadState = {{"activeRenderer", "in-process"}, {"powerW", nullptr}};
    bool debug = false, captureRequested = false;
    double updateMs = 0, captureMs = 0, ipcMs = 0, gpuCaptureMs = -1;
    struct Pixels {
        int width = 0, height = 0;
        sim::Camera camera;
        std::vector<uint8_t> rgba;
        double readbackMs = 0, ageMs = 0;
        std::string worldId;
    };
    bool completed();  // Nonblocking GPU fence test; current owner GL context.
    Pixels snapshot(); // Owner event loop after the frame; never a compositor snapshot.
    world::Json timings() const;
    std::string device;
    struct Label {
        float x, y;
        std::string text;
        Vec3 color{1, 1, 1};
    };
    std::vector<Label> labels;
    world::Json overlayStyle = overlayDefaults(), waypoint = nullptr, route = world::Json::array(),
                toolPrompt = nullptr;
    bool showAreaMarkers = false, showInteractionHints = true;
    float useDistance = 6;
    void applyOverlayStyle(const world::Json &patch); // Deferred, validates before any upload.
    world::Json overlayDiagnostics = world::Json::object();
    std::string font = "monospace";
    world::Json profile;
    static bool intelTier(const std::string &quality, const std::string &device) {
        return quality == "intel" || (quality == "auto" && device.find("Intel") != std::string::npos);
    }
    void applyGraphics(const world::Json &); // Deferred, owner EGL context current; no world re-upload.
    world::Json graphics = effectiveGraphics(graphicsDefaults(), "generic");
    bool intelQuality = false, effects = true;
    int msaa = 4;
    bool dynamicResolution = true;
    float logicalScale = 1; // output physical pixels per logical pixel
    float renderScale = 0; // zero = automatic
    int targetFPS = 60;
    float pointerX = .5f, pointerY = .5f;
    bool scopeVignette = false;
    std::vector<interaction::Line> lines;
    std::map<unsigned, Mat4> transforms;
    bool materialDetails = true; // Also permits like-for-like benchmark comparisons.
    uint64_t frames = 0;

  private:
    struct Batch {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        unsigned material = 0, node = 0;
        Vec3 center;
    };
    struct PendingUpload {
        struct Job {
            size_t bytes;
            std::function<void()> run;
            std::string kind;
        };
        std::shared_ptr<const world::World> world;
        world::Json profile;
        decltype(world::World::environmentHigh) levels;
        std::vector<Batch> batches;
        std::vector<GLuint> textures, colors, samplers;
        GLuint cube = 0;
        GLsync fence = nullptr;
        std::vector<Job> jobs;
        size_t next = 0;
    };
    std::unique_ptr<PendingUpload> pending_;
    double uploadMs_ = 0, uploadMaxMs_ = 0, uploadMaxJobMs_ = 0;
    std::string uploadSlowestJob_;
    size_t uploadBytes_ = 0, uploadSteps_ = 0;
    std::shared_ptr<const world::World> world_;
    std::vector<Batch> batches_;
    std::vector<GLuint> textures_, colorTextures_, samplers_;
    GLuint scene_ = 0, tone_ = 0, quadVao_ = 0, quadVbo_ = 0, screenVao_ = 0, repeat_ = 0, clamp_ = 0;
    GLuint environment_ = 0, bloom_ = 0, sky_ = 0, native_ = 0, depthProgram_ = 0;
    GLuint inkNormals_ = 0, inkProgram_ = 0, inkFbo_ = 0, inkNormal_ = 0, inkDepth_ = 0;
    size_t submittedBatches_ = 0, culledBatches_ = 0;
    int cubeLevels_ = 1;
    GLuint bloomFbo_[2]{}, bloomTexture_[2]{};
    GLuint msaa_ = 0, color_ = 0, depth_ = 0, resolve_ = 0, resolveTexture_ = 0;
    GLuint resolvedDepth_ = 0, nativeFbo_ = 0, nativeTexture_ = 0, nativeDepth_ = 0, upscale_ = 0;
    Resolution resolution_;
    int scaledWidth_ = 0, scaledHeight_ = 0;
    int width_ = 0, height_ = 0, samples_ = 4;
    int allocatedSamples_ = 0;
    std::deque<GLsync> frameFences_;
    uint64_t skipped_ = 0, backlog_ = 0, fpsFrames_ = 0, fpsBacklog_ = 0;
    double deliveredFPS_ = 0;
    bool queuePressure_ = false;
    std::chrono::steady_clock::time_point fpsSince_{};
    GLuint collisionVao_ = 0, collisionVbo_ = 0;
    GLsizei collisionCount_ = 0;
    std::array<GPUTimer, 8> gpu_;
    std::array<double, 4> cpu_{};
    double frameMs_ = 0, renderMs_ = 0;
    std::chrono::steady_clock::time_point lastFrame_{};
    sim::Camera frameCamera_;
    std::string frameWorld_;
    bool frameTyping_ = false;
    GLuint iconTexture_ = 0;
    int iconWidth_ = 0, iconHeight_ = 0;
    struct Icon {
        float x, y;
        int index;
        Vec3 color;
    };
    std::vector<Icon> icons_;
    void icon(const Icon &, int, int);
    void worldOverlays(const sim::Camera &, std::span<const surfaces::Surface>, int, int);
    GLuint textProgram_ = 0, textTexture_ = 0, textVao_ = 0, textBuffer_ = 0;
    std::string loadedFont_;
    struct Glyph {
        float width = 0, height = 0, left = 0, top = 0, advance = 0, u = 0, v = 0;
    };
    std::array<Glyph, 128> glyphs_{};
    void textAtlas();
    void debugHUD(int, int);
    void text(float x, float y, const std::string &, Vec3 color, int w, int h, float scale = 1);
    void panel(float x, float y, float width, float height);
    void present(GLuint, int, int, bool);
    void debugGeometry(const sim::Camera &, std::span<const surfaces::Surface>);
    int warmStep_ = -1;
    GLuint warmFbo_ = 0, warmTexture_ = 0;
    GLsync warmFence_ = nullptr;
    double warmMs_ = 0, warmMaxMs_ = 0;
    bool continueWarmup();
    void init();
    void targets(int, int);
    void freeTargets();
    void freeWorld();
};
} // namespace hyprune::render
