#pragma once
#include "GPUTimer.hpp"
#include "Graphics.hpp"
#include "OverlayStyle.hpp"
#include "Resolution.hpp"
#include "interaction/Gizmo.hpp"
#include "interaction/Tools.hpp"
#include "surfaces/Surface.hpp"
#include "world/Character.hpp"
#include "world/ObjectState.hpp"
#include "world/World.hpp"
#include "quests/Markers.hpp"
#include <GLES3/gl3.h>
#include <chrono>
#include <deque>
#include <map>
#include <optional>
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
    bool collisionWireDirty = false; // the debug collision wireframe waits for the debug view
    // RFC-0011 user placements: object assets (and their images) appended to the current
    // world after it became current. Stage stages everything not uploaded yet; continue
    // from the owner tick (GL context current, never in the frame). Instances of an asset
    // draw once its batches exist. stage returns false while another upload is pending.
    bool stageObjectAssets();
    bool continueObjectAssets(double milliseconds = 2, size_t byteBudget = 2 * 1024 * 1024); // true when idle
    bool objectAssetsPending() const { return bool(assets_); }
    void cancelObjectAssets();
    // Optional prepared, display-encoded opaque world colour. Native depth,
    // window buffers, glass, prompts and debug UI remain in this GL owner.
    GLuint externalWorldFbo = 0;
    bool worldOnly = false;
    bool warmShaders = true; // Offline benchmark can disable; runtime always warms.
    world::Json offloadState = {{"activeRenderer", "in-process"}, {"powerW", nullptr}};
    bool debug = false, captureRequested = false;
    // Render on demand: the owner sets this when every frame input is unchanged
    // since the last rendered frame. draw() then re-presents the cached native
    // frame (one output pass) instead of rendering the world again.
    bool reuseFrame = false;
    // Steady cadence: new frames on every paceInterval-th presentation when target_fps is
    // below the display rate, never at irregular moments.
    int paceInterval = 1;
    // Feel diagnostics: per presentation {steady ms, 0 new / 1 repeated / 2 idle, yaw, pitch, x, y, z}.
    std::deque<std::array<double, 7>> presentLog;
    bool srgbDecodeAllowed = true; // Tests force the two-copy fallback.
    // graphics.upscaler (IPC 0.16): "fsr1" = AMD FSR 1 EASU+RCAS when the world
    // renders below output size; "bilinear" = bilinear + bounded unsharp mask,
    // also used automatically without GLSL ES 3.1. HYPRUNE_UPSCALER overrides the
    // setting for diagnostics and A/B profiling.
    std::string upscaler = "fsr1";
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
    // RFC-0012 markers: the runtime that lays them out per frame (set by the owner) and
    // the last frame's layout for diagnostics. Drawn in present() (MarkerOverlay.cpp).
    const quests::MarkerRuntime *markers = nullptr;
    world::Json markerDiagnostics = world::Json::object();
    std::string font = "monospace";
    world::Json profile;
    static bool intelTier(const std::string &quality, const std::string &device) {
        return quality == "intel" || (quality == "auto" && device.find("Intel") != std::string::npos);
    }
    void applyGraphics(const world::Json &); // Deferred, owner EGL context current; no world re-upload.
    // world::CompressedFormat bits this GL context accepts (current context required).
    static unsigned compressedFormats();
    world::Json graphics = effectiveGraphics(graphicsDefaults(), "generic");
    bool intelQuality = false, effects = true;
    int msaa = 4;
    bool dynamicResolution = true;
    float logicalScale = 1; // output physical pixels per logical pixel
    float renderScale = 0; // zero = automatic
    int targetFPS = 60;
    float pointerX = .5f, pointerY = .5f;
    bool scopeVignette = false;
    // The aiming reticle, drawn crisp at native output resolution in present(). Sizes in
    // logical px. State changes shape as well as colour (colour-blind safe).
    // NPCs (RFC-0010 prototype): skinned characters, drawn after the opaque world with
    // the world's shading. Set per tick by the runtime; skin = Character::pose() output.
    struct Npc {
        std::shared_ptr<const world::Character> character;
        Mat4 model = Mat4::identity();
        std::vector<Mat4> skin;
        std::optional<Vec3> accent; // recolours the material named `accent`
        bool moving = false;        // animating: keeps frames coming while in view
        // Probe light at its position (ambient up rgb, sun, ambient down rgb, sun visibility);
        // all zero: the shader's default light.
        std::array<float, 8> probe{};
    };
    std::vector<Npc> npcs;
    int npcsDrawn = 0;
    void releaseCharacters(); // GL context current
    // Uploads the characters in `npcs` that have no GPU copy yet (in the tick, GL state
    // saved and restored), so drawing never uploads inside the render pass. True if any.
    bool prepareCharacters();
    struct Reticle {
        enum Style { CrossDot, TDot, CircleDot, Dot, Cross, Chevron };
        enum State { Neutral, Target, Valid, OutOfRange, Invalid, Holding, Typing };
        int style = CrossDot, state = Neutral;
        float size = 18, thickness = 2, gap = 4, opacity = .9f, spread = 0;
        bool outline = true, visible = true;
        Vec3 color{.95f, .95f, .95f};
        std::string hint; // e.g. "18 m" when out of range
    } reticle;
    std::vector<interaction::Line> lines;
    // Position tool gizmo and placement ghost: anti-aliased screen-space primitives
    // drawn at native output resolution over the tone-mapped world, dimmed and dashed
    // where world geometry is in front. Readouts sit beside the pointer (normalized).
    interaction::GizmoFrame gizmo;
    std::vector<Label> readouts;
    std::map<unsigned, Mat4> transforms;
    // World 0.8 placed objects (RFC-0004): the animation clock starts when a world
    // becomes current and follows the steady clock; graphics.reduced_motion pins
    // every animation at its t = 0 pose.
    double animationSeconds() const;
    // A pinned world-animation clock in seconds: the offload worker draws at the time the
    // plugin sends; tests freeze both renderers at the same time. Unset: the steady clock.
    std::optional<double> animationClock;
    bool animating() const; // an animated object exists and motion is allowed
    // True while animating and an animated instance's swept bounds are in the
    // view frustum and not region-culled, or a moving NPC is in the frustum: the
    // owner keeps frames coming. The NPC half alone feeds diagnostics.
    bool animationVisible(const sim::Camera &, float aspect) const;
    bool npcAnimationVisible(const sim::Camera &, float aspect) const;
    // Machines prototype (HYPRUNE_MACHINES_PROTOTYPE): per-instance draw state set in the
    // tick (parallel to World::objectInstances; empty: authored), extra draws and labels.
    std::vector<world::ObjectDrawState> objectStates;
    std::vector<world::ObjectExtra> objectExtras;
    std::vector<world::WorldLabel> worldLabels;
    // An instance's swept bounds in the frustum and not region-culled (frame scheduling).
    bool objectInView(size_t instance, const sim::Camera &, float aspect) const;
    size_t worldLabelsDrawn = 0;
    bool materialDetails = true; // Also permits like-for-like benchmark comparisons.
    static constexpr int lightmapMipLevels = 2; // Deepest lightmap mip sampled (4 px bake gutters).
    uint64_t frames = 0;

  private:
    struct Batch {
        GLuint vao = 0, vbo = 0, ibo = 0;
        GLsizei count = 0; // indices when indexed, else vertices
        bool indexed = false;
        unsigned material = 0, node = 0;
        int asset = -1;      // world object asset (node is then the asset's node), else a scene batch
        int mesh = -1;       // the asset's mesh index (object batches)
        Vec3 center, lo, hi; // bounds of the pre-transformed vertices (asset space for objects)
    };
    struct PendingUpload {
        struct Job {
            size_t bytes;
            std::function<void()> run;
            std::string kind;
            std::function<bool()> ready = {}; // optional: false defers the job to a later tick
        };
        std::shared_ptr<const world::World> world;
        world::Json profile;
        decltype(world::World::environmentHigh) levels;
        std::vector<Batch> batches;
        std::vector<const world::Mesh *> meshes; // scene meshes, then object asset meshes
        std::vector<GLuint> textures, colors, samplers, dataSamplers;
        size_t gpuBytes = 0;
        GLuint materialUbo = 0;
        GLsizei materialStride = 0;
        GLuint cube = 0;
        GLsync fence = nullptr;
        std::vector<Job> jobs;
        size_t next = 0;
        // Object assets appended to the current world (RFC-0011 user placements): only
        // images from imageBase and asset meshes from assetBase; no scene, no environment.
        bool incremental = false;
        size_t imageBase = 0, assetBase = 0, assetEnd = 0;
    };
    using UploadAdder = std::function<void(size_t, std::function<void()>, std::string)>;
    void imageJobs(PendingUpload *, const UploadAdder &);
    static void materialConstants(PendingUpload *);
    void meshJobs(PendingUpload *, const UploadAdder &);
    std::unique_ptr<PendingUpload> assets_; // incremental object asset upload
    size_t uploadedAssets_ = 0;             // world object assets with batches
    std::unique_ptr<PendingUpload> pending_;
    double uploadMs_ = 0, uploadMaxMs_ = 0, uploadMaxJobMs_ = 0;
    std::string uploadSlowestJob_;
    size_t uploadBytes_ = 0, uploadSteps_ = 0;
    std::shared_ptr<const world::World> world_;
    std::vector<Batch> batches_;
    std::chrono::steady_clock::time_point animationEpoch_{};
    GLuint instanceVbo_ = 0; // per-frame instance records (model, probe lighting, paint, tint)
    static constexpr GLuint recordFloats = 32;
    size_t objectInstancesVisible_ = 0, objectDrawCalls_ = 0, objectInstancesDrawn_ = 0;
    bool npcVisible(const Npc &, const std::array<std::array<float, 4>, 6> &planes) const;
    bool instanceVisible(const world::ObjectInstance &, const std::array<std::array<float, 4>, 6> &planes,
                         const std::vector<bool> &areas, bool regions) const;
    // textures_/colorTextures_ name the same texture unless an image needs two
    // copies; dataSamplers_ differ from samplers_ only for sRGB skip-decode.
    std::vector<GLuint> textures_, colorTextures_, samplers_, dataSamplers_;
    bool srgbDecode_ = false;
    size_t gpuBytes_ = 0;
    GLuint materialUbo_ = 0; // one std140 Material range per world material
    GLsizei materialStride_ = 0;
    struct CharacterGPU {
        struct Primitive {
            GLuint vao = 0, vbo = 0, ibo = 0;
            GLsizei count = 0;
            unsigned material = 0;
        };
        std::vector<Primitive> primitives;
        std::vector<GLuint> textures;
        GLuint materials = 0; // MaterialStd140 ranges, one per material
    };
    std::map<const world::Character *, CharacterGPU> characterGPU_;
    GLuint skinned_ = 0, jointUbo_ = 0;
    CharacterGPU &characterGPU(const world::Character &);
    void drawNpcs(const std::array<std::array<float, 4>, 6> &planes, int tone, float exposure);
    GLuint scene_ = 0, tone_ = 0, reticle_ = 0, quadVao_ = 0, quadVbo_ = 0, screenVao_ = 0, repeat_ = 0, clamp_ = 0, mipClamp_ = 0;
    GLuint environment_ = 0, bloom_ = 0, sky_ = 0, native_ = 0, depthProgram_ = 0;
    GLuint inkComposite_ = 0, inkNormals_ = 0, inkProgram_ = 0, inkFbo_ = 0, inkNormal_ = 0, inkDepth_ = 0;
    size_t submittedBatches_ = 0, culledBatches_ = 0, frustumCulled_ = 0;
    int cubeLevels_ = 1;
    GLuint bloomFbo_[2]{}, bloomTexture_[2]{};
    GLuint msaa_ = 0, color_ = 0, depth_ = 0, resolve_ = 0, resolveTexture_ = 0;
    GLuint resolvedDepth_ = 0, nativeFbo_ = 0, nativeTexture_ = 0, nativeDepth_ = 0, upscale_ = 0;
    Resolution resolution_;
    int scaledWidth_ = 0, scaledHeight_ = 0;
    int width_ = 0, height_ = 0, samples_ = 4;
    int allocatedSamples_ = 0;
    std::deque<GLsync> frameFences_;
    uint64_t skipped_ = 0, backlog_ = 0, fpsFrames_ = 0, fpsBacklog_ = 0, idleFrames_ = 0;
    double deliveredFPS_ = 0;
    bool queuePressure_ = false;
    std::chrono::steady_clock::time_point fpsSince_{};
    GLuint collisionVao_ = 0, collisionVbo_ = 0;
    GLsizei collisionCount_ = 0;
    std::array<GPUTimer, 9> gpu_;
    std::array<double, 4> cpu_{};
    double frameMs_ = 0, renderMs_ = 0;
    std::chrono::steady_clock::time_point lastFrame_{};
    std::chrono::steady_clock::time_point lastPresent_{};
    double presentPeriodMs_ = 1000. / 60;
    int presentsSinceNew_ = 0;
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
    quests::MarkerLayout markerLayout_;
    GLuint markerProgram_ = 0, markerTextProgram_ = 0;
    void prepareMarkers(const sim::Camera &, std::span<const surfaces::Surface>, int, int);
    void drawMarkers(int, int);
    void destroyMarkers();
    void markerText(float x, float y, const std::string &, Vec3 color, float alpha, int w, int h, float scale);
    float markerTextWidth(const std::string &, float scale) const;
    void markerShape(float x, float y, int shape, float radius, Vec3 color, float alpha, int ring, bool dashed,
                     float angle, int w, int h, float extentX = 0, float extentY = 0);
    GLuint textProgram_ = 0, textTexture_ = 0, textVao_ = 0, textBuffer_ = 0;
    // World labels (machines prototype): a 64 px glyph atlas built with the world's
    // font setup (an upload job, never in the frame) and quads drawn in the world pass.
    struct LabelGlyph {
        float u0, v0, u1, v1, width, height, left, top, advance;
    };
    std::map<uint32_t, LabelGlyph> labelGlyphs_;
    float labelAscent_ = 48, labelLine_ = 64;
    GLuint labelProgram_ = 0, labelTexture_ = 0, labelVao_ = 0, labelVbo_ = 0;
    void labelAtlas();
    void drawWorldLabels(const Mat4 &vp);
    std::string loadedFont_;
    struct Glyph {
        float width = 0, height = 0, left = 0, top = 0, advance = 0, u = 0, v = 0;
    };
    std::array<Glyph, 128> glyphs_{};
    void textAtlas();
    void debugHUD(int, int);
    void text(float x, float y, const std::string &, Vec3 color, int w, int h, float scale = 1);
    void drawReticle(int w, int h, bool typing);
    void drawGizmo(int w, int h);
    void drawLabels(const std::vector<Label> &, int w, int h);
    GLuint gizmoProgram_ = 0, gizmoVao_ = 0, gizmoVbo_ = 0;
    void panel(float x, float y, float width, float height);
    void present(GLuint, int, int, bool);
    void debugGeometry(const sim::Camera &, std::span<const surfaces::Surface>);
    int warmStep_ = -1;
    GLuint warmFbo_ = 0, warmTexture_ = 0;
    GLsync warmFence_ = nullptr;
    double warmMs_ = 0, warmMaxMs_ = 0;
    bool continueWarmup();
    void init();
    GLuint easu_ = 0, rcas_ = 0, fsrInputFbo_ = 0, fsrInputTex_ = 0, fsrEasuFbo_ = 0, fsrEasuTex_ = 0;
    std::array<int, 4> fsrSize_{};
    bool fsrFailed_ = false, fsrActive_ = false;
    std::string fsrError_;
    bool fsrPrograms();
    std::map<int, GLuint> sceneVariants_; // world shader per graphics feature key (0 = failed/compiling)
    std::map<int, std::string> variantSources_;
    int featureKey() const;
    void specializeScene();
    GLuint worldProgram() const;
    void fsrTargets(int, int);
    void freeFsrTargets();
    void targets(int, int);
    void freeTargets();
    void freeWorld();
};
} // namespace hyprune::render
