#include "control/Control.hpp"
#include "interaction/CaptureNotes.hpp"
#include "interaction/Config.hpp"
#include "interaction/Content.hpp"
#include "interaction/Hands.hpp"
#include "interaction/InputFile.hpp"
#include "interaction/InputQueue.hpp"
#ifdef HYPRUNE_LIVE_BUILD
#include "../../../dev/live/Bootstrap.hpp"
#endif
#include "interaction/DebugTool.hpp"
#include "interaction/Gizmo.hpp"
#include "interaction/Placement.hpp"
#include "interaction/Tools.hpp"
#include "quests/Markers.hpp"
#include "sim/ViewBob.hpp"
#include "surfaces/Stacking.hpp"
#include "world/Mounts.hpp"
#include "world/Objects.hpp"
#include "world/Occlusion.hpp"
#include "world/Catalog.hpp"
#include <format>
#include <regex>
#include <sys/inotify.h>
#include <sys/random.h>
#include "world/Spatial.hpp"
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/LayerState.hpp>
#include <hyprland/src/desktop/state/ViewState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/layout/target/WindowTarget.hpp>
#include <hyprland/src/managers/KeybindManager.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/pointer/PointerController.hpp>
#include <hyprland/src/pointer/PointerManager.hpp>
#include <hyprland/src/protocols/LayerShell.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprutils/memory/UniquePtr.hpp>
#include <sstream>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#include "Capture.hpp"
#include "platform/RenderPass.hpp"
#include "hud/Telemetry.hpp"
#include "interaction/Resize.hpp"
#include "ipc/Bridge.hpp"
#include "ipc/Protocol.hpp"
#include "ipc/Text.hpp"
#include "offload/Client.hpp"
#include "offload/Characters.hpp"
#include "offload/Machines.hpp"
#include "offload/Objects.hpp"
#include "offload/Supervisor.hpp"
#include "render/FrameExport.hpp"
#include "render/GLState.hpp"
#include "sim/Cinema.hpp"
#include "sim/Companion.hpp"
#include "sim/Npc.hpp"
#include "sim/NpcMotion.hpp"
#include "interaction/Walkability.hpp"
#include "world/Paths.hpp"
#include "world/Machines.hpp"
#include "world/Character.hpp"
#include "offload/Pci.hpp"
#include "render/PNG.hpp"
#include "render/Renderer.hpp"
#include "render/Watermark.hpp"
#include "replay/Session.hpp"
#include <fcntl.h>
#include <fstream>
#include <future>
#include <linux/input-event-codes.h>
#include <unistd.h>
namespace hyprune::platform {
using Json = nlohmann::json;
namespace {
HANDLE handle = nullptr;
struct Runtime {
    render::Renderer renderer;
    offload::Client offload;
    // World objects in the offload worker: what it has (sender mirror), the package's
    // asset count and where each later merged asset came from (catalog objects).
    offload::ObjectSender offloadObjects;
    offload::MachineSender offloadMachines; // machine draw state (offload/Machines.hpp)
    std::vector<offload::AssetSource> mergedAssets;
    size_t packageAssets = 0;
    uint64_t worldLoads = 0; // a reload of the same world merges catalog assets anew: new worker
    std::string offloadLocalReason() const;
    Json offloadRequest, offloadPolicy = Json::object(), offloadChanges = Json::object();
    Json offloadDraws = Json::object(); // frames drawn while connected: shown / stale / missing, by front age (10 ms bins)
    uint64_t offloadGeneration = 0;
    bool renderedOffload = false;
    sim::Camera displayedCamera;
    const sim::Camera &pointerCamera() const { return renderedOffload ? displayedCamera : camera; }
    void offloadTick();
    // RFC-0012 (Quests.inc): markers, the TAB-tap reveal and quest facts for hypruned.
    quests::MarkerRuntime markers;
    int64_t nextFacts = 0;
    std::string markerMode() const;
    void questsTick();
    // stage: 0 the in-between pump (receive and import), 1 a frame tick's start (promote the
    // next frame), 2 the frame tick once the camera and scene are final (send its pose).
    void offloadPump(int stage = 0);
    void routeLineTick();
    void waypointArrival();
    Json routeDiagnostics();
    std::string routeLineKey, routeTailKey; // the feet cell and the route the drawn line was laid for
    std::vector<Vec3> routeTail;            // the line from the route's first point on
    SP<CEventLoopTimer> offloadTimer;
    uint64_t offloadDeferred = 0;
    Json performance() const;
    Capture capture;
    surfaces::Layout layout;
    interaction::Hands hands;
    interaction::Input input;
    replay::Session session;
    bool replayDispatch = false;
    Json replayConfig, replayInput, replayEditor;
    Json sessionState() const;
    Json sessionInitial() const;
    void restoreSession(const Json &);
    uint64_t inputClock() const;
    std::vector<interaction::Action> inputEvent(const std::string &, const std::string &, unsigned, bool, uint64_t, float = 0, bool = false);
    Json sessionMethod(const std::string &, const Json &);
    void stopReplay();
    void replayBeforeTick();
    void replayAfterTick();
    interaction::InputFile inputFile;
    interaction::Tools editor;
    std::filesystem::path editorFile;
    Json lastEditor, lastPublished, captureRequest = nullptr;
    std::deque<Json> recentIPC, recentEvents;
    std::chrono::steady_clock::time_point nextPublish{}, nextProjection{};
    Json legacyProjection = nullptr;
    std::string projectionWorld, projectionOutput;
    render::GPUTimer captureGPU;
    unsigned modifiers = 0;
    bool debug = false, lookHeld = false, capturePending = false;
    std::filesystem::path captureDirectory;
    // Capture writes and their notes, one at a time in order; at most four snapshots wait.
    interaction::SerialJobs captureJobs;
    std::shared_ptr<std::atomic<int>> captureSnapshots = std::make_shared<std::atomic<int>>(0);
    int64_t lastCaptureStamp = 0;
    std::deque<std::string> captureIds; // this plugin load's captures, newest last (bounded)
    std::string relativeSurface;
    float localX = .5f, localY = .5f;
    std::string inputMode() const;
    void applyInput(const Json &);
    Json steals(const Json &) const;
    void acceptCapture(const std::string &, unsigned, bool);
    void action(interaction::Action);
    void useAim(bool zoom);
    void leaveInteraction();
    void resumeHeldMovement();
    bool wasExclusive = false;
    void debugCapture();
    void toolClick(const std::string &action);
    void toolsTick();
    std::optional<interaction::WorldHit> worldHit();
    void saveEditor();
    std::optional<std::pair<std::string, interaction::WorldHit>> objectAim();
    std::optional<std::pair<std::string, interaction::WorldHit>> meshAim();
    std::vector<Triangle> originalCollision;
    std::map<unsigned, Mat4> objectModels;
    interaction::Transform selectedTransform, dragStart;
    std::optional<float> dragFloor; // the floor an object rested on when a gizmo drag began (scale settles on it)
    int dragPixelWidth = 0, dragPixelHeight = 0;
    Vec3 dragPoint{}, dragNormal{};
    bool objectCarry = false;
    float objectDistance = 0;
    // Gizmo 2.0 (Gizmo.inc). Object edits preview during a drag or carry and are
    // committed (with an undo record) on release; collision follows commits only.
    interaction::Gizmo gizmo;
    interaction::Part gizmoHover = interaction::Part::None;
    struct ObjectPreview {
        bool active = false, valid = true, startSupported = true;
        std::set<std::string> waived; // soft rules the start pose already broke
        interaction::Transform pose, candidate;
        interaction::PlacementVerdict verdict;
    } preview;
    Vec3 dragPivot{};
    Json editBefore = nullptr;
    uint64_t objectRevision = 0;
    std::set<uint32_t> gizmoKeys;
    std::optional<interaction::Box> windowBlocked;
    std::string ghostFor;
    std::vector<std::pair<Vec3, Vec3>> ghostLines;
    std::map<size_t, Mat4> instanceOverrides;
    bool gizmoObjects = false;
    std::vector<interaction::Box> gizmoKeepClear;
    interaction::GizmoView gizmoView();
    std::pair<float, float> gizmoPointer(const interaction::GizmoView &);
    interaction::Box selectionBox(const interaction::Transform &);
    Vec3 selectionPivot(const interaction::Transform &);
    std::vector<Triangle> selectionTriangles(const interaction::Transform &);
    interaction::PlacementScene placementScene();
    interaction::PlacementVerdict placementCheck(const interaction::Transform &, const std::set<std::string> &waived = {});
    std::set<std::string> waivedRules(const interaction::Transform &);
    Json objectRecord(const interaction::Transform &);
    Json currentRecord(const std::string &worldId, const std::string &id);
    void writeObjectRecord(const std::string &worldId, const std::string &id, const Json &record);
    bool commitObject(const interaction::Transform &, const std::string &what);
    Json windowPose(const Capture::Entry &);
    void resizeClient(Capture::Entry &, surfaces::Surface &, double width, double height);
    void applyWindowPose(Capture::Entry &, const Json &pose);
    void applyUndo(bool redo);
    void dropSelection(bool align);
    std::optional<std::pair<std::string, interaction::WorldHit>> instanceAim();
    std::map<size_t, Mat4> applyInstanceModels();
    void previewObject(const interaction::Transform &, bool carry);
    void commitPreview();
    void gizmoConfigure();
    bool gizmoBegin();
    interaction::Transform gizmoTransform(const interaction::Gizmo::Result &);
    void gizmoUpdate();
    void gizmoRelease(bool cancel);
    void cancelGizmo();
    bool gizmoKey(const std::string &key, unsigned mods, bool down, uint32_t code);
    // Tool 3 (Paths.inc): the point or segment under the reticle, a point being dragged.
    struct PathDrag {
        std::string path;
        int point;
        Json before;
    };
    std::optional<interaction::PathPick> pathHover;
    std::optional<PathDrag> pathDrag;
    bool showAllPaths = false;
    bool pathsVisible() const;
    std::optional<Vec3> pathTarget();
    void pathTick();
    bool routePath(const std::string &id) const;
    bool walkablePoint(Vec3);
    void refuseUnwalkable();
    bool pathTargetWalkable = true; // tool 3: the point under the reticle suits the active route
    void pathEdit(const std::string &id, const Json &before);
    void pathPress();
    void pathRelease(bool cancel);
    void pathRemove();
    void pathDraw();
    Json pathDiagnostics() const;
    // A Ctrl press held back in tool 4 (Gizmo.inc): its actions run after a short delay
    // unless an undo/redo chord consumes it; its release is dropped if they never ran.
    struct PendingCtrl {
        uint32_t code = 0;
        std::vector<interaction::Action> actions;
        std::chrono::steady_clock::time_point since;
        bool fired = false, consumed = false;
    };
    std::optional<PendingCtrl> pendingCtrl;
    bool deferCtrl(const std::string &key, bool down, uint32_t code, const std::vector<interaction::Action> &actions);
    void ctrlTick();
    void ghostEdges();
    void gizmoDraw();
    Json gizmoDiagnostics();
    // Tool 9, the debug tool (DebugTool.inc): the shown layer's items, how they were built,
    // and the info card of the item Primary inspected (by id and type, kept across rebuilds).
    std::vector<interaction::DebugItem> debugItems;
    struct DebugBuilt {
        int layer = -1;
        const world::World *world = nullptr;
        uint64_t revision = 0;
        Vec3 eye{};
    } debugBuilt;
    std::vector<uint8_t> debugKinds; // classifyCollision, per world and collision revision
    const world::World *debugKindsWorld = nullptr;
    uint64_t debugKindsRevision = 0;
    std::vector<std::string> debugCard;
    std::string debugSelected;
    int debugCardLayer = -1;
    Vec3 debugCardAt{};
    double debugBuildMs = 0;
    bool debugToolShown() const;
    interaction::DebugInputs debugInputs();
    void debugToolDraw();
    void debugInspect();
    Json debugToolDiagnostics();
    Json debugToolState() const;
    void projectCursor();
    // Frame-time diagnostics (HYPRUNE_FRAME_LOG=<file>, development): JSON lines per
    // presentation, tick (with section times), renderer draw and IPC method. Buffered in
    // memory and written at the end of a tick, never in the render pass.
    struct FrameLog {
        std::FILE *file = nullptr;
        std::map<std::string, double> sections;
        std::vector<std::string> pending;
        std::vector<std::string> marks;
        double renderStart = 0;
        static double now() {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        void line(const Json &j) {
            if (file && pending.size() < 100000)
                pending.push_back(j.dump());
        }
        void flush() {
            if (!file)
                return;
            for (auto &l : pending)
                std::fputs((l + "\n").c_str(), file);
            std::fflush(file);
            pending.clear();
        }
    } frameLog;
    struct Timed {
        FrameLog *log;
        std::string name;
        double start;
        ~Timed() {
            if (log && log->file)
                log->sections[name] += FrameLog::now() - start;
        }
    };
    Timed timed(std::string name) { return {frameLog.file ? &frameLog : nullptr, std::move(name), frameLog.file ? FrameLog::now() : 0}; }
    // RFC-0011 P1 objects (Objects.inc): the catalog (world package, libraries, user
    // folder), user placements and authored overrides as world object instances, pocket
    // items for tool 5 and the IPC 0.22 methods.
    world::CatalogRoots catalogRoots;
    world::ScanCache scanCache;
    std::shared_ptr<const world::CatalogSnapshot> catalog = std::make_shared<world::CatalogSnapshot>();
    std::vector<world::CatalogEntry> packageCatalog;
    uint64_t catalogRevision = 0;
    std::future<world::CatalogSnapshot> catalogScan;
    bool catalogDirty = true;
    std::chrono::steady_clock::time_point catalogQuiet{};
    int catalogWatch = -1;
    std::map<int, std::filesystem::path> catalogWatches;
    struct LibraryJob {
        world::InstallResult installed;
        world::CatalogSnapshot snapshot;
    };
    std::future<LibraryJob> libraryInstall;
    struct AssetSlot {
        std::string error;
        int index = -1; // world->objectAssets index once merged
        world::CatalogEntry entry;
        std::future<std::shared_ptr<world::World>> load;
    };
    std::map<std::string, AssetSlot> assetSlots; // by ref, for the loaded world
    unsigned compressedFormats = 0;
    bool compressedKnown = false;
    size_t authoredInstances = 0, sceneCollision = 0;
    std::vector<unsigned> originalCollisionNodes;
    struct ObjectInfo {
        std::string id, source, ref, name, areaId, state = "active", rule, support;
        bool movable = false, locked = false, character = false;
        float scaleMin = .01f, scaleMax = 100;
        int index = -1; // world->objectInstances index, -1 when not instanced (missing, loading)
        Vec3 position{};
        std::array<float, 4> rotation{0, 0, 0, 1};
        float scale = 1;
        const world::CatalogEntry *entry = nullptr; // resolved catalog entry (user placements)
    };
    std::vector<ObjectInfo> objectInfos; // authored instances, then this world's user placements
    std::map<std::string, std::string> objectVerified; // placement id -> pose key checked at load
    std::set<std::string> objectNotices;              // one-shot notices (blocked, substituted, missing)
    struct PocketObject {
        std::string itemId, ref, name;
    };
    std::vector<PocketObject> pocketObjects;
    uint64_t pocketSerial = 0;
    bool objectsDirty = true;
    uint64_t builtObjects = ~0ull, instanceRevision = 0, instanceSignature = 0; // collision follows instanceRevision
    struct ObjectGhost {
        bool active = false, valid = false, surface = false;
        std::string ref, rule, message;
        int asset = -1;
        interaction::Transform pose;
        float distance = -1;
    } objectGhost;
    std::chrono::steady_clock::time_point protectedNotice{};
    // A committed object's probe light (64 rays) runs on a worker; its visibility regions
    // are set in the tick. Results apply to the instance with that id and model.
    struct RelightJob {
        const world::World *world = nullptr;
        std::string id;
        Mat4 model;
        std::future<world::ProbeLight> light;
    };
    std::vector<RelightJob> relights;
    void relight(size_t instance);
    void relightTick();
    void joinRelights(); // before anything changes what a probe reads
    void catalogInit();
    void catalogTick();
    void catalogWatchDirs();
    const world::CatalogEntry *catalogEntry(const std::string &ref, std::string *substitute = nullptr);
    Json catalogEntries();
    void objectsTick();
    void objectsSync();
    void objectsWorldLoaded();
    ObjectInfo *objectInfo(const std::string &id);
    ObjectInfo *selectedInfo();
    interaction::Transform objectPose(const std::string &id);
    int assetFor(const world::CatalogEntry &);
    interaction::PlacementVerdict objectVerdict(int asset, const interaction::Transform &, const std::string &support,
                                                std::optional<size_t> self, const std::set<std::string> &waived = {},
                                                bool player = true);
    std::optional<std::string> budgetRefusal(const world::CatalogEntry &, const std::string &exceptId = "");
    std::set<std::string> objectWaived(int asset, const interaction::Transform &start, std::optional<size_t> self);
    std::optional<interaction::Transform> placeAhead(const world::CatalogEntry &, int asset, bool surface, float *distance,
                                                     std::string *why);
    Json objectsState();
    Json pocketObjectsJson() const;
    std::optional<Json> objectsMethod(const std::string &method, const Json &params);
    std::string objectsProtocol = "0.1"; // the calling session's protocol, for objectsMethod
    std::string newPlacementId() const;
    std::string areaAt(Vec3) const;
    Json placementRecord(const ObjectInfo &, const interaction::Transform &);
    void objectEdit(const std::string &plainId, Json before, Json after);
    void objectRefused(const std::string &what, const interaction::PlacementVerdict &);
    void objectNotice(const std::string &code, const std::string &message, const std::string &once = "");
    bool pocketSelected() const;
    void placeFromPocket(bool surface);
    void pocketAimedObject();
    void objectGhostTick();
    void objectGhostDraw(interaction::GizmoFrame &, const interaction::GizmoView &);
    void scaleObject(const std::string &id, float steps);
    void resetObject(const std::string &id);

    Json config = interaction::defaultConfig(), configured = config;
    std::set<std::string> graphicsOverrides;
    Json graphicsBaseProfile = nullptr;
    interaction::InputFile graphicsFile{"graphics", Json::object(), [](const Json &patch) {
                                            interaction::configPatch(interaction::defaultConfig(),
                                                                     {{"graphics", patch}});
                                            return patch;
                                        }};
    // Player settings (IPC 0.19 player.get/set): persisted overrides win over Lua config.
    interaction::InputFile playerFile{"player", Json::object(), [](const Json &patch) {
                                          if (!patch.is_object())
                                              throw std::runtime_error("player settings must be an object");
                                          auto keys = interaction::playerSettingsKeys();
                                          for (auto &[key, value] : patch.items())
                                              if (std::find(keys.begin(), keys.end(), key) == keys.end())
                                                  throw std::runtime_error("unknown player setting: " + key);
                                          interaction::configPatch(interaction::defaultConfig(), {{"player", patch}});
                                          return patch;
                                      }};
    void applyPlayer(const Json &patch) { config["player"] = interaction::configPatch(configured, {{"player", patch}})["player"]; }
    Json playerSettings() const {
        Json defaults = Json::object(), effective = Json::object(), ranges = Json::object();
        auto all = interaction::defaultConfig()["player"];
        for (auto &key : interaction::playerSettingsKeys()) {
            defaults[key.get<std::string>()] = all[key.get<std::string>()];
            effective[key.get<std::string>()] = config["player"][key.get<std::string>()];
        }
        return {{"config", effective},
                {"defaults", defaults},
                {"overrides", playerFile.current},
                {"options", interaction::playerOptions()},
                {"ranges", interaction::playerRanges()},
                {"path", ipc::boundedText(playerFile.path.string(), ipc::PathLimit)},
                {"error", ipc::boundedText(playerFile.error, ipc::MessageLimit)}};
    }
    void applyGraphics(const Json &patch) {
        auto next = interaction::configPatch(configured, {{"graphics", patch}});
        config["graphics"] = next["graphics"];
        // graphics.npc_motion (IPC 0.23) wins over Lua npc.motion; null inherits it.
        config["npc"]["motion"] = config["graphics"]["npc_motion"].is_string() ? config["graphics"]["npc_motion"]
                                                                               : configured["npc"]["motion"];
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        renderer.applyGraphics(config["graphics"]);
        if (!renderer.profile.is_null() && !graphicsBaseProfile.is_null()) {
            renderer.profile["exposure"] = patch.contains("exposure") || graphicsOverrides.contains("exposure")
                                               ? next["graphics"]["exposure"]
                                               : graphicsBaseProfile["exposure"];
            renderer.profile["bloom"]["strength"] =
                patch.contains("bloom_strength") || graphicsOverrides.contains("bloom_strength")
                    ? next["graphics"]["bloom_strength"]
                    : graphicsBaseProfile["bloom"]["strength"];
            if (graphicsBaseProfile.contains("outline")) {
                renderer.profile["outline"] = render::effectiveOutline(graphicsBaseProfile, renderer.graphics);
                renderer.graphics["outline_width_px"] = renderer.graphics["outline_quality"] == "fast" ? Json(1) : renderer.profile["outline"]["widthPx"];
                renderer.graphics["outline_colour"] = renderer.profile["outline"]["colour"];
            }
            renderer.graphics["exposure"] = renderer.profile["exposure"];
            renderer.graphics["bloom_strength"] = renderer.profile["bloom"]["strength"];
        }
        baseFov = next["graphics"]["fov"];
        if (!zoomHeld && !control.photoHeld())
            camera.fov = baseFov;
    }
    bool outsidePlayable = false;
    bool superHeld = false, altHeld = false, noclip = false, freeCursor = false, layerCursor = false, zoomHeld = false,
         cursorWarpPending = false;
    float zoom = 1, baseFov = 65, lookX = 0, lookY = 0;
    sim::Intent smoothIntent;
    int tool = 1;
    std::map<std::string, std::string> toolPressActions;
    void toolInput(const std::string &, interaction::Action);
    Json toolContext() const {
        Json gestures = Json::array();
        for (auto [action, label] : {std::pair{"carry", "Carry"},
                                     {"resize", "Resize"},
                                     {"distance", "Push / pull"},
                                     {"rotate", "Rotate"},
                                     {"content.scale", "Content scale"},
                                     {"window.reset", "Reset"},
                                     {"carry.facing", "Face player / keep orientation"}}) {
            Json bindings = Json::array();
            for (auto &binding : input.config["modes"]["world"][action]) {
                auto t = interaction::parseBinding(binding);
                bool carrying =
                    std::string_view(action) == "carry.facing" && hands.gesture == interaction::Gesture::Carry;
                if ((carrying && (modifiers == t.mods || (modifiers & ~interaction::Super) == t.mods)) ||
                    (std::string_view(action) != "carry.facing" && (modifiers & interaction::Super) &&
                     t.mods == modifiers))
                    bindings.push_back(binding);
            }
            if (!bindings.empty())
                gestures.push_back({{"action", action}, {"label", label}, {"bindings", bindings}});
        }
        return {{"modifiers", modifiers}, {"override", bool(modifiers & interaction::Super)}, {"gestures", gestures}};
    }
    Json contentFeedback = nullptr;
    uint64_t contentSequence = 0;
    void readyNotice();
    int readyNotices = 0; // once per plugin load: init and the first inactive world load both ask
    std::string tagged, unplacedSelected;
    std::optional<surfaces::Surface> placementPreview();
    // Tool-mount snap target for a client being placed (carry, pocket, tagged).
    std::optional<std::pair<const Json *, surfaces::Surface>> mountTarget(const Capture::Entry &);
    std::string snapTarget; // anchor highlighted this tick (diagnostics)
    Json unplacedState();
    void contentScale(bool reset, float steps);

    uint32_t gestureButton = 0;
    std::set<uint32_t> swallowedButtons, swallowedKeys, nativeButtons, nativeKeys, freeNativeButtons, freeNativeKeys;
    std::map<uint32_t, std::string> inputNames;
    WP<CWLSurfaceResource> grabResource;
    std::string cursorState = "desktop";
    bool savedCursorHidden = false, cursorHidden = false;
    void cursor();
    void modeToggle();
    void useLayers();
    void toggleFreeCursor();
    bool freeWorldPointer(Vector2D) const;
    void freePointerPosition(Vector2D);
    void freePointerButton(IPointer::SButtonEvent);
    struct CursorReturn {
        sim::Camera camera;
        sim::Climbing climbing;
        std::string window, wall, relative;
        float x = .5f, y = .5f;
        bool fullscreen = false, use = false;
    } cursorReturn;
    void button(IPointer::SButtonEvent, bool super);
    void axis(IPointer::SAxisEvent);
    void handTick(float);
    void floatIntoWorld(PHLWINDOWREF);
    void moveWindow(const PHLWINDOW &, int workspace, bool tile);
    std::string resumeUse; // surface typed into when a window gesture started
    std::map<const void *, Vector2D> arrivalSizes; // size before a move into the room
    void motionTick(float);
    struct Handover {
        int phase = 0, workspace = 0;
        float elapsed = 0;
        sim::Camera before, from, to;
        bool animated = false;
    } handover;
    struct FullscreenState {
        std::vector<std::pair<PHLWINDOWREF, bool>> covering;
        int phase = 0;
        float elapsed = 0;
        std::string id;
        surfaces::Surface before, from, to;
    } fullscreen;
    void focusWall(const std::string &);
    void transitionTick(float);
    void restoreFullscreenWindows() {
        for (auto &[w, allowed] : fullscreen.covering)
            if (w)
                w->m_allowedOverFullscreen = allowed;
        fullscreen.covering.clear();
    }
    void routePointer(const std::string &, surfaces::Hit, uint32_t = 0);

    float pointerX = .5f, pointerY = .5f;
    std::string pendingArea;
    void refresh();
    void home(const Json &, bool travel);
    void unfocus();
    sim::Camera camera;
    control::Control control;
    sim::Walk walk;
    sim::Fly fly;
    sim::MotionStrategy *movement = &walk;
    sim::Climbing climbing;
    sim::ViewBob viewBob;
    // NPCs (RFC-0010, RFC-0011 P2; Npcs.inc): an agent per active character instance.
    struct NpcRun {
        std::string id, name, signature, path, state = "active", activity, seat;
        bool user = false, companion = false;
        unsigned asset = 0;
        sim::NpcAgent agent;
        std::shared_ptr<const world::Character> character;
        std::optional<Vec3> accent;
        Json interactions = Json::array();
        std::vector<Mat4> skin;
        world::Character::PoseInput posed;
        bool stillPosed = false, lit = false;
        Vec3 regionAt{1e9f, 1e9f, 1e9f}, lightAt{};
        // Hidden behind opaque geometry from the eye (world::characterOccluded), re-checked
        // when the eye or the character moves: no frames requested, not drawn.
        bool occluded = false;
        Vec3 occludedEye{1e9f, 1e9f, 1e9f}, occludedAt{1e9f, 1e9f, 1e9f};
        std::optional<std::future<world::ProbeLight>> light;
        std::array<float, 8> probe{};
        std::chrono::steady_clock::time_point published{};
    };
    std::vector<NpcRun> npcs;
    std::string npcState = "none", npcReason;
    std::array<uint64_t, 4> npcSynced{}; // instanceRevision, pathRevision, builtObjects, companionRevision agents were built from
    uint64_t npcRevision = 0, pathRevision = 0, npcProtected = 0;
    std::set<std::string> seatTaken;     // occupied seat anchors (refs)
    std::map<const world::Character *, world::Character::SeatFit> seatFits;
    struct Walkability {
        bool walkable = true;
        std::vector<std::string> problems;     // what no detour gets round
        std::vector<sim::NpcDetour> detours;   // ways round blocked stretches (RFC-0010 steering)
    };
    std::map<std::string, Walkability> walkCache;
    struct NpcDrawing {
        std::string pathId, use;
        Json spawn = nullptr;
        std::string bind;
    };
    std::optional<NpcDrawing> npcDrawing;
    struct NpcInteraction {
        std::string objectId, interactionId, type, label, panel;
        Json choices = Json::array(), lines = Json::array();
        int line = 0;
        std::chrono::steady_clock::time_point started{};
    };
    std::optional<NpcInteraction> npcInteraction;
    Json npcAimed = nullptr;
    Vec3 npcPlayer{};          // what the agents saw last tick (diagnostics)
    bool npcPresent = false;
    Json pathRecord(const std::string &ref) const;
    interaction::PlacementScene npcScene();
    const Walkability &routeWalkability(const Json &path, const interaction::Body &);
    Json pathsList();
    std::optional<sim::NpcSeat> resolveSeat(const std::string &ref, Vec3 near);
    bool reserveSeat(const std::string &ref);
    void npcSync();
    NpcRun *npcRun(const std::string &id);
    void npcWorldLoaded();
    sim::NpcMotionInput npcMotion();
    std::optional<std::pair<Vec3, std::vector<Vec3>>> screenFront(const Json &anchor, Vec3 from, const interaction::Body &,
                                                                  const interaction::PlacementScene &);
    std::string npcPaused();
    void npcTick(float dt);
    std::optional<std::pair<std::string, float>> characterAim();
    bool protectCharacter(const std::string &what, const char *action);
    Json interactionList(const NpcRun &) const;
    void beginInteraction(const std::string &objectId, const std::string &interactionId);
    void endInteraction();
    bool interactUse();
    void npcInteractionTick();
    Json npcsState();
    void npcPrompts();
    Json characterRecord(const world::CatalogEntry &, const std::string &id, const std::string &path, Vec3 at, float yaw,
                         const Json &settings);
    void requireRoute(const std::string &ref, const interaction::Body &, const std::string &except = "");
    interaction::Body bodyOf(const world::CatalogEntry &);
    std::optional<std::pair<Vec3, float>> standingSpot(const interaction::Body &, const Json &pose,
                                                       interaction::WalkProblem *why = nullptr);
    ipc::Error spotRefusal(const interaction::WalkProblem &, bool inFront);
    std::string spawnCharacter(const std::string &ref, const std::string &path, const Json &pose, const Json &settings);
    Json finishDrawing();
    void bindCharacter(const std::string &id, const Json &patch);
    std::optional<Json> npcsMethod(const std::string &method, const Json &params);
    // The companion (Companion.inc, RFC-0010 companions amendment, IPC 0.25). The chosen ref
    // is user state (companion.json, HYPRUNE_COMPANION_FILE); modes are per world (editor).
    // {ref?, pace?}: pace "run" when chosen in its E menu or companion.mode ("walk", the default,
    // is not written). The pace outlives companion.clear, so {pace} alone is valid.
    interaction::InputFile companionFile{"companion", Json::object(), [](const Json &j) {
                                             bool ok = j.is_object();
                                             if (ok)
                                                 for (auto &[k, v] : j.items())
                                                     ok = ok && ((k == "ref" && v.is_string() && v.get<std::string>().size() <= 256) ||
                                                                 (k == "pace" && (v == "walk" || v == "run")));
                                             if (!ok)
                                                 throw std::runtime_error("companion.json must be {ref?, pace?}");
                                             return j;
                                         }};
    sim::Companion companionBrain;
    std::string companionStatus = "none", companionReason, companionName;
    Json companionBlock = nullptr; // the chosen object's companion block
    interaction::Body companionBody;
    std::map<const world::Character *, std::string> companionChecked; // core's character check, first error
    uint64_t companionRevision = 0;
    Vec3 companionFeet{}, companionPublished{};
    float companionHeading = 0;
    bool companionCarry = false, companionFocused = false, companionMoving = false, companionInView = false;
    const char *companionLastStep = "none"; // diagnostics: how the last companion step ended
    float companionFocusRetry = 0;
    int companionFocusMoves = 0, companionFocusMisses = 0;
    int companionPops = 0; // teleports seen in view (diagnostics; never expected)
    size_t companionLine = 0;
    // Carrying a character (the companion or a user character; Companion.inc).
    std::string carried; // its id, or empty
    Vec3 carryFrom{};
    float carryHeading = 0, carryDistance = 2;
    struct CarryDrop {
        Vec3 at{};
        bool valid = false;
        std::string route;
        float s = 0, length = 0;
        std::vector<Vec3> samples;
    };
    CarryDrop carryDrop;
    std::string methodProtocol = "0.1"; // the peer protocol of the method being run
    Json companionCapability(const world::CatalogEntry &) const;
    std::string companionRef() const;
    Json companionConfig();
    std::string companionMode() const;
    void companionInject(std::vector<ObjectInfo> &);
    Json companionInteractions() const;
    std::vector<std::string> companionLines() const;
    void companionRecord(Json record);
    void companionBind(const Json &patch);
    void companionSetMode(const std::string &mode);
    void companionSetPace(const std::string &pace);
    std::string companionPace() const;
    void companionView(sim::CompanionView &, const interaction::PlacementScene &walk, const interaction::PlacementScene &stand);
    void companionPlanner(const interaction::PlacementScene &walk);
    interaction::PlacementScene companionWalkScene();
    struct CompanionPlannerScope { // clears the planner's scene callbacks when the scene goes
        sim::GridPlanner &planner;
        ~CompanionPlannerScope() { planner.stand = {}, planner.leg = {}; }
    };
    bool companionStep(NpcRun &, float dt, const sim::NpcWorld &, const std::string &paused);
    bool carriable(const std::string &id);
    interaction::Body carryBody(const std::string &id);
    bool characterPickUp();
    void characterCarryTick();
    void characterCarryDraw();
    void characterPutDown();
    std::optional<Json> companionMethod(const std::string &method, const Json &params);
    Json companionState();
    Json companionDiagnostics();
    Json npcDiagnostics();
    Json offloadCharacters() const;
    // Machines (RFC-0013 draft prototype): data-channel values drive every placed
    // instance of a machine asset: default bindings (source com.hyprune.sources.<instance
    // id>), the user bindings file machines.json (re-read on change) and the development
    // override HYPRUNE_MACHINES_PROTOTYPE. Synced and applied in the tick only.
    world::Machines machines;
    interaction::InputFile machinesFile{"machines", Json{{"instances", Json::object()}}, [](const Json &j) {
                                            if (!j.is_object() || !j.value("instances", Json::object()).is_object())
                                                throw std::runtime_error("machines.json: {\"instances\": {<instance id>: {...}}}");
                                            for (auto &[id, entry] : j.value("instances", Json::object()).items())
                                                if (!entry.is_object())
                                                    throw std::runtime_error("machines.json: instance " + id + " must be an object");
                                            return j;
                                        }};
    Json machinesDev = nullptr; // the development file, when it names the loaded world
    uint64_t machinesSyncedRevision = ~0ull, machinesSyncedAssets = 0;
    bool machinesStale = true;  // sync on the next tick (world load, bindings file change)
    std::set<std::string> machineHints; // instance IDs told which source they read
    void machinesLoad();
    void machinesTick(float dt);
    // What the reticle tells the player (IPC 0.19 interaction.reticle); distance/reach in m, -1 = none.
    struct ReticleView {
        std::string state = "hidden", action;
        float distance = -1, reach = -1;
        bool operator==(const ReticleView &) const = default;
    } reticle;
    void reticleTick();
    bool tooFar(float distance, const char *reachKey, const char *action = "place");
    void refuseTooFar(float distance, float reach, const char *action, const char *code);
    ReticleView reticleFlash;
    std::chrono::steady_clock::time_point reticleFlashUntil{};
    Vec3 motionLast{};
    float motionYaw = 0, motionSpeed = 0, motionTurn = 0; // smoothed m/s and rad/s, for the bob and reticle
    std::shared_ptr<world::World> world;
    std::unique_ptr<ipc::Bridge> ipc;
    PHLMONITORREF monitor;
    PHLWINDOWREF savedFocus;
    // Hold the return workspace alive even when it becomes empty/hidden.
    PHLWORKSPACE previousWorkspace, previousSpecialWorkspace;
    int worldWorkspace = 0;
    void returnToWorldWorkspace();
    bool active = false, typing = false, loading = false, loadCancelled = false;
    platform::RenderPass renderPass; // see RenderPass.hpp: survives PRE without POST
    bool renderPassLogged = false;
    void settleRenderPass();
    void renderPassLeaked();
    std::future<std::shared_ptr<world::World>> pendingWorld;
    std::shared_ptr<world::World> uploadingWorld;
    std::string pendingSpawn, pendingToggle, resolvedLocation;
    bool toggleAfterLoad = false;
    const char *loadStep = "load"; // the finishLoad phase, for a failure's log line
    std::chrono::steady_clock::time_point loadDeadline;
    double accumulator = 0;
    // Motion follows presentation, as frames are shown: a tick runs right after each
    // presented frame and moves by the time that really passed, so every frame advances
    // by its own interval (a free 16 ms timer beat against vblank: 0, 1 or 2 fixed steps
    // per shown frame). The timer only drives ticks while nothing is presented (idle).
    CHyprSignalListener presentedListener;
    PHLMONITORREF presentedMonitor;
    std::chrono::steady_clock::time_point lastPresented{};
    std::chrono::steady_clock::time_point presentedAt{}; // the backend's presentation time
    void framePresented(std::chrono::steady_clock::time_point);
    // Demo camera driver (Cinema.inc, nested dev hook): paths and follow shots moved per
    // presented frame.
    sim::cinema::Director cinema;
    std::chrono::steady_clock::time_point cinemaClock{};
    double cinemaVsync = 0; // virtual vblank (s), phase-locked to presents: the camera moves whole refresh intervals
    render::FrameExport frameExport; // every presented frame of the world's output, for recordings
    uint64_t exportPasses = 0;
    render::FrameProbe frameProbe;  // pacing measurements (Cinema.inc "probe")
    std::vector<double> probePresents; // presentation times (ms) while probing
    // While exporting, time is counted in presented frames: every tick moves the world (and
    // the camera, characters and animations) by one refresh interval per frame presented
    // since the last tick, so each exported frame advances by the same amount.
    uint64_t presents = 0, presentsTicked = 0;
    uint64_t offloadPresents = 0; // presents counted at the last offload frame tick
    double offloadClock = 0;      // the animation clock last given to the worker
    bool offloadClockSent = false;
    std::chrono::steady_clock::time_point offloadFrameTick{};
    uint64_t exportUnticked = 0; // exported frames rendered without a tick since the last one (camera held, then doubled)
    double exportAnimation = 0;
    bool exportPinnedAnimation = false;
    double refreshInterval();
    void exportStart(const std::string &path, int width, int height, size_t queueBytes, int every = 1);
    void exportStop();
    bool cinemaCollide = false, cinemaTracing = false;
    std::vector<std::array<double, 8>> cinemaTrace; // t, dt, tick dt, eye, yaw, pitch
    std::optional<sim::cinema::Subject> cinemaSubject(const std::string &);
    void cinemaTick(float dt);
    Json cinemaCommand(const Json &);
    std::string focused, focusedParent, lastError, grabbed;
    std::set<uint32_t> buttons, appKeys;
    WP<CWLSurfaceResource> appKeyResource;
    void cancelGrab();
    std::set<uint32_t> keys;
    SP<CEventLoopTimer> timer;
    std::vector<CHyprSignalListener> listeners;
    CFunctionHook *motionHook = nullptr, *cursorHook = nullptr, *keyboardHook = nullptr;
    WP<IKeyboard> eventKeyboard;
    WP<IPointer> eventPointer;
    CFunctionHook *buttonHook = nullptr, *axisHook = nullptr;
    std::optional<bool> pendingCursorHide;
    uint64_t deferred = 0, tickDeferred = 0;
    std::vector<uint64_t> inputDeferred;
    interaction::InputQueue inputQueue;
    void runInput(uint64_t upTo);
    void scheduleInput(uint64_t upTo);
    void deferInput(std::function<void()>);
    // Windows taken into the world across a world load, until the new capture re-keys them.
    std::vector<std::pair<PHLWINDOWREF, int>> leftHomes;
    // The workspace each output showed last, so a switch knows what it leaves.
    std::map<const void *, int> shownWorkspaces;
    void settleWorkspaceSwitch(const PHLWORKSPACE &);
    std::chrono::steady_clock::time_point lastTick = std::chrono::steady_clock::now();
    std::filesystem::path installed;
    struct Binding {
        int pid;
        std::string start;
        wl_client *client;
        std::string lease;
        std::chrono::steady_clock::time_point seen;
    };
    std::map<std::string, Binding> bindings;
    Json locations = Json::array();
    void workspaceLocation();
    void deriveHomes();
    SP<CWLSurfaceResource> directorSurface();
    void publish();
    void publishHUD();
    Json hudZone = nullptr, hudTool = nullptr;
    uint64_t hudSequence = 0;
    std::chrono::steady_clock::time_point nextHUDPose{}, nextHUDFrame{};
    void toggle(const std::string &output = "");
    void exit();
    void tick();
    // Render on demand: a world frame is a function of these inputs. While they
    // are unchanged, tick schedules nothing and a compositor-initiated repaint
    // re-presents the cached native frame.
    std::vector<surfaces::Surface> frameSurfaces();
    uint64_t frameKey(std::span<const surfaces::Surface>);
    bool frameForced();
    // World 0.8 objects: animation keeps frames coming while an animated object
    // can be on screen (set each tick; false on the desktop, output off or loading).
    bool animationFrames = false;
    uint64_t renderedKey = 0;
    std::vector<uint64_t> frameParts;    // frameKey part hashes (diagnostics)
    std::map<std::string, uint64_t> frameCauses; // first changed part per key change
    void focus(const std::string &, bool keyboard, bool enterUse = true);
    void load(const std::string &, const std::string &spawn = "");
    std::optional<Json> method(const std::string &, const Json &);
    void finishLoad();
};
std::unique_ptr<Runtime> runtime;
using Move = void (*)(void *, const Vector2D &);
Move originalMove = nullptr;
using HideCursor = void (*)(void *, bool);
HideCursor originalHideCursor = nullptr;
void cursorHide(void *self, bool requested) {
    if (!runtime || !runtime->active) {
        originalHideCursor(self, requested);
        return;
    }
    bool pending = runtime->pendingCursorHide.has_value();
    runtime->pendingCursorHide = requested;
    if (pending)
        return;
    runtime->deferInput([self] {
        if (!runtime->pendingCursorHide)
            return;
        bool hide = *runtime->pendingCursorHide;
        runtime->pendingCursorHide.reset();
        if (runtime->active && (runtime->cursorState == "reticle" || runtime->cursorState == "virtual"))
            hide = true;
        if (hide != runtime->cursorHidden) {
            originalHideCursor(self, hide);
            runtime->cursorHidden = hide;
        }
    });
}
bool locked() {
    return g_pSessionLockManager && g_pSessionLockManager->isSessionLocked();
}
void error(const std::exception &e) {
    if (runtime) {
        runtime->lastError = e.what();
        std::fprintf(stderr, "[hyprune] %s\n", e.what());
    }
}
template <class F> void safe(F f) {
    try {
        f();
    } catch (const std::exception &e) {
        error(e);
    } catch (...) {
        std::fprintf(stderr, "[hyprune] unknown callback exception\n");
    }
}
using KeyboardInput = void (*)(void *, const IKeyboard::SKeyEvent &, SP<IKeyboard>);
KeyboardInput originalKeyboardInput = nullptr;
void keyboardInput(void *self, const IKeyboard::SKeyEvent &event, SP<IKeyboard> keyboard) {
    // The cancellable bus runs before Hyprland switches the seat keyboard. Keep the
    // event source only for that synchronous call; native processing remains intact.
    auto previous = runtime->eventKeyboard;
    runtime->eventKeyboard = keyboard;
    runtime->markers.input(std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch()).count());
    originalKeyboardInput(self, event, keyboard);
    runtime->eventKeyboard = previous;
}
using MouseButtonInput = void (*)(void *, IPointer::SButtonEvent, SP<IPointer>);
using MouseAxisInput = void (*)(void *, IPointer::SAxisEvent, SP<IPointer>);
MouseButtonInput originalMouseButton = nullptr;
MouseAxisInput originalMouseAxis = nullptr;
void mouseButtonInput(void *self, IPointer::SButtonEvent event, SP<IPointer> pointer) {
    runtime->markers.input(std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch()).count());
    auto previous = runtime->eventPointer;
    runtime->eventPointer = pointer;
    originalMouseButton(self, event, pointer);
    runtime->eventPointer = previous;
}
void mouseAxisInput(void *self, IPointer::SAxisEvent event, SP<IPointer> pointer) {
    auto previous = runtime->eventPointer;
    runtime->eventPointer = pointer;
    originalMouseAxis(self, event, pointer);
    runtime->eventPointer = previous;
}
bool interactiveLayerAt(Vector2D pos) {
    if (!runtime || !runtime->monitor)
        return false;
    auto monitor = runtime->monitor.lock();
    auto hit = Desktop::viewState()->hitTest();
    Vector2D local;
    PHLLS found;
    if (hit.layerPopupSurfaceAt(pos, monitor, &local, &found) && found && found->m_layer >= 2)
        return true;
    for (size_t layer : {3, 2})
        if (hit.layerSurfaceAt(pos, &monitor->m_layerSurfaceLayers[layer], &local, &found))
            return true;
    return false;
}
bool layerKeyboardFocus() {
    if (!runtime || !runtime->monitor)
        return false;
    for (size_t layer : {3, 2})
        for (auto &surface : runtime->monitor->m_layerSurfaceLayers[layer])
            if (surface && surface->m_mapped &&
                surface->wlSurface()->resource() == g_pSeatManager->m_state.keyboardFocus)
                return true;
    return false;
}
bool ownsInput() {
    return runtime && runtime->active && !runtime->loading && !runtime->control.uiVisible() && !locked() &&
           runtime->monitor == Desktop::focusState()->monitor() && runtime->layout.desktop.is_null() &&
           runtime->fullscreen.phase != 2 && (!runtime->freeCursor || runtime->cursorWarpPending) &&
           !runtime->handover.phase;
}
void motion(void *self, const Vector2D &delta) {
    auto section = runtime ? runtime->timed("input.motion") : Runtime::Timed{nullptr, "", 0};
    if (runtime) // input activity for focus credit (a timestamp only)
        runtime->markers.input(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch()).count());
    if (runtime && runtime->session.playing && !runtime->replayDispatch) return;
    if (runtime && runtime->session.recording && ownsInput())
        runtime->session.event({{"type","motion"},{"dx",delta.x},{"dy",delta.y},{"mode",runtime->inputMode()}});
    if (runtime && runtime->active && runtime->control.uiVisible() && !runtime->captureRequest.is_null() &&
        runtime->captureRequest.value("status", "") == "waiting" &&
        runtime->captureRequest.value("action", "") == "look")
        runtime->deferInput([] { runtime->acceptCapture("MouseMove", runtime->modifiers, true); });
    if (runtime && runtime->active) {
        auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        runtime->inputEvent(runtime->inputMode(), "MouseMove", runtime->modifiers, true, now, float(delta.x));
    }
    if (runtime && runtime->active && runtime->control.uiVisible() && !runtime->directorSurface())
        return;
    if (ownsInput()) {
        if (runtime->editor.activeHandle >= 0) {
            runtime->pointerX =
                std::clamp(runtime->pointerX + float(delta.x / runtime->monitor->m_size.x), 0.f, .9999f);
            runtime->pointerY =
                std::clamp(runtime->pointerY + float(delta.y / runtime->monitor->m_size.y), 0.f, .9999f);
            return;
        }
        if (runtime->typing && runtime->config["input"]["typing_cursor"] == true) {
            auto mon = runtime->monitor.lock();
            if (auto plane = runtime->capture.plane(runtime->relativeSurface)) {
                runtime->localX += float(delta.x) / std::max(1, plane->pixelWidth);
                runtime->localY += float(delta.y) / std::max(1, plane->pixelHeight);
                runtime->projectCursor();
            } else {
                runtime->pointerX += float(delta.x) / float(mon->m_size.x);
                runtime->pointerY += float(delta.y) / float(mon->m_size.y);
            }
            if (!runtime->replayDispatch && (runtime->pointerX < 0 || runtime->pointerX >= 1 || runtime->pointerY < 0 || runtime->pointerY >= 1) &&
                runtime->buttons.empty()) {
                auto global =
                    mon->m_position + Vector2D{runtime->pointerX * mon->m_size.x, runtime->pointerY * mon->m_size.y};
                for (auto &m : State::monitorState()->monitors())
                    if (m != mon && global.x >= m->m_position.x && global.y >= m->m_position.y &&
                        global.x < m->m_position.x + m->m_size.x && global.y < m->m_position.y + m->m_size.y) {
                        runtime->freeCursor = true;
                        runtime->cursorWarpPending = true;
                        runtime->deferInput([global] {
                            runtime->cursor();
                            Pointer::pointerController()->warpTo(global, true);
                            runtime->cursorWarpPending = false;
                        });
                        return;
                    }
            }
            runtime->pointerX = std::clamp(runtime->pointerX, 0.f, .9999f);
            runtime->pointerY = std::clamp(runtime->pointerY, 0.f, .9999f);
            return;
        }
        auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        (void)now;
        bool mouseLook = runtime->lookHeld;
        for (auto &v : runtime->input.config["modes"]["world"]["look"]) {
            auto t = interaction::parseBinding(v);
            if (t.key == "MouseMove" && (!t.mods || t.mods == runtime->modifiers))
                mouseLook = true;
        }
        if (!mouseLook)
            return;
        runtime->lookX += float(delta.x) * runtime->config["player"]["look_sensitivity"].get<float>();
        runtime->lookY +=
            sim::Camera::lookPitch(float(delta.y), runtime->config["player"]["look_sensitivity"].get<float>(),
                                   runtime->input.config["invert_y"].get<bool>());
        return;
    }
    if (runtime && runtime->active && runtime->cursorWarpPending)
        return;
    if (runtime && runtime->replayDispatch) return;
    originalMove(self, delta);
}
Vec3 pointerRay() {
    float aspect = runtime->monitor ? runtime->monitor->m_size.x / float(runtime->monitor->m_size.y) : 1;
    bool pointer = runtime->layerCursor || runtime->typing || runtime->editor.activeHandle >= 0;
    return runtime->pointerCamera().ray(pointer ? runtime->pointerX : .5f, pointer ? runtime->pointerY : .5f, aspect);
}
std::optional<std::pair<std::string, surfaces::Hit>> aim(bool backFaces = false) {
    std::optional<std::pair<std::string, surfaces::Hit>> best;
    for (auto &surface : runtime->capture.visible())
        if (auto h = surfaces::intersect(surface, runtime->pointerCamera().position, pointerRay(), true, backFaces);
            h && (!best || h->distance < best->second.distance))
            best = {{surface.id, *h}};
    if (best && runtime->world) {
        auto &world = *runtime->world;
        auto hit = interaction::raycast(runtime->pointerCamera().position, pointerRay(), world.collision, &world.collisionIndex);
        if (hit && hit->distance < best->second.distance - .01f)
            return std::nullopt;
    }
    return best;
}
void Runtime::renderPassLeaked() {
    if (renderPassLogged)
        return;
    renderPassLogged = true;
    std::fprintf(stderr, "[hyprune] compositor render pass ended without RENDER_POST (beginRender failed?); "
                         "recovered, further occurrences counted silently\n");
}
void Runtime::settleRenderPass() {
    // Hyprland returns without RENDER_POST when beginRender fails; a pass still
    // open at an event-loop entry point never ended (idle/timer callbacks never
    // run inside renderMonitor). Rule 2 holds: a real pass is never settled.
    if (renderPass.settle())
        renderPassLeaked();
}
// A switch to or from a world workspace (room, homes, world locations) is a world transition,
// not a desktop one: finish Hyprland's slide/fade at once, as its own instant switches do.
// The user's other workspaces keep their configured animation and no setting is changed,
// so unloading leaves nothing to restore.
void Runtime::settleWorkspaceSwitch(const PHLWORKSPACE &to) {
    auto output = to ? to->m_monitor.lock() : nullptr;
    if (!output || to->m_isSpecialWorkspace)
        return;
    int from = std::exchange(shownWorkspaces[output.get()], int(to->m_id));
    auto owned = surfaces::worldWorkspaces(config["windows"]["world_workspace"].get<int>(), layout.effectiveBindings(),
                                           locations);
    if (!owned.contains(int(to->m_id)) && !owned.contains(from))
        return;
    auto settle = [output = PHLMONITORREF{output}] {
        for (auto &workspace : State::workspaceState()->workspaces())
            if (workspace && !workspace->m_isSpecialWorkspace && workspace->m_monitor == output) {
                workspace->m_renderOffset->warp();
                workspace->m_alpha->warp();
            }
    };
    // Switches come from dispatchers and core's deferred work, outside rendering; if one
    // ever arrives inside a pass, settle on the next event-loop turn (rule 2).
    if (renderPass.active())
        deferInput(settle);
    else
        settle();
}
void Runtime::deferInput(std::function<void()> operation) {
    scheduleInput(inputQueue.push([operation = std::move(operation)] { safe(operation); }));
}
// Runs queued input up to `upTo`, oldest first; inside a render pass it waits for an idle
// callback. The tick runs everything queued before it steps.
void Runtime::runInput(uint64_t upTo) {
    settleRenderPass(); // an idle callback never runs inside renderMonitor
    if (renderPass.active())
        scheduleInput(upTo);
    else
        inputQueue.run(upTo);
}
void Runtime::scheduleInput(uint64_t upTo) {
    auto id = std::make_shared<uint64_t>(0);
    *id = g_pEventLoopManager->doLater([id, upTo] {
        if (!runtime)
            return;
        std::erase(runtime->inputDeferred, *id);
        runtime->runInput(upTo);
    });
    inputDeferred.push_back(*id);
}
void Runtime::cancelGrab() {
    if (!appKeys.empty()) {
        auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        if (appKeyResource && g_pSeatManager->m_state.keyboardFocus == appKeyResource)
            for (auto key : appKeys)
                g_pSeatManager->sendKeyboardKey(now, key, WL_KEYBOARD_KEY_STATE_RELEASED);
        appKeys.clear();
        appKeyResource.reset();
    }
    if (!buttons.empty() && grabResource) {
        auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        for (auto button : buttons)
            g_pSeatManager->sendPointerButton(now, button, WL_POINTER_BUTTON_STATE_RELEASED);
        g_pSeatManager->sendPointerFrame();
    }
    buttons.clear();
    grabbed.clear();
    grabResource.reset();
    hands.end();
    gestureButton = 0;
}
std::pair<int, std::string> processIdentity(int pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    std::getline(f, line);
    auto end = line.rfind(')');
    if (end == std::string::npos)
        return {-1, ""};
    std::istringstream rest(line.substr(end + 2));
    std::string value, start;
    int parent = -1;
    for (int field = 3; field <= 22 && rest >> value; field++) {
        if (field == 4)
            parent = std::stoi(value);
        if (field == 22)
            start = value;
    }
    return {parent, start};
}
SP<CWLSurfaceResource> Runtime::directorSurface() {
    for (auto &layer : Desktop::layerState()->layers()) {
        if (!layer->m_mapped || layer->m_monitor != monitor || layer->m_namespace != "hyprune-director")
            continue;
        auto resource = layer->m_layerSurface.lock();
        auto surface = resource ? resource->m_surface.lock() : nullptr;
        if (!surface)
            continue;
        for (auto &[actor, b] : bindings)
            if (surface->client() == b.client && layer->getPID() == b.pid && control.ownsLease(actor, b.lease))
                return surface;
    }
    return nullptr;
}
void Runtime::refresh() {
    if (renderPass.active())
        throw std::runtime_error("capture inside render pass");
    if (monitor && world) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        capture.spawnDistance = config["windows"]["spawn_distance"];
        capture.windowScale = config["windows"]["scale"];
        capture.worldWorkspace = worldWorkspace;
        capture.refresh(monitor.lock(), world.get(), &layout, &camera);
        std::erase_if(leftHomes, [&](auto &left) {
            if (!left.first || !left.first->m_isMapped)
                return true;
            for (auto &e : capture.entries)
                if (e.window == left.first) {
                    layout.homes.try_emplace(e.surface.id, left.second);
                    return true;
                }
            return false;
        });
        // Configure after the capture iteration completes: native layout changes
        // may reorder the compositor's window list or invoke other plugin hooks.
        for (auto window : std::exchange(capture.floatRequests, {}))
            deferInput([window] { runtime->floatIntoWorld(window); });
        for (auto window : std::exchange(capture.worldRequests, {}))
            deferInput([window] {
                // Into the world: natively onto the room workspace, where it floats. Only if it
                // is still on its way: a return in between keeps it home.
                if (!runtime->active || !window || !window->m_isMapped || !window->m_workspace ||
                    window->m_workspace->m_id == runtime->worldWorkspace)
                    return;
                auto entry = std::find_if(runtime->capture.entries.begin(), runtime->capture.entries.end(),
                                          [&](auto &e) { return e.window == window; });
                if (entry == runtime->capture.entries.end())
                    return;
                auto instance = entry->instance;
                instance.workspaceId = int(window->m_workspace->m_id);
                if (!runtime->layout.pulled(instance, runtime->world.get()))
                    return;
                runtime->moveWindow(window.lock(), runtime->worldWorkspace, false);
            });
        for (auto window : std::exchange(capture.tileRequests, {}))
            deferInput([window] {
                // Back on its home wall: restore the tiling it had before the world floated it.
                if (!runtime->active || !window || !window->m_isMapped || !window->m_isFloating)
                    return;
                auto w = window.lock();
                runtime->capture.worldFloated.erase(w.get());
                if (w->m_workspace && w->m_workspace->m_id == runtime->worldWorkspace)
                    return; // room windows stay floating
                auto tiled = Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_DISABLE, w);
                if (!tiled)
                    throw std::runtime_error("home re-tile rejected: " + tiled.error().message);
            });
        for (auto [window, logical] : std::exchange(capture.configureRequests, {}))
            deferInput([window, logical] {
                if (!runtime->active || !window || !window->m_isMapped || !window->m_target)
                    return;
                auto w = window.lock();
                // A transient relationship may arrive after the first map commit.
                if (w->parent() || w->x11Parent())
                    return;
                for (const auto &entry : runtime->capture.entries)
                    if (entry.window == w && !entry.parentId.empty())
                        return;
                if (!w->m_workspace || w->m_workspace->m_id != runtime->worldWorkspace || !w->m_isFloating)
                    return; // Never resize/float clients on other workspaces.
                auto resized = Config::Actions::resize(logical, false, w);
                if (!resized)
                    throw std::runtime_error("spawn resize rejected: " + resized.error().message);
                w->m_target->rememberFloatingSize(logical);
                w->sendWindowSize(true);
            });
    }
}
// Native workspace moves for the world: into the room (floated there by the next capture)
// or back to a wall's workspace, tiled. Never from a render pass: input, IPC and deferred
// work only.
void Runtime::moveWindow(const PHLWINDOW &w, int workspace, bool tile) {
    if (renderPass.active())
        throw std::runtime_error("workspace move inside render pass");
    if (!w || !w->m_isMapped || workspace <= 0)
        return;
    if (!w->m_workspace || w->m_workspace->m_id != workspace) {
        auto r = g_pKeybindManager->m_dispatchers.at("movetoworkspacesilent")(
            std::to_string(workspace) + ",address:" + std::format("0x{:x}", reinterpret_cast<uintptr_t>(w.get())));
        if (!r.success)
            throw std::runtime_error("workspace move failed");
    }
    if (tile && w->m_isFloating && workspace != config["windows"]["world_workspace"].get<int>()) { // the room floats
        capture.worldFloated.erase(w.get());
        auto tiled = Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_DISABLE, w);
        if (!tiled)
            throw std::runtime_error("client tiling rejected: " + tiled.error().message);
    }
}
void Runtime::floatIntoWorld(PHLWINDOWREF window) {
    // Every way into the world (moved or dragged in from another output, Director pull or
    // recall, carried off a home wall, placed or pocketed) leaves the client tiled where it
    // was; only clients opened in the room float on open. Float it at the size it had
    // before (capped to a sensible share of the output), on this output, before it is posed.
    if (!active || !window || !window->m_isMapped || !window->m_target || !monitor)
        return;
    auto w = window.lock();
    if (w->parent() || w->x11Parent() || w->m_isFloating || Fullscreen::controller()->isFullscreen(w) ||
        (!(w->m_workspace && w->m_workspace->m_id == worldWorkspace && w->m_monitor == monitor) &&
         std::none_of(capture.entries.begin(), capture.entries.end(), [&](auto &e) { return e.window == w; })))
        return; // room arrivals on this output, or windows the world already shows
    auto m = monitor.lock();
    Vector2D size = w->m_target->position().size();
    if (auto it = arrivalSizes.find(w.get()); it != arrivalSizes.end()) {
        size = it->second;
        arrivalSizes.erase(it);
    }
    // A full-output tile would be a wall-sized quad: a window covering more than half of
    // this output is scaled (same aspect) to 35% of its area; smaller windows keep their size.
    double area = std::max(1., size.x * size.y), output = double(m->m_size.x) * m->m_size.y;
    double fit = std::min({area > .5 * output ? std::sqrt(.35 * output / area) : 1., m->m_size.x / std::max(1., size.x),
                           m->m_size.y / std::max(1., size.y)});
    size = {std::max(160., std::round(size.x * fit)), std::max(100., std::round(size.y * fit))};
    auto floated = Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_ENABLE, w);
    if (!floated)
        throw std::runtime_error("world float rejected: " + floated.error().message);
    auto resized = Config::Actions::resize(size, false, w);
    if (!resized)
        throw std::runtime_error("world float resize rejected: " + resized.error().message);
    // A floating position remembered from another output would move it off this one.
    auto moved = Config::Actions::move(m->m_position + (m->m_size - size) / 2, false, w);
    if (!moved)
        throw std::runtime_error("world float move rejected: " + moved.error().message);
    w->m_target->rememberFloatingSize(size);
    w->sendWindowSize(true);
    capture.worldFloated.insert(w.get());
}
void Runtime::home(const Json &binding, bool travel) {
    if (!world || binding.at("worldId") != world->id)
        return;
    auto area = std::find_if(world->areas.begin(), world->areas.end(),
                             [&](auto &a) { return a.at("id") == binding.at("areaId"); });
    if (area == world->areas.end())
        throw ipc::Error(-32003, "bound area missing after world update");
    auto spawn = world::areaSpawn(*world, binding.at("areaId"));
    auto position = spawn.position + Vec3{0, 1.7f, 0};
    auto yaw = spawn.yaw, pitch = spawn.pitch;
    sim::Camera test = camera;
    test.position = position;
    sim::resolveCapsule(test, world->collision, &world->collisionIndex);
    if (length(test.position - position) > .02f)
        throw ipc::Error(-32005, "area arrival obstructed");
    typing = false;
    keys.clear();
    smoothIntent = {};
    lookX = lookY = 0;
    cancelGrab();
    pointerX = pointerY = .5f;
    layout.desktop = nullptr;
    camera.yaw = yaw;
    camera.pitch = pitch;
    if (travel)
        control.execute("travel.start",
                        {{"to", {{"position", {position.x, position.y, position.z}}}}, {"durationMs", 600}}, "core",
                        "com.hyprune.core", world.get(), camera, Json::array());
    else
        camera.position = position;
}
void Runtime::unfocus() {
    if (!layout.desktop.is_null() && !handover.phase && handover.animated) {
        handover.from = camera;
        handover.to = handover.before;
        handover.elapsed = 0;
        handover.phase = 2;
    }
    layout.desktop = nullptr;
    returnToWorldWorkspace();
    typing = false;
    keys.clear();
    smoothIntent = {};
    lookX = lookY = 0;
    cancelGrab();
    pointerX = pointerY = .5f;
    publish();
}
void Runtime::deriveHomes() {
    // Each loaded world's areas get default homes at workspace_base + slot.
    // Explicit binds, configured locations and the room workspace win.
    layout.workspaceBase = config["windows"]["workspace_base"];
    layout.reservedWorkspaces = {config["windows"]["world_workspace"].get<int>()};
    if (worldWorkspace)
        layout.reservedWorkspaces.insert(worldWorkspace);
    for (auto &entry : locations)
        layout.reservedWorkspaces.insert(entry.at("id").get<int>());
    layout.deriveDefaults(world.get());
}
void Runtime::workspaceLocation() {
    if (layerCursor) return;
    if (!active || !monitor || !monitor->m_activeWorkspace)
        return;
    auto id = monitor->m_activeWorkspace->m_id;
    if (!layout.binding(id, nullptr) &&
        std::none_of(locations.begin(), locations.end(), [&](auto &entry) { return entry.at("id") == id; })) {
        // Leaving every home (e.g. back to the room) re-arms travel to the same one.
        resolvedLocation.clear();
        return;
    }
    if (auto b = layout.binding(id, nullptr)) {
        std::string key =
            std::to_string(id) + ":" + b->at("worldId").get<std::string>() + ":" + b->at("areaId").get<std::string>();
        if (key == resolvedLocation)
            return;
        if (loading) {
            loadCancelled = true;
            return;
        }
        resolvedLocation = key;
        if (world && b->at("worldId") == world->id)
            home(*b, true);
        else {
            pendingArea = b->at("areaId");
            load(b->at("worldId"));
        }
        return;
    }
    for (const auto &entry : locations)
        if (entry.at("id") == id) {
            auto key = std::to_string(id) + ":" + entry.at("worldId").get<std::string>() + ":" +
                       entry.at("spawnId").get<std::string>();
            if (key == resolvedLocation)
                return;
            if (loading) {
                loadCancelled = true;
                return;
            }
            resolvedLocation = key;
            if (!world || world->id != entry.at("worldId").get<std::string>())
                load(entry.at("worldId"), entry.at("spawnId"));
            else {
                auto sp = world->spawn(entry.at("spawnId"));
                control.execute("travel.start",
                                {{"to", {{"position", {sp.position.x, sp.position.y + 1.7f, sp.position.z}}}}}, "core",
                                "com.hyprune.core", world.get(), camera, Json::array());
            }
            return;
        }
}
Json Runtime::performance() const {
    Json state = {{"profile", "unknown"}, {"powerW", nullptr}, {"capW", 0}, {"capSource", "profile"}, {"device", nullptr},
                  {"targetFPS", 0}, {"reason", "off"}, {"pid", nullptr}, {"generation", offloadGeneration}};
    state.update(offloadPolicy);
    state["mode"] = config["graphics"]["offload"];
    state["activeRenderer"] = renderedOffload ? "offload" : "in-process";
    state["status"] = renderedOffload ? "active" : offload.connected() ? "warming" : "local";
    if (renderedOffload) state["reason"] = "ready";
    // A worker the supervisor stopped (watchdog, policy) looks like transport EOF to
    // the client: report the supervisor's reason, not the symptom.
    if (!offload.error.empty()) {
        auto supervised = offloadPolicy.value("reason", std::string{});
        bool running = supervised.empty() || supervised == "admitted" || supervised == "warming" ||
                       supervised == "off" || supervised == "inactive";
        state["reason"] = running ? offload.error : supervised;
    }
    if (auto local = offloadLocalReason(); !local.empty() && config["graphics"]["offload"] != "off")
        state["reason"] = local;
    bool custom = false;
    for (auto &[key, value] : graphicsFile.current.items())
        if (key != "quality" && !value.is_null()) custom = true;
    state["preset"] = custom ? Json("custom") : config["graphics"]["quality"];
    state["requested"] = config["graphics"];
    state["effective"] = renderer.graphics;
    state["costsMs"] = renderer.timings()["gpuEffectsMs"];
    if (renderedOffload && offload.timings.is_object() && offload.timings.contains("gpuEffectsMs")) {
        auto worker = offload.timings.at("gpuEffectsMs");
        state["costsMs"]["depth_reconstruction"] = state["costsMs"]["upscale_sharpen"];
        for (auto key : {"world_shading_shared", "sky", "msaa_resolve", "bloom", "upscale_sharpen", "outline"})
            state["costsMs"][key] = worker.at(key);
        state["costsMs"]["transfer"] = offload.timings.value("transferGpuMs", Json(nullptr));
        state["effective"]["target_fps"] = offloadPolicy.value("targetFPS", 45);
    }
    if (offloadPolicy.value("capW", 0.) > 0)
        state["effective"]["offload_power_cap_w"] = offloadPolicy.value("capW", 0.);
    state["effective"]["offload"] = renderedOffload ? "on" : "off";
    state["workerTimings"] = offload.timings;
    // npc_motion is a graphics setting on the wire but not a rendering control.
    state["requested"].erase("npc_motion");
    state["effective"].erase("npc_motion");
    state["controls"] = Json::object();
    for (auto &[key, value] : config["graphics"].items()) {
        if (key == "npc_motion")
            continue;
        std::string group = key == "bloom" || key == "bloom_strength" ? "bloom" :
                            (key == "outline" || key == "outline_quality" || key == "outline_width_px" || key == "outline_colour") ? "outline" : key == "sky" ? "sky" : key == "msaa" ? "msaa_resolve" :
                            key == "sharpen" || key == "upscaler" ? "upscale_sharpen" : key == "alpha_haze" ? "native_surfaces_geometry" :
                            key == "fog" || key == "normal_maps" || key == "specular" || key == "reflections" ||
                            key == "ambient_occlusion" || key == "anisotropy" ? "world_shading_shared" : "";
        auto cost = group.empty() ? Json(nullptr) : state["costsMs"].value(group, Json(nullptr));
        state["controls"][key] = {{"requested", value}, {"effective", state["effective"].at(key)},
                                 {"gpuMs", group == "bloom" || group == "sky" || group == "outline" ? cost : Json(nullptr)},
                                 {"sharedPass", group.empty() ? Json(nullptr) : Json(group)}, {"passGpuMs", cost}};
    }
    return state;
}
// The GPU the compositor renders with; offload_device "auto" prefers another one.
static const std::string &compositorPci() {
    static const std::string pci = offload::pciOfFd(g_pCompositor->m_drmRenderNode.fd >= 0 ? g_pCompositor->m_drmRenderNode.fd
                                                                                         : g_pCompositor->m_drm.fd);
    return pci;
}
// Draw state only the in-process renderer has (none at the moment: machines travel in
// each pose since offload/Machines.hpp).
std::string Runtime::offloadLocalReason() const {
    return "";
}
void Runtime::offloadTick() {
    if (renderPass.active()) throw std::runtime_error("offload lifecycle inside frame");
    auto g = config["graphics"];
    g.erase("npc_motion"); // not a renderer setting: changing it never restarts the worker
    bool enabled = active && world && monitor && !loading && !locked() && g["offload"] != "off" &&
                   offloadLocalReason().empty();
    // Catalog objects decode in the worker too: it may read the object roots that exist
    // (one created later restarts it once with the new root).
    Json roots = Json::array();
    std::error_code error;
    for (auto &root : {catalogRoots.userObjects, catalogRoots.userLibraries})
        if (!root.empty() && std::filesystem::is_directory(root, error))
            roots.push_back(root.string());
    for (auto &root : catalogRoots.systemLibraries)
        if (std::filesystem::is_directory(root, error))
            roots.push_back(root.string());
    Json request = {{"enabled", enabled}, {"graphics", g}, {"compositorPci", compositorPci()},
                    {"worldPath", world ? (installed / world->id).string() : ""}, {"worldLoad", worldLoads},
                    {"objectRoots", roots},
                    {"width", monitor ? int(monitor->m_pixelSize.x) : 0},
                    {"height", monitor ? int(monitor->m_pixelSize.y) : 0}};
    if (request != offloadRequest) {
        // Diagnostics: what restarted the worker (each change is a new generation).
        Json changed = Json::object();
        for (auto &[key, value] : request.items())
            if (!offloadRequest.is_object() || offloadRequest.value(key, Json()) != value) {
                if (key != "graphics")
                    changed[key] = value;
                else
                    for (auto &[k, v] : value.items())
                        if (!offloadRequest.is_object() || offloadRequest[key].value(k, Json()) != v)
                            changed["graphics." + k] = v;
            }
        auto history = offloadChanges.value("history", Json::array());
        history.push_back(changed);
        if (history.size() > 8)
            history.erase(history.begin());
        offloadChanges = {{"count", offloadChanges.value("count", 0) + 1}, {"history", history}};
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        offload.stop(); renderedOffload = false;
        offload.error.clear();
        offloadRequest = request;
        request["generation"] = ++offloadGeneration;
        ipc->renderer(request);
    }
    offloadPump(0);
    renderer.offloadState = {{"activeRenderer", renderedOffload ? "offload" : "in-process"},
                            {"powerW", offloadPolicy.value("powerW", Json(nullptr))},
                            {"profile", offloadPolicy.value("profile", "unknown")},
                            {"targetFPS", offloadPolicy.value("targetFPS", 0)},
                            {"capW", offloadPolicy.value("capW", 0.)},
                            {"worker", offload.timings}};
}
void Runtime::offloadPump(int stage) {
    if (renderPass.active()) throw std::runtime_error("offload polling inside frame");
    if (offload.connected()) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        // Flatten the simulation's visual-only bob into the existing pose packet.
        // The physical capsule and movement publication keep their original pose.
        auto view = camera;
        view.position.y += view.climbBob;
        view.climbBob = 0;
        // World objects: the animation clock in every pose, their state as a delta against
        // what the worker has, committed once the pose is sent.
        auto objects = [&] {
            // The animation clock only moves while an animation is in view (render on demand
            // in-process does the same), so a still view sends no new pose. Asked here, not
            // from animationFrames: the frame tick sends its pose before it recomputes that.
            bool animationInView = monitor && renderer.animationVisible(
                view, float(monitor->m_pixelSize.x) / std::max(1.f, float(monitor->m_pixelSize.y)));
            if (animationInView || !offloadClockSent)
                offloadClock = renderer.animationSeconds(), offloadClockSent = true;
            return Json{{"clock", offloadClock}, {"logicalScale", renderer.logicalScale},
                        {"objects", world ? offloadObjects.delta(*world, renderer.transforms, mergedAssets, packageAssets)
                                          : Json(nullptr)},
                        {"characters", offloadCharacters()},
                        {"machines", offloadMachines.delta(renderer)}};
        };
        // Poses go out from the frame tick only: live, one per compositor frame at the policy's
        // rate while anything changes; while a recording exports frames, in lockstep. The
        // in-between pump receives and imports. share: the frame budget this tick adds.
        using Send = offload::Client::Send;
        int fps = offloadPolicy.value("targetFPS", 30);
        double share = 0;
        if (stage == 2) {
            auto now = std::chrono::steady_clock::now();
            auto frames = presents - offloadPresents;
            share = frames ? double(frames) * fps * refreshInterval()
                           : std::chrono::duration<double>(now - offloadFrameTick).count() * fps; // idle: timer ticks
            offloadPresents = presents;
            offloadFrameTick = now;
        }
        offload.tick(view, renderer.graphics, renderer.profile, fps, objects, [&] { offloadObjects.commit(), offloadMachines.commit(); },
                     stage == 0 ? Send::None : frameExport.active() ? (stage == 2 ? Send::Lockstep : Send::None)
                                                                    : stage == 1 ? Send::Promote : Send::Frame, share);
        if (!offload.connected()) {
            auto errors = offloadChanges.value("errors", Json::array());
            errors.push_back({{"generation", offloadGeneration}, {"error", offload.error}});
            if (errors.size() > 8)
                errors.erase(errors.begin());
            offloadChanges["errors"] = errors;
            auto stop = offloadRequest; stop["enabled"] = false; stop["generation"] = offloadGeneration;
            ipc->renderer(stop);
        }
    }
}
// RFC-0012 arrival: a waypoint the player reaches is done. Reached: within the waypoint
// marker's range[0] (2 m) across the ground, the target from 1 m below the feet to 1 m
// above the eye (not a floor above or below). A route ending there goes with it.
void Runtime::waypointArrival() {
    auto &wp = control.location["waypoint"];
    if (!world || !active || wp.is_null() || !wp.contains("position"))
        return;
    Vec3 target{wp["position"][0], wp["position"][1], wp["position"][2]};
    Vec3 eye = camera.position, feet = eye - Vec3{0, camera.eyeHeight, 0};
    if (std::hypot(target.x - eye.x, target.z - eye.z) > quests::waypointArrivalRadius || target.y < feet.y - 1 ||
        target.y > eye.y + 1)
        return;
    auto &route = control.location["route"];
    bool routeEnds = route.is_array() && !route.empty() && route.back().contains("position") &&
                     length(Vec3{route.back()["position"][0], route.back()["position"][1], route.back()["position"][2]} - target) <= 1;
    control.execute("waypoint.clear", Json::object(), "", "", world.get(), camera, {});
    if (routeEnds)
        control.execute("route.clear", Json::object(), "", "", world.get(), camera, {});
    publish();
}
// Development diagnostics: the drawn route line and the straight one it replaces (legs
// through collision), and the overlay labels as drawn panels in output pixels.
Json Runtime::routeDiagnostics() {
    Json out = {{"points", renderer.routeLine.size()}, {"buried", 0}, {"straightBuried", 0}, {"labels", Json::array()}};
    if (world && !renderer.routeLine.empty()) {
        interaction::PlacementScene scene;
        scene.collision = world->collision;
        scene.index = &world->collisionIndex;
        int buried = 0, straight = 0;
        for (size_t k = 1; k < renderer.routeLine.size(); ++k)
            buried += interaction::lineBlocked(scene, renderer.routeLine[k - 1], renderer.routeLine[k]);
        Vec3 previous = renderer.routeLine.front();
        for (auto &node : renderer.route) {
            Vec3 next{node["position"][0], node["position"][1].get<float>() - 1.7f + .025f, node["position"][2]};
            straight += interaction::lineBlocked(scene, previous, next);
            previous = next;
        }
        out["buried"] = buried, out["straightBuried"] = straight;
    }
    if (monitor) {
        float w = monitor->m_pixelSize.x, scale = std::clamp(w / 1920.f, 1.f, 2.f), pad = 6 * scale;
        for (auto &l : renderer.labels)
            if (l.lines.empty()) {
                float x = l.x * w;
                out["labels"].push_back({{"text", l.text.substr(0, 32)}, {"y", l.y * monitor->m_pixelSize.y},
                                         {"panel", {x - pad, x + renderer.labelWidth(l.text, scale) + pad}}, {"textX", x}});
            }
    }
    return out;
}
// The waypoint route line follows the ground (not straight through a hill or a floor):
// the route's own legs are laid once per route, the leg from the feet again when the
// feet move a quarter metre.
void Runtime::routeLineTick() {
    if (!world || renderer.waypoint.is_null() || !renderer.route.is_array() || renderer.route.empty()) {
        renderer.routeLine.clear();
        routeLineKey.clear(), routeTailKey.clear();
        return;
    }
    interaction::PlacementScene scene;
    scene.collision = world->collision;
    scene.index = &world->collisionIndex;
    constexpr float lift = .025f;
    auto tailKey = world->id + renderer.route.dump();
    bool tailChanged = tailKey != routeTailKey;
    std::vector<Vec3> points;
    for (auto &node : renderer.route)
        if (node.contains("position") && node["position"].is_array() && node["position"].size() == 3)
            points.push_back(Vec3{node["position"][0], node["position"][1].get<float>() - 1.7f, node["position"][2]});
    if (points.empty()) {
        renderer.routeLine.clear();
        return;
    }
    if (tailChanged) {
        routeTailKey = tailKey;
        routeTail = interaction::groundLine(scene, points[0], {points.begin() + 1, points.end()}, .5f, lift);
    }
    Vec3 feet = camera.position - Vec3{0, camera.eyeHeight, 0};
    auto key = std::to_string(std::lround(feet.x * 4)) + "," + std::to_string(std::lround(feet.y * 4)) + "," +
               std::to_string(std::lround(feet.z * 4));
    if (key == routeLineKey && !tailChanged)
        return;
    routeLineKey = key;
    auto line = interaction::groundLine(scene, feet, {points[0]}, .5f, lift);
    line.insert(line.end(), routeTail.begin() + 1, routeTail.end()); // the tail starts at points[0]
    renderer.routeLine = std::move(line);
}
void Runtime::publishHUD() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    Json zone = active && monitor ? hud::zoneAt(world.get(), camera.position) : Json(nullptr);
    auto emit = [&](const char *event, Json data) {
        data["monotonicMs"] = ms;
        ipc->notify(event, std::move(data));
    };
    if (zone != hudZone) {
        if (!hudZone.is_null())
            emit("zone.exited", {{"zone", hudZone}});
        hudZone = zone;
        if (!hudZone.is_null())
            emit("zone.entered", {{"zone", hudZone}});
    }
    if (!active || !monitor || !world)
        return;
    if (now >= nextHUDPose) {
        nextHUDPose = now + std::chrono::milliseconds(34);
        emit("hud.pose", {{"outputId", "output-" + std::to_string(monitor->m_id)},
                         {"worldId", world->id}, {"worldName", world->name},
                         {"position", {camera.position.x, camera.position.y, camera.position.z}},
                         {"heading", camera.yaw}, {"pitch", camera.pitch}, {"zone", zone},
                         {"tool", editor.slot}, {"subtype", editor.submode()}});
    }
    Json toolState = {{"slot", editor.slot},
                      {"name", editor.slot == editor.debugSlot ? Json("debug") : config["tools"]["slots"].at(std::clamp(editor.slot, 1, 5) - 1)},
                      {"subtype", editor.submode()}};
    if (hudTool != toolState) {
        hudTool = toolState;
        emit("tool.changed", toolState);
    }
    if (now >= nextHUDFrame) {
        nextHUDFrame = now + std::chrono::milliseconds(250);
        emit("hud.frame", hud::frame(renderer.timings()));
    }
}
void Runtime::publish() {
    if (!ipc)
        return;
    control.retainOutputs(monitor ? Json::array({"output-" + std::to_string(monitor->m_id)}) : Json::array());
    Json surfaces = Json::array();
    for (auto &e : capture.entries)
        surfaces.push_back({{"id", e.surface.id},
                            {"kind", "window"},
                            {"title", e.surface.title},
                            {"logicalSize", {e.surface.pixelWidth, e.surface.pixelHeight}},
                            {"focused", e.surface.id == focused}});
    for (auto &f : capture.workspaces)
        surfaces.push_back({{"id", f.surface.id},
                            {"kind", "workspace"},
                            {"title", f.surface.title},
                            {"logicalSize", {f.surface.pixelWidth, f.surface.pixelHeight}},
                            {"focused", !layout.desktop.is_null() && layout.desktop == f.workspaceId}});
    // Native desktop inventory does not depend on world capture leases, homes,
    // or the launcher having authored workspace mappings.
    for (auto &workspace : State::workspaceState()->workspaces()) {
        if (workspace->m_id <= 0)
            continue;
        auto id = "workspace-" + std::to_string(workspace->m_id);
        if (std::none_of(surfaces.begin(), surfaces.end(), [&](auto &s) { return s.at("id") == id; }))
            surfaces.push_back({{"id", id},
                                {"kind", "workspace"},
                                {"title", ipc::boundedText(workspace->m_name, ipc::TitleLimit)},
                                {"logicalSize", {1280, 720}},
                                {"focused", workspace->isVisible()}});
    }
    Json homes = locations;
    for (auto &b : layout.effectiveBindings()) {
        std::erase_if(homes.get_ref<Json::array_t &>(), [&](auto &v) { return v.at("id") == b.at("id"); });
        std::string spawnId = "arrival";
        if (world && b.at("worldId") == world->id) {
            auto area = std::find_if(world->areas.begin(), world->areas.end(),
                                     [&](auto &a) { return a.at("id") == b.at("areaId"); });
            if (area != world->areas.end())
                spawnId = area->value("spawnId", world->defaultSpawn);
        }
        homes.push_back(
            {{"id", b.at("id")}, {"label", b.at("areaId")}, {"worldId", b.at("worldId")}, {"spawnId", spawnId}});
    }
    while (homes.size() > 32)
        homes.erase(homes.begin());
    Json spatial = {
        {"world", world ? Json{{"id", world->id}, {"name", world->name}, {"places", world->places}} : Json(nullptr)},
        {"map", world ? world->map : Json(nullptr)},
        {"output", nullptr},
        {"projection", nullptr}};
    if (monitor) {
        auto output = "output-" + std::to_string(monitor->m_id);
        spatial["output"] = {{"id", output},
                             {"name", ipc::boundedText(monitor->m_name, ipc::OutputNameLimit)},
                             {"width", int(monitor->m_pixelSize.x)},
                             {"height", int(monitor->m_pixelSize.y)},
                             {"scale", monitor->m_scale}};
        if (active && world) {
            auto now = std::chrono::steady_clock::now();
            if (now >= nextProjection || projectionWorld != world->id || projectionOutput != output) {
                nextProjection = now + std::chrono::milliseconds(100);
                projectionWorld = world->id;
                projectionOutput = output;
                legacyProjection = {
                    {"outputId", output},
                    {"worldId", world->id},
                    {"monotonicMs",
                     std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()},
                    {"markers", world::projectMarkers(*world, camera, float(monitor->m_size.x) / monitor->m_size.y)}};
            }
            spatial["projection"] = legacyProjection;
            // A shell drawing markers itself (markers.core_draw off) reads them here (IPC 0.21).
            if (!markers.settings.coreDraw)
                for (auto &m : markers.projection(camera, float(monitor->m_size.x) / monitor->m_size.y,
                                                  std::chrono::duration_cast<std::chrono::milliseconds>(
                                                      now.time_since_epoch()).count()))
                    if (spatial["projection"]["markers"].size() < 64)
                        spatial["projection"]["markers"].push_back(m);
        }
    }
    Json apps = Json::array();
    for (auto &window : Desktop::windowState()->windows()) {
        if (apps.size() >= 128)
            break;
        if (!window->m_isMapped || window->isHidden() || !window->m_workspace || window->m_workspace->m_id <= 0 ||
            window->parent() || window->x11Parent())
            continue;
        auto entry =
            std::find_if(capture.entries.begin(), capture.entries.end(), [&](auto &e) { return e.window == window; });
        auto id = entry != capture.entries.end()
                      ? entry->surface.id
                      : "desktop-window-" + std::to_string(reinterpret_cast<uintptr_t>(window.get()));
        if (entry == capture.entries.end())
            surfaces.push_back({{"id", id},
                                {"kind", "window"},
                                {"title", ipc::boundedText(window->m_title, ipc::TitleLimit)},
                                {"logicalSize",
                                 {std::max(1, int(window->resource() ? window->resource()->m_current.size.x : 1)),
                                  std::max(1, int(window->resource() ? window->resource()->m_current.size.y : 1))}},
                                {"focused", Desktop::focusState()->window() == window}});
        apps.push_back({{"surfaceId", id},
                        {"workspaceId", int(window->m_workspace->m_id)},
                        {"title", ipc::boundedText(window->m_title, ipc::TitleLimit)},
                        {"appClass", ipc::boundedText(window->m_class, ipc::ClassLimit)},
                        {"icon", ipc::boundedText(window->m_class, ipc::ClassLimit)}});
    }
    Json publication = {
        {"interaction",
         {{"mode", layerCursor                 ? "cursor"
                   : !layout.desktop.is_null() ? "workspace"
                   : fullscreen.phase == 2     ? "fullscreen"
                   : typing                    ? "typing"
                                               : "movement"},
          {"tool", tool},
          {"tools", config["tools"]["slots"]},
          {"noclip", noclip},
          {"cursor", cursorState},
          {"taggedSurfaceId", tagged.empty() ? Json(nullptr) : Json(tagged)},
          // "use" (a refused E beyond the use distance) is not an IPC 0.19 action: "" on the wire.
          {"reticle", {{"state", reticle.state}, {"action", reticle.action == "use" ? "" : reticle.action}}}}},
        {"keymap",
         {{"config", input.config},
          {"path", ipc::boundedText(inputFile.path.string(), ipc::PathLimit)},
          {"conflicts", interaction::inputConflicts(input.config)},
          {"steals", steals(input.config)},
          {"capture", captureRequest},
          {"holds", input.holds()},
          {"error", ipc::boundedText(inputFile.error, ipc::MessageLimit)}}},
        {"editor",
         {{"unplaced", unplacedState()},
          {"contentScale", contentFeedback},
          {"toolInput", toolContext()},
          {"toolSlots", editor.slots()},
          {"toolDefinitions",
           [&] {
               // IPC 0.26: slots 1-5, then the debug tool (older peers see the five).
               auto definitions = editor.definitions();
               definitions.push_back(editor.debugDefinition());
               return definitions;
           }()},
          {"debugTool", debugToolState()},
          {"carryFacing", hands.gesture == interaction::Gesture::Carry ? Json(hands.facePlayer) : Json(nullptr)},
          {"mode", inputMode()},
          {"tool", editor.slot},
          {"submode", editor.submode()},
          {"selectedId", editor.selected.empty() ? Json(nullptr) : Json(editor.selected)},
          {"selectedKind", editor.kind == "object" && !editor.selected.starts_with("object:") ? "node" : editor.kind},
          {"gizmo", {{"space", editor.localAxes ? "local" : "world"}, {"pivot", editor.pivotBase ? "base" : "centre"}, {"snap", editor.snap}}},
          {"paths",
           [&] {
               // IPC 0.23 path fields (older peers see them stripped): use, name, speed, stops.
               Json paths = Json::array();
               for (auto p : editor.paths) {
                   p["use"] = p.value("use", "generic");
                   if (!p.contains("name"))
                       p["name"] = nullptr;
                   if (!p.contains("speed"))
                       p["speed"] = nullptr;
                   if (!p.contains("stops"))
                       p["stops"] = Json::array();
                   paths.push_back(std::move(p));
               }
               return paths;
           }()},
          {"objects", editor.objects},
          {"locked", Json::array()},
          {"debug", debug}}},
        {"apps", apps},
        {"mounts", layout.state(world.get())},
        {"presentation",
         {{"profile", world ? renderer.profile : Json(nullptr)},
          {"fov", camera.fov},
          {"quality", renderer.intelQuality ? "intel" : "high"}}},
        {"spatial", spatial},
        {"workspaces",
         {{"currentId", monitor && monitor->m_activeWorkspace && monitor->m_activeWorkspace->m_id > 0
                            ? Json(monitor->m_activeWorkspace->m_id)
                            : Json(nullptr)},
          {"locations", homes}}},
        {"runtime",
         {{"active", active},
          {"outputs", monitor ? Json::array({"output-" + std::to_string(monitor->m_id)}) : Json::array()}}},
        {"world", world ? Json{{"id", world->id}, {"spawnId", world->defaultSpawn}} : Json(nullptr)},
        {"surfaces", surfaces},
        {"movement",
         {{"modeId", movement == &walk ? "com.hyprune.walk" : "com.hyprune.fly"},
          {"position", {camera.position.x, camera.position.y, camera.position.z}},
          {"state", climbing.active()              ? "climbing"
                    : movement == &walk && !noclip ? "walking"
                                                   : "flying"},
          {"ladderId", climbing.active() ? Json(climbing.ladderId) : Json(nullptr)},
          {"prompt",
           world && !noclip && movement == &walk && (climbing.active() || climbing.nearby(camera, world->climbables))
               ? climbing.prompt(sim::climbMode(config["movement"]["climb_mode"].get<std::string>()))
               : ""},
          {"orientation", camera.orientation()}}},
        {"shell", {{"leases", control.leases}}},
        {"location", control.location},
        {"integrations", {{"sources", control.sources}}}};
    publication["markers"] = markers.stateKey();
    publication["objects"] = objectsState();
    publication["npcs"] = npcsState();
    publication["live"] = {{"instances", machines.live()}};
    publication["companion"] = companionState();
    for (auto &e : capture.entries)
        if (world && editor.locked(world->id, e.instance))
            publication["editor"]["locked"].push_back(e.surface.id);
    lastPublished = publication;
    Json derived = Json::array();
    for (auto &b : layout.defaults)
        derived.push_back(b.at("id"));
    ipc->publish(publication, derived);
    for (auto &[event, params] : layout.events)
        ipc->notify(event, params);
    layout.events.clear();
    for (auto &[event, params] : control.events) {
        recentEvents.push_back({{"event", event}, {"params", params}});
        while (recentEvents.size() > 32)
            recentEvents.pop_front();
        ipc->notify(event, params);
    }
    control.events.clear();
}
void Runtime::load(const std::string &id, const std::string &spawnId) {
    if (session.recording || session.playing) throw ipc::Error(-32004, "stop recording/replay before loading another world");
    if (id.empty() || id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos ||
        id == "." || id == "..")
        throw ipc::Error(-32602, "invalid installed world ID");
    if (loading)
        throw ipc::Error(-32004, "world load already pending");
    keys.clear();
    cancelGrab();
    typing = false;
    control.beginLoad(spawnId.empty() ? Json{{"worldId", id}} : Json{{"worldId", id}, {"spawnId", spawnId}},
                      monitor ? Json("output-" + std::to_string(monitor->m_id)) : Json(nullptr));
    publish();
    loadCancelled = false;
    loading = true;
    pendingSpawn = spawnId;
    loadDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(28);
    auto package = installed / id;
    // KTX2 textures transcode on the loader to what this GPU advertises.
    static const unsigned formats = [] {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        return render::Renderer::compressedFormats();
    }();
    // World 0.9 library references resolve against the installed libraries (RFC-0011).
    world::LoadOptions options{formats};
    options.libraryRoots.push_back(catalogRoots.userLibraries);
    for (auto &root : catalogRoots.systemLibraries)
        options.libraryRoots.push_back(root);
    pendingWorld = std::async(std::launch::async, [package, id, options] {
        auto candidate = world::load(package, options);
        if (candidate->id != id)
            throw std::runtime_error("installed ID mismatch");
        return candidate;
    });
}
void Runtime::finishLoad() {
    if (!loading || (!uploadingWorld && (!pendingWorld.valid() ||
                                         pendingWorld.wait_for(std::chrono::seconds(0)) != std::future_status::ready)))
        return;
    try {
        bool start = !uploadingWorld;
        if (start)
            uploadingWorld = pendingWorld.get();
        auto candidate = uploadingWorld;
        if (loadCancelled || locked())
            throw ipc::Error(-32004, "world load cancelled");
        if (!ipc->healthy() || std::chrono::steady_clock::now() > loadDeadline)
            throw ipc::Error(-32006, "world load expired");
        auto spawn = pendingArea.empty() ? candidate->spawn(pendingSpawn) : world::areaSpawn(*candidate, pendingArea);
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        applyGraphics(graphicsFile.current);
        if (start)
            renderer.beginPrepare(candidate);
        if (!renderer.continuePrepare())
            return;
        uploadingWorld.reset();
        loading = false;
        // Loading ticks may reapply user overrides during shader warm-up. Read
        // immutable authored defaults, never the already overridden GPU profile.
        graphicsBaseProfile = candidate->atmosphere.is_object() ? candidate->atmosphere : world::defaultAtmosphere();
        renderer.profile = graphicsBaseProfile;
        applyGraphics(graphicsFile.current);
        world = std::move(candidate);
        cinema.stop();
        cinemaTracing = false;
        loadStep = "characters";
        npcWorldLoaded();
        loadStep = "machines";
        machinesLoad();
        loadStep = "layout";
        outsidePlayable = false;
        // Windows taken into the world keep the home they left across a world change; the
        // new world's capture gives them new surface IDs.
        for (auto &e : capture.entries)
            if (auto h = layout.homes.find(e.surface.id); h != layout.homes.end())
                leftHomes.push_back({e.window, h->second});
        layout.homes.clear();
        capture.release();
        layout.invalidateCollision();
        layout.freePoses.clear();
        layout.occupiedPoses.clear();
        layout.spawnAreas.clear();
        deriveHomes();
        restoreFullscreenWindows();
        fullscreen = {};
        handover = {};
        relativeSurface.clear();
        editor.selected.clear();
        cancelGizmo(); ghostFor.clear(); instanceOverrides.clear();
        objectModels.clear();
        renderer.transforms.clear();
        originalCollision = world->collision;
        loadStep = "objects";
        objectsWorldLoaded();
        loadStep = "control";
        hands.end();
        auto transition = control.location["transition"];
        control.reset(world.get());
        control.location["transition"] = transition;
        control.endLoad(true);
        world->defaultSpawn = spawn.id;
        float lens = camera.fov;
        camera = {};
        climbing.reset(camera);
        camera.fov = lens;
        camera.position = spawn.position + Vec3{0, 1.7f, 0};
        camera.yaw = spawn.yaw;
        camera.pitch = spawn.pitch;
        layout.carried.clear();
        layout.desktop = nullptr;
        pendingArea.clear();
        movement = world->collision.empty() ? static_cast<sim::MotionStrategy *>(&fly) : &walk;
        loadStep = "refresh";
        if (active) {
            // These are existing clients entering a different world, not newly mapped
            // windows. Resolve authored/durable mounts before enabling spawn placement.
            capture.spawnNew = false;
            refresh();
            capture.spawnNew = true;
        }
        publish();
        ipc->complete(Json{{"revision", ipc->revision()}});
#ifndef HYPRUNE_LIVE_BUILD
        if (!active && !toggleAfterLoad)
            deferInput([] { runtime->readyNotice(); });
#endif
        if (toggleAfterLoad) {
            toggleAfterLoad = false;
            toggle(pendingToggle);
        }
    } catch (const ipc::Error &e) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        renderer.cancelPrepare();
        uploadingWorld.reset();
        loading = false;
        control.endLoad(false, loadCancelled || locked());
        publish();
        ipc->fail(e.code, e.what());
        error(e);
        toggleAfterLoad = false;
        pendingArea.clear();
    } catch (const std::exception &e) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        renderer.cancelPrepare();
        uploadingWorld.reset();
        loading = false;
        control.endLoad(false, loadCancelled || locked());
        publish();
        // A referenced library version that is not installed: the previous scene stays.
        if (std::string(e.what()).starts_with("world.library.missing: "))
            ipc->notify("runtime.notice", {{"level", "warning"}, {"code", "world.library.missing"},
                                           {"message", ipc::boundedText(std::string(e.what()).substr(23), ipc::MessageLimit)}});
        std::fprintf(stderr, "[hyprune] world load failed (%s): %s\n", loadStep, e.what());
        ipc->fail(-32602, e.what());
        error(e);
        toggleAfterLoad = false;
        pendingArea.clear();
    }
    loadStep = "load";
}

void Runtime::returnToWorldWorkspace() {
    if (renderPass.active())
        throw std::runtime_error("workspace switch inside render pass");
    if (!active || !monitor || !worldWorkspace)
        return;
    auto room = State::workspaceState()->query().id(worldWorkspace).run();
    if (!room)
        room = State::workspaceState()->create(worldWorkspace, monitor->m_id);
    if (room->m_monitor != monitor) {
        auto result = Config::Actions::moveToMonitor(room, monitor.lock());
        if (!result)
            throw std::runtime_error("cannot place world workspace: " + result.error().message);
    }
    Desktop::focusState()->rawMonitorFocus(monitor.lock());
    monitor->changeWorkspace(room, false, true);
}

void Runtime::readyNotice() {
    if (active || loading || readyNotices)
        return;
    if (renderPass.active())
        throw std::runtime_error("ready notification inside render pass");
    std::string chord;
    for (const auto &b : g_pKeybindManager->m_keybinds) {
        auto description = b->description;
        std::transform(description.begin(), description.end(), description.begin(), [](unsigned char c) { return std::tolower(c); });
        if (b->enabled && !b->key.empty() &&
            (b->arg.find("hyprune.toggle") != std::string::npos ||
             (description.find("hyprune") != std::string::npos && description.find("toggle") != std::string::npos))) {
            chord = interaction::bindingName({b->key, b->modmask, false});
            break;
        }
    }
    std::string message =
        "Hyprune ready · " + (chord.empty() ? "use your configured toggle to enter" : "press " + chord + " to enter");
    HyprlandAPI::addNotification(handle, message, CHyprColor{.4f, .73f, .87f, 1.f}, 7000);
    ++readyNotices;
}
void Runtime::toggle(const std::string &output) {
    if (renderPass.active())
        throw std::runtime_error("world toggle inside render pass");
    if (active) {
        exit();
        return;
    }
    if (locked())
        return;
    if (!ipc->healthy())
        throw std::runtime_error("hypruned unavailable; reload core to start a fresh session");
    if (!world) {
        load("org.hyprune.atrium");
        toggleAfterLoad = true;
        pendingToggle = output;
        return;
    }
    PHLMONITOR chosen = Desktop::focusState()->monitor();
    if (!output.empty())
        for (auto &m : State::monitorState()->monitors())
            if (m->m_name == output)
                chosen = m;
    if (!chosen)
        throw std::runtime_error("no output");
    if (chosen->m_transform != WL_OUTPUT_TRANSFORM_NORMAL)
        throw ipc::Error(-32007, "transformed world outputs unsupported");
    monitor = chosen;
    control.outputId = "output-" + std::to_string(chosen->m_id);
    savedFocus = Desktop::focusState()->window();
    previousWorkspace = chosen->m_activeWorkspace;
    previousSpecialWorkspace = chosen->m_activeSpecialWorkspace;
    worldWorkspace = config["windows"]["world_workspace"];
    active = true;
    try {
        if (chosen->m_activeSpecialWorkspace)
            chosen->setSpecialWorkspace(nullptr);
        returnToWorldWorkspace();
    } catch (...) {
        active = false;
        worldWorkspace = 0;
        previousWorkspace.reset();
        previousSpecialWorkspace.reset();
        throw;
    }
    savedCursorHidden = !g_pHyprRenderer->shouldRenderCursor();
    cursorHidden = savedCursorHidden;
    capture.spawnNew = false;
    typing = false;
    keys.clear();
    lastTick = std::chrono::steady_clock::now();
    // Visibility changes are coalesced after the frame. Hardware cursor behavior
    // still requires the supervised DRM checklist in docs/M3A.md.
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    refresh();
    capture.spawnNew = true;
    unplacedState();
    editor.slot = tool = layout.unplaced.empty() ? 1 : 5;
    cursor();
    publish();
}
void Runtime::exit() {
    cinema.stop();
    cinemaTracing = false;
    exportStop();
    control.releasePhoto(camera);
    climbing.reset(camera);
    if (renderPass.active())
        throw std::runtime_error("world exit inside render pass");
    if (loading)
        loadCancelled = true;
    if (!active)
        return;
    for (auto held : freeNativeButtons) {
        g_pSeatManager->sendPointerButton(0, held, WL_POINTER_BUTTON_STATE_RELEASED);
        swallowedButtons.insert(held);
    }
    if (!freeNativeButtons.empty()) g_pSeatManager->sendPointerFrame();
    freeNativeButtons.clear();
    active = false;
    if (session.recording) session.stopRecording();
    if (session.playing) stopReplay();
    offloadTick();
    input.clear();
    relativeSurface.clear();
    captureRequest = nullptr;
    cancelGizmo();
    objectCarry = false;
    pendingCursorHide.reset(); // an old hide request must not outlive cursor restoration
    cursorWarpPending = false;
    restoreFullscreenWindows();
    fullscreen = {};
    handover = {};
    freeCursor = layerCursor = false;
    cursorReturn = {};
    cursor();
    layout.desktop = nullptr;
    control.stop();
    for (auto &[actor, b] : bindings)
        control.associate(actor, false);
    bindings.clear();
    typing = false;
    focused.clear();
    keys.clear();
    cancelGrab();
    capture.release();
    // Normal exits run in doLater. Unload runs outside any render pass and must
    // finish synchronously: no callback into an unloaded .so may survive it.
    if (monitor && previousWorkspace) {
        Desktop::focusState()->rawMonitorFocus(monitor.lock());
        monitor->changeWorkspace(previousWorkspace, false, true);
        if (previousSpecialWorkspace)
            monitor->setSpecialWorkspace(previousSpecialWorkspace);
    }
    previousWorkspace.reset();
    previousSpecialWorkspace.reset();
    worldWorkspace = 0;
    if (savedFocus && savedFocus->m_isMapped)
        Desktop::focusState()->rawWindowFocus(savedFocus.lock(), Desktop::FOCUS_REASON_OTHER);
    if (monitor)
        g_pHyprRenderer->damageMonitor(monitor.lock());
    publish();
}
void Runtime::focus(const std::string &id, bool keyboard, bool enterUse) {
    if (renderPass.active())
        throw std::runtime_error("focus inside render pass");
    if (auto frame = capture.workspace(id)) {
        if (auto binding = layout.binding(frame->workspaceId, world.get()))
            resolvedLocation = std::to_string(frame->workspaceId) + ":" + world->id + ":" + binding->at("areaId").get<std::string>();
        cancelGrab();
        keys.clear();
        smoothIntent = {};
        lookX = lookY = 0;
        if (!handover.animated)
            handover.before = camera;
        Desktop::focusState()->rawMonitorFocus(monitor.lock());
        auto result = g_pKeybindManager->m_dispatchers.at("workspace")(std::to_string(frame->workspaceId));
        if (!result.success)
            throw ipc::Error(-32004, "workspace focus failed");
        control.stop();
        layout.desktop = frame->workspaceId;
        typing = false;
        keys.clear();
        cancelGrab();
        publish();
        return;
    }
    auto *entry = capture.find(id);
    if (!entry) {
        for (auto &workspace : State::workspaceState()->workspaces())
            if (id == "workspace-" + std::to_string(workspace->m_id)) {
                auto target = workspace->m_id;
                exit();
                auto result = g_pKeybindManager->m_dispatchers.at("workspace")(std::to_string(target));
                if (!result.success)
                    throw ipc::Error(-32004, "desktop workspace focus failed");
                publish();
                return;
            }
        for (auto &window : Desktop::windowState()->windows())
            if (window->m_isMapped &&
                id == "desktop-window-" + std::to_string(reinterpret_cast<uintptr_t>(window.get()))) {
                exit();
                Desktop::focusState()->fullWindowFocus(window, Desktop::FOCUS_REASON_CLICK);
                publish();
                return;
            }
        throw ipc::Error(-32003, "unknown surface");
    }
    auto w = entry->window.lock();
    if (keyboard && focused != id)
        cancelGrab();
    if (keyboard && w->m_workspace && !w->m_workspace->isVisible() && w->m_workspace->m_id == worldWorkspace)
        returnToWorldWorkspace();
    if (keyboard && w->m_workspace && !w->m_workspace->isVisible()) {
        auto b = layout.binding(entry->instance.workspaceId, world.get());
        if (!b || !world || world->areas.empty() || !layout.pulled(entry->instance, world.get()))
            throw ipc::Error(-32007, "hidden workspace keyboard focus is unsupported");
        // Clicking a tool at its physical location selects its native workspace,
        // while retaining this camera rather than starting another home journey.
        auto previous = resolvedLocation;
        resolvedLocation = std::to_string(entry->instance.workspaceId) + ":" + b->at("worldId").get<std::string>() +
                           ":" + b->at("areaId").get<std::string>();
        Desktop::focusState()->rawMonitorFocus(monitor.lock());
        auto result = g_pKeybindManager->m_dispatchers.at("workspace")(std::to_string(entry->instance.workspaceId));
        if (!result.success) {
            resolvedLocation = previous;
            throw ipc::Error(-32004, "tool workspace activation failed");
        }
        control.stop();
    }
    if (keyboard) {
        Desktop::focusState()->fullWindowFocus(w, Desktop::FOCUS_REASON_CLICK);
        Desktop::windowState()->raise(w);
        g_pSeatManager->setKeyboardFocus(w->resource());
        if (enterUse) {
            typing = true;
            input.clear();
            keys.clear();
            smoothIntent = {};
            lookX = lookY = 0;
        }
        focused = id;
        freeCursor = false;
    }
    g_pSeatManager->setPointerFocus(w->resource(), {entry->surface.pixelWidth * .5, entry->surface.pixelHeight * .5});
    publish();
}
void Runtime::cursor() {
    if (renderPass.active())
        throw std::runtime_error("cursor inside render pass");
    std::string next = !active || freeCursor       ? "desktop"
                       : !layout.desktop.is_null() ? "workspace"
                       : fullscreen.phase == 2     ? "fullscreen"
                       : control.uiVisible()       ? "director"
                       : typing                    ? "virtual"
                                                   : "reticle";
    if (next == cursorState)
        return;
    bool hide = next == "reticle" || next == "virtual";
    if (!active)
        hide = savedCursorHidden;
    if (cursorHidden != hide) {
        g_pHyprRenderer->setCursorHidden(hide);
    }
    cursorState = next;
    if (monitor && (next == "fullscreen" || next == "workspace"))
        Pointer::pointerController()->warpTo(monitor->m_position + monitor->m_size * .5, true);
}
#include "FreeCursor.inc"
void Runtime::useLayers() { toggleFreeCursor(); }
void Runtime::modeToggle() {
    if (!active)
        return;
    if (!layout.desktop.is_null()) {
        unfocus();
        return;
    }
    if (fullscreen.phase == 2)
        return;
    cancelGrab();
    typing = !typing;
    freeCursor = false;
    keys.clear();
    smoothIntent = {};
    lookX = lookY = 0;
    pointerX = pointerY = .5f;
    if (!typing) {
        focused.clear();
        g_pSeatManager->setPointerFocus(nullptr, {});
        g_pSeatManager->setKeyboardFocus(nullptr);
    }
    cursor();
    publish();
}
void Runtime::routePointer(const std::string &id, surfaces::Hit hit, uint32_t time) {
    if (auto target = capture.target(id, hit.u, hit.v)) {
        if (!buttons.empty() && grabResource && target->resource != grabResource)
            return;
        g_pSeatManager->setPointerFocus(target->resource, target->local);
        g_pSeatManager->sendPointerMotion(time, target->local);
        g_pSeatManager->sendPointerFrame();
    } else if (buttons.empty())
        g_pSeatManager->setPointerFocus(nullptr, {});
}
void Runtime::focusWall(const std::string &id) {
    auto wall = capture.workspace(id);
    if (!wall || handover.phase)
        return;
    cancelGrab();
    control.stop();
    typing = false;
    keys.clear();
    handover.animated = true;
    handover.before = camera;
    handover.from = camera;
    handover.to = camera;
    handover.workspace = wall->workspaceId;
    auto normal = wall->surface.normal();
    auto forward = normal * -1;
    handover.to.position =
        wall->surface.center + normal * (wall->surface.height / (2 * std::tan(camera.fov * .01745329252f / 2)));
    handover.to.yaw = std::atan2(forward.x, -forward.z);
    handover.to.pitch = std::asin(forward.y);
    handover.elapsed = 0;
    handover.phase = 1;
}
void Runtime::machinesLoad() {
    // The development override names the world, manifests for package assets (world 0.8
    // has no traits), bindings and options: {"worldId", "manifests", "instances",
    // "staleMs", "effectAsset"}. Machines themselves sync in the tick, after the
    // world's objects (catalog placements merge later).
    machines = {};
    machinesDev = nullptr;
    machinesStale = true;
    machineHints.clear();
    renderer.objectStates.clear();
    renderer.objectExtras.clear();
    renderer.worldLabels.clear();
    auto file = getenv("HYPRUNE_MACHINES_PROTOTYPE");
    if (!file || !world)
        return;
    try {
        auto spec = interaction::readJSON(file, 1 << 20);
        if (spec.value("worldId", "") == world->id)
            machinesDev = spec;
    } catch (const std::exception &e) {
        lastError = std::string("machines prototype: ") + e.what();
    }
}
void Runtime::machinesTick(float dt) {
    if (machinesFile.poll())
        machinesStale = true;
    if (!world)
        return;
    // Instances change with placements and catalog merges (objectsSync): sync then.
    if (machinesStale || instanceRevision != machinesSyncedRevision || world->objectAssets.size() != machinesSyncedAssets) {
        machinesStale = false;
        machinesSyncedRevision = instanceRevision, machinesSyncedAssets = world->objectAssets.size();
        bool was = machines.active();
        world::Machines::Setup setup;
        setup.dev = machinesDev, setup.devRoot = installed / world->id, setup.worldId = world->id;
        setup.user = machinesFile.error.empty() ? machinesFile.current : Json(nullptr);
        try {
            machines.sync(*world, setup);
        } catch (const std::exception &e) {
            lastError = std::string("machines: ") + e.what();
            machines = {};
        }
        for (auto &why : machines.errors())
            objectNotice("machine.invalid", ("Machine: " + why).substr(0, 240), "machine:" + why);
        if (!machinesFile.error.empty())
            objectNotice("machine.invalid", ("machines.json: " + machinesFile.error).substr(0, 240), "machines.json:" + machinesFile.error);
        // A one-time hint: which data source a newly placed machine reads.
        for (auto &[k, asset] : machines.fresh) {
            const auto &id = world->objectInstances[k].id;
            if (machineHints.insert(id).second && k >= authoredInstances)
                objectNotice("machine.source", "Machine " + id + " reads data source " + world::Machines::defaultClient + "." + id +
                                                   ": add an entry with id \"" + id + "\" to sources.json");
        }
        if (was && !machines.active()) {
            renderer.objectStates.clear();
            renderer.objectExtras.clear();
            renderer.worldLabels.clear();
        }
    }
    if (!machines.active())
        return;
    // Same clock as Control's publication monotonicMs. Still: reduced motion (states,
    // labels, counts and flashes change; nothing spins or travels), or a pinned animation
    // clock (tests: both renderers draw machines at rest, as they draw the pinned time).
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    bool still = config["graphics"]["reduced_motion"] == true || renderer.animationClock.has_value();
    machines.tick(dt, control.sources, now, still);
    renderer.objectStates = machines.states;
    renderer.objectExtras = machines.extras;
    renderer.worldLabels = machines.labels;
}
void Runtime::motionTick(float dt) {
    // Measured motion (not intent): walking into a wall neither bobs nor spreads the reticle.
    // Teleports (spawn, travel, scripted poses) read as standing still.
    if (dt > 0) {
        Vec3 d = camera.position - motionLast;
        float step = std::hypot(d.x, d.z), yaw = std::abs(std::remainder(camera.yaw - motionYaw, 6.2831853f));
        bool teleport = step > 3 || yaw > 1.2f; // poses, spawn, travel: no player motion in one tick
        float speed = teleport ? 0 : step / dt, turn = teleport ? 0 : yaw / dt;
        float blend = 1 - std::exp(-dt / .08f);
        motionSpeed += (speed - motionSpeed) * blend;
        motionTurn += (turn - motionTurn) * blend;
    }
    motionLast = camera.position;
    motionYaw = camera.yaw;
    bool reduced = config["graphics"]["reduced_motion"] == true;
    bool walking = movement == &walk && !noclip && camera.grounded && !climbing.active() && !typing && !loading &&
                   !control.driving() && !control.uiVisible() && !control.photoHeld() && !handover.phase &&
                   fullscreen.phase == 0 && !layerCursor;
    auto level = reduced ? sim::ViewBob::Off : sim::ViewBob::level(config["player"]["view_bob"].get<std::string>());
    viewBob.update(dt, motionSpeed, config["player"]["walk_speed"], config["player"]["run_speed"], level, walking, camera.yaw);
    camera.viewBob = viewBob.offset;
}
bool Runtime::tooFar(float distance, const char *reachKey, const char *action) {
    float reach = config["player"][reachKey];
    if (distance <= reach)
        return false;
    refuseTooFar(distance, reach, action, "placement.out_of_range");
    return true;
}
std::string tooFarText(float distance) {
    // ASCII: the reticle label uses core's own glyph atlas.
    return distance < 10 ? std::format("Too far: {:.1f} m", distance) : std::format("Too far: {:.0f} m", distance);
}
void Runtime::refuseTooFar(float distance, float reach, const char *action, const char *code) {
    // A reach-limited action refused: say so (notice) and flash the out-of-range reticle,
    // also when the player didn't look at the reticle before clicking.
    ipc->notify("runtime.notice", {{"level", "info"}, {"code", code},
                                   {"message", std::format("{} (reach {:g} m)", tooFarText(distance), reach)}});
    reticleFlash = {"out_of_range", action, distance, reach};
    reticleFlashUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
}
void Runtime::reticleTick() {
    ReticleView next;
    auto ranged = [&](float distance, const char *reachKey, const char *action) {
        float reach = config["player"][reachKey];
        next = {distance <= reach ? "valid" : "out_of_range", action, distance, reach};
    };
    auto movable = [&](const Capture::Entry *e) {
        return e && e->parentId.empty() && !(world && editor.locked(world->id, e->instance));
    };
    if (!active || layerCursor || !world || loading)
        next.state = "hidden";
    else if (typing)
        next.state = "typing";
    else if (hands.gesture == interaction::Gesture::Carry) {
        // What releasing the carried window does: the same tests as the release itself.
        next = {"holding", "", -1, -1};
        float heldDepth = dot(hands.pose.center - pointerCamera().position, pointerRay());
        for (auto &f : capture.workspaces)
            if (auto h = surfaces::intersect(f.surface, pointerCamera().position, pointerRay());
                h && h->distance < 12 && h->distance <= heldDepth + .5f)
                next = {"valid", "drop", h->distance, 12};
        if (next.state == "holding")
            if (auto held = capture.find(hands.pose.id))
                if (mountTarget(*held))
                    next = {"valid", "mount", -1, config["player"]["reach_mount"]};
    } else if (hands.gesture != interaction::Gesture::None)
        next = {"holding", "", -1, -1};
    else if (superHeld) {
        auto h = aim();
        if (!h)
            if (auto back = aim(true); back && back->second.back && !capture.workspace(back->first))
                h = back;
        if (h) {
            auto e = capture.find(h->first);
            if (capture.workspace(h->first) || movable(e))
                ranged(h->second.distance, "reach_grab", "grab");
            else if (e)
                next = {"invalid", "grab", h->second.distance, config["player"]["reach_grab"]};
            else
                next.state = "neutral";
        } else if (auto o = instanceAim()) {
            // Movable world objects: grab in reach; a locked one is refused.
            auto *info = objectInfo(o->first.substr(7));
            if (info && info->locked)
                next = {"invalid", "grab", o->second.distance, config["player"]["reach_grab"]};
            else
                ranged(o->second.distance, "reach_grab", "grab");
        } else if (auto c = characterAim()) {
            if (carriable(c->first)) // the companion and user characters can be carried
                ranged(c->second, "reach_grab", "grab");
            else
                next = {"invalid", "grab", c->second, config["player"]["reach_grab"]}; // residents are protected
        }
        else
            next.state = "neutral";
    } else {
        next.state = "neutral";
        auto blocker = interaction::raycast(pointerCamera().position, pointerRay(), world->collision, &world->collisionIndex);
        auto unplaced = capture.find(unplacedSelected);
        if (unplaced && !layout.unplaced.contains(unplaced->surface.id))
            unplaced = nullptr;
        switch (editor.slot) {
        case 1:
            if (auto h = aim(); h && !capture.workspace(h->first) &&
                (npcAimed.is_null() || !characterAim() || characterAim()->second > h->second.distance))
                next = {"target", "click", h->second.distance, -1};
            else if (!npcAimed.is_null())
                next = {"valid", "interact", -1, input.config["useDistance"]};
            break;
        case 2:
            // The click's own order: a window under the reticle is (re)selected first.
            if (auto h = aim(); h && movable(capture.find(h->first)))
                next = {"valid", "select", h->second.distance, -1};
            else if (auto e = tagged.empty() ? nullptr : capture.find(tagged)) {
                if (mountTarget(*e))
                    next = {"valid", "mount", -1, config["player"]["reach_mount"]};
                else if (blocker)
                    ranged(blocker->distance, "reach_placement", "mount");
            }
            break;
        case 3:
            if (blocker)
                ranged(blocker->distance, "reach_placement", "path");
            if (blocker && !pathTargetWalkable)
                next.state = "invalid"; // a route point off walkable floor
            break;
        case 4:
            // Position tool: a handle under the reticle, a drag (refused poses invalid),
            // or something to select.
            if (gizmo.dragging() || objectCarry)
                next = {preview.active && !preview.valid ? "invalid" : "holding", preview.active && !preview.valid ? "place" : "grab", -1, -1};
            else if (gizmoHover != interaction::Part::None)
                next = {"target", "grab", -1, -1};
            else if (auto h = aim(); h && !capture.workspace(h->first))
                next = {"valid", "select", h->second.distance, -1};
            else if (auto o = objectAim())
                next = {"valid", "select", o->second.distance, -1};
            else if (auto c = characterAim())
                next = {"invalid", "select", c->second, -1};
            break;
        case 5:
            if (pocketSelected()) {
                // The object ghost's verdict: placeable here, out of reach, or refused.
                if (objectGhost.active && objectGhost.asset >= 0 && !objectGhost.message.empty() && objectGhost.rule == "support")
                    next = {"invalid", "place", objectGhost.distance, config["player"]["reach_placement"]};
                else if (objectGhost.active && objectGhost.asset >= 0)
                    next = {objectGhost.valid ? "valid" : objectGhost.rule == "reach" ? "out_of_range" : "invalid", "place",
                            objectGhost.distance, config["player"]["reach_placement"]};
            } else if (unplaced) {
                if (mountTarget(*unplaced))
                    next = {"valid", "mount", -1, config["player"]["reach_mount"]};
                else if (blocker) {
                    ranged(blocker->distance, "reach_placement", "place");
                    if (next.state == "valid") {
                        // The surface placement's own test: room for the window here.
                        auto placed = unplaced->surface;
                        placed.yaw = 0;
                        placed.rotation = interaction::orientation(blocker->normal);
                        placed.center = blocker->point + blocker->normal * .06f;
                        if (!surfaces::pushOut(placed, world->collision) || !layout.vacant(placed, placed.id))
                            next.state = "invalid";
                    }
                }
            } else if (auto h = aim(); h && !capture.workspace(h->first)) {
                if (movable(capture.find(h->first)))
                    ranged(h->second.distance, "reach_placement", "pocket");
                else
                    next = {"invalid", "pocket", h->second.distance, config["player"]["reach_placement"]};
            } else if (auto o = instanceAim()) {
                auto *info = objectInfo(o->first.substr(7));
                if (info && info->source == "user" && !info->locked)
                    ranged(o->second.distance, "reach_placement", "pocket");
                else
                    next = {"invalid", "pocket", o->second.distance, config["player"]["reach_placement"]};
            } else if (auto c = characterAim())
                next = {"invalid", "pocket", c->second, config["player"]["reach_placement"]};
            break;
        default:
            break;
        }
    }
    if (next.state != "hidden" && next.state != "typing" && std::chrono::steady_clock::now() < reticleFlashUntil)
        next = reticleFlash; // a refused action: out of range for a moment
    bool changed = next.state != reticle.state || next.action != reticle.action;
    reticle = next;
    if (changed)
        publish();
}
void Runtime::handTick(float dt) {
    if (hands.gesture == interaction::Gesture::None)
        return;
    auto e = capture.find(hands.pose.id);
    if (!e) {
        hands.end();
        gestureButton = 0;
        return;
    }
    if (hands.update(camera, pointerRay(), dt, world->collision, &world->collisionIndex)) {
        layout.freePoses[e->surface.id] = hands.pose;
        if ((hands.gesture == interaction::Gesture::Resize || hands.gesture == interaction::Gesture::CornerResize) &&
            e->window->m_target) {
            auto w = e->window.lock();
            auto box = w->m_target->position();
            double width = interaction::resizeExtent(
                double(hands.pose.width / hands.start.width * hands.start.pixelWidth), 160., monitor->m_size.x);
            double height = interaction::resizeExtent(
                double(hands.pose.height / hands.start.height * hands.start.pixelHeight), 100., monitor->m_size.y);
            if (std::abs(box.w - width) >= 1 || std::abs(box.h - height) >= 1) {
                auto floated = Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_ENABLE, w);
                if (!floated)
                    throw std::runtime_error("client float rejected: " + floated.error().message);
                auto result = Config::Actions::resize({width, height}, false, w);
                if (!result)
                    throw std::runtime_error("client resize rejected: " + result.error().message);
                w->m_target->rememberFloatingSize({width, height});
                w->sendWindowSize(true);
                if (auto c = layout.contentFor(e->instance, world.get())) {
                    auto sizing = *c;
                    sizing["logicalSize"] = {int(std::round(width)), int(std::round(height))};
                    sizing["worldSize"] = {hands.pose.width, hands.pose.height};
                    layout.setContent(e->instance, world.get(), sizing);
                }
            }
        }
    }
}
void Runtime::button(IPointer::SButtonEvent event, bool super) {
    bool down = event.state == WL_POINTER_BUTTON_STATE_PRESSED;
    // A client press owns its physical release even after mode/focus/config changes.
    if (!down && buttons.erase(event.button)) {
        if (grabResource) {
            g_pSeatManager->sendPointerButton(event.timeMs, event.button, event.state);
            g_pSeatManager->sendPointerFrame();
        }
        if (buttons.empty()) {
            grabbed.clear();
            grabResource.reset();
        }
        return;
    }
    if (!down && event.button == gestureButton) {
        auto id = hands.pose.id;
        auto g = hands.gesture;
        hands.end();
        gestureButton = 0;
        if (g == interaction::Gesture::Carry) {
            auto held = capture.find(id);
            std::optional<std::pair<int, float>> wall;
            // Drop into a workspace wall only when the window was brought to it. A wall
            // merely behind the held window (every area main wall is a home) keeps the
            // release a normal placement instead of swallowing the window.
            float heldDepth = dot(hands.pose.center - pointerCamera().position, pointerRay());
            for (auto &f : capture.workspaces)
                if (auto h = surfaces::intersect(f.surface, pointerCamera().position, pointerRay());
                    h && h->distance < 12 && h->distance <= heldDepth + .5f && (!wall || h->distance < wall->second))
                    wall = {{f.workspaceId, h->distance}};
            if (held && wall) {
                moveWindow(held->window.lock(), layout.release(held->instance, world.get(), wall->first), true);
            } else if (held) {
                layout.carried.clear();
                if (auto snapped = mountTarget(*held))
                    layout.placeOnMount(held->instance, *snapped->first, world.get());
                else if (layout.homeOf(held->instance, world.get()))
                    layout.mount(held->instance, world.get(), hands.pose);
            }
        }
        if (g != interaction::Gesture::Carry)
            if (auto held = capture.find(id)) {
                layout.carried.clear();
                if (layout.homeOf(held->instance, world.get()))
                    layout.mount(held->instance, world.get(), hands.pose);
            }
        refresh();
        publish();
        if (auto id = std::exchange(resumeUse, ""); !id.empty() && capture.find(id))
            try {
                focus(id, true, true); // back to typing in the window the gesture started from
            } catch (const std::exception &) {
            }
        return;
    }
    if (!ownsInput())
        return;
    if (event.button == config["input"]["typing_button"].get<uint32_t>() && down) {
        modeToggle();
        return;
    }
    auto hit = aim();
    if (!grabbed.empty())
        if (auto plane = capture.plane(grabbed))
            if (auto h = surfaces::intersect(*plane, pointerCamera().position, pointerRay(), false))
                hit = {{grabbed, *h}};
    if (down && super &&
        (event.button == config["input"]["carry_button"].get<uint32_t>() ||
         event.button == config["input"]["resize_button"].get<uint32_t>())) {
        // A window seen from behind can still be carried (never clicked): it turns to face us.
        if (!hit && event.button == config["input"]["carry_button"].get<uint32_t>())
            if (auto back = aim(true); back && back->second.back && !capture.workspace(back->first))
                hit = back;
        if (!hit)
            return;
        if (hit->second.distance > config["player"]["reach_grab"].get<float>()) {
            refuseTooFar(hit->second.distance, config["player"]["reach_grab"], "grab", "interaction.out_of_range");
            return;
        }
        auto e = capture.find(hit->first);
        if (auto wall = capture.workspace(hit->first)) {
            // Pull the actual topmost native client under the workspace-wall hit.
            float x = hit->second.u * monitor->m_size.x + monitor->m_position.x,
                  y = hit->second.v * monitor->m_size.y + monitor->m_position.y;
            // Topmost as the compositor stacks them, not by capture order (a tiled window
            // captured last was taken from under the floating ones above it).
            const auto &native = Desktop::windowState()->windows();
            std::vector<surfaces::NativeSlot> slots;
            std::vector<Capture::Entry *> owners;
            for (auto &entry : capture.entries)
                if (entry.instance.workspaceId == wall->workspaceId && !layout.pulled(entry.instance, world.get()))
                    if (auto w = entry.window.lock())
                        if (auto box = w->logicalBox()) {
                            int layer = Fullscreen::controller()->isFullscreen(w) ? 3
                                        : w->m_pinned                             ? 2
                                        : w->m_isFloating                         ? 1
                                                                                  : 0;
                            slots.push_back({box->x, box->y, box->w, box->h, layer,
                                             size_t(std::find(native.begin(), native.end(), w) - native.begin())});
                            owners.push_back(&entry);
                        }
            if (auto top = surfaces::topmostAt(slots, x, y)) {
                e = owners[*top];
                auto box = e->window.lock()->logicalBox();
                auto s = wall->surface;
                s.id = e->surface.id;
                s.pixelWidth = e->surface.pixelWidth;
                s.pixelHeight = e->surface.pixelHeight;
                s.width = wall->surface.width * box->w / monitor->m_size.x;
                s.height = wall->surface.height * box->h / monitor->m_size.y;
                s.center =
                    wall->surface.center +
                    wall->surface.right() * ((box->x - monitor->m_position.x + box->w / 2) / monitor->m_size.x - .5f) *
                        wall->surface.width +
                    wall->surface.up() * (.5f - (box->y - monitor->m_position.y + box->h / 2) / monitor->m_size.y) *
                        wall->surface.height +
                    wall->surface.normal() * .06f;
                e->surface = s;
                if (auto h = surfaces::intersect(s, pointerCamera().position, pointerRay()))
                    hit = {{s.id, *h}};
            }
        }
        if (!e || !e->parentId.empty() || editor.locked(world->id, e->instance))
            return;
        // A gesture started while typing into a window returns to typing there afterwards.
        resumeUse = typing ? focused : "";
        cancelGrab();
        typing = false;
        keys.clear();
        smoothIntent = {};
        if (layout.homeOf(e->instance, world.get()))
            layout.pull(e->instance, world.get());
        auto g = event.button == config["input"]["carry_button"].get<uint32_t>() ? interaction::Gesture::Carry
                                                                                 : interaction::Gesture::CornerResize;
        // Super+LMB on a window's edge band (about 10 cm) resizes from the nearest corner,
        // as on the desktop; anywhere else it carries.
        if (g == interaction::Gesture::Carry && !hit->second.back) {
            float bu = std::clamp(.1f / std::max(e->surface.width, .01f), .03f, .12f),
                  bv = std::clamp(.1f / std::max(e->surface.height, .01f), .03f, .12f);
            if (hit->second.u < bu || hit->second.u > 1 - bu || hit->second.v < bv || hit->second.v > 1 - bv)
                g = interaction::Gesture::CornerResize;
        }
        if ((g == interaction::Gesture::Resize || g == interaction::Gesture::CornerResize) &&
            Fullscreen::controller()->isFullscreen(e->window.lock(), Fullscreen::FSMODE_MAXIMIZED))
            Fullscreen::controller()->setFullscreenMode(e->window.lock(), Fullscreen::FSMODE_NONE,
                                                        Fullscreen::FSMODE_NONE);
        hands.begin(g, e->surface, hit->second, camera);
        if (g == interaction::Gesture::Carry) {
            auto previous = hands.pose;
            bool fromBehind = hit->second.back;
            hands.facing(fromBehind || config["windows"]["carry_face_player"].get<bool>(), camera);
            if (!interaction::sweepSafe(previous, hands.pose, world->collision, &world->collisionIndex)) {
                hands.pose = previous;
                hands.facing(false, camera);
            } else if (fromBehind) {
                bool reduced = config["graphics"]["reduced_motion"] == true;
                hands.flipFrom(previous, reduced ? 0.f : config["windows"]["transition_seconds"].get<float>());
            }
        }
        layout.freePoses[e->surface.id] = hands.pose;
        gestureButton = event.button;
        return;
    }
    if (down && hit)
        if (auto t = capture.target(hit->first, hit->second.u, hit->second.v)) {
            if (buttons.empty()) {
                // A child press preserves its parent keyboard focus and popup grab serial.
                if (focused != t->parentId)
                    focus(t->parentId, true, typing);
                if (typing && relativeSurface.empty()) {
                    relativeSurface = capture.workspace(hit->first) ? hit->first : t->parentId;
                    localX = hit->second.u;
                    localY = hit->second.v;
                    projectCursor();
                }
                grabbed = hit->first;
                grabResource = t->resource;
            }
            buttons.insert(event.button);
            routePointer(hit->first, hit->second, event.timeMs);
        }
    if (grabResource) {
        g_pSeatManager->sendPointerButton(event.timeMs, event.button, event.state);
        g_pSeatManager->sendPointerFrame();
    }
    if (!down) {
        buttons.erase(event.button);
        if (buttons.empty()) {
            grabbed.clear();
            grabResource.reset();
        }
    }
}
void Runtime::axis(IPointer::SAxisEvent event) {
    if (!ownsInput())
        return;
    float raw = event.deltaDiscrete ? event.deltaDiscrete / 120.f : event.delta * .05f;
    float steps =
        std::clamp(event.relativeDirection == WL_POINTER_AXIS_RELATIVE_DIRECTION_INVERTED ? raw : -raw, -2.f, 2.f);
    if (zoomHeld) {
        zoom = std::clamp(zoom * std::pow(1.12f, steps), 1.f, 20.f);
        return;
    }
    if (hands.gesture == interaction::Gesture::Carry) {
        hands.wheel(steps * std::log(config["windows"]["wheel_factor"].get<float>()) / std::log(1.06f));
        return;
    }
    auto hit = aim();
    if (!hit)
        return;
    routePointer(hit->first, hit->second, event.timeMs);
    if (capture.target(hit->first, hit->second.u, hit->second.v)) {
        g_pSeatManager->sendPointerAxis(event.timeMs, event.axis, event.delta, event.deltaDiscrete / 120,
                                        event.deltaDiscrete, event.source, event.relativeDirection);
        g_pSeatManager->sendPointerFrame();
    }
}
void Runtime::transitionTick(float dt) {
    float duration =
        config["graphics"]["reduced_motion"] == true ? 0.f : config["windows"]["transition_seconds"].get<float>();
    if (handover.phase) {
        handover.elapsed += dt;
        float t = duration ? std::min(1.f, handover.elapsed / duration) : 1.f;
        t = t * t * (3 - 2 * t);
        camera.position = handover.from.position + (handover.to.position - handover.from.position) * t;
        camera.yaw = handover.from.yaw + std::remainder(handover.to.yaw - handover.from.yaw, 6.2831853f) * t;
        camera.pitch = handover.from.pitch + (handover.to.pitch - handover.from.pitch) * t;
        if (t >= 1) {
            int phase = handover.phase;
            handover.phase = 0;
            if (phase == 2)
                handover.animated = false;
            if (phase == 1)
                focus("workspace-" + std::to_string(handover.workspace), true);
        }
    }
    auto fs = Fullscreen::controller()->getFullscreenWindow(monitor.lock());
    if (fullscreen.phase) {
        auto old = capture.find(fullscreen.id);
        if (!old || (fs && fs != old->window)) {
            cancelGrab();
            keys.clear();
            input.clear();
            smoothIntent = {};
            typing = false;
            relativeSurface.clear();
            focused.clear();
            focusedParent.clear();
            if (old) {
                old->surface = fullscreen.before;
                if (layout.freePoses.contains(fullscreen.id))
                    layout.freePoses[fullscreen.id] = fullscreen.before;
            } else
                layout.freePoses.erase(fullscreen.id);
            restoreFullscreenWindows();
            fullscreen = {};
            cursor();
        }
    }
    if (fullscreen.phase == 0 && fs)
        for (auto &e : capture.entries)
            if (e.window == fs) {
                cancelGrab();
                keys.clear();
                fullscreen.id = e.surface.id;
                fullscreen.before = fullscreen.from = e.surface;
                fullscreen.phase = 1;
                fullscreen.elapsed = 0;
                break;
            }
    if (fullscreen.phase == 2 && !fs) {
        fullscreen.phase = 3;
        fullscreen.from = fullscreen.to;
        fullscreen.to = fullscreen.before;
        fullscreen.elapsed = 0;
    }
    if (fullscreen.phase == 1 && !fs) {
        fullscreen.phase = 3;
        fullscreen.from = capture.find(fullscreen.id)->surface;
        fullscreen.to = fullscreen.before;
        fullscreen.elapsed = 0;
    }
    if (fullscreen.phase == 1 || fullscreen.phase == 3) {
        if (fullscreen.phase == 1) {
            auto &s = fullscreen.to;
            s = fullscreen.before;
            s.center = camera.position + camera.forward();
            s.yaw = 0;
            s.rotation = interaction::orientation(camera.forward() * -1);
            s.height = 2 * std::tan(camera.fov * .01745329252f / 2) * 1.01f;
            s.width = s.height * monitor->m_pixelSize.x / monitor->m_pixelSize.y;
        }
        fullscreen.elapsed += dt;
        float t = duration ? std::min(1.f, fullscreen.elapsed / duration) : 1.f;
        t = t * t * (3 - 2 * t);
        auto s = fullscreen.from;
        s.center = s.center + (fullscreen.to.center - s.center) * t;
        s.width += (fullscreen.to.width - s.width) * t;
        s.height += (fullscreen.to.height - s.height) * t;
        s.yaw += std::remainder(fullscreen.to.yaw - s.yaw, 2 * std::numbers::pi_v<float>) * t;
        float sign = 0, norm = 0;
        for (int j = 0; j < 4; j++)
            sign += fullscreen.from.rotation[j] * fullscreen.to.rotation[j];
        for (int j = 0; j < 4; j++) {
            s.rotation[j] = fullscreen.from.rotation[j] * (1 - t) + fullscreen.to.rotation[j] * (sign < 0 ? -t : t);
            norm += s.rotation[j] * s.rotation[j];
        }
        for (auto &q : s.rotation)
            q /= std::sqrt(norm);
        if (auto e = capture.find(fullscreen.id)) {
            e->surface = s;
            e->visible = true;
        }
        if (t >= 1) {
            if (fullscreen.phase == 1) {
                fullscreen.phase = 2;
                for (auto &e : capture.entries)
                    if (e.surface.id != fullscreen.id && e.parentId.empty() && e.window) {
                        fullscreen.covering.push_back({e.window, e.window->m_allowedOverFullscreen});
                        e.window->m_allowedOverFullscreen = false;
                    }
                if (auto entry = capture.find(fullscreen.id)) {
                    Desktop::focusState()->fullWindowFocus(entry->window.lock(), Desktop::FOCUS_REASON_OTHER,
                                                           entry->window->resource(), true);
                    Desktop::windowState()->raise(entry->window.lock());
                }
                typing = false;
                cancelGrab();
            } else {
                if (layout.freePoses.contains(fullscreen.id))
                    layout.freePoses[fullscreen.id] = fullscreen.before;
                restoreFullscreenWindows();
                fullscreen = {};
                typing = false;
                returnToWorldWorkspace();
            }
        }
    }
}
std::string Runtime::inputMode() const {
    if (layerCursor) return "cursor";
    if (control.uiVisible())
        return "menu";
    if (!layout.desktop.is_null() || fullscreen.phase || handover.phase)
        return "focus";
    return typing ? "interactive" : "world";
}
void Runtime::applyInput(const Json &j) {
    auto next = interaction::validateInput(j);
    if (next == input.config)
        return;
    if (next["modes"] != input.config["modes"]) {
        cancelGrab();
        keys.clear();
        smoothIntent = {};
        lookHeld = false;
        zoomHeld = false;
        cancelGizmo();
        objectCarry = false;
    }
    input.apply(next);
    control.holdSeconds = input.config["longHoldMs"].get<float>() / 1000.f;
    publish();
}
Json Runtime::steals(const Json &config) const {
    Json out = Json::array();
    for (auto &[mode, actions] : config["modes"].items())
        for (auto &[action, bindings] : actions.items())
            for (auto &value : bindings) {
                auto t = interaction::parseBinding(value);
                auto sym = xkb_keysym_from_name(t.key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
                std::string native = t.key == "LMB"       ? "mouse:272"
                                     : t.key == "RMB"     ? "mouse:273"
                                     : t.key == "MMB"     ? "mouse:274"
                                     : t.key == "Back"    ? "mouse:275"
                                     : t.key == "Forward" ? "mouse:276"
                                                          : t.key;
                for (auto &bind : g_pKeybindManager->m_keybinds) {
                    if (!bind->enabled || (!bind->ignoreMods && bind->modmask != t.mods))
                        continue;
                    auto bs = xkb_keysym_to_lower(xkb_keysym_from_name(bind->key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE));
                    bool match = bind->key == native || (sym != XKB_KEY_NoSymbol && bs == sym) ||
                                 (t.key == "Wheel" && (bind->key == "mouse_up" || bind->key == "mouse_down")) ||
                                 (t.key == "WheelUp" && bind->key == "mouse_up") ||
                                 (t.key == "WheelDown" && bind->key == "mouse_down");
                    if (match)
                        out.push_back({{"mode", mode},
                                       {"action", action},
                                       {"binding", value},
                                       {"description",
                                        ipc::boundedText(bind->description.empty() ? bind->handler + " " + bind->arg
                                                                                   : bind->description,
                                                         ipc::DescriptionLimit)},
                                       {"submap", ipc::boundedText(bind->submap.name, ipc::SubmapLimit)}});
                }
            }
    return out;
}
void Runtime::acceptCapture(const std::string &key, unsigned mods, bool down) {
    if (captureRequest.is_null() || captureRequest.value("status", "") != "waiting")
        return;
    bool modifier = key == "Super_L" || key == "Super_R" || key == "Shift_L" || key == "Shift_R" ||
                    key == "Control_L" || key == "Control_R" || key == "Alt_L" || key == "Alt_R";
    if ((modifier && down) || (!modifier && !down))
        return;
    if (modifier) {
        unsigned bit = key.starts_with("Super")     ? 64
                       : key.starts_with("Shift")   ? 1
                       : key.starts_with("Control") ? 4
                                                    : 8;
        mods &= ~bit;
    }
    bool hold = captureRequest["action"] == "menu";
    auto existing =
        input.config["modes"][captureRequest["mode"].get<std::string>()][captureRequest["action"].get<std::string>()];
    auto slot = captureRequest["slot"].get<size_t>();
    if (slot < existing.size())
        hold = hold || interaction::parseBinding(existing[slot]).hold;
    if (key.starts_with("Wheel") || key == "MouseMove")
        hold = false;
    captureRequest["binding"] = interaction::bindingName({key, mods, hold});
    captureRequest["status"] = "captured";
    publish();
}
void Runtime::projectCursor() {
    if (auto s = capture.plane(relativeSurface)) {
        auto point = s->center + s->right() * ((localX - .5f) * s->width) + s->up() * ((.5f - localY) * s->height);
        auto projected = camera.project(point, monitor->m_size.x / monitor->m_size.y);
        if (projected.depth > .01f) { pointerX=projected.x; pointerY=projected.y; }
    }
}
void Runtime::useAim(bool zoom) {
    if (!world || loading)
        return;
    auto hit = aim();
    {
        float best = input.config["useDistance"];
        if (hit)
            best = std::min(best, hit->second.distance);
        // The tolerance margin must keep the same occlusion policy as aim().
        // Otherwise a rejected ray can re-enter a window through solid geometry.
        if (auto obstacle =
                interaction::raycast(pointerCamera().position, pointerRay(), world->collision, &world->collisionIndex))
            best = std::min(best, obstacle->distance + .01f);
        for (auto &s : capture.visible())
            if (auto h = surfaces::intersect(s, pointerCamera().position, pointerRay(), false)) {
                float margin = input.config["useTolerance"];
                if (h->u >= -margin / s.width && h->u <= 1 + margin / s.width && h->v >= -margin / s.height &&
                    h->v <= 1 + margin / s.height && h->distance < best) {
                    best = h->distance;
                    h->u = std::clamp(h->u, 0.f, 1.f - 1.f / std::max(1, s.pixelWidth));
                    h->v = std::clamp(h->v, 0.f, 1.f - 1.f / std::max(1, s.pixelHeight));
                    hit = {{s.id, *h}};
                }
            }
    }
    if (hit && !zoom && hit->second.distance > input.config["useDistance"].get<float>() && !capture.workspace(hit->first))
        refuseTooFar(hit->second.distance, input.config["useDistance"], "use", "interaction.out_of_range");
    if (!hit || (!zoom && hit->second.distance > input.config["useDistance"].get<float>()))
        return;
    cancelGrab();
    input.clear();
    keys.clear();
    smoothIntent = {};
    zoomHeld = false;
    this->zoom = 1;
    camera.fov = baseFov;
    if (auto wall = capture.workspace(hit->first)) {
        if (zoom) {
            relativeSurface.clear();
            focusWall(hit->first);
            return;
        }
        if (auto b = layout.binding(wall->workspaceId, world.get()))
            resolvedLocation =
                std::to_string(wall->workspaceId) + ":" + world->id + ":" + b->at("areaId").get<std::string>();
        Desktop::focusState()->rawMonitorFocus(monitor.lock());
        auto r = g_pKeybindManager->m_dispatchers.at("workspace")(std::to_string(wall->workspaceId));
        if (!r.success)
            throw std::runtime_error("workspace use failed");
        typing = true;
        relativeSurface = hit->first;
        localX = hit->second.u;
        localY = hit->second.v;
        projectCursor();
        if (auto target = capture.target(hit->first, localX, localY)) {
            focus(target->parentId, true);
            relativeSurface = hit->first;
        }
        cursor();
        publish();
        return;
    }
    auto target = capture.target(hit->first, hit->second.u, hit->second.v);
    if (!target)
        return;
    focus(target->parentId, true);
    relativeSurface = target->parentId;
    localX = hit->second.u;
    localY = hit->second.v;
    projectCursor();
    if (zoom)
        if (auto e = capture.find(target->parentId)) {
            relativeSurface.clear();
            if (auto w = e->window.lock(); w && w->m_target) {
                auto box = w->m_target->position();
                w->m_target->rememberFloatingSize({box.w, box.h});
            }
            Fullscreen::controller()->setFullscreenMode(e->window.lock(), Fullscreen::FSMODE_FULLSCREEN,
                                                        Fullscreen::FSMODE_FULLSCREEN);
        }
    cursor();
    publish();
}
void Runtime::resumeHeldMovement() {
    auto kb = g_pSeatManager->m_keyboard.lock();
    if (!active || !kb || !kb->m_xkbSymState || inputMode() != "world")
        return;
    const auto &world = input.config["modes"]["world"];
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch()).count();
    for (uint32_t code = 1; code < 256; ++code) {
        if (!kb->getPressed(code) || swallowedKeys.contains(code))
            continue;
        auto key = interaction::bindingKey(kb->m_xkbSymState, code + 8);
        // Only continuous movement resumes; a held Enter, U or Tab never re-fires.
        bool movement = false;
        for (auto name : {"forward", "back", "left", "right"})
            for (const auto &binding : world.value(name, Json::array()))
                movement = movement || binding == key;
        if (!movement)
            continue;
        swallowedKeys.insert(code); // its release ends the movement through the normal path
        for (auto a : inputEvent("world", key, modifiers, true, now, 0, false))
            action(a);
    }
}
void Runtime::leaveInteraction() {
    layerCursor = false;
    if (monitor && (freeCursor || monitor != Desktop::focusState()->monitor())) {
        Desktop::focusState()->rawMonitorFocus(monitor.lock());
        Pointer::pointerController()->warpTo(monitor->m_position + monitor->m_size * .5, true);
        freeCursor = false;
    }
    captureRequest = nullptr;
    input.clear();
    cancelGrab();
    keys.clear();
    smoothIntent = {};
    zoomHeld = false;
    relativeSurface.clear();
    objectCarry = false;
    cancelGizmo();
    if (control.uiVisible()) {
        control.stop();
        cursor();
        publish();
        return;
    }
    if (!layout.desktop.is_null()) {
        unfocus();
        return;
    }
    if (fullscreen.phase) {
        if (auto e = capture.find(fullscreen.id))
            Fullscreen::controller()->setFullscreenMode(e->window.lock(), Fullscreen::FSMODE_NONE,
                                                        Fullscreen::FSMODE_NONE);
        return;
    }
    typing = false;
    freeCursor = false;
    focused.clear();
    focusedParent.clear();
    g_pSeatManager->setPointerFocus(nullptr, {});
    g_pSeatManager->setKeyboardFocus(nullptr);
    pointerX = pointerY = .5f;
    returnToWorldWorkspace();
    cursor();
    publish();
}
void Runtime::debugCapture() {
    if (renderPass.active())
        throw std::runtime_error("capture inside render pass");
    if (*captureSnapshots >= 4)
        throw ipc::Error(-32004, "capture queue full; still writing earlier captures");
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    if (!renderer.completed()) {
        renderer.captureRequested = true;
        capturePending = true;
        return;
    }
    capturePending = false;
    renderer.captureRequested = false;
    auto pixels = renderer.snapshot();
    auto now = std::chrono::system_clock::now();
    int64_t stamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    stamp = std::max(stamp, lastCaptureStamp + 1); // unique IDs for rapid captures
    lastCaptureStamp = stamp;
    const auto captureId = "capture-" + std::to_string(stamp);
    auto path = captureDirectory / (captureId + ".json");
    const auto captureMode = inputMode();
    Json surfaces = Json::array();
    for (auto &e : capture.entries)
        surfaces.push_back({{"id", e.surface.id},
                            {"title", e.surface.title},
                            {"position", {e.surface.center.x, e.surface.center.y, e.surface.center.z}},
                            {"rotation", e.surface.rotation},
                            {"size", {e.surface.width, e.surface.height}},
                            {"parentId", e.parentId},
                            {"visible", e.visible},
                            {"wall", e.wall}});
    for (auto &child : capture.children)
        surfaces.push_back({{"id", child.surface.id},
                            {"parentId", child.parentId},
                            {"visible", child.visible},
                            {"offset", {child.offset.x, child.offset.y}},
                            {"position", {child.surface.center.x, child.surface.center.y, child.surface.center.z}},
                            {"size", {child.surface.width, child.surface.height}}});
    Json value = {{"version", 1},
                  {"timestampMs", stamp},
                  {"camera",
                   {{"position", {camera.position.x, camera.position.y, camera.position.z}},
                    {"yaw", camera.yaw},
                    {"pitch", camera.pitch},
                    {"fov", camera.fov},
                    {"eyeHeight", camera.eyeHeight}}},
                  {"mode", inputMode()},
                  {"tool", editor.slot},
                  {"submode", editor.submode()},
                  {"state", lastPublished},
                  {"surfaces", surfaces},
                  {"placements", layout.state(world.get())},
                  {"recentIPC", recentIPC},
                  {"recentEvents", recentEvents},
                  {"input", input.recent}};
    auto frameCamera = pixels.camera;
    value["camera"] = {{"position", {frameCamera.position.x, frameCamera.position.y, frameCamera.position.z}},
                       {"yaw", frameCamera.yaw},
                       {"pitch", frameCamera.pitch}, {"roll",frameCamera.roll}, {"lensShift",{frameCamera.shiftX,frameCamera.shiftY}},
                       {"fov", frameCamera.fov},
                       {"eyeHeight", frameCamera.eyeHeight}};
    value["frame"] = {{"number", renderer.frames},     {"width", pixels.width},
                      {"height", pixels.height},       {"readbackMs", pixels.readbackMs},
                      {"ageMs", pixels.ageMs},         {"source", "last-completed-world-frame"},
                      {"timings", renderer.timings()}, {"overlays", renderer.overlayDiagnostics}};
    auto image = path;
    image.replace_extension(".png");
    value["screenshot"] = image.string();
    value["frame"]["worldId"] = pixels.worldId;
    bool watermark = config["capture"]["watermark"].get<bool>();
    ++*captureSnapshots;
    captureJobs.push([path, image, watermark, value = std::move(value), pixels = std::move(pixels),
                      waiting = captureSnapshots]() mutable -> std::string {
            struct Release { std::shared_ptr<std::atomic<int>> n; ~Release() { --*n; } } release{waiting};
            // Writer thread, off the render pass: the optional brand mark touches only these pixels.
            render::WatermarkResult mark;
            if (watermark) {
                auto markStarted = std::chrono::steady_clock::now();
                try {
                    mark = render::applyWatermark(pixels.rgba, pixels.width, pixels.height);
                } catch (const std::exception &e) {
                    mark = {};
                    mark.reason = e.what();
                }
                value["frame"]["watermarkMs"] =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - markStarted).count();
            }
            value["frame"]["watermark"] = render::watermarkJson(watermark, mark);
            auto started = std::chrono::steady_clock::now();
            render::writePNG(image, pixels.width, pixels.height, pixels.rgba);
            value["frame"]["pngWriteMs"] =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            interaction::atomicJSON(path, value, 4 * 1048576);
            return "Saved JSON + PNG: " + path.string();
        });
    captureIds.push_back(captureId);
    while (captureIds.size() > 512)
        captureIds.pop_front();
    // IPC 0.17 shells offer a note prompt; the capture never waits for it.
    ipc->notify("capture.taken", {{"captureId", captureId}, {"timestampMs", stamp}, {"mode", captureMode}});
}
void Runtime::action(interaction::Action a) {
    if (control.photoHeld() && a.name != "capture" && a.name != "debug" && a.name != "record.toggle" && a.name != "world.toggle") return;
    if (a.name == "record.toggle") {
        if (a.pressed && a.edge && !session.playing) sessionMethod(session.recording ? "recording.stop" : "recording.start", Json::object());
        return;
    }
    if (a.name == "primary" || a.name == "secondary" || a.name == "alternate" || a.name == "cycle") {
        toolInput(a.name, a);
        return;
    }
    auto held = [&](uint32_t key) {
        if (a.pressed)
            keys.insert(key);
        else
            keys.erase(key);
    };
    if (a.name == "carry.facing") {
        if (a.pressed && a.edge && hands.gesture == interaction::Gesture::Carry) {
            auto old = hands;
            hands.facing(!hands.facePlayer, camera);
            if (!interaction::sweepSafe(old.pose, hands.pose, world->collision, &world->collisionIndex))
                hands = old;
            layout.freePoses[hands.pose.id] = hands.pose;
            refresh();
            publish();
        }
        return;
    }
    if (a.name == "content.scale" || a.name == "window.reset") {
        if (a.pressed)
            contentScale(a.name == "window.reset", a.value);
        return;
    }
    if (a.name == "look")
        lookHeld = a.pressed;
    else if (a.name == "forward")
        held(KEY_W);
    else if (a.name == "back")
        held(KEY_S);
    else if (a.name == "left")
        held(KEY_A);
    else if (a.name == "right")
        held(KEY_D);
    else if (a.name == "up") {
        held(KEY_SPACE);
        if (a.pressed && a.edge)
            climbing.jump();
    } else if (a.name == "down")
        held(KEY_LEFTCTRL);
    else if (a.name == "sprint")
        held(KEY_LEFTSHIFT);
    else if (a.name == "scope") {
        zoomHeld = a.pressed;
        if (a.pressed && a.edge)
            zoom = 2;
    } else if (a.name == "carry" || a.name == "resize") {
        uint32_t code = a.name == "carry" ? config["input"]["carry_button"].get<uint32_t>()
                                          : config["input"]["resize_button"].get<uint32_t>();
        // Carry needs no particular tool for movable world objects (RFC-0011); scene nodes
        // that own collision stay tool 4 targets.
        // The companion and user characters are the characters carry picks up (to set them
        // down on a route or a spot); authored residents stay protected.
        if (a.name == "carry" && a.pressed && a.edge && !aim() && !gizmo.dragging() && !instanceAim() && characterPickUp())
            return;
        if (a.name == "carry" && !a.pressed && !carried.empty()) {
            characterPutDown();
            return;
        }
        if (a.name == "carry" && a.pressed && a.edge && !aim() && !gizmo.dragging() && !instanceAim() &&
            protectCharacter("Carry", "grab"))
            return;
        if (a.name == "carry" && a.pressed && !aim() && !gizmo.dragging())
            if (auto object = editor.slot == 4 ? objectAim() : instanceAim()) {
                if (auto *info = object->first.starts_with("object:") ? objectInfo(object->first.substr(7)) : nullptr) {
                    if (info->locked) {
                        objectNotice("object.locked", info->name + " is locked");
                        reticleFlash = {"invalid", "grab", object->second.distance, config["player"]["reach_grab"]};
                        reticleFlashUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
                        return;
                    }
                    if (tooFar(object->second.distance, "reach_grab", "grab"))
                        return;
                }
                if (editor.selected != object->first)
                    gizmo.resetFlips();
                editor.selected = object->first;
                editor.kind = "object";
                objectCarry = true;
                objectDistance = object->second.distance;
                dragStart = selectedTransform = objectPose(editor.selected);
                dragPoint = object->second.point - dragStart.position;
                editBefore = currentRecord(world->id, editor.selected);
                preview = {};
                preview.active = true;
                preview.pose = dragStart;
                preview.waived = waivedRules(dragStart);
                return;
            }
        if (objectCarry && !a.pressed) {
            objectCarry = false;
            commitPreview();
            return;
        }
        button({.timeMs = 0,
                .button = code,
                .state = a.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED},
               true);
    } else if (a.name == "distance" || a.name == "rotate") {
        if (!a.pressed || !world)
            return;
        auto hit = aim();
        auto e = hands.gesture != interaction::Gesture::None ? capture.find(hands.pose.id)
                 : hit                                       ? capture.find(hit->first)
                                                             : nullptr;
        if (!e) {
            auto object = objectCarry ? std::optional<std::pair<std::string, interaction::WorldHit>>{} : objectAim();
            if (objectCarry || object) {
                if (object) {
                    if (auto *info = object->first.starts_with("object:") ? objectInfo(object->first.substr(7)) : nullptr;
                        info && info->locked) {
                        objectNotice("object.locked", info->name + " is locked");
                        return;
                    }
                    editor.selected = object->first;
                    editor.kind = "object";
                    selectedTransform = objectPose(editor.selected);
                } else
                    selectedTransform = objectPose(editor.selected);
                if (a.name == "distance") {
                    if (objectCarry)
                        objectDistance = std::clamp(objectDistance * std::pow(1.12f, a.value), .3f, 30.f);
                    else
                        selectedTransform.position += pointerRay() * a.value * .25f;
                } else {
                    auto q = selectedTransform.rotation;
                    float sn = std::sin(a.value * .1308997f), cs = std::cos(a.value * .1308997f);
                    selectedTransform.rotation = {q[0] * cs + q[1] * sn, q[1] * cs - q[0] * sn, q[2] * cs + q[3] * sn,
                                                  q[3] * cs - q[2] * sn};
                }
                if (objectCarry)
                    ; // the carry previews and commits on release
                else if (commitObject(selectedTransform, a.name == "distance" ? "Move" : "Rotate"))
                    toolsTick();
                publish();
            }
            return;
        }
        if (editor.locked(world->id, e->instance))
            return;
        if (a.name == "distance") {
            bool temporary = hands.gesture == interaction::Gesture::None;
            if (temporary)
                hands.begin(interaction::Gesture::Carry, e->surface, hit->second, camera);
            hands.wheel(a.value);
            handTick(.1f);
            if (temporary) {
                hands.end();
                layout.carried.clear();
                if (layout.homeOf(e->instance, world.get()))
                    layout.mount(e->instance, world.get(), hands.pose);
            }
        } else {
            auto old = hands.gesture != interaction::Gesture::None ? hands.pose : e->surface;
            auto next = old;
            auto q = old.rotation;
            float s = std::sin(a.value * .1308997f), c = std::cos(a.value * .1308997f);
            next.rotation = {q[0] * c + q[1] * s, q[1] * c - q[0] * s, q[2] * c + q[3] * s, q[3] * c - q[2] * s};
            if (interaction::sweepSafe(old, next, world->collision, &world->collisionIndex)) {
                layout.freePoses[e->surface.id] = next;
                if (hands.gesture != interaction::Gesture::None) {
                    auto x = dot(hands.offset, old.right()), y = dot(hands.offset, old.up());
                    hands.pose = hands.start = next;
                    hands.grabYaw = camera.yaw;
                    hands.offset = next.right() * x + next.up() * y;
                } else if (layout.homeOf(e->instance, world.get()))
                    layout.mount(e->instance, world.get(), next);
            }
        }
    } else if (!a.pressed || !a.edge)
        return;
    else if (a.name == "exit") {
        if (control.uiVisible()) leaveInteraction(); else toggleFreeCursor();
    }
    else if (a.name == "menu") {
        input.clear();
        control.openMenu();
    } else if (a.name == "markers.reveal") {
        markers.tap(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
    } else if (a.name == "use") {
        // A character's interaction (RFC-0011): the aimed one, unless a window is nearer;
        // E also advances a `say` and closes a choice list.
        if (a.pressed && world && (npcInteraction || !npcAimed.is_null())) {
            auto window = aim();
            auto character = characterAim();
            bool nearer = npcInteraction || (character && (!window || character->second < window->second.distance));
            if (nearer && interactUse())
                return;
        }
        if (!world || noclip || movement != &walk ||
            !climbing.use(camera, world->climbables,
                          sim::climbMode(config["movement"]["climb_mode"].get<std::string>())))
            useAim(false);
    } else if (a.name == "focus")
        useAim(true);
    else if (a.name == "debug")
        debug = !debug;
    else if (a.name == "capture")
        deferInput([] { runtime->debugCapture(); });
    else if (a.name == "noclip") {
        noclip = !noclip;
        climbing.reset(camera);
        camera.verticalSpeed = 0;
    } else if (a.name == "fly") {
        movement = movement == &walk ? static_cast<sim::MotionStrategy *>(&fly) : &walk;
        climbing.reset(camera);
        keys.clear();
        smoothIntent = {};
        camera.verticalSpeed = 0;
    } else if (a.name == "transport")
        ipc->notify("runtime.notice", {{"level", "info"},
                                       {"code", "transport.pending"},
                                       {"message", "Transport summon is reserved for the transport milestone."}});
    else if (a.name == "hud.cycle") {
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch()).count();
        ++hudSequence;
        for (auto &[actor, b] : bindings)
            if (monitor && control.ownsLease(actor, b.lease))
                ipc->notify("ui.hud.cycle", {{"leaseId", b.lease},
                    {"outputId", "output-" + std::to_string(monitor->m_id)},
                    {"sequence", hudSequence}, {"monotonicMs", now}}, actor);
    } else if (a.name == "autopilot") {
        if (control.driving())
            control.execute("autodrive.stop", Json::object(), "", "", world.get(), camera, {});
        else if (!control.location["route"].empty())
            control.execute("autodrive.start", Json::object(), "", "", world.get(), camera, {});
        else if (!control.location["waypoint"].is_null()) {
            control.execute("route.set", {{"points", {control.location["waypoint"]["target"]}}}, "", "", world.get(),
                            camera, {});
            control.execute("autodrive.start", Json::object(), "", "", world.get(), camera, {});
        } else
            ipc->notify("runtime.notice", {{"level", "info"},
                                           {"code", "autopilot.target"},
                                           {"message", "Set a waypoint or route before enabling autopilot."}});
    } else if (a.name.starts_with("tool")) {
        cancelGrab();
        cancelGizmo();
        objectCarry = false;
        editor.slot = tool = std::stoi(a.name.substr(4));
        tagged.clear();
        debugCard.clear();
        debugSelected.clear();
        if (npcDrawing && editor.activePath == npcDrawing->pathId)
            finishDrawing(); // leaving (or re-selecting) tool 3 finishes a drawn route
        editor.finishPath();
    }
    publish();
}
namespace {
std::optional<unsigned> editorNode(const world::World &w, const std::string &id) {
    if (w.objectNodes.contains(id))
        return w.objectNodes.at(id);
    if (id.starts_with("node-")) {
        auto text = id.substr(5);
        if (!text.empty() && text.find_first_not_of("0123456789") == std::string::npos) {
            auto n = std::stoul(text);
            if (n < w.nodeParents.size())
                return n;
        }
    }
    return {};
}
// World 0.8 placed objects are editor targets as "object:<instance id>" (overlay
// records keep the M3B shape; P1's overrides list would key them by instance id).
std::optional<size_t> instanceIndex(const world::World &w, const std::string &id) {
    if (!id.starts_with("object:"))
        return {};
    for (size_t k = 0; k < w.objectInstances.size(); ++k)
        if (id.compare(7, std::string::npos, w.objectInstances[k].id) == 0)
            return k;
    return {};
}
bool belongs(const world::World &w, unsigned n, unsigned root) {
    for (int i = n; i >= 0 && size_t(i) < w.nodeParents.size(); i = w.nodeParents[i])
        if (unsigned(i) == root)
            return true;
    return false;
}
Vec3 objectOrigin(const world::World &w, const std::string &id) {
    if (auto k = instanceIndex(w, id))
        return w.objectInstances[*k].position;
    for (auto &o : w.objects)
        if (o["id"] == id)
            return {o["pose"]["position"][0], o["pose"]["position"][1], o["pose"]["position"][2]};
    auto node = editorNode(w, id);
    if (!node)
        return {};
    Vec3 lo{100000, 100000, 100000}, hi{-100000, -100000, -100000};
    bool found = false;
    for (auto &m : w.meshes)
        if (belongs(w, m.node, *node))
            for (auto &v : m.vertices) {
                found = true;
                lo = {std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
                hi = {std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
            }
    return found ? (lo + hi) * .5f : Vec3{};
}
} // namespace
#include "Gizmo.inc"
#include "Objects.inc"
#include "Paths.inc"
#include "Npcs.inc"
#include "Companion.inc"
#include "DebugTool.inc"
void Runtime::saveEditor() {
    if (session.playing) return;
    auto value = editor.saved();
    if (value == lastEditor)
        return;
    interaction::atomicJSON(editorFile, value);
    if (!lastEditor.is_object() || value["paths"] != lastEditor["paths"])
        ++pathRevision; // characters on edited routes rebuild (Npcs.inc)
    lastEditor = value;
}
// Only geometry that owns its collision may be selected, carried or transformed; a static
// mesh in front blocks the aim. World package geometry is otherwise static.
std::optional<std::pair<std::string, interaction::WorldHit>> Runtime::objectAim() {
    auto hit = meshAim();
    // World 0.8 objects (env-gated): the nearest of a placed object and scene geometry.
    // Characters are never targets: NPCs are neither scene nodes nor placed objects.
    if (auto object = instanceAim(); object && (!hit || object->second.distance < hit->second.distance))
        return object;
    if (!hit)
        return {};
    auto node = editorNode(*world, hit->first);
    if (!node || !world::movableNode(*world, *node))
        return {};
    return hit;
}
std::optional<std::pair<std::string, interaction::WorldHit>> Runtime::meshAim() {
    if (!world)
        return {};
    auto o = pointerCamera().position, d = pointerRay();
    std::optional<std::pair<std::string, interaction::WorldHit>> result;
    auto name = [&](unsigned node) {
        std::string id = "node-" + std::to_string(node);
        for (auto &[n, root] : world->objectNodes)
            if (belongs(*world, node, root))
                return n;
        return id;
    };
    // Without a live preview, the visual triangles are the meshes at their committed
    // transforms: walk the ray through their index (every tick in tool 4).
    if (renderer.transforms == objectModels && !world->visualTriangles.empty() &&
        world->visualIndex.size() == world->visualTriangles.size()) {
        if (world->visualSource.size() != world->visualTriangles.size()) {
            world->visualSource.clear();
            for (unsigned m = 0; m < world->meshes.size(); ++m)
                for (unsigned i = 0; i + 2 < world->meshes[m].vertices.size(); i += 3)
                    world->visualSource.push_back({m, i});
        }
        if (world->visualSource.size() == world->visualTriangles.size()) {
            std::optional<unsigned> nearest;
            std::optional<interaction::WorldHit> hit;
            for (auto id : world->visualIndex.ray(o, d)) {
                auto h = interaction::raycast(o, d, std::span<const Triangle>(&world->visualTriangles[id], 1));
                if (h && (!hit || h->distance < hit->distance))
                    hit = h, nearest = id;
            }
            if (hit)
                result = {{name(world->meshes[world->visualSource[*nearest].first].node), *hit}};
            return result;
        }
    }
    float nearest = 100000;
    for (auto &m : world->meshes) {
        auto model = renderer.transforms.contains(m.node) ? renderer.transforms.at(m.node) : Mat4::identity();
        for (size_t i = 0; i + 2 < m.vertices.size(); i += 3) {
            Triangle t{model.point(m.vertices[i].position), model.point(m.vertices[i + 1].position),
                       model.point(m.vertices[i + 2].position)};
            auto h = interaction::raycast(o, d, std::span<const Triangle>(&t, 1));
            if (!h || h->distance >= nearest)
                continue;
            nearest = h->distance;
            result = {{name(m.node), *h}};
        }
    }
    return result;
}
std::optional<interaction::WorldHit> Runtime::worldHit() {
    if (!world)
        return {};
    auto mesh = meshAim();
    auto collision = interaction::raycast(pointerCamera().position, pointerRay(), world->collision, &world->collisionIndex);
    if (mesh && (!collision || mesh->second.distance < collision->distance))
        return mesh->second;
    return collision;
}
Json Runtime::unplacedState() {
    Json windows = Json::array();
    for (const auto &e : capture.entries)
        if (layout.unplaced.contains(e.surface.id))
            windows.push_back({{"surfaceId", e.surface.id},
                               {"title", e.instance.title},
                               {"appClass", e.instance.appClass},
                               {"logicalSize", {e.surface.pixelWidth, e.surface.pixelHeight}}});
    std::stable_sort(windows.begin(), windows.end(), [&](const Json &a, const Json &b) {
        auto rank = [&](const Json &v) {
            auto it =
                std::find(layout.pocketOrder.begin(), layout.pocketOrder.end(), v["surfaceId"].get<std::string>());
            return std::distance(layout.pocketOrder.begin(), it);
        };
        return rank(a) < rank(b);
    });
    // RFC-0011: the pocket queue holds windows and objects; the selection is either.
    bool selected = std::any_of(windows.begin(), windows.end(), [&](auto &v) { return v["surfaceId"] == unplacedSelected; }) ||
                    pocketSelected();
    if (!selected)
        unplacedSelected = !windows.empty() ? windows[0]["surfaceId"].get<std::string>()
                           : !pocketObjects.empty() ? pocketObjects.back().itemId
                                                    : "";
    bool any = !windows.empty() || !pocketObjects.empty();
    auto count = windows.size() + pocketObjects.size();
    // The wire lists at most 128 pocketed windows (IPC 0.26, the apps limit; older peers get 16),
    // in pocket order; the Placement tool's cycle covers all of them.
    if (windows.size() > pocketWindowLimit) {
        auto chosen = std::find_if(windows.begin(), windows.end(), [&](auto &v) { return v["surfaceId"] == unplacedSelected; });
        if (chosen != windows.end() && size_t(chosen - windows.begin()) >= pocketWindowLimit)
            std::rotate(windows.begin() + (pocketWindowLimit - 1), chosen, chosen + 1); // the selected one stays listed
        windows.erase(windows.begin() + pocketWindowLimit, windows.end());
    }
    return {{"preview", active && editor.slot == 5 && !freeCursor && any},
            {"count", std::min<size_t>(count, pocketWindowLimit + pocketObjectLimit)},
            {"windows", windows},
            {"objects", pocketObjectsJson()},
            {"selectedId", unplacedSelected.empty() ? Json(nullptr) : Json(unplacedSelected)}};
}
std::optional<std::pair<const Json *, surfaces::Surface>> Runtime::mountTarget(const Capture::Entry &e) {
    if (!world || !e.parentId.empty() || e.surface.pixelWidth <= 0 || e.surface.pixelHeight <= 0)
        return {};
    auto origin = pointerCamera().position;
    auto ray = pointerRay();
    auto blocker = interaction::raycast(origin, ray, world->collision, &world->collisionIndex);
    auto home = layout.binding(e.instance.workspaceId, world.get());
    std::optional<std::pair<const Json *, surfaces::Surface>> best;
    float nearest = INFINITY;
    for (const auto &mount : world->anchors) {
        if (mount.at("mount").at("role") != "tool-mount" || (home && mount.at("areaId") != home->at("areaId")))
            continue;
        auto id = mount.at("id").get<std::string>();
        if (std::any_of(layout.instances.begin(), layout.instances.end(),
                        [&](auto &v) { return v.value("anchorId", "") == id && v.at("surfaceId") != e.surface.id; }))
            continue;
        // The authored frame with a forgiving rim; the face may sit just behind its skin.
        surfaces::Surface frame;
        auto &p = mount.at("pose").at("position");
        frame.center = {p[0], p[1], p[2]};
        frame.rotation = mount.at("pose").at("rotation").get<std::array<float, 4>>();
        frame.yaw = 0;
        frame.width = mount.at("size")[0].get<float>() + .3f;
        frame.height = mount.at("size")[1].get<float>() + .3f;
        auto hit = surfaces::intersect(frame, origin, ray);
        if (!hit || hit->distance > config["player"]["reach_mount"].get<float>() || hit->distance >= nearest || (blocker && blocker->distance < hit->distance - .35f))
            continue;
        auto fitted = surfaces::fitToMount(mount, e.surface.pixelWidth, e.surface.pixelHeight, world->collision);
        if (!fitted)
            continue; // aspect outside its limits, or no collision-free fit
        fitted->id = e.surface.id;
        fitted->title = e.surface.title;
        fitted->texture = e.surface.texture;
        fitted->pixelWidth = e.surface.pixelWidth;
        fitted->pixelHeight = e.surface.pixelHeight;
        best = {{&mount, *fitted}};
        nearest = hit->distance;
    }
    return best;
}
std::optional<surfaces::Surface> Runtime::placementPreview() {
    auto e = capture.find(unplacedSelected);
    if (!e || !world || !layout.unplaced.contains(e->surface.id))
        return {};
    if (auto snapped = mountTarget(*e)) { // the ghost shows where it will sit: in the frame
        snapped->second.opacity = .35f;
        return snapped->second;
    }
    auto s = e->surface;
    s.yaw = -camera.yaw;
    s.rotation = {0, 0, 0, 1};
    s.center = camera.position + camera.forward() * config["windows"]["spawn_distance"].get<float>();
    s.opacity = .35f;
    return s;
}
void Runtime::contentScale(bool reset, float steps) {
    if (!world || !monitor)
        return;
    auto hit = aim();
    auto e = hands.gesture != interaction::Gesture::None ? capture.find(hands.pose.id)
             : hit                                       ? capture.find(hit->first)
                                                         : nullptr;
    if (!e && hands.gesture == interaction::Gesture::None && !objectCarry) {
        // On a movable world object: content.scale scales it by 1.12 per step within its
        // movable.scale range; window.reset resets it (RFC-0011 "Tools on objects").
        if (auto o = instanceAim()) {
            auto id = o->first.substr(7);
            if (reset)
                resetObject(id);
            else
                scaleObject(id, steps);
            if (editor.kind == "object" && editor.selected == o->first)
                selectedTransform = objectPose(editor.selected);
            publish();
        }
        return;
    }
    if (!e || !e->parentId.empty() || editor.locked(world->id, e->instance) || !e->window || !e->window->m_target)
        return;
    auto w = e->window.lock();
    auto s = e->surface;
    auto existing = layout.contentFor(e->instance, world.get());
    Json sizing = existing ? *existing
                           : Json{{"logicalSize", {s.pixelWidth, s.pixelHeight}},
                                  {"baseSize", {s.pixelWidth, s.pixelHeight}},
                                  {"worldSize", {s.width, s.height}}};
    auto dims = reset ? interaction::contentSize(sizing["baseSize"][0], sizing["baseSize"][1], 0, monitor->m_size.x,
                                                 monitor->m_size.y)
                      : interaction::contentSize(sizing["logicalSize"][0], sizing["logicalSize"][1], steps,
                                                 monitor->m_size.x, monitor->m_size.y);
    if (reset) {
        s.yaw = -camera.yaw;
        s.rotation = {0, 0, 0, 1};
        s.center = camera.position + camera.forward() * config["windows"]["spawn_distance"].get<float>();
        if (!surfaces::pushOut(s, world->collision) || !layout.vacant(s, s.id)) {
            ipc->notify("runtime.notice", {{"level", "info"},
                                           {"code", "placement.blocked"},
                                           {"message", "Reset needs a free spawn spot. Aim into an open area."}});
            return;
        }
    }
    sizing["logicalSize"] = dims;
    // Core size configuration is deferred via the input callback, never drawing.
    auto floated = Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_ENABLE, w);
    if (!floated)
        throw std::runtime_error("content scale float rejected: " + floated.error().message);
    auto result = Config::Actions::resize({double(dims[0]), double(dims[1])}, false, w);
    if (!result)
        throw std::runtime_error("content scale configure rejected: " + result.error().message);
    w->m_target->rememberFloatingSize({double(dims[0]), double(dims[1])});
    w->sendWindowSize(true);
    contentFeedback = {{"surfaceId", e->surface.id},
                       {"scale", sizing["baseSize"][0].get<double>() / dims[0]},
                       {"logicalSize", dims},
                       {"sequence", ++contentSequence},
                       {"reset", reset}};
    layout.setContent(e->instance, world.get(), sizing);
    layout.mount(e->instance, world.get(), s);
    if (hands.gesture != interaction::Gesture::None) {
        if (reset) {
            hands.end();
            gestureButton = 0;
        } else {
            hands.pose.pixelWidth = dims[0];
            hands.pose.pixelHeight = dims[1];
        }
    }
    refresh();
    publish();
}
void Runtime::toolInput(const std::string &slot, interaction::Action event) {
    if (slot == "cycle") {
        if (!event.pressed || !event.edge) return;
        // A key/button Cycle binding advances once; wheel bindings retain direction.
        if (event.value == 0) event.value = 1;
    }
    std::string action = editor.slotAction(slot);
    if (slot != "cycle") {
        if (event.pressed && event.edge)
            toolPressActions[slot] = action;
        else if (!event.pressed) {
            if (auto it = toolPressActions.find(slot); it != toolPressActions.end()) {
                action = it->second;
                toolPressActions.erase(it);
            }
        }
    }
    if (slot == "cycle" && zoomHeld) {
        zoom = std::clamp(zoom * std::pow(1.12f, event.value), 1.f, 20.f);
        return;
    }
    if (action.empty())
        return;
    if (action.starts_with("client.")) {
        if (action == "client.scroll")
            axis({.timeMs = 0,
                  .axis = WL_POINTER_AXIS_VERTICAL_SCROLL,
                  .delta = -event.value * 20.,
                  .deltaDiscrete = int32_t(-event.value * 120)});
        else
            button({.timeMs = 0,
                    .button = action == "client.primary"     ? 272u
                              : action == "client.secondary" ? 273u
                                                             : 274u,
                    .state = event.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED},
                   false);
        return;
    }
    if (action == "path.add" && !event.pressed) {
        pathRelease(false);
        publish();
        return;
    }
    if (action == "gizmo.drag" && !event.pressed) {
        // Typed values keep the drag open until Enter or Escape.
        if (!gizmo.numeric())
            gizmoRelease(false);
        publish();
        return;
    }
    if (!event.pressed || !event.edge || !world)
        return;
    if (action == "placement.pocket") {
        auto h = aim();
        auto e = h ? capture.find(h->first) : nullptr;
        if (!e && instanceAim()) {
            pocketAimedObject();
            publish();
            return;
        }
        if (!e && protectCharacter("Pocket", "pocket")) {
            publish();
            return;
        }
        if (!e || !e->parentId.empty() || editor.locked(world->id, e->instance) || tooFar(h->second.distance, "reach_placement", "pocket"))
            return;
        cancelGrab();
        layout.stash(e->instance, e->surface);
        unplacedSelected = e->surface.id;
        ipc->notify("runtime.notice",
                    {{"level", "info"}, {"code", "placement.pocketed"}, {"message", "Pocketed " + e->instance.title}});
        refresh();
    } else if (action == "placement.cycle") {
        auto state = unplacedState();
        std::vector<std::string> list;
        for (auto &w : state["windows"])
            list.push_back(w["surfaceId"]);
        for (auto &o : pocketObjects)
            list.push_back(o.itemId);
        if (!list.empty()) {
            int at = 0;
            for (size_t i = 0; i < list.size(); ++i)
                if (list[i] == unplacedSelected)
                    at = int(i);
            at = (at + int(list.size()) + (event.value > 0 ? 1 : -1)) % int(list.size());
            unplacedSelected = list[at];
        }
    } else if (action == "tool.cycle") {
        editor.wheel(event.value > 0 ? 1 : -1);
        cancelGizmo();
    } else if (action == "debug.inspect") {
        debugInspect();
    } else if (action == "debug.clear") {
        debugCard.clear();
        debugSelected.clear();
    } else if (action == "window.lock") {
        if (auto h = aim())
            if (auto e = capture.find(h->first)) {
                editor.toggleLock(world->id, e->instance);
                saveEditor();
            }
    } else if (action == "path.remove") {
        pathRemove();
    } else if (action == "path.closed") {
        editor.toggleClosed();
        saveEditor();
    } else if (action == "gizmo.space") {
        editor.localAxes = !editor.localAxes;
        cancelGizmo();
    } else if (action == "gizmo.snap")
        editor.snap = !editor.snap;
    else if (action == "placement.ahead" || action == "placement.surface")
        toolClick(action);
    else if (action == "window.mount" || action == "path.add" || action == "gizmo.drag")
        toolClick(action);
    publish();
}
void Runtime::toolClick(const std::string &action) {
    if (!world)
        return;
    if ((action == "placement.ahead" || action == "placement.surface") && pocketSelected()) {
        placeFromPocket(action == "placement.surface");
        publish();
        return;
    }
    if (action == "placement.ahead" || action == "placement.surface") {
        auto preview = placementPreview();
        if (!preview)
            return;
        auto e = capture.find(unplacedSelected);
        auto placed = *preview;
        placed.opacity = 1;
        auto snapped = mountTarget(*e);
        if (snapped) {
            // Into the tool mount's frame: its own anchored placement, at the client's size.
        } else if (action == "placement.surface") {
            auto hit = worldHit();
            if (!hit || tooFar(hit->distance, "reach_placement"))
                return;
            placed.yaw = 0;
            placed.rotation = interaction::orientation(hit->normal);
            placed.center = hit->point + hit->normal * .06f;
        }
        if (!snapped && (!surfaces::pushOut(placed, world->collision) || !layout.vacant(placed, placed.id))) {
            ipc->notify("runtime.notice", {{"level", "info"},
                                           {"code", "placement.blocked"},
                                           {"message", "No free space here. Aim at another spot."}});
            return;
        }
        auto w = e->window.lock();
        auto floated = Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_ENABLE, w);
        if (!floated)
            throw std::runtime_error("placement float rejected: " + floated.error().message);
        if (w->m_target) {
            auto resized = Config::Actions::resize({double(placed.pixelWidth), double(placed.pixelHeight)}, false, w);
            if (!resized)
                throw std::runtime_error("placement size rejected: " + resized.error().message);
            w->m_target->rememberFloatingSize({double(placed.pixelWidth), double(placed.pixelHeight)});
            w->sendWindowSize(true);
        }
        if (snapped)
            layout.placeOnMount(e->instance, *snapped->first, world.get());
        else
            layout.mount(e->instance, world.get(), placed);
        unplacedSelected.clear();
        refresh();
        publish();
        return;
    }
    if (action == "window.mount") {
        auto hit = aim();
        if (hit && capture.find(hit->first)) {
            tagged = hit->first;
            layout.freePoses[tagged] = capture.find(tagged)->surface;
            layout.carried.clear();
            return;
        }
        if (!tagged.empty())
            if (auto e = capture.find(tagged)) {
                if (auto snapped = mountTarget(*e)) {
                    layout.placeOnMount(e->instance, *snapped->first, world.get());
                    tagged.clear();
                    refresh();
                    publish();
                    return;
                }
                if (auto wall = worldHit()) {
                    if (tooFar(wall->distance, "reach_placement", "mount"))
                        return;
                    auto s = e->surface;
                    s.yaw = 0;
                    s.rotation = interaction::orientation(wall->normal);
                    s.center = wall->point + wall->normal * .06f;
                    if (surfaces::pushOut(s, world->collision)) {
                        layout.mount(e->instance, world.get(), s);
                        tagged.clear();
                        refresh();
                        publish();
                    }
                    return;
                }
            }
        return;
    }
    if (action == "path.add") {
        pathPress();
        publish();
        return;
    }
    if (action != "gizmo.drag")
        return;
    // A client configure can change the captured aspect after selection. Start
    // each gesture from the current surface, rather than the cached selection.
    if (editor.kind == "window")
        if (auto e = capture.find(editor.selected)) {
            interaction::Hands normalized;
            normalized.begin(interaction::Gesture::Carry, e->surface, {.u = .5f, .v = .5f});
            selectedTransform = {
                e->surface.center, {e->surface.width, e->surface.height, 1}, normalized.start.rotation};
        }
    if (!editor.selected.empty()) {
        if (editor.kind == "window")
            if (auto e = capture.find(editor.selected); e && editor.locked(world->id, e->instance)) {
                // Locked windows keep their handles but refuse edits.
                if (gizmoHover != interaction::Part::None)
                    return;
            }
        if (editor.kind == "object")
            selectedTransform = objectPose(editor.selected);
        if (gizmoBegin())
            return;
    }
    if (auto h = aim())
        if (auto e = capture.find(h->first)) {
            if (editor.selected != e->surface.id)
                gizmo.resetFlips();
            editor.selected = e->surface.id;
            editor.kind = "window";
            auto s = e->surface;
            interaction::Hands normalized;
            normalized.begin(interaction::Gesture::Carry, s, h->second);
            selectedTransform = {s.center, {s.width, s.height, 1}, normalized.start.rotation};
            publish();
            return;
        }
    if (auto h = objectAim()) {
        if (editor.selected != h->first)
            gizmo.resetFlips();
        editor.selected = h->first;
        editor.kind = "object";
        selectedTransform = objectPose(editor.selected);
        publish();
    } else if (protectCharacter("Select", "select"))
        publish();
}
void Runtime::toolsTick() {
    ctrlTick();
    if (!world)
        return;
    if (originalCollision.empty())
        originalCollision = world->collision;
    // Object carry (Super+LMB, tool 4) previews like a gizmo drag; release commits
    // the last valid pose (RFC-0011 placement rules).
    if (objectCarry && editor.kind == "object") {
        auto candidate = preview.active ? preview.pose : objectPose(editor.selected);
        candidate.position = camera.position + pointerRay() * objectDistance - dragPoint;
        previewObject(candidate, true);
    }
    if (gizmo.dragging())
        gizmoUpdate();
    pathTick();
    // Windows follow the gizmo live; a swept pose that would enter collision stops
    // the window and shows where it was refused.
    if (gizmo.dragging() && editor.kind == "window") {
        if (auto e = capture.find(editor.selected)) {
            auto next = e->surface;
            next.center = selectedTransform.position;
            next.rotation = selectedTransform.rotation;
            next.yaw = 0;
            next.width = std::clamp(selectedTransform.scale.x, .1f, 12.f);
            next.height = std::clamp(selectedTransform.scale.y, .1f, 8.f);
            windowBlocked.reset();
            if (interaction::sweepSafe(e->surface, next, world->collision, &world->collisionIndex)) {
                layout.freePoses[e->surface.id] = next;
                layout.carried.clear();
                if (interaction::isScale(gizmo.active()) && e->window->m_target) {
                    // Configure against the gesture baseline: the world pose
                    // updates before the client acknowledges its new pixels.
                    // Feeding those pending pixels back cancels the resize.
                    auto width = interaction::resizeExtent(double(next.width / dragStart.scale.x * dragPixelWidth), 160.,
                                                           monitor->m_size.x),
                         height = interaction::resizeExtent(double(next.height / dragStart.scale.y * dragPixelHeight), 100.,
                                                            monitor->m_size.y);
                    resizeClient(*e, next, width, height);
                }
                if (layout.homeOf(e->instance, world.get()))
                    layout.mount(e->instance, world.get(), next);
            } else {
                interaction::Box b;
                b.centre = next.center;
                b.axes = {next.right(), next.up(), next.normal()};
                b.half = {next.width * .5f, next.height * .5f, .03f};
                windowBlocked = b;
            }
        } else
            editor.selected.clear();
    }
    // Persisted moves of static geometry (from before this rule) are dropped, not applied.
    // Records of world objects stay (dormant while their instance is absent, RFC-0011).
    Json kept = Json::array();
    for (auto &o : editor.objects) {
        if (!o.contains("objectId") || !o["objectId"].is_string())
            continue; // not a scene-node record: dropped
        std::string id = o["objectId"];
        bool mine = o["worldId"] == world->id, instance = id.starts_with("object:");
        auto node = mine && !instance ? editorNode(*world, id) : std::optional<unsigned>{0};
        if (!mine || instance || (node && world::movableNode(*world, *node)))
            kept.push_back(o);
    }
    if (editor.kind == "object") {
        bool instance = instanceIndex(*world, editor.selected).has_value();
        auto node = instance ? std::nullopt : editorNode(*world, editor.selected);
        if (!instance && (!node || !world::movableNode(*world, *node))) {
            cancelGizmo();
            editor.selected.clear();
            editor.kind.clear();
            objectCarry = false;
        }
    }
    if (kept.size() != editor.objects.size()) {
        editor.objects = std::move(kept);
        saveEditor();
    }
    // Committed transforms move meshes and collision together; previews draw only.
    std::map<unsigned, Mat4> committed;
    for (auto &o : editor.objects)
        if (o["worldId"] == world->id) {
            std::string id = o["objectId"];
            auto node = instanceIndex(*world, id) ? std::nullopt : editorNode(*world, id);
            if (!node)
                continue;
            auto matrix = objectPose(id).matrix(objectOrigin(*world, id));
            for (size_t n = 0; n < world->nodeParents.size(); n++)
                if (belongs(*world, n, *node))
                    committed[n] = matrix;
        }
    objectGhostTick();
    auto instances = applyInstanceModels();
    if (committed != objectModels || instances != instanceOverrides || builtObjects != instanceRevision) {
        builtObjects = instanceRevision;
        auto rebuild = timed("tools.rebuild");
        std::set<size_t> relight;
        for (auto &[k, m] : instances)
            if (!instanceOverrides.contains(k) || !(instanceOverrides.at(k) == m))
                relight.insert(k);
        for (auto &[k, m] : instanceOverrides)
            if (!instances.contains(k))
                relight.insert(k);
        // Visual triangles are the scene meshes at their committed node transforms: only a
        // node commit changes them (instance moves change collision and lighting only).
        bool nodesChanged = committed != objectModels || world->visualTriangles.empty();
        objectModels = committed;
        instanceOverrides = instances;
        // Scene collision (moved with committed node transforms), then every drawn
        // instance's proxy at its committed pose; hidden and blocked ones have none.
        size_t scene = std::min({sceneCollision, originalCollision.size(), originalCollisionNodes.size()});
        world->collision.assign(originalCollision.begin(), originalCollision.begin() + scene);
        world->collisionNodes.assign(originalCollisionNodes.begin(), originalCollisionNodes.begin() + scene);
        for (size_t i = 0; i < world->collision.size(); i++)
            if (committed.contains(world->collisionNodes[i])) {
                auto m = committed.at(world->collisionNodes[i]);
                auto &t = world->collision[i];
                t = {m.point(t.a), m.point(t.b), m.point(t.c)};
            }
        for (auto &[k, m] : instances) {
            auto &inst = world->objectInstances[k];
            auto &asset = world->objectAssets[inst.asset];
            inst.collisionFirst = world->collision.size();
            inst.collisionCount = 0;
            if (inst.hidden)
                continue;
            for (auto &t : asset.collision) {
                world->collision.push_back({m.point(t.a), m.point(t.b), m.point(t.c)});
                world->collisionNodes.push_back(world::objectCollisionNode);
                ++inst.collisionCount;
            }
        }
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        layout.invalidateCollision();
        {
            auto section = timed("tools.index");
            world->collisionIndex.rebuild(world->collision);
        }
        if (nodesChanged) {
            joinRelights();
            auto visual = timed("tools.visual");
            world->visualTriangles.clear();
            for (const auto &mesh : world->meshes) {
                auto it = committed.find(mesh.node);
                auto matrix = it == committed.end() ? Mat4::identity() : it->second;
                for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3)
                    world->visualTriangles.push_back({matrix.point(mesh.vertices[i].position),
                                                      matrix.point(mesh.vertices[i + 1].position),
                                                      matrix.point(mesh.vertices[i + 2].position)});
            }
            world->visualIndex.rebuild(world->visualTriangles);
            world::buildOccluders(*world, committed);
        }
        // A moved object takes the visibility and probe light of its new place.
        {
            auto section = timed("tools.relight");
            for (auto k : relight)
                this->relight(k);
        }
        {
            auto section = timed("tools.wireframe");
            renderer.collisionChanged();
        }
        ++objectRevision;
    }
    renderer.transforms = committed;
    if (preview.active && editor.kind == "object" && !instanceIndex(*world, editor.selected))
        if (auto node = editorNode(*world, editor.selected)) {
            auto matrix = preview.pose.matrix(objectOrigin(*world, editor.selected));
            for (size_t n = 0; n < world->nodeParents.size(); n++)
                if (belongs(*world, n, *node))
                    renderer.transforms[n] = matrix;
        }
    renderer.lines.clear();
    // A compatible tool mount under the aim lights up while a client is being placed.
    snapTarget.clear();
    Capture::Entry *placing = hands.gesture == interaction::Gesture::Carry ? capture.find(hands.pose.id)
                              : editor.slot == 5 && !freeCursor && layout.unplaced.contains(unplacedSelected)
                                  ? capture.find(unplacedSelected)
                              : editor.slot == 2 && !tagged.empty() ? capture.find(tagged)
                                                                    : nullptr;
    if (placing)
        if (auto snapped = mountTarget(*placing)) {
            snapTarget = snapped->first->at("id");
            auto &f = snapped->second;
            auto decode = [](float v) { return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f); };
            auto c = renderer.overlayStyle["promptColor"];
            Vec3 glow{decode(c[0]), decode(c[1]), decode(c[2])};
            auto r = f.right() * (f.width * .5f + .05f), u = f.up() * (f.height * .5f + .05f);
            auto o = f.center + f.normal() * .03f;
            std::array<Vec3, 4> corner{o - r - u, o + r - u, o + r + u, o - r + u};
            for (int k = 0; k < 4; k++) {
                renderer.lines.push_back({corner[k], corner[(k + 1) % 4], glow * .35f, .04f}); // soft halo
                renderer.lines.push_back({corner[k], corner[(k + 1) % 4], glow, .012f});
            }
        }
    pathDraw();
    characterCarryTick();
    characterCarryDraw();
    for (auto &e : capture.entries)
        if (editor.locked(world->id, e.instance) && e.visible) {
            auto &s = e.surface;
            auto c = s.center + s.right() * (s.width * .5f - .1f) + s.up() * (s.height * .5f - .1f) + s.normal() * .04f;
            auto r = s.right() * .05f, u = s.up() * .05f;
            Vec3 gold{1, .7f, .1f};
            renderer.lines.push_back({c - r - u, c + r - u, gold});
            renderer.lines.push_back({c + r - u, c + r + u, gold});
            renderer.lines.push_back({c + r + u, c - r + u, gold});
            renderer.lines.push_back({c - r + u, c - r - u, gold});
            renderer.lines.push_back({c - r * .7f + u, c - r * .7f + u * 1.7f, gold});
            renderer.lines.push_back({c - r * .7f + u * 1.7f, c + r * .7f + u * 1.7f, gold});
            renderer.lines.push_back({c + r * .7f + u * 1.7f, c + r * .7f + u, gold});
        }
    if (editor.slot == 4 && !editor.selected.empty() && !gizmo.dragging() && !objectCarry) {
        if (editor.kind == "window") {
            if (auto e = capture.find(editor.selected)) {
                selectedTransform.position = e->surface.center;
                selectedTransform.scale = {e->surface.width, e->surface.height, 1};
            } else
                editor.selected.clear();
        } else
            selectedTransform = objectPose(editor.selected);
    }
    gizmoDraw();
}
std::optional<Json> Runtime::method(const std::string &method, const Json &wire) {
    auto section = timed("method " + method);
    if (frameLog.file && !method.starts_with("_"))
        frameLog.marks.push_back(method);
    Json params = wire;
    recentIPC.push_back({{"method", method},
                         {"timeMs", std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now().time_since_epoch())
                                        .count()}});
    while (recentIPC.size() > 32)
        recentIPC.pop_front();
    std::string actor = params.value("_actor", ""), client = params.value("_client", "");
    params.erase("_actor");
    params.erase("_client");
    int peerPid = params.value("_pid", -1);
    auto protocol = params.value("_protocol", "0.1");
    methodProtocol = protocol;
    params.erase("_pid");
    params.erase("_protocol");
    if (method == "replay.start" && control.photoHeld()) throw ipc::Error(-32005,"release photo camera before replay");
    if (method == "capture.annotate") {
        if (params.size() != 2 || !params.contains("captureId") || !params.contains("note") ||
            !params["captureId"].is_string())
            throw ipc::Error(-32602, "captureId and note required");
        std::string id = params["captureId"];
        if (!interaction::validCaptureId(id) || std::find(captureIds.begin(), captureIds.end(), id) == captureIds.end())
            throw ipc::Error(-32005, "unknown capture");
        std::string note;
        try {
            note = interaction::captureNote(params["note"]);
        } catch (const std::invalid_argument &e) {
            throw ipc::Error(-32602, e.what());
        }
        auto noteTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
        // Queued behind that capture's own write: the note never races its file.
        captureJobs.push([dir = captureDirectory, id, note, noteTime] {
            interaction::annotateCapture(dir, id, note, noteTime);
            return std::string();
        });
        return Json{{"revision", ipc->revision()}};
    }
    if (method.starts_with("recording.") || method.starts_with("replay.")) return sessionMethod(method, params);
    if (method == "_renderer.channel") {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        if (params.at("generation") != offloadGeneration || !offloadRequest.value("enabled", false))
            close(params.at("fd").get<int>());
        else {
            offload.attach(params.at("fd"), offloadRequest.at("width"), offloadRequest.at("height"), offloadGeneration);
            offloadObjects.reset(packageAssets);
            offloadMachines.reset();
        }
        return Json::object();
    }
    if (method == "_renderer.state") {
        if (params.at("generation") == offloadGeneration) offloadPolicy = params;
        return Json::object();
    }
    if (method == "graphics.get" || method == "graphics.set") {
        if (renderPass.active())
            throw ipc::Error(-32004, "graphics update during render");
        if (method == "graphics.set") {
            if (params.size() != 1 || !params.contains("patch") || !params["patch"].is_object())
                throw ipc::Error(-32602, "graphics patch required");
            if (!ipc::protocol::atLeast(protocol, "0.10"))
                for (auto key : {"offload", "offload_power_cap_w", "scale_min", "scale_max"})
                    if (params["patch"].contains(key)) throw ipc::Error(-32602, "setting requires IPC 0.10");
            if (!ipc::protocol::atLeast(protocol,"0.13") && (params["patch"].contains("outline") || params["patch"].contains("visibility_culling")))
                throw ipc::Error(-32602,"setting requires IPC 0.13");
            if (!ipc::protocol::atLeast(protocol,"0.14") && (params["patch"].contains("outline_quality") || params["patch"].contains("outline_width_px") || params["patch"].contains("outline_colour")))
                throw ipc::Error(-32602,"setting requires IPC 0.14");
            if (!ipc::protocol::atLeast(protocol, "0.16") && params["patch"].contains("upscaler"))
                throw ipc::Error(-32602, "setting requires IPC 0.16");
            if (!ipc::protocol::atLeast(protocol, "0.23") && params["patch"].contains("npc_motion"))
                throw ipc::Error(-32602, "setting requires IPC 0.23");
            if (params["patch"].contains("npc_motion") && !params["patch"]["npc_motion"].is_string())
                throw ipc::Error(-32602, "npc_motion must be auto, always, still or off");
            if (!ipc::protocol::atLeast(protocol, "0.18")) {
                auto &patch = params["patch"];
                auto cap = patch.value("offload_power_cap_w", Json(50));
                if (patch.contains("offload_device") || !cap.is_number() || cap.get<double>() > 100)
                    throw ipc::Error(-32602, "setting requires IPC 0.18");
            }
            if (!ipc::protocol::atLeast(protocol, "0.19") && params["patch"].contains("fov") &&
                params["patch"]["fov"].is_number() && params["patch"]["fov"].get<double>() > 110)
                throw ipc::Error(-32602, "setting requires IPC 0.19");
            auto merged = graphicsFile.current;
            merged.update(params["patch"]);
            try {
                interaction::configPatch(configured, {{"graphics", merged}});
            } catch (const std::exception &e) {
                throw ipc::Error(-32602, e.what());
            }
            graphicsFile.save(merged);
            applyGraphics(merged);
        } else if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        Json response{{"config", config["graphics"]},
                      {"effective", renderer.graphics},
                      {"overrides", graphicsFile.current},
                      {"path", ipc::boundedText(graphicsFile.path.string(), ipc::PathLimit)},
                      {"error", ipc::boundedText(graphicsFile.error, ipc::MessageLimit)},
                      {"presets", render::graphicsPresets()},
                      {"options", render::graphicsOptions()},
                      {"unsupported", {"ssr", "runtime_shadows", "contact_shadows"}},
                      {"timings", renderer.timings()}};
        // npc_motion: the effective motion (Lua npc.motion when not set here); older peers see none.
        for (auto section : {"config", "effective"})
            response[section]["npc_motion"] = config["npc"]["motion"];
        if (!ipc::protocol::atLeast(protocol, "0.23")) {
            for (auto section : {"config", "effective", "overrides", "options"})
                response[section].erase("npc_motion");
            for (auto &preset : response["presets"])
                preset.erase("npc_motion");
        } else if (response["overrides"].contains("npc_motion") && response["overrides"]["npc_motion"].is_null())
            response["overrides"].erase("npc_motion");
        if (!ipc::protocol::atLeast(protocol, "0.19")) // fov above 110 arrived with IPC 0.19
            for (auto section : {"config", "effective", "overrides"})
                if (response[section].contains("fov") && response[section]["fov"].is_number())
                    response[section]["fov"] = std::min(110., response[section]["fov"].get<double>());
        if (ipc::protocol::atLeast(protocol, "0.10")) response["performance"] = performance();
        else for (auto section : {"config", "effective", "overrides", "options"})
            for (auto key : {"offload", "offload_power_cap_w", "scale_min", "scale_max"}) response[section].erase(key);
        if (!ipc::protocol::atLeast(protocol, "0.18")) {
            // Older schemas: the cap is a number in 20..100; no device selection.
            double effectiveCap = offloadPolicy.value("capW", 0.) > 0 ? offloadPolicy.value("capW", 0.) : 80.;
            auto legacyCap = [&](Json &value) { value = std::clamp(value.is_number() ? value.get<double>() : effectiveCap, 20., 100.); };
            auto legacy = [&](Json &section) {
                if (!section.is_object()) return;
                section.erase("offload_device");
                if (section.contains("offload_power_cap_w")) legacyCap(section["offload_power_cap_w"]);
            };
            for (auto section : {"config", "effective", "overrides", "options"})
                legacy(response[section]);
            if (response.contains("performance")) {
                auto &perf = response["performance"];
                perf.erase("capSource");
                perf.erase("device");
                legacy(perf["requested"]);
                legacy(perf["effective"]);
                perf["controls"].erase("offload_device");
                if (perf["controls"].contains("offload_power_cap_w"))
                    for (auto key : {"requested", "effective"}) legacyCap(perf["controls"]["offload_power_cap_w"][key]);
            }
        }
        if (!ipc::protocol::atLeast(protocol, "0.16")) {
            for (auto section : {"config", "effective", "overrides", "options"})
                response[section].erase("upscaler");
            for (auto &preset : response["presets"])
                preset.erase("upscaler");
            if (response.contains("performance"))
                for (auto section : {"requested", "effective", "controls"})
                    response["performance"][section].erase("upscaler");
        }
        if (!ipc::protocol::atLeast(protocol,"0.14")) {
            for(auto section:{"config","effective","overrides","options"}) for(auto key:{"outline_quality","outline_width_px","outline_colour"}) response[section].erase(key);
            for(auto &preset:response["presets"]) preset.erase("outline_quality");
            if(response.contains("performance"))
                for(auto section:{"requested","effective","controls"}) for(auto key:{"outline_quality","outline_width_px","outline_colour"}) response["performance"][section].erase(key);
        }
        if (!ipc::protocol::atLeast(protocol,"0.13")) {
            auto strip = [](Json &value) { if(value.is_object()) { value.erase("outline"); value.erase("visibility_culling"); } };
            for(auto section:{"config","effective","overrides","options"})strip(response[section]);
            for(auto &preset:response["presets"])strip(preset);
            if(response.contains("performance"))
                for(auto section:{"requested","effective","controls"})strip(response["performance"][section]);
        }
        return response;
    }
    if (method == "player.get" || method == "player.set") {
        if (method == "player.set") {
            if (params.size() != 1 || !params.contains("patch") || !params["patch"].is_object() || params["patch"].empty())
                throw ipc::Error(-32602, "player patch required");
            auto merged = playerFile.current;
            auto keys = interaction::playerSettingsKeys();
            for (auto &[key, value] : params["patch"].items()) {
                if (std::find(keys.begin(), keys.end(), key) == keys.end())
                    throw ipc::Error(-32602, "unknown player setting: " + key);
                if (value.is_null())
                    merged.erase(key); // back to the default
                else
                    merged[key] = value;
            }
            try {
                interaction::configPatch(configured, {{"player", merged}});
            } catch (const std::exception &e) {
                throw ipc::Error(-32602, e.what());
            }
            playerFile.save(merged);
            applyPlayer(merged);
            publish();
        } else if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        return playerSettings();
    }
    if (method == "input.get") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        auto response = Json{{"config", input.config},
                             {"path", ipc::boundedText(inputFile.path.string(), ipc::PathLimit)},
                             {"steals", steals(input.config)},
                             {"conflicts", interaction::inputConflicts(input.config)}};
        if (!ipc::protocol::atLeast(protocol, "0.26"))
            response = interaction::withoutDebugTool(response);
        if (!ipc::protocol::atLeast(protocol, "0.15"))
            response = interaction::beforePlacementInput(response);
        return ipc::protocol::atLeast(protocol, "0.9") ? response : interaction::legacyInputNames(response);
    }
    if (method == "overlay.style") {
        if (renderPass.active() || params.size() != 2 || !params.contains("leaseId") || !params["leaseId"].is_string() ||
            !control.ownsLease(actor, params["leaseId"]) || !params.contains("style"))
            throw ipc::Error(-32602, "overlay style needs owning shell lease");
        static constexpr std::array reticleColours{"reticleColor", "validColor", "invalidColor"};
        bool reticleAware = ipc::protocol::atLeast(protocol, "0.19");
        bool questAware = ipc::protocol::atLeast(protocol, "0.21");
        if (!questAware && params["style"].is_object() && params["style"].contains("questColor"))
            throw ipc::Error(-32602, "setting requires IPC 0.21");
        if (!reticleAware && params["style"].is_object())
            for (auto key : reticleColours)
                if (params["style"].contains(key))
                    throw ipc::Error(-32602, "setting requires IPC 0.19");
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        renderer.applyOverlayStyle(params["style"]);
        auto style = renderer.overlayStyle;
        if (!reticleAware)
            for (auto key : reticleColours)
                style.erase(key);
        if (!questAware)
            style.erase("questColor");
        return Json{{"style", style}};
    }
    if (method == "input.set" || method == "input.preview") {
        if (params.size() != 1 || !params.contains("config"))
            throw ipc::Error(-32602, "config required");
        Json next;
        try {
            auto requested = params["config"];
            if (!ipc::protocol::atLeast(protocol,"0.12") && requested.contains("modes"))
                for (auto &[mode,actions] : requested["modes"].items()) {
                    if(actions.contains("record.toggle")) throw std::runtime_error("record.toggle requires IPC 0.12");
                    if(input.config["modes"].contains(mode)) actions["record.toggle"]=input.config["modes"][mode]["record.toggle"];
                }
            if (!ipc::protocol::atLeast(protocol, "0.15") && requested.contains("modes") &&
                requested["modes"].contains("world"))
                for (auto key : {"tool5", "carry.facing", "content.scale", "window.reset", "alternate"})
                    requested["modes"]["world"][key] = input.config["modes"]["world"][key];
            // Peers before IPC 0.26 never see tool9: it keeps its current bindings, less any the
            // peer has just given another action.
            if (!ipc::protocol::atLeast(protocol, "0.26") && requested.contains("modes") && requested["modes"].is_object() &&
                requested["modes"].contains("world") && requested["modes"]["world"].is_object()) {
                auto &world = requested["modes"]["world"];
                Json kept = Json::array();
                for (auto &b : input.config["modes"]["world"].value("tool9", Json::array())) {
                    bool taken = false;
                    for (auto &[action, bindings] : world.items())
                        if (action != "tool9" && bindings.is_array())
                            taken |= std::find(bindings.begin(), bindings.end(), b) != bindings.end();
                    if (!taken)
                        kept.push_back(b);
                }
                world["tool9"] = kept;
            }
            next = interaction::validateInput(requested);
        } catch (const std::exception &e) {
            throw ipc::Error(-32602, e.what());
        }
        if (method == "input.preview") {
            auto response = Json{{"config", next}, {"steals", steals(next)}, {"conflicts", interaction::inputConflicts(next)}};
            if (!ipc::protocol::atLeast(protocol, "0.26"))
                response = interaction::withoutDebugTool(response);
            if (!ipc::protocol::atLeast(protocol, "0.15"))
                response = interaction::beforePlacementInput(response);
            return ipc::protocol::atLeast(protocol, "0.9") ? response : interaction::legacyInputNames(response);
        }
        if (!interaction::inputConflicts(next).empty())
            throw ipc::Error(-32602, "conflicting Hyprune actions in one mode");
        inputFile.save(next);
        applyInput(next);
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "input.capture.cancel") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        captureRequest = nullptr;
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "input.capture") {
        if (params.size() != 4 || !params.contains("leaseId") || !control.ownsLease(actor, params["leaseId"]) ||
            !control.uiVisible() || !directorSurface())
            throw ipc::Error(-32002, "capture requires the active verified Director lease");
        auto mode = params.at("mode").get<std::string>(), name = params.at("action").get<std::string>();
        if (!ipc::protocol::atLeast(protocol, "0.15") && name == "wheel")
            name = "cycle";
        if (!ipc::protocol::atLeast(protocol, "0.9") && name == "tool5")
            name = "hud.cycle";
        if (!input.config["modes"].contains(mode) || !input.config["modes"][mode].contains(name) ||
            !params["slot"].is_number_integer() || params["slot"] < 0 || params["slot"] > 2)
            throw ipc::Error(-32602, "unknown capture slot");
        captureRequest = {
            {"mode", mode}, {"action", name}, {"slot", params["slot"]}, {"status", "waiting"}, {"binding", nullptr}};
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "_quests.sync") {
        markers.sync(params);
        return Json::object();
    }
    if (method == "markers.reveal") {
        if (params.size() > 1 || (params.size() == 1 && !params.contains("durationMs")))
            throw ipc::Error(-32602, "markers.reveal takes only durationMs");
        std::optional<int64_t> duration;
        if (params.contains("durationMs")) {
            if (!params["durationMs"].is_number_integer() || params["durationMs"] < 1000 || params["durationMs"] > 15000)
                throw ipc::Error(-32602, "durationMs must be 1000-15000");
            duration = params["durationMs"].get<int64_t>();
        }
        markers.request(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count(),
                        duration);
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "_layout.restore") {
        layout.restore(params);
        deriveHomes();
        publish();
        return Json::object();
    }
    if (method == "_operation.cancel") {
        loadCancelled = true;
        return Json::object();
    }
    if (method == "_connection.closed") {
        bindings.erase(actor);
        control.disconnect(actor, camera);
        publish();
        return Json::object();
    }
    // Locked: nothing changes the world, but a shell may still give its lease back (it
    // leaves with world mode; refusing it left a lease the shell could never claim again).
    if (locked() && method != "shell.release")
        throw ipc::Error(-32005, "session locked");
    if (world::objectMethods().contains(method)) {
        objectsProtocol = protocol;
        return objectsMethod(method, params);
    }
    if (method == "workspace.bind") {
        if (params.size() != 3 || !params.at("id").is_number_integer())
            throw ipc::Error(-32602, "invalid area binding");
        layout.bind(params.at("id"), params.at("worldId"), params.at("areaId"), world.get());
        refresh();
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "workspace.focus") {
        if (params.size() != 1 || !params.at("id").is_number_integer())
            throw ipc::Error(-32602, "invalid workspace focus");
        focus("workspace-" + std::to_string(params.at("id").get<int>()), true);
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "workspace.unfocus") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        unfocus();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "placements.list") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        auto placements = layout.placements;
        if (!ipc::protocol::atLeast(protocol, "0.15"))
            for (auto &v : placements)
                v.erase("content");
        if (!ipc::protocol::atLeast(protocol, "0.5"))
            std::erase_if(placements.get_ref<Json::array_t &>(), [](auto &p) { return p.contains("pose"); });
        return Json{{"placements", placements}};
    }
    if (method == "placements.remove") {
        if (params.size() != 3 || !params.at("rule").is_object())
            throw ipc::Error(-32602, "invalid placement rule");
        std::erase_if(layout.placements.get_ref<Json::array_t &>(), [&](auto &p) {
            return p.at("worldId") == params.at("worldId") && p.at("areaId") == params.at("areaId") &&
                   p.at("rule") == params.at("rule");
        });
        refresh();
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "surfaces.recall") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        std::vector<surfaces::Instance> live;
        std::vector<surfaces::Layout::RecallWindow> sized;
        for (auto &e : capture.entries)
            if (layout.pulled(e.instance, nullptr)) {
                live.push_back(e.instance);
                sized.push_back({e.instance, e.surface.pixelWidth, e.surface.pixelHeight});
            }
        layout.recall(live);
        if (active && world && monitor)
            layout.spreadRecalled(sized, world.get(), camera,
                                  float(monitor->m_size.x) / std::max(1., double(monitor->m_size.y)));
        typing = false;
        keys.clear();
        cancelGrab();
        refresh();
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "surface.pocket" || method == "surface.unpocket") {
        if (!active || !world || !layout.desktop.is_null())
            throw ipc::Error(-32005, "exploration inactive");
        if (params.size() != 1 || !params.contains("surfaceId") || !params.at("surfaceId").is_string())
            throw ipc::Error(-32602, "invalid pocket request");
        auto e = capture.find(params.at("surfaceId"));
        if (!e)
            throw ipc::Error(-32003, "unknown window");
        if (!e->parentId.empty())
            throw ipc::Error(-32005, "child surfaces have no pocket");
        const auto id = e->surface.id;
        bool placed = false;
        if (method == "surface.pocket") {
            if (editor.locked(world->id, e->instance))
                throw ipc::Error(-32005, "window is locked");
            if (!layout.unplaced.contains(id)) { // idempotent: a pocketed window stays where it is in the queue
                cancelGrab();
                layout.stash(e->instance, e->surface);
                ipc->notify("runtime.notice", {{"level", "info"},
                                               {"code", "placement.pocketed"},
                                               {"message", "Pocketed " + e->instance.title}});
            }
            unplacedSelected = id;
        } else {
            if (!layout.unplaced.contains(id))
                throw ipc::Error(-32004, "window is not in the pocket");
            cancelGrab();
            if (layout.hasPlacement(e->instance, world.get())) {
                layout.unplaced.erase(id); // back to its durable placement
                placed = true;
            } else {
                // The pose a newly opened room window gets: in front of the player, short of walls.
                auto s = e->surface;
                s.yaw = -camera.yaw;
                s.rotation = {0, 0, 0, 1};
                float distance = config["windows"]["spawn_distance"].get<float>();
                if (auto wall = interaction::raycast(camera.position, camera.forward(), world->collision,
                                                     &world->collisionIndex))
                    distance = interaction::limitSpawnDistance(distance, wall->distance);
                s.center = camera.position + camera.forward() * distance;
                surfaces::pushOut(s, world->collision);
                float aspect = monitor ? float(monitor->m_size.x / monitor->m_size.y) : 16.f / 9.f;
                placed = layout.spawn(e->instance, world.get(), camera, s, aspect);
            }
            if (placed)
                std::erase(layout.pocketOrder, id);
            else {
                // No free full-size spot: keep it pocketed, selected for the Placement tool.
                unplacedSelected = id;
                editor.slot = tool = 5;
                ipc->notify("runtime.notice", {{"level", "info"},
                                               {"code", "placement.blocked"},
                                               {"message", "No free spot here. Aim and click to place " +
                                                               e->instance.title + "."}});
            }
        }
        refresh();
        publish();
        ipc->notify("mounts.changed", {{"mounts", layout.state(world.get())}});
        if (method == "surface.pocket")
            return Json{{"revision", ipc->revision()}};
        return Json{{"revision", ipc->revision()}, {"placed", placed}};
    }
    if (method == "surface.pull" || method == "surface.place" || method == "surface.return") {
        if (!active || !world || !layout.desktop.is_null())
            throw ipc::Error(-32005, "exploration inactive");
        if (params.size() != (params.contains("anchorId") ? 2u : 1u) || !params.at("surfaceId").is_string() ||
            (params.contains("anchorId") && method != "surface.place"))
            throw ipc::Error(-32602, "invalid tool mutation");
        auto e = capture.find(params.at("surfaceId"));
        if (!e)
            throw ipc::Error(-32003, "unknown window");
        typing = false;
        keys.clear();
        cancelGrab();
        if (method == "surface.pull") {
            auto safe = surfaces::carryPlane(camera, 1.4f, 1.4f * e->surface.pixelHeight / e->surface.pixelWidth,
                                             world->collision);
            (void)safe;
            layout.pull(e->instance, world.get());
        }
        if (method == "surface.place") {
            auto before = layout.placements;
            auto carried = layout.carried;
            try {
                auto id = layout.place(e->instance, params.value("anchorId", ""), world.get());
                surfaces::Surface plane;
                surfaces::anchorPlane(plane, *layout.anchor(id, world.get()), e->surface.pixelWidth,
                                      e->surface.pixelHeight);
                if (!surfaces::pushOut(plane, world->collision))
                    throw ipc::Error(-32005, "mount blocked by geometry");
            } catch (...) {
                layout.placements = before;
                layout.carried = carried;
                throw;
            }
        }
        if (method == "surface.return") // back to its wall: natively home again, tiled
            moveWindow(e->window.lock(), layout.release(e->instance, world.get()), true);
        refresh();
        publish();
        ipc->notify("mounts.changed", {{"mounts", layout.state(world.get())}});
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "data.object.bind") {
        if (params.size() != 2 || !world)
            throw ipc::Error(-32602, "invalid object binding");
        std::string source = params.at("sourceId");
        if (source != client && !source.starts_with(client + "."))
            throw ipc::Error(-32002, "data source namespace not owned");
        auto o = std::find_if(world->objects.begin(), world->objects.end(),
                              [&](auto &o) { return o.at("id") == params.at("objectId"); });
        if (o == world->objects.end())
            throw ipc::Error(-32003, "unknown data object");
        (*o)["sourceId"] = source;
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "camera.fov") {
        if (control.photoHeld()) throw ipc::Error(-32005, "release photo camera first");
        if (params.size() != 1 || !params.at("degrees").is_number())
            throw ipc::Error(-32602, "invalid FOV");
        float f = params.at("degrees");
        if (!std::isfinite(f) || f < 30 || f > 110)
            throw ipc::Error(-32602, "FOV outside 30..110 degrees");
        baseFov = camera.fov = f;
        config["graphics"]["fov"] = configured["graphics"]["fov"] = f;
        publish();
        ipc->notify("presentation.changed", {{"presentation",
                                              {{"profile", world ? renderer.profile : Json(nullptr)},
                                               {"fov", camera.fov},
                                               {"quality", renderer.intelQuality ? "intel" : "high"}}}});
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "atmosphere.get") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        auto profile=world ? renderer.profile : Json(nullptr);
        if(profile.is_object() && !ipc::protocol::atLeast(protocol,"0.13"))profile.erase("outline");
        return Json{{"profile",profile},{"quality", renderer.intelQuality ? "intel" : "high"}};
    }
    if (method == "atmosphere.set") {
        if (params.size() != 1 || !world)
            throw ipc::Error(-32602, "invalid atmosphere");
        try {
            world::validateAtmosphere(params.at("profile"));
        } catch (const std::exception &e) {
            throw ipc::Error(-32602, e.what());
        }
        auto before = renderer.profile;
        // Pixel assets remain immutable; no IPC path is opened. Sky/environment changes require a world reload.
        if (params["profile"]["sky"] != before["sky"] ||
            params["profile"].value("environment", Json(nullptr)) != before.value("environment", Json(nullptr)))
            throw ipc::Error(-32007, "sky/environment edits require package reload");
        renderer.profile = params["profile"];
        graphicsBaseProfile = renderer.profile;
        applyGraphics(graphicsFile.current);
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "shell.bind") {
        if (params.size() != 2 || !params.contains("pid") || !params.at("pid").is_number_integer() ||
            !params.contains("leaseId") || !params.at("leaseId").is_string())
            throw ipc::Error(-32602, "invalid shell binding");
        auto value = params.at("pid").get<int64_t>();
        if (value <= 0 || value > 2147483647)
            throw ipc::Error(-32602, "invalid shell PID");
        int pid = value;
        auto identity = processIdentity(pid);
        std::string lease = params.at("leaseId");
        if (!active || pid <= 0 || identity.first != peerPid || identity.second.empty() ||
            !control.ownsLease(actor, lease))
            throw ipc::Error(-32002, "shell must be authenticated peer's direct child with an owned lease");
        wl_client *verified = nullptr;
        for (auto &layer : Desktop::layerState()->layers()) {
            if (layer->m_monitor != monitor || !layer->m_mapped || layer->m_namespace != "hyprune-hud")
                continue;
            auto resource = layer->m_layerSurface.lock();
            auto surface = resource ? resource->m_surface.lock() : nullptr;
            if (!surface)
                continue;
            pid_t waylandPid;
            uid_t uid;
            gid_t gid;
            wl_client_get_credentials(surface->client(), &waylandPid, &uid, &gid);
            if (waylandPid == pid && uid == getuid())
                verified = surface->client();
        }
        if (!verified)
            throw ipc::Error(-32002, "no credential-matched HUD on leased output");
        bindings[actor] = {pid, identity.second, verified, lease, std::chrono::steady_clock::now()};
        control.associate(actor, true);
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (method == "workspace.activate") {
        if (params.size() != 1 || !params.contains("id") || !params.at("id").is_number_integer())
            throw ipc::Error(-32602, "invalid workspace");
        auto entry = std::find_if(locations.begin(), locations.end(),
                                  [&](const Json &v) { return v.at("id") == params.at("id"); });
        if (entry == locations.end() && !layout.binding(params.at("id"), nullptr))
            throw ipc::Error(-32003, "unmapped workspace");
        if (!active || !monitor)
            throw ipc::Error(-32005, "world mode inactive");
        Desktop::focusState()->rawMonitorFocus(monitor.lock());
        auto result = g_pKeybindManager->m_dispatchers.at("workspace")(std::to_string(params.at("id").get<int>()));
        if (!result.success)
            throw ipc::Error(-32004, "workspace activation failed");
        workspaceLocation();
        publish();
        return Json{{"revision", ipc->revision()}};
    }
    if (control::Control::methods().contains(method)) {
        if (method == "shell.overlay" && params.value("exclusiveInput", false) &&
            !ipc::protocol::atLeast(protocol, "0.3"))
            throw ipc::Error(-32007, "verified association requires IPC 0.3");
        if ((method == "travel.start" || method == "autodrive.start") && !active)
            throw ipc::Error(-32005, "world mode inactive");
        auto outputs = monitor ? Json::array({"output-" + std::to_string(monitor->m_id)}) : Json::array();
        if (method == "camera.photo" && (!active || typing || handover.phase || fullscreen.phase || !layout.desktop.is_null() || session.playing))
            throw ipc::Error(-32005, "photo camera requires active world control");
        auto result = control.execute(method, params, actor, client, world.get(), camera, outputs);
        if (method == "camera.photo") { climbing.reset(camera); keys.clear(); lookX=lookY=0; smoothIntent={}; cancelGrab(); }
        if (method == "camera.get" && !ipc::protocol::atLeast(protocol,"0.13"))
            for (auto field : {"roll", "fov", "lensShift", "held", "renderScale"}) result.erase(field);
        if (method == "camera.set" || method == "travel.start" || method == "autodrive.start")
            climbing.reset(camera);
        if (method == "shell.release") {
            bindings.erase(actor);
            control.associate(actor, false);
        }
        if (control.uiVisible() || control.driving()) {
            keys.clear();
            cancelGrab();
            typing = false;
        }
        publish();
        if (method != "camera.get")
            result["revision"] = ipc->revision();
        return result;
    }
    if (method == "runtime.exit") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        exit();
    } else if (method == "world.load") {
        if (!params.contains("worldId") || params.size() > (params.contains("spawnId") ? 2u : 1u))
            throw ipc::Error(-32602, "invalid load params");
        load(params.at("worldId"), params.value("spawnId", ""));
        return std::nullopt;
    } else if (method == "movement.set") {
        if (params.size() != 1)
            throw ipc::Error(-32602, "invalid movement params");
        std::string mode = params.at("modeId");
        if (mode == "com.hyprune.walk" && world && !world->collision.empty())
            movement = &walk;
        else if (mode == "com.hyprune.fly")
            movement = &fly;
        else
            throw ipc::Error(-32007, "unsupported movement mode");
        climbing.reset(camera);
        smoothIntent = {};
        camera.verticalSpeed = 0;
        keys.clear();
    } else if (method == "surface.focus") {
        if (params.size() != 2 || (params.at("intent") != "pointer" && params.at("intent") != "keyboard"))
            throw ipc::Error(-32602, "invalid focus params");
        focus(params.at("surfaceId"), params.at("intent") == "keyboard");
    }
    publish();
    return Json{{"revision", ipc->revision()}};
}
#include "Replay.inc"
#include "Quests.inc"
#include "Cinema.inc"
void Runtime::tick() {
    if (renderPass.active())
        throw std::runtime_error("deferred work inside compositor render pass");
    // Frame log: this tick's sections, written when it ends (every return path).
    struct TickLog {
        Runtime *r;
        double start = FrameLog::now();
        ~TickLog() {
            if (!r->frameLog.file)
                return;
            Json sections = Json::object();
            for (auto &[name, ms] : r->frameLog.sections)
                if (ms >= .05)
                    sections[name] = std::round(ms * 100) / 100;
            Json j{{"kind", "tick"}, {"t", start}, {"ms", FrameLog::now() - start}, {"sections", sections}};
            if (!r->frameLog.marks.empty())
                j["marks"] = r->frameLog.marks;
            r->frameLog.line(j);
            r->frameLog.sections.clear();
            r->frameLog.marks.clear();
            r->frameLog.flush();
        }
    } tickLog{this};
    // Input that arrived since the last tick takes effect in this frame: its idle callback can
    // come after this tick, a frame late, where a replay applies it on time.
    runInput(inputQueue.last());
    if (offload.connected()) { // the frame boundary: the worker frame shown next is chosen now
        auto section = timed("offload.promote");
        offloadPump(1);
    }
    auto updateStarted = std::chrono::steady_clock::now();
    if (capturePending) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        if (renderer.completed())
            debugCapture();
    }
    if (auto done = captureJobs.poll()) {
        if (!done->error.empty())
            ipc->notify("runtime.notice", {{"level", "warning"}, {"code", "capture.failed"}, {"message", done->error}});
        else if (!done->result.empty())
            ipc->notify("runtime.notice", {{"level", "info"}, {"code", "capture.saved"}, {"message", done->result}});
    }
    if (renderPass.active())
        throw std::runtime_error("deferred work inside compositor render pass");
    {
        auto section = timed("ipc");
        ipc->poll();
    }
    if (auto next = graphicsFile.poll())
        applyGraphics(*next);
    if (auto next = playerFile.poll())
        applyPlayer(*next);
    if (companionFile.poll()) { // edited by hand or another client: choose again
        ++companionRevision;
        objectsDirty = true;
    }
    if (!session.playing) if (auto next = inputFile.poll())
        applyInput(*next);
    renderer.debug = debug;
    if (debug && renderer.collisionWireDirty) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        renderer.collisionChanged();
    }
    replayBeforeTick();
    {
        auto section = timed("actions");
        for (auto &a : input.tick(inputClock(), inputMode()))
            action(a);
    }
    if (!ipc->healthy()) {
        if (active)
            exit();
        return;
    }
    // Relative pointer delivery can emit the bus notification before the pointer
    // manager commits its new position. Observe the committed position here too.
    if (active && typing && freeCursor && !layerCursor && !cursorWarpPending && monitor) {
        auto p = Pointer::mgr()->position();
        auto m = monitor.lock();
        if (p.x >= m->m_position.x && p.y >= m->m_position.y && p.x < m->m_position.x + m->m_size.x &&
            p.y < m->m_position.y + m->m_size.y) {
            freeCursor = false;
            pointerX = (p.x - m->m_position.x) / m->m_size.x;
            pointerY = (p.y - m->m_position.y) / m->m_size.y;
            Desktop::focusState()->rawMonitorFocus(m);
            cursor();
        }
    }
    // Observe workspace changes before committing a ready asynchronous load.
    // This also lets a newer location cancel an in-flight mapping request.
    {
        auto section = timed("workspace");
        safe([&] { workspaceLocation(); });
    }
    finishLoad();
    {
        auto section = timed("objects");
        objectsTick();
    }
    if (locked() && active)
        exit();
    if (auto m = monitor.lock(); m && presentedMonitor != monitor) {
        presentedListener = m->m_events.presented.listen([](Time::steady_tp when) {
            if (runtime)
                runtime->framePresented(when);
        });
        presentedMonitor = monitor;
    }
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;
    // Nothing presented for a while (the world not shown): wall time, so paths still end.
    if (frameExport.active() && (presents != presentsTicked || now - lastPresented < std::chrono::milliseconds(100))) {
        if (presents - presentsTicked > 1)
            exportUnticked += presents - presentsTicked - 1;
        dt = float(double(std::min<uint64_t>(presents - presentsTicked, 6)) * refreshInterval());
        if (exportPinnedAnimation)
            renderer.animationClock = exportAnimation += dt;
    }
    presentsTicked = presents;
    if (session.recording || session.playing) dt = session.playing && session.mode == "camera" ? 0.f : replay::Session::Step;
    for (auto it = bindings.begin(); it != bindings.end();) {
        bool live = false;
        if (processIdentity(it->second.pid).second == it->second.start &&
            control.ownsLease(it->first, it->second.lease))
            for (auto &layer : Desktop::layerState()->layers()) {
                auto resource = layer->m_layerSurface.lock();
                auto surface = resource ? resource->m_surface.lock() : nullptr;
                if (surface && surface->client() == it->second.client && layer->getPID() == it->second.pid &&
                    layer->m_monitor == monitor) {
                    live = true;
                    break;
                }
            }
        // Layer switches may destroy the HUD immediately before mapping Director.
        // The process identity and client are checked again before routing any key.
        if (live)
            it->second.seen = now;
        if (processIdentity(it->second.pid).second != it->second.start ||
            (!live && now - it->second.seen > std::chrono::milliseconds(500))) {
            control.associate(it->first, false);
            it = bindings.erase(it);
        } else
            ++it;
    }
    if (control.exclusive()) {
        if (auto surface = directorSurface())
            g_pSeatManager->setKeyboardFocus(surface);
    }
    // Keys pressed while a shell overlay held the keyboard (e.g. W right after Enter in
    // the capture note prompt) went to the shell; walking must not wait for a re-press.
    if (wasExclusive && !control.exclusive())
        resumeHeldMovement();
    wasExclusive = control.exclusive();
    if (!layerCursor) {
        sim::Intent walking;
        walking.stepHeight = config["player"]["step_height"];
        walking.jumpSpeed = config["player"]["jump_speed"];
        auto section = timed("control");
        control.tick(dt, camera, world.get(), movement == &walk && !noclip ? &walk : nullptr, walking);
    }
    if (control.uiVisible()) {
        keys.clear();
        smoothIntent = {};
        lookX = lookY = 0;
        cancelGrab();
        typing = false;
    }
    if (active && monitor)
        transitionTick(dt);
    if (active && capture.prune(&layout))
        publish();
    animationFrames = false;
    bool cinemaTicked = false;
    if (active && monitor && layout.desktop.is_null() && fullscreen.phase != 2) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        if (!ownsInput()) {
            if (monitor != Desktop::focusState()->monitor())
                control.hold(false);
            keys.clear();
            if (!layerCursor) cancelGrab();
        }
        if (!grabbed.empty() && !capture.plane(grabbed))
            cancelGrab();
        sim::Intent intent;
        if (!typing && !handover.phase && fullscreen.phase == 0) {
            intent.forward = float(keys.contains(KEY_W)) - float(keys.contains(KEY_S));
            intent.right = float(keys.contains(KEY_D)) - float(keys.contains(KEY_A));
            intent.up = float(keys.contains(KEY_SPACE)) - float(keys.contains(KEY_LEFTCTRL));
            intent.jump = keys.contains(KEY_SPACE);
            intent.crouch = keys.contains(KEY_LEFTCTRL) && movement == &walk;
            intent.jumpSpeed = config["player"]["jump_speed"];
            intent.stepHeight = config["player"]["step_height"];
            // Walking runs at run_speed with Shift; flight scales by the same run/walk ratio.
            float walkSpeed = config["player"]["walk_speed"], runSpeed = config["player"]["run_speed"];
            bool running = keys.contains(KEY_LEFTSHIFT);
            intent.speed = movement == &walk ? (running ? runSpeed : walkSpeed) / 3.5f
                                             : config["player"]["fly_speed"].get<float>() / 5.f * (running ? runSpeed / walkSpeed : 1.f);
        }
        float lookT = config["player"]["look_inertia"];
        float blend = lookT > 0 ? 1 - std::exp(-dt / lookT) : 1;
        if (control.photoHeld()) { lookX=lookY=0; keys.clear(); }
        camera.yaw += lookX * blend;
        camera.pitch = std::clamp(camera.pitch + lookY * blend, -1.48f, 1.48f);
        lookX *= 1 - blend;
        lookY *= 1 - blend;
        camera.fov = std::max(3.f, baseFov / (zoomHeld ? zoom : 1.f));
        control.restorePhoto(camera);
        renderer.debug = debug;
        renderer.showAreaMarkers = input.config["show_area_markers"];
        waypointArrival();
        renderer.waypoint = control.location["waypoint"];
        renderer.route = control.location["route"];
        routeLineTick();
        renderer.useDistance = input.config["useDistance"];
        renderer.scopeVignette = zoomHeld && input.config["scopeVignette"].get<bool>();
        float moveT = config["player"]["move_inertia"];
        float mb = moveT > 0 ? 1 - std::exp(-dt / moveT) : 1;
        smoothIntent.forward += (intent.forward - smoothIntent.forward) * mb;
        smoothIntent.right += (intent.right - smoothIntent.right) * mb;
        smoothIntent.up += (intent.up - smoothIntent.up) * mb;
        intent.forward = climbing.active() ? intent.forward : smoothIntent.forward;
        intent.right = climbing.active() ? intent.right : smoothIntent.right;
        intent.up = smoothIntent.up;
        if (climbing.active() && (typing || !ownsInput() || control.uiVisible() || control.driving() ||
                                  handover.phase || fullscreen.phase || movement != &walk || noclip))
            climbing.reset(camera);
        // Recording and replay keep exact fixed steps; live motion moves by the frame's dt
        // (the walker and flight sub-step at 2 ms themselves).
        auto movementSection = timed("movement");
        const bool fixedSteps = session.recording || session.playing;
        accumulator = fixedSteps ? std::min(accumulator + double(dt), 4.0 / 60.0) : std::min(double(dt), 4.0 / 60.0);
        while (fixedSteps ? accumulator >= 1.0 / 60.0 : accumulator > 1e-6) {
            const float h = fixedSteps ? 1.f / 60.f : float(std::min(accumulator, .05));
            if (!layerCursor && !loading && !control.driving() && !control.uiVisible() && !handover.phase && fullscreen.phase == 0 &&
                !cinema.active()) {
                const auto beforeMove = camera.position;
                if (noclip) {
                    auto speed = intent.speed;
                    intent.speed = config["player"]["fly_speed"].get<float>() / 5.f;
                    fly.step(camera, intent, h, {});
                    intent.speed = speed;
                } else if (movement == &walk && !typing && ownsInput())
                    climbing.step(camera, intent, h, world->climbables,
                                  sim::climbMode(config["movement"]["climb_mode"].get<std::string>()),
                                  config["graphics"]["reduced_motion"], world->collision, &world->collisionIndex);
                else
                    movement->step(camera, intent, h, world->collision, &world->collisionIndex);
                if (movement == &fly && !noclip && !typing && ownsInput() && !control.photoHeld())
                    camera.position = world->playable.constrain(
                        beforeMove, camera.position, config["player"]["fly_bounds_allowance"].get<float>(), h);
            }
            accumulator -= h;
        }
        if (!fixedSteps)
            accumulator = 0;
        { auto done = std::move(movementSection); }
        const bool outside = noclip && !world->playable.contains(camera.position);
        if (outside && !outsidePlayable)
            ipc->notify("runtime.notice", {{"level", "warning"},
                                           {"code", "world.bounds.left"},
                                           {"message", "Leaving world bounds · noclip is unbounded"}});
        outsidePlayable = outside;
        if (!noclip && !control.photoHeld() && !cinema.active() && config["player"]["respawn_below"].get<float>() > 0 &&
            camera.position.y < world->spawn().position.y + 1.7f - config["player"]["respawn_below"].get<float>()) {
            auto s = world->spawn();
            climbing.reset(camera);
            camera.eyeHeight = 1.7f;
            camera.position = s.position + Vec3{0, 1.7f, 0};
            camera.verticalSpeed = 0;
            keys.clear();
            smoothIntent = {};
            cancelGrab();
        }
        {
            auto section = timed("motion");
            motionTick(dt);
        }
        {
            auto section = timed("npc");
            npcTick(dt);
        }
        {
            auto section = timed("machines");
            machinesTick(dt);
        }
        if (cinema.active()) {
            auto section = timed("cinema");
            cinemaTick(dt);
            cinemaTicked = true;
        }
        {
            auto section = timed("hands");
            handTick(dt);
        }
        {
            auto section = timed("tools");
            toolsTick();
        }
        {
            auto section = timed("reticle");
            reticleTick();
        }
        auto captureStarted = std::chrono::steady_clock::now();
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        captureGPU.begin();
        {
            auto section = timed("refresh");
            refresh();
        }
        captureGPU.end();
        if (offload.connected()) { // camera, characters and objects are final: this frame's pose
            auto section = timed("offload.send");
            offloadPump(2);
        }
        renderer.captureMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - captureStarted).count();
        if (fullscreen.phase == 1 || fullscreen.phase == 3)
            transitionTick(0);
        for (auto &entry : capture.entries)
            if (typing && entry.visible && !entry.parentKeyboard && !entry.parentId.empty() &&
                entry.parentId == focused && Desktop::focusState()->window() == entry.window) {
                focusedParent = focused;
                focused = entry.surface.id;
                break;
            }
        if (!focused.empty() && !capture.find(focused)) {
            if (!focusedParent.empty() && capture.find(focusedParent))
                focused = focusedParent;
            else {
                typing = false;
                focused.clear();
            }
            focusedParent.clear();
            keys.clear();
            smoothIntent = {};
        }
        if (!layerCursor && typing && !focused.empty() && !g_pSeatManager->m_seatGrab)
            if (auto e = capture.find(focused))
                if (g_pSeatManager->m_state.keyboardFocus != e->window->resource())
                    g_pSeatManager->setKeyboardFocus(e->window->resource());
        renderer.pointerX = layerCursor ? -1.f : (typing || editor.activeHandle >= 0) ? pointerX : .5f;
        renderer.pointerY = layerCursor ? -1.f : (typing || editor.activeHandle >= 0) ? pointerY : .5f;
        {
            auto &r = renderer.reticle;
            auto &player = config["player"];
            static const std::map<std::string, int> states{
                {"neutral", r.Neutral}, {"target", r.Target},   {"valid", r.Valid},  {"out_of_range", r.OutOfRange},
                {"invalid", r.Invalid}, {"holding", r.Holding}, {"typing", r.Typing}};
            auto styles = interaction::playerOptions()["reticle_style"];
            r.style = int(std::find(styles.begin(), styles.end(), player["reticle_style"]) - styles.begin());
            r.visible = states.contains(reticle.state);
            r.state = r.visible ? states.at(reticle.state) : r.Neutral;
            r.size = player["reticle_size"];
            r.thickness = player["reticle_thickness"];
            r.gap = player["reticle_gap"];
            r.opacity = player["reticle_opacity"];
            r.outline = player["reticle_outline"];
            auto colour = [&](const char *key) {
                auto &c = renderer.overlayStyle[key];
                return Vec3{c[0], c[1], c[2]};
            };
            r.color = r.state == r.Valid                               ? colour("validColor")
                      : r.state == r.Invalid || r.state == r.OutOfRange ? colour("invalidColor")
                      : r.state == r.Typing  ? Vec3{.5f, 1.f, .65f}
                      : player["reticle_colour"].is_array()
                          ? Vec3{player["reticle_colour"][0], player["reticle_colour"][1], player["reticle_colour"][2]}
                          : colour("reticleColor");
            // Dynamic: opens up while moving or turning, settles when still (motion is smoothed).
            bool dynamic = player["reticle_dynamic"] == true && config["graphics"]["reduced_motion"] != true;
            float motion = std::clamp(motionSpeed / std::max(.5f, player["run_speed"].get<float>()) + motionTurn / 4.f, 0.f, 1.f);
            r.spread = dynamic && r.state != r.Typing ? motion * (6 + r.gap * .5f) : 0;
            r.hint = reticle.state == "out_of_range" && reticle.distance > 0 ? tooFarText(reticle.distance) : "";
        }
        auto pointerHit = aim();
        if (!grabbed.empty()) {
            pointerHit.reset();
            if (auto plane = capture.plane(grabbed))
                if (auto h = surfaces::intersect(*plane, pointerCamera().position, pointerRay()))
                    pointerHit = {{grabbed, *h}};
        }
        if (ownsInput() && !layerCursor && hands.gesture == interaction::Gesture::None && editor.activeHandle < 0 &&
            (typing || editor.slot == 1)) {
            auto time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            if (pointerHit)
                routePointer(pointerHit->first, pointerHit->second, time);
            else if (buttons.empty())
                g_pSeatManager->setPointerFocus(nullptr, {});
        }
        for (auto &e : capture.entries)
            e.surface.focused = e.surface.id == focused;
        // Render on demand with animated world objects: only while one's swept bounds
        // are in view (and not region-culled) on a lit output. Otherwise an unchanged
        // frame is reused and animation forces nothing.
        animationFrames = !loading && monitor->m_enabled && monitor->m_dpmsStatus &&
                          renderer.animationVisible(camera, float(monitor->m_pixelSize.x) /
                                                                std::max(1.f, float(monitor->m_pixelSize.y)));
        auto keyStart = frameLog.file ? FrameLog::now() : 0;
        bool changedFrame = frameForced() || frameKey(frameSurfaces()) != renderedKey;
        if (frameLog.file)
            frameLog.sections["framekey"] += FrameLog::now() - keyStart;
        if (changedFrame) {
            if (frameLog.file)
                frameLog.marks.push_back("frame");
            g_pHyprRenderer->damageMonitor(monitor.lock());
            monitor->scheduleFrame();
        }
    }
    if (cinema.active() && !cinemaTicked) // the world is not shown: the driver's time still runs
        cinemaTick(dt);
    replayAfterTick();
    renderer.updateMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStarted).count();
    cursor();
    {
        auto section = timed("quests");
        questsTick();
    }
    auto ipcStarted = std::chrono::steady_clock::now();
    if (ipcStarted >= nextPublish || !control.events.empty() || !layout.events.empty()) {
        nextPublish = ipcStarted + std::chrono::milliseconds(33);
        auto section = timed("publish");
        publish();
    }
    {
        auto section = timed("offload");
        offloadTick();
    }
    {
        auto section = timed("hud");
        publishHUD();
    }
    ipc->poll();
    renderer.gpuCaptureMs = captureGPU.available ? captureGPU.milliseconds : -1;
    renderer.ipcMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ipcStarted).count();
}
std::vector<surfaces::Surface> Runtime::frameSurfaces() {
    auto surfaces = capture.visible();
    if (editor.slot == 5 && !freeCursor)
        if (auto preview = placementPreview())
            surfaces.push_back(*preview);
    if (fullscreen.phase == 1 || fullscreen.phase == 3) {
        auto fs = capture.find(fullscreen.id);
        if (fs) {
            std::erase_if(surfaces, [&](auto &s) { return s.id == fs->surface.id; });
            surfaces.push_back(fs->surface);
        }
    }
    renderer.showInteractionHints = !freeCursor && !typing; // in use (E) the "E USE / F FOCUS" prompt is moot
    renderer.toolPrompt = nullptr;
    auto prompt = [&](Vec3 point, std::string text) {
        renderer.toolPrompt = {{"position", {point.x, point.y, point.z}}, {"text", text}};
    };
    if (renderer.showInteractionHints && editor.slot == 4 && gizmoHover == interaction::Part::None && !gizmo.dragging()) {
        if (auto object = objectAim(); object && object->first != editor.selected)
            prompt(object->second.point, "LMB SELECT / WHEEL " + editor.submode());
    }
    if (renderer.showInteractionHints && editor.slot == 2 && editor.windowMode == 0 && !tagged.empty()) {
        if (auto hit = worldHit())
            prompt(hit->point, "LMB MOUNT HERE");
    }
    if (renderer.showInteractionHints && !noclip && movement == &walk && !typing &&
        (climbing.active() || climbing.nearby(camera, world->climbables)))
        prompt(camera.position + camera.forward() * 2,
               climbing.prompt(
                   sim::climbMode(config["movement"]["climb_mode"].get<std::string>())));
    if (world && (!renderer.toolPrompt.is_object() || npcInteraction))
        npcPrompts();
    renderer.renderScale = control.photoHeld() ? control.photoScale :
        (renderer.graphics["render_scale"] == "auto" ? 0.f : renderer.graphics["render_scale"].get<float>());
    renderer.dynamicResolution = !control.photoHeld() && renderer.graphics["dynamic_resolution"].get<bool>();
    renderer.logicalScale = monitor ? monitor->m_scale : 1.f;
    return surfaces;
}
namespace {
// FNV-1a over the exact draw inputs; collisions only cost a skipped repaint
// until the next input change, and 64 bits make them practically impossible.
struct FrameHash {
    uint64_t value = 1469598103934665603ull;
    void bytes(const void *data, size_t size) {
        for (auto c = static_cast<const unsigned char *>(data); size--; ++c)
            value = (value ^ *c) * 1099511628211ull;
    }
    template <class T>
        requires std::is_arithmetic_v<T>
    FrameHash &operator()(T v) {
        bytes(&v, sizeof v);
        return *this;
    }
    FrameHash &operator()(Vec3 v) { return (*this)(v.x)(v.y)(v.z); }
    FrameHash &operator()(const std::string &s) {
        bytes(s.data(), s.size());
        return (*this)(s.size());
    }
    FrameHash &operator()(const Json &j) { return (*this)(j.dump()); }
};
} // namespace
void Runtime::framePresented(std::chrono::steady_clock::time_point when) {
    // Called from the backend's presentation feedback: never tick here, defer it.
    lastPresented = std::chrono::steady_clock::now();
    presentedAt = when;
    ++presents;
    if (frameProbe.active() && probePresents.size() < 100000)
        probePresents.push_back(std::chrono::duration<double, std::milli>(when.time_since_epoch()).count());
    if (frameLog.file)
        frameLog.line({{"kind", "present"}, {"t", FrameLog::now()}});
    if (!tickDeferred)
        tickDeferred = g_pEventLoopManager->doLater([] {
            if (!runtime)
                return;
            runtime->tickDeferred = 0;
            safe([] { runtime->tick(); });
        });
}
bool Runtime::frameForced() {
    // Time-driven or externally produced frames always render. The environment
    // switch keeps GPU clocks steady for fixed-scale profiling (dev/nested/perf.py).
    static const bool continuous = getenv("HYPRUNE_CONTINUOUS_RENDER");
    return continuous || debug || animationFrames || loading || handover.phase || fullscreen.phase || capturePending || renderer.captureRequested ||
           offload.connected() || session.recording || session.playing || control.driving() || cinema.moving() ||
           frameExport.active() || !renderer.frames;
}
uint64_t Runtime::frameKey(std::span<const surfaces::Surface> surfaces) {
    FrameHash h;
    std::vector<uint64_t> parts;
    auto part = [&](const char *) { parts.push_back(h.value); };
    auto &r = renderer;
    h(reinterpret_cast<uintptr_t>(world.get()))(camera.position)(camera.yaw)(camera.pitch)(camera.roll)(camera.fov);
    h(camera.shiftX)(camera.shiftY)(camera.eyeHeight)(camera.climbBob);
    part("camera");
    // NPCs: shown/hidden, placed and posed (a paused or still NPC keeps the key stable; an
    // animating one out of view doesn't change the image, and in view frames are forced).
    h(r.npcs.size())(r.charactersReady()); // a character's first frame once its upload is done
    // Occlusion changes once per crossing: the frame after a character goes fully behind a
    // wall draws it gone, instead of leaving its last frame at the wall's edge. Only drawn
    // ones count: an off-screen resident walking behind walls changes nothing on screen.
    if (auto m = monitor.lock(); m && !r.npcs.empty())
        for (bool drawn : r.npcsInSight(camera, float(m->m_pixelSize.x) / std::max(1.f, float(m->m_pixelSize.y))))
            h(drawn);
    if (npcState != "animating")
        for (auto &npc : r.npcs) {
            h.bytes(npc.model.v.data(), sizeof npc.model.v);
            if (!npc.skin.empty())
                h.bytes(npc.skin.data(), npc.skin.size() * sizeof(Mat4));
        }
    part("npcs");
    if (auto m = monitor.lock()) {
        h(m->m_pixelSize.x)(m->m_pixelSize.y)(m->m_scale);
        // Machines: a bound change renders a frame only while its instance is in view;
        // out of view it updates state and forces nothing (seen on the next view change).
        float aspect = float(m->m_pixelSize.x) / std::max(1.f, float(m->m_pixelSize.y));
        for (auto k : machines.instances())
            if (renderer.objectInView(k, camera, aspect))
                h(k)(machines.states[k].revision);
    }
    part("monitor");
    for (auto &s : surfaces) {
        h(s.id)(s.center)(s.width)(s.height)(s.yaw)(s.opacity)(s.texture)(s.focused)(s.pixelWidth)(s.pixelHeight);
        for (auto q : s.rotation)
            h(q);
    }
    part("surfaces");
    // Client content: any commit, including hidden clients composed onto walls,
    // and wall layout (a tiled swap recomposes a wall without a commit).
    for (auto &e : capture.entries) {
        h(*e.commits)(e.visible)(e.instance.workspaceId);
        if (auto w = e.window.lock())
            if (auto box = w->logicalBox())
                h(box->x)(box->y)(box->w)(box->h);
    }
    part("clients");
    for (auto &c : capture.children)
        h(*c.commits);
    // Wall composites: a recomposition (stacking, layout) must reach the world frame.
    for (auto &f : capture.workspaces)
        h(f.key);
    part("children");
    h(typing && !layerCursor)(r.debug)(r.showAreaMarkers)(r.showInteractionHints)(r.useDistance)(r.renderScale);
    h(r.logicalScale)(r.pointerX)(r.pointerY)(r.scopeVignette)(r.materialDetails)(r.effects)(r.worldOnly)(r.font);
    part("flags");
    h(r.waypoint)(r.route)(r.toolPrompt)(r.overlayStyle)(r.graphics)(r.profile);
    part("overlays");
    h(markers.frameHash(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count()));
    part("markers");
    for (auto &l : r.lines)
        h(l.a)(l.b)(l.color)(l.width);
    part("lines");
    h(objectRevision)(r.gizmo.strokes.size())(r.gizmo.fills.size());
    for (auto &st : r.gizmo.strokes)
        h(st.a.x)(st.a.y)(st.b.x)(st.b.y)(st.color.r)(st.color.a)(st.width);
    for (auto &f : r.gizmo.fills)
        for (auto &p : f.points)
            h(p.x)(p.y)(f.color.r)(f.color.a);
    part("gizmo");
    for (auto &l : r.readouts) {
        h(l.text)(l.x)(l.y);
        for (auto &line : l.lines)
            h(line);
    }
    h(r.debugBanner.title);
    for (auto &[label, color] : r.debugBanner.legend)
        h(label)(color);
    part("readouts");
    // Characters' instances follow their agents every tick; their frames come from the NPC
    // rule above (in view while animating), so they stay out of this part of the key.
    if (world)
        for (auto &inst : world->objectInstances)
            if (!inst.character)
                h.bytes(inst.model.v.data(), sizeof inst.model.v);
    part("instances");
    for (auto &[node, m] : r.transforms) {
        h(node);
        for (auto v : m.v)
            h(v);
    }
    part("transforms");
    // Diagnostics: which parts changed since the previous key (counted per part).
    static const char *names[] = {"camera", "npcs", "monitor", "surfaces", "clients", "children", "flags", "overlays", "markers",
                                  "lines", "gizmo", "readouts", "instances", "transforms"};
    if (frameParts.size() == parts.size())
        for (size_t i = 0; i < parts.size(); ++i)
            if (parts[i] != frameParts[i] && (i == 0 || parts[i - 1] == frameParts[i - 1]))
                ++frameCauses[names[i]];
    frameParts = std::move(parts);
    return h.value;
}
class Pass final : public IPassElement {
  public:
    std::vector<UP<IPassElement>> draw() override {
        safe([] {
            if (!runtime || !runtime->active || locked())
                return;
            auto &data = g_pHyprRenderer->m_renderData;
            auto *fb = dynamic_cast<Render::GL::CGLFramebuffer *>(data.currentFB.get());
            if (!fb)
                return;
            if (runtime->loading) {
                runtime->renderer.drawLoading(fb->getFBID(), data.currentFB->m_size.x, data.currentFB->m_size.y);
                return;
            }
            if (!runtime->layout.desktop.is_null() || runtime->fullscreen.phase == 2) {
                runtime->renderer.drawHUD(fb->getFBID(), data.currentFB->m_size.x, data.currentFB->m_size.y);
                return;
            }
            auto surfaces = runtime->frameSurfaces();
            auto &r = runtime->renderer;
            auto key = runtime->frameKey(surfaces);
            r.reuseFrame = !runtime->frameForced() && key == runtime->renderedKey;
            r.externalWorldFbo = runtime->control.photoHeld() ? 0 : runtime->offload.fbo(runtime->frameExport.active() ? 1000 : 50);
            if (runtime->offload.connected() && !runtime->control.photoHeld()) {
                // Diagnostics: frames drawn from the worker vs. stale (local stand-in), by front age.
                auto age = runtime->offload.frontAgeMs();
                auto &d = runtime->offloadDraws;
                std::string kind = r.externalWorldFbo ? "shown" : age < 0 ? "missing" : "stale";
                d[kind] = d.value(kind, 0) + 1;
                if (age >= 0) {
                    if (!d.contains("ageMs"))
                        d["ageMs"] = Json::object();
                    auto bin = std::to_string(std::min(int(age / 10) * 10, 100));
                    d["ageMs"][bin] = d["ageMs"].value(bin, 0) + 1;
                }
            }
            auto view = r.externalWorldFbo ? runtime->offload.camera() : runtime->camera;
            auto frames = r.frames;
            double drawStart = runtime->frameLog.file ? Runtime::FrameLog::now() : 0;
            r.draw(fb->getFBID(), data.currentFB->m_size.x, data.currentFB->m_size.y, view,
                   surfaces, runtime->typing && !runtime->layerCursor);
            if (runtime->frameLog.file) // buffered only; written by the next tick
                runtime->frameLog.line({{"kind", "draw"}, {"t", drawStart}, {"ms", Runtime::FrameLog::now() - drawStart},
                                        {"new", r.frames != frames}});
            if (r.frames != frames) {
                runtime->renderedKey = key;
                if (runtime->session.recording) { auto state=runtime->sessionState(); state["camera"]=replay::pose(view); runtime->session.view(state,r.frames); }
                runtime->displayedCamera = view;
                runtime->renderedOffload = r.externalWorldFbo != 0;
            }
            r.externalWorldFbo = 0;
        });
        return {};
    }
    bool needsLiveBlur() override { return false; }
    bool needsPrecomputeBlur() override { return false; }
    const char *passName() override { return "Hyprune"; }
    ePassElementType type() override { return EK_CUSTOM; }
    bool undiscardable() override { return true; }
    bool disableSimplification() override { return true; }
};
// The frame export's grab: last in the output's pass, after layers and cursors.
class ExportPass final : public IPassElement {
  public:
    std::vector<UP<IPassElement>> draw() override {
        safe([] {
            if (!runtime || !(runtime->frameExport.active() || runtime->frameProbe.active()))
                return;
            auto &data = g_pHyprRenderer->m_renderData;
            auto *fb = dynamic_cast<Render::GL::CGLFramebuffer *>(data.currentFB.get());
            if (!fb)
                return;
            render::GLState guard;
            int w = int(data.currentFB->m_size.x), h = int(data.currentFB->m_size.y);
            if (runtime->frameProbe.active()) {
                auto now = std::chrono::steady_clock::now();
                bool worker = runtime->renderedOffload;
                runtime->frameProbe.grab(fb->getFBID(), w, h, std::chrono::duration<double, std::milli>(now.time_since_epoch()).count(),
                                         worker ? runtime->offload.frontSequence() : 0,
                                         worker ? runtime->offload.frontAgeMs()
                                                : std::chrono::duration<double, std::milli>(now - runtime->lastTick).count());
            }
            if (runtime->frameExport.active()) {
                runtime->frameExport.grab(fb->getFBID(), w, h);
                ++runtime->exportPasses;
            }
        });
        return {};
    }
    bool needsLiveBlur() override { return false; }
    bool needsPrecomputeBlur() override { return false; }
    const char *passName() override { return "HypruneExport"; }
    ePassElementType type() override { return EK_CUSTOM; }
    bool undiscardable() override { return true; }
    bool disableSimplification() override { return true; }
};
void deferToggle(std::string output) {
    if (runtime->deferred)
        g_pEventLoopManager->removeDoLater(runtime->deferred);
    runtime->deferred = g_pEventLoopManager->doLater([output] {
        if (!runtime)
            return;
        runtime->deferred = 0;
        safe([&] { runtime->toggle(output); });
    });
}
Json luaValue(lua_State *L, int index, int depth = 0) {
    if (depth > 8)
        throw std::runtime_error("config nesting too deep");
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN:
        return bool(lua_toboolean(L, index));
    case LUA_TNUMBER:
        if (lua_isinteger(L, index))
            return lua_tointeger(L, index);
        return lua_tonumber(L, index);
    case LUA_TSTRING:
        return std::string(lua_tostring(L, index));
    case LUA_TTABLE: {
        Json result = lua_rawlen(L, index) > 0 ? Json::array() : Json::object();
        lua_pushnil(L);
        int count = 0;
        while (lua_next(L, index)) {
            if (++count > 64)
                throw std::runtime_error("config table too large");
            auto v = luaValue(L, -1, depth + 1);
            if (result.is_array()) {
                if (!lua_isinteger(L, -2))
                    throw std::runtime_error("array key must be integer");
                int i = lua_tointeger(L, -2);
                if (i < 1 || i > 5)
                    throw std::runtime_error("array index out of range");
                result[i - 1] = v;
            } else {
                if (lua_type(L, -2) != LUA_TSTRING)
                    throw std::runtime_error("config key must be string");
                result[lua_tostring(L, -2)] = v;
            }
            lua_pop(L, 1);
        }
        return result;
    }
    default:
        throw std::runtime_error("unsupported config value");
    }
}
int luaConfig(lua_State *L) {
    std::string failure;
    Json next;
    try {
        next = interaction::configPatch(runtime->configured, luaValue(L, 1));
        std::set<xkb_keysym_t> used;
        for (auto &[name, key] : next["input"]["bindings"].items()) {
            auto sym = xkb_keysym_from_name(key.get<std::string>().c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
            if (sym == XKB_KEY_NoSymbol)
                throw std::runtime_error("unknown keysym for " + name);
            if (!used.insert(sym).second)
                throw std::runtime_error("duplicate keysym for " + name);
            if (sym == XKB_KEY_F12 || sym == XKB_KEY_Escape || sym == XKB_KEY_Tab || sym == XKB_KEY_Shift_L ||
                sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R || sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R ||
                (sym >= XKB_KEY_1 && sym <= XKB_KEY_5))
                throw std::runtime_error("core-owned key for " + name);
        }
    } catch (const std::exception &e) {
        failure = e.what();
    }
    if (!failure.empty())
        return luaL_error(L, "hyprune.config: %s", failure.c_str());
    if (lua_gettop(L) > 0) {
        auto patch = luaValue(L, 1);
        if (patch.contains("graphics"))
            for (auto &[name, value] : patch["graphics"].items())
                runtime->graphicsOverrides.insert(name);
    }
    runtime->configured = next;
    runtime->deferInput([next] {
        auto old = runtime->config;
        if (old == next && !runtime->graphicsOverrides.contains("exposure") &&
            !runtime->graphicsOverrides.contains("bloom_strength"))
            return;
        if (old["input"] != next["input"] || old["tools"] != next["tools"]) {
            runtime->cancelGrab();
            runtime->keys.clear();
            runtime->smoothIntent = {};
            runtime->tagged.clear();
        }
        runtime->config = next;
        runtime->applyPlayer(runtime->playerFile.current);
        runtime->deriveHomes();
        runtime->applyGraphics(runtime->graphicsFile.current);
        runtime->cursor();
        runtime->publish();
    });
    lua_pushstring(L, next.dump().c_str());
    return 1;
}
int luaToggle(lua_State *state) {
    deferToggle(lua_isstring(state, 1) ? lua_tostring(state, 1) : "");
    return 0;
}
// Local test / user configuration interface. These are not wire-protocol methods.
int luaCamera(lua_State *state) {
    if (!runtime)
        return 0;
    auto section = runtime->timed("lua.camera"); // development diagnostics (nested harness)
    if (lua_gettop(state) >= 5) {
        runtime->climbing.reset(runtime->camera);
        runtime->camera.position = {float(luaL_checknumber(state, 1)), float(luaL_checknumber(state, 2)),
                                    float(luaL_checknumber(state, 3))};
        runtime->camera.yaw = luaL_checknumber(state, 4);
        runtime->camera.pitch = std::clamp(float(luaL_checknumber(state, 5)), -1.48f, 1.48f);
        runtime->camera.verticalSpeed = 0;
    }
    Json j = {
        {"position", {runtime->camera.position.x, runtime->camera.position.y, runtime->camera.position.z}},
        {"yaw", runtime->camera.yaw},
        {"eyeHeight", runtime->camera.eyeHeight},
        {"pitch", runtime->camera.pitch},
        {"frames", runtime->renderer.frames},
        {"animationFrames", runtime->animationFrames},
        {"frameCauses", runtime->frameCauses},
        {"route", runtime->routeDiagnostics()},
        {"offloadRequests", runtime->offloadChanges},
        {"offloadClient", runtime->offload.diagnostics()},
        {"offloadDraws", runtime->offloadDraws},
        {"grabbed", runtime->grabbed},
        {"gesture", int(runtime->hands.gesture)},
        {"activeHandle", runtime->editor.activeHandle},
        {"positionMode", runtime->editor.positionMode},
        {"gizmo", runtime->gizmoDiagnostics()},
        {"path", runtime->pathDiagnostics()},
        {"debugTool", runtime->debugToolDiagnostics()},
        {"selectedScale",
         {runtime->selectedTransform.scale.x, runtime->selectedTransform.scale.y, runtime->selectedTransform.scale.z}},
        {"superHeld", runtime->superHeld},
        {"readyNotices", runtime->readyNotices},
        {"ownsInput", ownsInput()},
        {"layerCursor", runtime->layerCursor},
        {"debug", runtime->debug},
        {"overlays", runtime->renderer.overlayDiagnostics},
        {"overlayStyle", runtime->renderer.overlayStyle},
        {"timings", runtime->renderer.timings()},
        {"captureDirectory", runtime->captureDirectory.string()},
        {"instances", runtime->layout.instances},
        {"aim", aim() ? aim()->first : ""},
        {"reticle",
         {{"state", runtime->reticle.state},
          {"action", runtime->reticle.action},
          {"distance", runtime->reticle.distance},
          {"reach", runtime->reticle.reach},
          {"spread", runtime->renderer.reticle.spread},
          {"hint", runtime->renderer.reticle.hint},
          {"color",
           {runtime->renderer.reticle.color.x, runtime->renderer.reticle.color.y, runtime->renderer.reticle.color.z}}}},
        {"viewBob", {runtime->camera.viewBob.x, runtime->camera.viewBob.y, runtime->camera.viewBob.z}},
        {"npcs",
         [] {
             // Frames forced by the shared animation path because a moving NPC is in view.
             auto d = runtime->npcDiagnostics();
             d["forcesFrames"] = runtime->animationFrames && d["animationVisible"].get<bool>();
             return d;
         }()},
        {"companion", runtime->companionDiagnostics()},
        {"machines",
         [] {
             auto m = runtime->monitor.lock();
             float aspect = m ? float(m->m_pixelSize.x) / std::max(1.f, float(m->m_pixelSize.y)) : 16 / 9.f;
             auto d = runtime->machines.diagnostics();
             for (auto &in : d["instances"]) {
                 size_t k = 0;
                 for (; k < runtime->world->objectInstances.size(); ++k)
                     if (runtime->world->objectInstances[k].id == in["id"].get<std::string>())
                         break;
                 in["inView"] = runtime->renderer.objectInView(k, runtime->camera, aspect);
             }
             d["active"] = runtime->machines.active();
             d["labelsDrawn"] = runtime->renderer.worldLabelsDrawn;
             return d;
         }()},
        {"motion", {{"speed", runtime->motionSpeed}, {"turn", runtime->motionTurn}}},
        {"mountTarget", runtime->snapTarget},
        {"renderPasses", runtime->renderPass.passes()},
        {"renderPassesLeaked", runtime->renderPass.leaked()},
        {"renderPassOpen", runtime->renderPass.active()},
        {"backdrops", runtime->capture.backdropDiagnostics()},
        {"gestureId", runtime->hands.pose.id},
        {"mode", runtime->typing ? "typing" : "movement"},
        {"tool", runtime->tool},
        {"noclip", runtime->noclip},
        {"fullscreen", runtime->fullscreen.phase},
        {"fullscreenId", runtime->fullscreen.id},
        {"handover", runtime->handover.phase},
        {"cursor", runtime->cursorState},
        {"nativeCursorVisible", g_pHyprRenderer->shouldRenderCursor()},
        {"nativePointerFocused", bool(g_pSeatManager->m_state.pointerFocus)},
        {"savedCursorHidden", runtime->savedCursorHidden},
        {"pointer", {runtime->pointerX, runtime->pointerY}},
        // Use mode's window-local virtual cursor (u, v on the root plane; motion is in its pixels).
        {"localCursor", {{"surfaceId", runtime->relativeSurface}, {"u", runtime->localX}, {"v", runtime->localY}}},
        {"config", runtime->config},
        {"children", runtime->capture.children.size()},
        {"seatGrab", bool(g_pSeatManager->m_seatGrab)},
        {"keyboardFocus", runtime->capture.find(runtime->focused)
                              ? bool(g_pSeatManager->m_state.keyboardFocus ==
                                     runtime->capture.find(runtime->focused)->window->resource())
                              : false},
        {"pressedButtons", runtime->buttons.size()},
        {"error", runtime->lastError},
        {"active", runtime->active},
        {"worldWorkspace", runtime->worldWorkspace},
        {"previousWorkspace", runtime->previousWorkspace ? runtime->previousWorkspace->m_id : 0}};
    j["childSurfaces"] = Json::array();
    for (auto &child : runtime->capture.children)
        j["childSurfaces"].push_back(
            {{"id", child.surface.id},
             {"parentId", child.parentId},
             {"offset", {child.offset.x, child.offset.y}},
             {"pixels", {child.surface.pixelWidth, child.surface.pixelHeight}},
             {"visible", child.visible},
             {"position", {child.surface.center.x, child.surface.center.y, child.surface.center.z}},
             {"size", {child.surface.width, child.surface.height}},
             {"pixels", {child.surface.pixelWidth, child.surface.pixelHeight}}});
    j["workspaceAnimations"] = Json::array();
    for (auto &workspace : State::workspaceState()->workspaces())
        if (workspace && !workspace->m_isSpecialWorkspace) {
            auto offset = workspace->m_renderOffset->value();
            j["workspaceAnimations"].push_back(
                {{"id", workspace->m_id},
                 {"offset", {offset.x, offset.y}},
                 {"alpha", workspace->m_alpha->value()},
                 {"animating", workspace->m_renderOffset->isBeingAnimated() || workspace->m_alpha->isBeingAnimated()}});
        }
    j["captureBytes"] = runtime->capture.leasedBytes;
    j["uncaptured"] = Json::array();
    for (auto &skipped : runtime->capture.skipped)
        j["uncaptured"].push_back({{"title", skipped.title}, {"reason", skipped.reason}, {"address", skipped.address}});
    j["captures"] = Json::array();
    for (auto &e : runtime->capture.entries)
        j["captures"].push_back(
            {{"id", e.surface.id},
             {"title", e.surface.title},
             {"address", std::format("0x{:x}", reinterpret_cast<uintptr_t>(e.window.get()))},
             {"parentId", e.parentId},
             {"floating", e.window && e.window->m_isFloating},
             {"commits", *e.commits},
             {"visible", e.visible},
             {"wall", e.wall},
             {"queued", e.queued},
             {"placed", runtime->layout.freePoses.contains(e.surface.id)},
             {"size", {e.surface.width, e.surface.height}},
             {"collision", runtime->world && e.visible && surfaces::overlaps(e.surface, runtime->world->collision)},
             {"rotation", e.surface.rotation},
             {"normal", {e.surface.normal().x, e.surface.normal().y, e.surface.normal().z}},
             {"workspaceId", e.instance.workspaceId},
             {"pixels", {e.surface.pixelWidth, e.surface.pixelHeight}},
             {"position", {e.surface.center.x, e.surface.center.y, e.surface.center.z}}});
    // Static-geometry regression evidence: moved nodes and a collision checksum.
    double collisionSum = 0;
    if (runtime->world)
        for (auto &t : runtime->world->collision)
            collisionSum += t.a.x + t.a.y * 3 + t.a.z * 7 + t.b.x + t.c.z;
    j["objectTransforms"] = runtime->renderer.transforms.size();
    j["inputRecent"] = Json::array();
    for (size_t i = runtime->input.recent.size() > 24 ? runtime->input.recent.size() - 24 : 0; i < runtime->input.recent.size(); ++i)
        j["inputRecent"].push_back(runtime->input.recent[i]);
    j["presentLog"] = Json::array();
    for (auto &p : runtime->renderer.presentLog)
        j["presentLog"].push_back(Json(p));
    runtime->renderer.presentLog.clear();
    j["collisionChecksum"] = collisionSum;
    // RFC-0011 objects: the tool 5 ghost, instance draw state and asset uploads.
    {
        auto &g = runtime->objectGhost;
        Json instances = Json::array();
        if (runtime->world)
            for (auto &inst : runtime->world->objectInstances)
                instances.push_back({{"id", inst.id}, {"asset", inst.asset}, {"hidden", inst.hidden},
                                     {"collision", {inst.collisionFirst, inst.collisionCount}},
                                     {"center", {inst.center.x, inst.center.y, inst.center.z}}, {"radius", inst.radius}});
        Json slots = Json::object();
        for (auto &[ref, slot] : runtime->assetSlots)
            slots[ref] = {{"index", slot.index}, {"error", slot.error}, {"loading", slot.load.valid()}};
        j["objects"] = {{"ghost", {{"active", g.active}, {"valid", g.valid}, {"asset", g.asset}, {"rule", g.rule}, {"message", g.message},
                                   {"distance", g.distance}, {"position", {g.pose.position.x, g.pose.position.y, g.pose.position.z}},
                                   {"rotation", g.pose.rotation}}},
                        {"instances", instances},
                        {"assets", slots},
                        {"uploading", runtime->renderer.objectAssetsPending()},
                        {"worldAssets", runtime->world ? runtime->world->objectAssets.size() : 0},
                        {"unplacedSelected", runtime->unplacedSelected},
                        {"catalogScanning", runtime->catalogScan.valid()},
                        {"selected", runtime->editor.selected}};
    }
    j["markers"] = {{"mode", runtime->markers.mode},
                    {"state", runtime->markers.stateKey()},
                    {"layout", runtime->renderer.markerDiagnostics},
                    {"occluded", runtime->markers.occluded},
                    {"inside", runtime->markers.inside}};
    j["workspaceFrames"] = Json::array();
    for (auto &f : runtime->capture.workspaces)
        j["workspaceFrames"].push_back({{"id", f.surface.id},
                                        {"workspaceId", f.workspaceId},
                                        {"composed", f.composed},
                                        {"title", f.surface.title},
                                        {"position", {f.surface.center.x, f.surface.center.y, f.surface.center.z}},
                                        {"size", {f.surface.width, f.surface.height}},
                                        {"right", {f.surface.right().x, f.surface.right().y, f.surface.right().z}},
                                        {"up", {f.surface.up().x, f.surface.up().y, f.surface.up().z}}});
    lua_pushstring(state, j.dump().c_str());
    return 1;
}

} // namespace
} // namespace hyprune::platform
using namespace hyprune::platform;
using namespace hyprune;
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE pluginHandle) {
    if (std::string(__hyprland_api_get_hash()) != std::string(__hyprland_api_get_client_hash()))
        throw std::runtime_error("Hyprune: Hyprland ABI mismatch");
    handle = pluginHandle;
#ifdef HYPRUNE_LIVE_BUILD
    hyprune::live::Environment liveEnvironment; // Restored before PLUGIN_INIT returns.
#endif
    runtime = std::make_unique<Runtime>();
    runtime->input.apply(runtime->inputFile.current);
    auto stateHome = getenv("XDG_STATE_HOME") ? std::filesystem::path(getenv("XDG_STATE_HOME"))
                                              : std::filesystem::path(getenv("HOME")) / ".local/state";
    runtime->captureDirectory = getenv("HYPRUNE_CAPTURE_DIR") ? std::filesystem::path(getenv("HYPRUNE_CAPTURE_DIR"))
                                                              : stateHome / "hyprune/captures";
    // Gizmo 2.0 switches that need a contract change before they can be defaults
    // (RFC-0011 P1): world 0.8 objects as targets, the universal mode, keepClear volumes.
    runtime->gizmoObjects = getenv("HYPRUNE_GIZMO_OBJECTS") && std::string(getenv("HYPRUNE_GIZMO_OBJECTS")) == "1";
    if (auto file = getenv("HYPRUNE_FRAME_LOG"))
        runtime->frameLog.file = std::fopen(file, "w");
    runtime->editor.universal = getenv("HYPRUNE_GIZMO_UNIVERSAL") && std::string(getenv("HYPRUNE_GIZMO_UNIVERSAL")) == "1";
    if (auto file = getenv("HYPRUNE_GIZMO_KEEPCLEAR"))
        try {
            for (auto &k : interaction::readJSON(file))
                runtime->gizmoKeepClear.push_back(interaction::aabbBox({k["min"][0], k["min"][1], k["min"][2]},
                                                                       {k["max"][0], k["max"][1], k["max"][2]}));
        } catch (const std::exception &e) {
            std::fprintf(stderr, "[hyprune] HYPRUNE_GIZMO_KEEPCLEAR ignored: %s\n", e.what());
        }
    if (auto p = getenv("HYPRUNE_EDITOR_FILE"))
        runtime->editorFile = p;
    else if (auto p = getenv("XDG_STATE_HOME"))
        runtime->editorFile = std::filesystem::path(p) / "hyprune/editor.json";
    else
        runtime->editorFile = std::filesystem::path(getenv("HOME")) / ".local/state/hyprune/editor.json";
    if (std::filesystem::exists(runtime->editorFile))
        runtime->editor.restore(interaction::readJSON(runtime->editorFile));
    if (runtime->editor.migrated) {
        // Version 1 kept world-object transforms in objects; keep the old file once.
        std::error_code ec;
        auto backup = runtime->editorFile;
        backup += ".v1";
        if (!std::filesystem::exists(backup, ec))
            std::filesystem::copy_file(runtime->editorFile, backup, ec);
        interaction::atomicJSON(runtime->editorFile, runtime->editor.saved());
    }
    runtime->lastEditor = runtime->editor.saved();
    runtime->catalogInit();
    // HYPRUNE_WORLD_DIR, else $XDG_DATA_HOME/hyprune/worlds (absolute only), else ~/.local/share:
    // the root the SDK CLI and the installers write (RFC-0015 "Where state lives").
    const char *installed = getenv("HYPRUNE_WORLD_DIR");
    const char *dataHome = getenv("XDG_DATA_HOME");
    if (installed)
        runtime->installed = installed;
    else if (dataHome && dataHome[0] == '/')
        runtime->installed = std::filesystem::path(dataHome) / "hyprune/worlds";
    else
        runtime->installed = std::filesystem::path(getenv("HOME") ? getenv("HOME") : "/") / ".local/share/hyprune/worlds";
    const char *dir = getenv("XDG_RUNTIME_DIR");
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!dir || !sig)
        throw std::runtime_error("Hyprune: missing session environment");
    // Relocated builds (release archives) name their helper at run time.
    const char *daemon = getenv("HYPRUNE_DAEMON");
    runtime->ipc = std::make_unique<hyprune::ipc::Bridge>(
        daemon && daemon[0] == '/' ? daemon : HYPRUNE_DAEMON_PATH,
        [](auto &method, auto &params) { return runtime->method(method, params); });
    if (const char *file = getenv("HYPRUNE_WORKSPACE_LOCATIONS")) {
        std::ifstream input(file);
        input >> runtime->locations;
        if (!runtime->locations.is_array() || runtime->locations.size() > 32)
            throw std::runtime_error("invalid workspace mapping");
        std::set<int> ids;
        for (auto &entry : runtime->locations) {
            if (entry.size() != 4 || !entry.at("id").is_number_integer() || entry.at("id").get<int>() <= 0 ||
                !ids.insert(entry.at("id").get<int>()).second || !entry.at("label").is_string() ||
                entry.at("label").get<std::string>().size() > 256 || !entry.at("worldId").is_string() ||
                !entry.at("spawnId").is_string())
                throw std::runtime_error("invalid workspace mapping entry");
        }
    }
    auto methods = hyprune::control::Control::methods();
    methods.insert({{"capture.annotate", "shell.control"},
                    {"workspace.bind", "world.control"},
                    {"workspace.focus", "surface.control"},
                    {"workspace.unfocus", "surface.control"},
                    {"surface.pull", "surface.control"},
                    {"surface.place", "surface.control"},
                    {"surface.return", "surface.control"},
                    {"surface.pocket", "surface.control"},
                    {"surface.unpocket", "surface.control"},
                    {"surfaces.recall", "surface.control"},
                    {"placements.list", "state.read"},
                    {"placements.remove", "surface.control"},
                    {"data.object.bind", "data.publish"},
                    {"atmosphere.get", "state.read"},
                    {"atmosphere.set", "world.control"},
                    {"overlay.style", "shell.control"},
                    {"markers.reveal", "shell.control"},
                    {"camera.fov", "movement.control"},
                    {"shell.bind", "shell.control"},
                    {"workspace.activate", "world.control"},
                    {"world.load", "world.control"},
                    {"surface.focus", "surface.control"},
                    {"graphics.get", "state.read"},
                    {"graphics.set", "world.control"},
                    {"recording.start", "input.control"}, {"recording.stop", "input.control"},
                    {"recording.mark", "input.control"}, {"recording.status", "input.control"},
                    {"replay.start", "input.control"}, {"replay.stop", "input.control"}, {"replay.status", "input.control"},
                    {"input.get", "state.read"},
                    {"player.get", "state.read"},
                    {"player.set", "movement.control"},
                    {"input.preview", "state.read"},
                    {"input.set", "input.control"},
                    {"input.capture", "input.control"},
                    {"input.capture.cancel", "input.control"},
                    {"movement.set", "movement.control"},
                    {"runtime.exit", "runtime.control"}});
    for (auto &[name, grant] : hyprune::world::objectMethods())
        methods.insert({name, grant});
    runtime->ipc->advertise(std::move(methods));
    runtime->deferInput([] {
        runtime->applyGraphics(runtime->graphicsFile.current);
        runtime->applyPlayer(runtime->playerFile.current);
    });
    HyprlandAPI::addDispatcherV2(handle, "hyprune.toggle", [](std::string arg) {
        deferToggle(arg);
        return SDispatchResult{};
    });
    HyprlandAPI::addLuaFunction(handle, "hyprune", "toggle", luaToggle);
    HyprlandAPI::addLuaFunction(handle, "hyprune", "camera", luaCamera);
#ifndef HYPRUNE_LIVE_BUILD
    // Nested demo camera driver (Cinema.inc): one JSON request in, one JSON reply out.
    HyprlandAPI::addLuaFunction(handle, "hyprune", "cinema", [](lua_State *state) {
        Json reply;
        try {
            if (!runtime)
                throw std::runtime_error("not running");
            reply = runtime->cinemaCommand(Json::parse(luaL_checkstring(state, 1)));
        } catch (const std::exception &e) {
            reply = {{"error", e.what()}};
        }
        lua_pushstring(state, reply.dump().c_str());
        return 1;
    });
#endif
#ifdef HYPRUNE_LIVE_BUILD
    HyprlandAPI::addLuaFunction(handle, "hyprune", "live_stop", [](lua_State *) {
        runtime->deferInput([] {
            runtime->exit();
            // Panic recovery: one deferred visibility reset, never a cursor shape change.
            if (!g_pHyprRenderer->shouldRenderCursor())
                g_pHyprRenderer->setCursorHidden(false);
            runtime->cursorHidden = false;
        });
        return 0;
    });
#endif
#ifndef HYPRUNE_LIVE_BUILD
    // Nested test hook: exactly what Hyprland 0.56.2 emits when beginRender fails
    // (RENDER_PRE, then return without RENDER_POST). Not in dev/live builds; anywhere
    // else calling it only exercises the recovery below.
    HyprlandAPI::addLuaFunction(handle, "hyprune", "test_render_fail", [](lua_State *) {
        Event::bus()->m_events.render.stage.emit(RENDER_PRE);
        return 0;
    });
#endif
#ifndef HYPRUNE_LIVE_BUILD
    // Nested test hook: the placement verdict for the tool 4 selection moved to
    // (x, y, z), same rotation and scale. Read-only; nothing is committed.
    // Nested test hook: pins the world-animation clock at t seconds (no argument: the
    // steady clock again). The offload worker draws at the same time.
    HyprlandAPI::addLuaFunction(handle, "hyprune", "animation_clock", [](lua_State *state) {
        if (runtime)
            runtime->renderer.animationClock =
                lua_gettop(state) >= 1 && !lua_isnil(state, 1) ? std::optional<double>(luaL_checknumber(state, 1)) : std::nullopt;
        return 0;
    });
    HyprlandAPI::addLuaFunction(handle, "hyprune", "gizmo_probe", [](lua_State *state) {
        Json j = nullptr;
        if (runtime && runtime->world && runtime->editor.kind == "object" && !runtime->editor.selected.empty()) {
            auto t = runtime->objectPose(runtime->editor.selected);
            auto box = runtime->selectionBox(t);
            Vec3 target{float(luaL_checknumber(state, 1)), float(luaL_checknumber(state, 2)), float(luaL_checknumber(state, 3))};
            t.position += target - box.centre; // (x, y, z) is the bounds centre
            auto v = runtime->placementCheck(t);
            j = {{"ok", v.ok}, {"rule", v.rule}, {"message", v.message}, {"supported", v.supported}};
        }
        lua_pushstring(state, j.dump().c_str());
        return 1;
    });
#endif
    HyprlandAPI::addLuaFunction(handle, "hyprune", "config", luaConfig);
    for (auto &match : HyprlandAPI::findFunctionsByName(handle, "CPointerManager4move"))
        if (match.demangled.find("CPointerManager::move(") != std::string::npos) {
            runtime->motionHook = HyprlandAPI::createFunctionHook(handle, match.address, (void *)motion);
            if (runtime->motionHook && runtime->motionHook->hook())
                originalMove = (Move)runtime->motionHook->m_original;
            break;
        }
    for (auto &match : HyprlandAPI::findFunctionsByName(handle, "setCursorHidden"))
        if (match.demangled.find("IHyprRenderer::setCursorHidden(bool)") != std::string::npos) {
            runtime->cursorHook = HyprlandAPI::createFunctionHook(handle, match.address, (void *)cursorHide);
            if (runtime->cursorHook && runtime->cursorHook->hook())
                originalHideCursor = (HideCursor)runtime->cursorHook->m_original;
            break;
        }
    if (!originalHideCursor)
        throw std::runtime_error("Hyprune: deferred cursor visibility hook unavailable");
    if (!originalMove)
        throw std::runtime_error("Hyprune: pointer motion hook unavailable");
    for (auto &match : HyprlandAPI::findFunctionsByName(handle, "onKeyboardKey"))
        if (match.demangled.find("CInputManager::onKeyboardKey(") != std::string::npos) {
            runtime->keyboardHook = HyprlandAPI::createFunctionHook(handle, match.address, (void *)keyboardInput);
            if (runtime->keyboardHook && runtime->keyboardHook->hook())
                originalKeyboardInput = (KeyboardInput)runtime->keyboardHook->m_original;
            break;
        }
    for (auto &match : HyprlandAPI::findFunctionsByName(handle, "onMouseButton"))
        if (match.demangled.starts_with("CInputManager::onMouseButton(")) {
            runtime->buttonHook = HyprlandAPI::createFunctionHook(handle, match.address, (void *)mouseButtonInput);
            if (runtime->buttonHook && runtime->buttonHook->hook())
                originalMouseButton = (MouseButtonInput)runtime->buttonHook->m_original;
            break;
        }
    for (auto &match : HyprlandAPI::findFunctionsByName(handle, "onMouseWheel"))
        if (match.demangled.starts_with("CInputManager::onMouseWheel(")) {
            runtime->axisHook = HyprlandAPI::createFunctionHook(handle, match.address, (void *)mouseAxisInput);
            if (runtime->axisHook && runtime->axisHook->hook())
                originalMouseAxis = (MouseAxisInput)runtime->axisHook->m_original;
            break;
        }
    if (!originalMouseButton || !originalMouseAxis)
        throw std::runtime_error("input source hooks unavailable");
    if (!originalKeyboardInput)
        throw std::runtime_error("Hyprune: event keyboard hook unavailable");
    runtime->listeners.push_back(Event::bus()->m_events.window.moveToWorkspace.listen([](PHLWINDOW window, PHLWORKSPACE workspace) {
        // Synchronous: only remember the size the client had before it reached the room;
        // floatIntoWorld() floats it later, from the tick (never inside this hook).
        if (runtime && runtime->active && workspace && workspace->m_id == runtime->worldWorkspace && window &&
            !window->m_isFloating && window->m_target && window->m_workspace != workspace) {
            if (runtime->arrivalSizes.size() > 64)
                runtime->arrivalSizes.clear();
            runtime->arrivalSizes[window.get()] = window->m_target->position().size();
        }
    }));
    runtime->listeners.push_back(Event::bus()->m_events.window.openEarly.listen([](PHLWINDOW window) {
        // This event follows static rules/launch-token workspace routing and
        // precedes newTarget(). Change only the initial layout flag: no render,
        // configure, focus or cursor call is allowed in this synchronous hook.
        // A static float rule sees the initial workspace and leaks to clients
        // redirected elsewhere. Test the final workspace and output instead.
        if (runtime->active && window->m_monitor == runtime->monitor && window->m_workspace &&
            window->m_workspace->m_id == runtime->worldWorkspace) {
            window->m_isFloating = true;
            runtime->capture.worldFloated.insert(window.get()); // floated by the world, not the user
        }
    }));
    runtime->listeners.push_back(Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        safe([&] {
            if (stage == RENDER_PRE && runtime->renderPass.begin(g_pHyprRenderer->m_renderData.pMonitor.get()))
                runtime->renderPassLeaked(); // the previous pass never got RENDER_POST
            if (runtime->frameLog.file && stage == RENDER_PRE)
                runtime->frameLog.renderStart = Runtime::FrameLog::now();
            if (runtime->frameLog.file && stage == RENDER_POST && runtime->frameLog.renderStart > 0) // the compositor's whole frame
                runtime->frameLog.line({{"kind", "render"}, {"t", runtime->frameLog.renderStart},
                                        {"ms", Runtime::FrameLog::now() - runtime->frameLog.renderStart}});
            if (stage == RENDER_POST)
                runtime->renderPass.end();
            if (stage == RENDER_POST_WINDOWS && runtime->active && !locked() &&
                g_pHyprRenderer->m_renderData.pMonitor == runtime->monitor)
                g_pHyprRenderer->addPassElement(makeUnique<Pass>());
            if (stage == RENDER_LAST_MOMENT && (runtime->frameExport.active() || runtime->frameProbe.active()) &&
                g_pHyprRenderer->m_renderData.pMonitor == runtime->monitor)
                g_pHyprRenderer->addPassElement(makeUnique<ExportPass>());
        });
    }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.mouse.move.listen([](Vector2D pos, Event::SCallbackInfo &info) {
            if (runtime && runtime->active && runtime->typing && runtime->freeCursor && !runtime->layerCursor &&
                !runtime->cursorWarpPending && runtime->monitor) {
                auto m = runtime->monitor.lock();
                if (pos.x >= m->m_position.x && pos.y >= m->m_position.y && pos.x < m->m_position.x + m->m_size.x &&
                    pos.y < m->m_position.y + m->m_size.y) {
                    runtime->freeCursor = false;
                    runtime->pointerX = (pos.x - m->m_position.x) / m->m_size.x;
                    runtime->pointerY = (pos.y - m->m_position.y) / m->m_size.y;
                    runtime->deferInput([] {
                        Desktop::focusState()->rawMonitorFocus(runtime->monitor.lock());
                        if (auto plane = runtime->capture.plane(runtime->relativeSurface))
                            if (auto hit = surfaces::intersect(*plane, runtime->pointerCamera().position, pointerRay(), false)) {
                                runtime->localX = hit->u;
                                runtime->localY = hit->v;
                            }
                        runtime->cursor();
                    });
                    info.cancelled = true;
                }
            }
            if (runtime->freeWorldPointer(pos) || (runtime->layerCursor && !runtime->buttons.empty())) {
                info.cancelled = true;
                runtime->deferInput([pos] { runtime->freePointerPosition(pos); });
            }
            if (ownsInput() || (runtime->active && runtime->monitor == Desktop::focusState()->monitor() &&
                                runtime->layout.desktop.is_null() && runtime->fullscreen.phase != 2 &&
                                (!runtime->freeCursor || runtime->cursorWarpPending) && !runtime->directorSurface()))
                info.cancelled = true;
        }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.keyboard.key.listen([](IKeyboard::SKeyEvent event, Event::SCallbackInfo &info) {
            safe([&] {
                auto kb = runtime->eventKeyboard.lock();
                if (!kb)
                    kb = g_pSeatManager->m_keyboard.lock();
                if (!kb || !kb->m_xkbSymState)
                    return;
                auto sym = xkb_state_key_get_one_sym(kb->m_xkbSymState, event.keycode + 8);
                bool down = event.state == WL_KEYBOARD_KEY_STATE_PRESSED;
                auto key = interaction::bindingKey(kb->m_xkbSymState, event.keycode + 8);
                if (down) {
                    auto [it, inserted] = runtime->inputNames.try_emplace(event.keycode, key);
                    key = it->second;
                } else if (auto it = runtime->inputNames.find(event.keycode); it != runtime->inputNames.end()) {
                    key = it->second;
                    runtime->inputNames.erase(it);
                }
                if (key.empty())
                    return;
                if (runtime->session.playing && key != "F12") { info.cancelled=true; return; }
                unsigned bit = sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R       ? 64
                               : sym == XKB_KEY_Shift_L || sym == XKB_KEY_Shift_R     ? 1
                               : sym == XKB_KEY_Control_L || sym == XKB_KEY_Control_R ? 4
                               : sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R         ? 8
                                                                                      : 0;
                if (bit) {
                    if (down)
                        runtime->modifiers |= bit;
                    else
                        runtime->modifiers &= ~bit;
                }
                runtime->superHeld = runtime->modifiers & 64;
                runtime->altHeld = runtime->modifiers & 8;
                bool owned = !down && runtime->swallowedKeys.erase(event.keycode);
                bool nativeFreeRelease = !down && runtime->freeNativeKeys.erase(event.keycode);
                if (nativeFreeRelease && !runtime->layerCursor) {
                    info.cancelled = true;
                    runtime->deferInput([event, kb] { g_pKeybindManager->onKeyEvent(event, kb); });
                    return;
                }
                if (owned)
                    info.cancelled = true;
                bool scoped = runtime->active && !locked() && runtime->monitor == Desktop::focusState()->monitor() &&
                              (!runtime->freeCursor || runtime->layerCursor);
                bool bindingScoped = runtime->active && !locked();
                if (!bindingScoped && !owned)
                    return;
                if (bindingScoped && !runtime->control.uiVisible() && interaction::freeCursorToggle(key, runtime->modifiers)) {
                    info.cancelled = true;
                    if (down && !runtime->swallowedKeys.contains(event.keycode)) {
                        runtime->swallowedKeys.insert(event.keycode);
                        runtime->session.event({{"type","key"},{"key",key},{"mods",runtime->modifiers},{"down",true},{"value",0},{"mode",runtime->inputMode()}});
                        runtime->deferInput([] { runtime->toggleFreeCursor(); });
                    }
                    return;
                }
                // Tool 4 keys (numeric entry, Escape cancels a drag, undo/redo, drop,
                // pivot) win over the adventure escape and the keymap while they apply.
                if (bindingScoped && !runtime->layerCursor && runtime->gizmoKey(key, runtime->modifiers, down, event.keycode)) {
                    info.cancelled = true;
                    if (down)
                        runtime->swallowedKeys.insert(event.keycode);
                    return;
                }
                // Escape without an application focus is always an adventure
                // escape, even on another monitor. Balanced before keybind dispatch.
                if (bindingScoped && key == "Escape" && !g_pSeatManager->m_state.keyboardFocus) {
                    info.cancelled = true;
                    if (down && !runtime->swallowedKeys.contains(event.keycode)) {
                        runtime->swallowedKeys.insert(event.keycode);
                        runtime->deferInput([] {
                            runtime->cursorReturn = {};
                            runtime->leaveInteraction();
                        });
                    }
                    return;
                }
                if (runtime->layerCursor) {
                    runtime->session.event({{"type","key"},{"key",key},{"mods",runtime->modifiers},{"down",down},{"value",0},{"mode","cursor"}}, true);
                    auto target = g_pSeatManager->m_state.keyboardFocus.lock();
                    // Hyprland's new-keyboard refocus uses native window geometry. A
                    // projected window needs its chosen focus retained across that switch,
                    // including the first key. Layers and other outputs remain native.
                    bool projected = target && std::any_of(runtime->capture.entries.begin(), runtime->capture.entries.end(),
                        [&](const auto &e) { return e.visible && e.window && e.window->resource() == target; });
                    if (projected || nativeFreeRelease || (!down && runtime->appKeys.contains(event.keycode))) {
                        info.cancelled = true;
                        if (down) {
                            runtime->swallowedKeys.insert(event.keycode);
                            runtime->freeNativeKeys.insert(event.keycode);
                        }
                        runtime->deferInput([event, kb, target] {
                            if (!g_pKeybindManager->onKeyEvent(event, kb)) return;
                            bool pressed = event.state == WL_KEYBOARD_KEY_STATE_PRESSED;
                            if (!pressed && !runtime->appKeys.erase(event.keycode)) return;
                            if (!runtime->active || !runtime->layerCursor || !target) return;
                            if (g_pSeatManager->m_keyboard != kb) {
                                g_pSeatManager->setKeyboard(kb);
                                g_pSeatManager->updateActiveKeyboardData();
                            }
                            if (!g_pSeatManager->m_seatGrab) g_pSeatManager->setKeyboardFocus(target);
                            g_pSeatManager->sendKeyboardKey(event.timeMs, event.keycode, event.state);
                            if (pressed) {
                                runtime->appKeys.insert(event.keycode);
                                runtime->appKeyResource = target;
                            }
                        });
                    }
                    return; // No Hyprune actions or emergency shortcut in free cursor.
                }
                if (bindingScoped && key == "F12") {
                    info.cancelled = true;
                    if (down) {
                        runtime->swallowedKeys.insert(event.keycode);
                        runtime->deferInput([] { runtime->exit(); });
                    }
                    return;
                }
                if (bindingScoped && !runtime->captureRequest.is_null() &&
                    runtime->captureRequest.value("status", "") == "waiting") {
                    info.cancelled = true;
                    if (down)
                        runtime->swallowedKeys.insert(event.keycode);
                    auto mods = runtime->modifiers;
                    runtime->deferInput([key, mods, down] {
                        if (key == "Escape" && down)
                            runtime->leaveInteraction();
                        else
                            runtime->acceptCapture(key, mods, down);
                    });
                    return;
                }
                auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now().time_since_epoch())
                               .count();
                bool gestureMovement = runtime->hands.gesture != interaction::Gesture::None || runtime->objectCarry ||
                                       runtime->input.gestureHeld() ||
                                       (runtime->editor.activeHandle >= 0 && runtime->superHeld);
                auto actions =
                    runtime->inputEvent(runtime->inputMode(), key, runtime->modifiers, down, now, 0, gestureMovement);
                // Tool 4: Ctrl's own actions (crouch / fly down) wait briefly, so Ctrl+Z / Ctrl+Y
                // consume the whole chord instead of dipping the camera first.
                if (runtime->deferCtrl(key, down, event.keycode, actions)) {
                    info.cancelled = true;
                    if (down)
                        runtime->swallowedKeys.insert(event.keycode);
                    return;
                }
                if (!actions.empty()) {
                    info.cancelled = true;
                    if (down)
                        runtime->swallowedKeys.insert(event.keycode);
                    runtime->deferInput([actions] {
                        for (auto a : actions)
                            runtime->action(a);
                    });
                    return;
                }
                if (!scoped && !owned)
                    return;
                // Uncolliding binds retain native dispatch in 2D/menu. Owned releases
                // stay swallowed after transitions; forwarded world presses receive a
                // matching manager release, without typing into hidden desktop clients.
                bool native = !runtime->layout.desktop.is_null() || runtime->fullscreen.phase == 2 ||
                              runtime->control.uiVisible() || (runtime->layerCursor && layerKeyboardFocus());
                if (native && !owned)
                    return;
                info.cancelled = true;
                if (down)
                    runtime->swallowedKeys.insert(event.keycode);
                if (owned && !runtime->nativeKeys.erase(event.keycode))
                    return;
                if (down)
                    runtime->nativeKeys.insert(event.keycode);
                runtime->deferInput([event, kb] {
                    if (!g_pKeybindManager->onKeyEvent(event, kb))
                        return;
                    if (!runtime->active || !runtime->typing || runtime->freeCursor || runtime->control.uiVisible())
                        return;
                    auto entry = runtime->capture.find(runtime->focused);
                    if (!entry)
                        return;
                    if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED) {
                        if (!runtime->appKeys.erase(event.keycode))
                            return;
                        if (!runtime->appKeyResource || runtime->appKeyResource != entry->window->resource())
                            return;
                    } else if (runtime->appKeys.empty())
                        runtime->appKeyResource = entry->window->resource();
                    if (g_pSeatManager->m_keyboard != kb) {
                        g_pSeatManager->setKeyboard(kb);
                        g_pSeatManager->updateActiveKeyboardData();
                    }
                    if (!g_pSeatManager->m_seatGrab)
                        g_pSeatManager->setKeyboardFocus(entry->window->resource());
                    g_pSeatManager->sendKeyboardKey(event.timeMs, event.keycode, event.state);
                    if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED)
                        runtime->appKeys.insert(event.keycode);
                });
            });
        }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.mouse.button.listen([](IPointer::SButtonEvent event, Event::SCallbackInfo &info) {
            safe([&] {
                if (runtime->session.playing) { info.cancelled=true; return; }
                bool down = event.state == WL_POINTER_BUTTON_STATE_PRESSED,
                     owned = !down && runtime->swallowedButtons.erase(event.button);
                bool scoped = runtime->active && !locked() && runtime->monitor == Desktop::focusState()->monitor() &&
                              (!runtime->freeCursor || runtime->layerCursor || runtime->cursorWarpPending);
                if (owned)
                    info.cancelled = true;
                bool bindingScoped = runtime->active && !locked();
                if (!bindingScoped && !owned)
                    return;
                std::string key = event.button == 272   ? "LMB"
                                  : event.button == 273 ? "RMB"
                                  : event.button == 274 ? "MMB"
                                  : event.button == 275 ? "Back"
                                  : event.button == 276 ? "Forward"
                                                        : "Button" + std::to_string(event.button);
                auto mods = runtime->modifiers;
                if (bindingScoped && !runtime->control.uiVisible() && interaction::freeCursorToggle(key, mods)) {
                    info.cancelled = true;
                    if (down) {
                        runtime->swallowedButtons.insert(event.button);
                        runtime->session.event({{"type","button"},{"key",key},{"mods",mods},{"down",true},{"value",0},{"mode",runtime->inputMode()}});
                        runtime->deferInput([] { runtime->toggleFreeCursor(); });
                    }
                    return;
                }
                if (!down && runtime->buttons.contains(event.button)) {
                    info.cancelled = true;
                    runtime->inputEvent(runtime->inputMode(), key, mods, false, 0);
                    runtime->deferInput([event] { runtime->button(event, false); });
                    return;
                }
                if (!down && runtime->freeNativeButtons.erase(event.button)) return;
                if (runtime->layerCursor) {
                    if (runtime->freeWorldPointer(Pointer::mgr()->position()) || !runtime->buttons.empty()) {
                        info.cancelled = true;
                        if (down) runtime->swallowedButtons.insert(event.button);
                        runtime->deferInput([event] { runtime->freePointerButton(event); });
                    } else if (down && !owned) runtime->freeNativeButtons.insert(event.button);
                    return;
                }
                if (bindingScoped && !runtime->captureRequest.is_null() &&
                    runtime->captureRequest.value("status", "") == "waiting") {
                    info.cancelled = true;
                    if (down)
                        runtime->swallowedButtons.insert(event.button);
                    runtime->deferInput([key, mods, down] { runtime->acceptCapture(key, mods, down); });
                    return;
                }
                auto actions = runtime->inputEvent(runtime->inputMode(), key, mods, down,
                                                    std::chrono::duration_cast<std::chrono::milliseconds>(
                                                        std::chrono::steady_clock::now().time_since_epoch())
                                                        .count());
                if (!actions.empty()) {
                    info.cancelled = true;
                    if (down)
                        runtime->swallowedButtons.insert(event.button);
                    runtime->deferInput([actions] {
                        for (auto a : actions)
                            runtime->action(a);
                    });
                    return;
                }
                if (!scoped && !owned)
                    return;
                if (scoped && runtime->inputMode() == "world" &&
                    event.button == runtime->config["input"]["typing_button"].get<uint32_t>() && mods == 0) {
                    info.cancelled = true;
                    if (down) {
                        runtime->swallowedButtons.insert(event.button);
                        runtime->deferInput([] { runtime->useLayers(); });
                    }
                    return;
                }
                if (runtime->layerCursor && !runtime->control.uiVisible()) {
                    if (!owned && interactiveLayerAt(Pointer::mgr()->position()))
                        return;
                    info.cancelled = true;
                    if (down)
                        runtime->swallowedButtons.insert(event.button);
                    return;
                }
                bool native = runtime->control.uiVisible() || !runtime->layout.desktop.is_null() ||
                              runtime->fullscreen.phase == 2;
                if (native && !owned)
                    return;
                info.cancelled = true;
                if (down)
                    runtime->swallowedButtons.insert(event.button);
                if (owned && !runtime->nativeButtons.erase(event.button))
                    return;
                if (down)
                    runtime->nativeButtons.insert(event.button);
                auto pointer = runtime->eventPointer.lock();
                runtime->deferInput([event, pointer] {
                    if (g_pKeybindManager->onMouseEvent(event, pointer) && runtime->typing)
                        runtime->button(event, false);
                });
            });
        }));
    runtime->listeners.push_back(Event::bus()->m_events.input.mouse.axis.listen([](IPointer::SAxisEvent event,
                                                                                   Event::SCallbackInfo &info) {
        safe([&] {
            if (runtime->session.playing) { info.cancelled=true; return; }
            if (!runtime->active || locked() || runtime->monitor != Desktop::focusState()->monitor() ||
                (runtime->freeCursor && !runtime->layerCursor && !runtime->cursorWarpPending))
                return;
            if (runtime->layerCursor) {
                if (runtime->freeWorldPointer(Pointer::mgr()->position())) {
                    info.cancelled = true;
                    runtime->deferInput([event] {
                        runtime->freePointerPosition(Pointer::mgr()->position());
                        if (auto hit = aim(); hit && runtime->capture.target(hit->first, hit->second.u, hit->second.v)) {
                            runtime->routePointer(hit->first, hit->second, event.timeMs);
                            g_pSeatManager->sendPointerAxis(event.timeMs, event.axis, event.delta, event.deltaDiscrete / 120,
                                event.deltaDiscrete, event.source, event.relativeDirection);
                            g_pSeatManager->sendPointerFrame();
                        }
                    });
                }
                return;
            }
            float raw = event.deltaDiscrete ? event.deltaDiscrete / 120.f : event.delta * .05f;
            float steps = std::clamp(
                event.relativeDirection == WL_POINTER_AXIS_RELATIVE_DIRECTION_INVERTED ? raw : -raw, -2.f, 2.f);
            std::string key = steps > 0 ? "WheelUp" : "WheelDown";
            auto mods = runtime->modifiers;
            if (!runtime->captureRequest.is_null() && runtime->captureRequest.value("status", "") == "waiting") {
                info.cancelled = true;
                runtime->deferInput([key, mods] { runtime->acceptCapture(key, mods, true); });
                return;
            }
            auto actions = runtime->inputEvent(runtime->inputMode(), key, mods, true,
                                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    std::chrono::steady_clock::now().time_since_epoch())
                                                    .count(),
                                                steps);
            if (!actions.empty()) {
                info.cancelled = true;
                runtime->deferInput([actions, event] {
                    for (auto a : actions) {
                        runtime->action(a);
                    }
                });
                return;
            }
            if (runtime->control.uiVisible() || !runtime->layout.desktop.is_null() || runtime->fullscreen.phase == 2)
                return;
            info.cancelled = true;
            auto pointer = runtime->eventPointer.lock();
            runtime->deferInput([event, pointer] {
                if (g_pKeybindManager->onAxisEvent(event, pointer) && runtime->typing)
                    runtime->axis(event);
            });
        });
    }));
    for (auto &workspace : State::workspaceState()->workspaces())
        if (workspace && workspace->m_visible && !workspace->m_isSpecialWorkspace)
            if (auto output = workspace->m_monitor.lock())
                runtime->shownWorkspaces[output.get()] = int(workspace->m_id);
    runtime->listeners.push_back(Event::bus()->m_events.workspace.active.listen(
        [](PHLWORKSPACE workspace) { safe([&] { runtime->settleWorkspaceSwitch(workspace); }); }));
    runtime->listeners.push_back(Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR monitor) {
        runtime->deferInput([monitor] {
            if (runtime->monitor == monitor) {
                runtime->exit();
                runtime->monitor.reset();
                runtime->control.outputId = nullptr;
                runtime->publish();
            }
        });
    }));
    runtime->timer = makeShared<CEventLoopTimer>(
        std::chrono::milliseconds(16),
        [](SP<CEventLoopTimer> self, void *) {
            // Presentations drive ticks while frames flow; the timer covers idle and startup.
            bool framesDrive = std::chrono::steady_clock::now() - runtime->lastPresented < std::chrono::milliseconds(30);
            if (!framesDrive && !runtime->tickDeferred)
                runtime->tickDeferred = g_pEventLoopManager->doLater([] {
                    if (!runtime)
                        return;
                    runtime->tickDeferred = 0;
                    runtime->settleRenderPass();
                    safe([] { runtime->tick(); });
                });
            self->updateTimeout(std::chrono::milliseconds(16));
        },
        nullptr);
    g_pEventLoopManager->addTimer(runtime->timer);
    runtime->offloadTimer = makeShared<CEventLoopTimer>(std::chrono::milliseconds(100),
        [](SP<CEventLoopTimer> self, void *) {
            if (runtime->offload.connected() && !runtime->offloadDeferred)
                runtime->offloadDeferred = g_pEventLoopManager->doLater([] {
                    if (!runtime) return;
                    runtime->offloadDeferred = 0;
                    runtime->settleRenderPass();
                    double start = runtime->frameLog.file ? Runtime::FrameLog::now() : 0;
                    safe([] { runtime->offloadPump(); });
                    if (runtime->frameLog.file)
                        runtime->frameLog.line({{"kind", "pump"}, {"t", start}, {"ms", Runtime::FrameLog::now() - start}});
                });
            self->updateTimeout(std::chrono::milliseconds(runtime->offload.connected() ? 4 : 100));
        }, nullptr);
    g_pEventLoopManager->addTimer(runtime->offloadTimer);
    runtime->publish();
#ifndef HYPRUNE_LIVE_BUILD
    runtime->deferInput([] { runtime->readyNotice(); });
#endif
    return {"hyprune-core", "World-driven desktop runtime", "Hyprune contributors", "0.8.3"};
}
APICALL EXPORT void PLUGIN_EXIT() {
    if (!runtime)
        return;
    if (runtime->deferred)
        g_pEventLoopManager->removeDoLater(runtime->deferred);
    if (runtime->tickDeferred)
        g_pEventLoopManager->removeDoLater(runtime->tickDeferred);
    if (runtime->offloadDeferred) g_pEventLoopManager->removeDoLater(runtime->offloadDeferred);
    if (runtime->offloadTimer) g_pEventLoopManager->removeTimer(runtime->offloadTimer);
    for (auto id : runtime->inputDeferred)
        g_pEventLoopManager->removeDoLater(id);
    runtime->inputDeferred.clear();
    runtime->inputQueue.clear();
    runtime->listeners.clear();
    if (runtime->timer)
        g_pEventLoopManager->removeTimer(runtime->timer);
    runtime->timer.reset();
    if (g_pHyprRenderer)
        g_pHyprRenderer->m_renderPass.removeAllOfType("Hyprune");
    safe([] { runtime->exit(); });
    if (runtime->motionHook)
        HyprlandAPI::removeFunctionHook(handle, runtime->motionHook);
    if (runtime->keyboardHook)
        HyprlandAPI::removeFunctionHook(handle, runtime->keyboardHook);
    if (runtime->buttonHook)
        HyprlandAPI::removeFunctionHook(handle, runtime->buttonHook);
    if (runtime->axisHook)
        HyprlandAPI::removeFunctionHook(handle, runtime->axisHook);
    originalMouseButton = nullptr;
    originalMouseAxis = nullptr;
    if (runtime->cursorHook)
        HyprlandAPI::removeFunctionHook(handle, runtime->cursorHook);
    originalHideCursor = nullptr;
    originalKeyboardInput = nullptr;
    originalMove = nullptr;
    if (Render::GL::g_pHyprOpenGL) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        runtime->captureGPU.destroy();
        runtime->offload.stop();
        runtime->renderer.destroy();
    }
    runtime.reset();
}
