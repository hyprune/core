#include "control/Control.hpp"
#include "interaction/Config.hpp"
#include "interaction/Hands.hpp"
#include "interaction/InputFile.hpp"
#ifdef HYPRUNE_LIVE_BUILD
#include "../../../dev/live/Bootstrap.hpp"
#endif
#include "interaction/Tools.hpp"
#include "world/Mounts.hpp"
#include "world/Spatial.hpp"
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
#include "interaction/Resize.hpp"
#include "ipc/Bridge.hpp"
#include "ipc/Protocol.hpp"
#include "hud/Telemetry.hpp"
#include "offload/Client.hpp"
#include "replay/Session.hpp"
#include "render/PNG.hpp"
#include "render/Renderer.hpp"
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
    Json offloadRequest, offloadPolicy = Json::object();
    uint64_t offloadGeneration = 0;
    bool renderedOffload = false;
    sim::Camera displayedCamera;
    const sim::Camera &pointerCamera() const { return renderedOffload ? displayedCamera : camera; }
    void offloadTick();
    void offloadPump();
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
    std::future<std::string> captureWrite;
    std::string relativeSurface;
    float localX = .5f, localY = .5f;
    std::string inputMode() const;
    void applyInput(const Json &);
    Json steals(const Json &) const;
    void acceptCapture(const std::string &, unsigned, bool);
    void action(interaction::Action);
    void useAim(bool zoom);
    void leaveInteraction();
    void debugCapture();
    void toolClick(bool secondary = false);
    void toolsTick();
    std::optional<interaction::WorldHit> worldHit();
    void saveEditor();
    std::optional<std::pair<std::string, interaction::WorldHit>> objectAim();
    std::vector<Triangle> originalCollision;
    std::map<unsigned, Mat4> objectModels;
    interaction::Transform selectedTransform, dragStart;
    int dragPixelWidth = 0, dragPixelHeight = 0;
    Vec3 dragPoint{}, dragNormal{};
    bool objectCarry = false;
    float objectDistance = 0;
    void projectCursor();

    Json config = interaction::defaultConfig(), configured = config;
    std::set<std::string> graphicsOverrides;
    Json graphicsBaseProfile = nullptr;
    interaction::InputFile graphicsFile{"graphics", Json::object(), [](const Json &patch) {
                                            interaction::configPatch(interaction::defaultConfig(),
                                                                     {{"graphics", patch}});
                                            return patch;
                                        }};
    void applyGraphics(const Json &patch) {
        auto next = interaction::configPatch(configured, {{"graphics", patch}});
        config["graphics"] = next["graphics"];
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
            renderer.graphics["exposure"] = renderer.profile["exposure"];
            renderer.graphics["bloom_strength"] = renderer.profile["bloom"]["strength"];
        }
        baseFov = next["graphics"]["fov"];
        if (!zoomHeld)
            camera.fov = baseFov;
    }
    bool superHeld = false, altHeld = false, noclip = false, freeCursor = false, layerCursor = false, zoomHeld = false,
         cursorWarpPending = false;
    float zoom = 1, baseFov = 60, lookX = 0, lookY = 0;
    sim::Intent smoothIntent;
    int tool = 1;
    std::string tagged;
    uint32_t gestureButton = 0;
    std::set<uint32_t> swallowedButtons, swallowedKeys, nativeButtons, nativeKeys;
    std::map<uint32_t, std::string> inputNames;
    WP<CWLSurfaceResource> grabResource;
    std::string cursorState = "desktop";
    bool savedCursorHidden = false, cursorHidden = false;
    void cursor();
    void modeToggle();
    void useLayers();
    void button(IPointer::SButtonEvent, bool super);
    void axis(IPointer::SAxisEvent);
    void handTick(float);
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
    std::shared_ptr<world::World> world;
    std::unique_ptr<ipc::Bridge> ipc;
    PHLMONITORREF monitor;
    PHLWINDOWREF savedFocus;
    // Hold the return workspace alive even when it becomes empty/hidden.
    PHLWORKSPACE previousWorkspace, previousSpecialWorkspace;
    int worldWorkspace = 0;
    void returnToWorldWorkspace();
    bool active = false, typing = false, loading = false, loadCancelled = false;
    int renderDepth = 0;
    std::future<std::shared_ptr<world::World>> pendingWorld;
    std::shared_ptr<world::World> uploadingWorld;
    std::string pendingSpawn, pendingToggle, resolvedLocation;
    bool toggleAfterLoad = false;
    std::chrono::steady_clock::time_point loadDeadline;
    double accumulator = 0;
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
    void deferInput(std::function<void()>);
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
    SP<CWLSurfaceResource> directorSurface();
    void publish();
    void publishHUD();
    Json hudZone = nullptr, hudTool = nullptr;
    uint64_t hudSequence = 0;
    std::chrono::steady_clock::time_point nextHUDPose{}, nextHUDFrame{};
    void toggle(const std::string &output = "");
    void exit();
    void tick();
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
    originalKeyboardInput(self, event, keyboard);
    runtime->eventKeyboard = previous;
}
using MouseButtonInput = void (*)(void *, IPointer::SButtonEvent, SP<IPointer>);
using MouseAxisInput = void (*)(void *, IPointer::SAxisEvent, SP<IPointer>);
MouseButtonInput originalMouseButton = nullptr;
MouseAxisInput originalMouseAxis = nullptr;
void mouseButtonInput(void *self, IPointer::SButtonEvent event, SP<IPointer> pointer) {
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
    bool pointer = runtime->typing || runtime->editor.activeHandle >= 0;
    return runtime->pointerCamera().ray(pointer ? runtime->pointerX : .5f, pointer ? runtime->pointerY : .5f, aspect);
}
std::optional<std::pair<std::string, surfaces::Hit>> aim() {
    std::optional<std::pair<std::string, surfaces::Hit>> best;
    for (auto &surface : runtime->capture.visible())
        if (auto h = surfaces::intersect(surface, runtime->pointerCamera().position, pointerRay());
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
void Runtime::deferInput(std::function<void()> operation) {
    auto id = std::make_shared<uint64_t>(0);
    *id = g_pEventLoopManager->doLater([id, operation = std::move(operation)] {
        if (!runtime)
            return;
        std::erase(runtime->inputDeferred, *id);
        if (runtime->renderDepth) {
            runtime->deferInput(std::move(operation));
            return;
        }
        safe(operation);
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
    if (renderDepth)
        throw std::runtime_error("capture inside render pass");
    if (monitor && world) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        capture.spawnDistance = config["windows"]["spawn_distance"];
        capture.windowScale = config["windows"]["scale"];
        capture.worldWorkspace = worldWorkspace;
        capture.refresh(monitor.lock(), world.get(), &layout, &camera);
        // Configure after the capture iteration completes: native layout changes
        // may reorder the compositor's window list or invoke other plugin hooks.
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
void Runtime::workspaceLocation() {
    if (!active || !monitor || !monitor->m_activeWorkspace)
        return;
    auto id = monitor->m_activeWorkspace->m_id;
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
    Json state = {{"profile", "unknown"}, {"powerW", nullptr}, {"capW", 0}, {"targetFPS", 0},
                  {"reason", "off"}, {"pid", nullptr}, {"generation", offloadGeneration}};
    state.update(offloadPolicy);
    state["mode"] = config["graphics"]["offload"];
    state["activeRenderer"] = renderedOffload ? "offload" : "in-process";
    state["status"] = renderedOffload ? "active" : offload.connected() ? "warming" : "local";
    if (renderedOffload) state["reason"] = "ready";
    if (!offload.error.empty()) state["reason"] = offload.error;
    if (!renderer.transforms.empty() && config["graphics"]["offload"] != "off")
        state["reason"] = "edited world transforms use local renderer";
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
        for (auto key : {"world_shading_shared", "sky", "msaa_resolve", "bloom", "upscale_sharpen"})
            state["costsMs"][key] = worker.at(key);
        state["costsMs"]["transfer"] = offload.timings.value("transferGpuMs", Json(nullptr));
        state["effective"]["target_fps"] = offloadPolicy.value("targetFPS", 45);
        state["effective"]["offload_power_cap_w"] = offloadPolicy.value("capW", 50.);
    }
    state["effective"]["offload"] = renderedOffload ? "on" : "off";
    state["workerTimings"] = offload.timings;
    state["controls"] = Json::object();
    for (auto &[key, value] : config["graphics"].items()) {
        std::string group = key == "bloom" || key == "bloom_strength" ? "bloom" :
                            key == "sky" ? "sky" : key == "msaa" ? "msaa_resolve" :
                            key == "sharpen" ? "upscale_sharpen" : key == "alpha_haze" ? "native_surfaces_geometry" :
                            key == "fog" || key == "normal_maps" || key == "specular" || key == "reflections" ||
                            key == "ambient_occlusion" || key == "anisotropy" ? "world_shading_shared" : "";
        auto cost = group.empty() ? Json(nullptr) : state["costsMs"].value(group, Json(nullptr));
        state["controls"][key] = {{"requested", value}, {"effective", state["effective"].at(key)},
                                 {"gpuMs", group == "bloom" || group == "sky" ? cost : Json(nullptr)},
                                 {"sharedPass", group.empty() ? Json(nullptr) : Json(group)}, {"passGpuMs", cost}};
    }
    return state;
}
void Runtime::offloadTick() {
    if (renderDepth) throw std::runtime_error("offload lifecycle inside frame");
    auto g = config["graphics"];
    bool enabled = active && world && monitor && !loading && !locked() &&
                   g["offload"] != "off" && renderer.transforms.empty();
    Json request = {{"enabled", enabled}, {"graphics", g},
                    {"worldPath", world ? (installed / world->id).string() : ""},
                    {"width", monitor ? int(monitor->m_pixelSize.x) : 0},
                    {"height", monitor ? int(monitor->m_pixelSize.y) : 0}};
    if (request != offloadRequest) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        offload.stop(); renderedOffload = false;
        offload.error.clear();
        offloadRequest = request;
        request["generation"] = ++offloadGeneration;
        ipc->renderer(request);
    }
    offloadPump();
    renderer.offloadState = {{"activeRenderer", renderedOffload ? "offload" : "in-process"},
                            {"powerW", offloadPolicy.value("powerW", Json(nullptr))},
                            {"profile", offloadPolicy.value("profile", "unknown")},
                            {"targetFPS", offloadPolicy.value("targetFPS", 0)},
                            {"capW", offloadPolicy.value("capW", 0.)},
                            {"worker", offload.timings}};
}
void Runtime::offloadPump() {
    if (renderDepth) throw std::runtime_error("offload polling inside frame");
    if (offload.connected()) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        offload.tick(camera, renderer.graphics, renderer.profile, offloadPolicy.value("targetFPS", 30));
        if (!offload.connected()) {
            auto stop = offloadRequest; stop["enabled"] = false; stop["generation"] = offloadGeneration;
            ipc->renderer(stop);
        }
    }
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
    Json toolState = {{"slot", editor.slot}, {"name", config["tools"]["slots"][editor.slot - 1]},
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
                                {"title", workspace->m_name},
                                {"logicalSize", {1280, 720}},
                                {"focused", workspace->isVisible()}});
    }
    float sy = std::sin(-camera.yaw / 2), cy = std::cos(-camera.yaw / 2), sp = std::sin(camera.pitch / 2),
          cp = std::cos(camera.pitch / 2);
    Json homes = locations;
    for (auto &b : layout.bindings) {
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
                             {"name", monitor->m_name},
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
                                {"title", window->m_title},
                                {"logicalSize",
                                 {std::max(1, int(window->resource() ? window->resource()->m_current.size.x : 1)),
                                  std::max(1, int(window->resource() ? window->resource()->m_current.size.y : 1))}},
                                {"focused", Desktop::focusState()->window() == window}});
        apps.push_back({{"surfaceId", id},
                        {"workspaceId", int(window->m_workspace->m_id)},
                        {"title", window->m_title},
                        {"appClass", window->m_class},
                        {"icon", window->m_class}});
    }
    Json publication = {
        {"interaction",
         {{"mode", !layout.desktop.is_null() ? "workspace"
                   : fullscreen.phase == 2   ? "fullscreen"
                   : typing                  ? "typing"
                                             : "movement"},
          {"tool", tool},
          {"tools", config["tools"]["slots"]},
          {"noclip", noclip},
          {"cursor", cursorState},
          {"taggedSurfaceId", tagged.empty() ? Json(nullptr) : Json(tagged)}}},
        {"keymap",
         {{"config", input.config},
          {"path", inputFile.path.string()},
          {"conflicts", interaction::inputConflicts(input.config)},
          {"steals", steals(input.config)},
          {"capture", captureRequest},
          {"holds", input.holds()},
          {"error", inputFile.error}}},
        {"editor",
         {{"mode", inputMode()},
          {"tool", editor.slot},
          {"submode", editor.submode()},
          {"selectedId", editor.selected.empty() ? Json(nullptr) : Json(editor.selected)},
          {"selectedKind", editor.kind},
          {"paths", editor.paths},
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
          {"orientation", {cy * sp, sy * cp, -sy * sp, cy * cp}}}},
        {"shell", {{"leases", control.leases}}},
        {"location", control.location},
        {"integrations", {{"sources", control.sources}}}};
    for (auto &e : capture.entries)
        if (world && editor.locked(world->id, e.instance))
            publication["editor"]["locked"].push_back(e.surface.id);
    lastPublished = publication;
    ipc->publish(publication);
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
    pendingWorld = std::async(std::launch::async, [package, id] {
        auto candidate = world::load(package);
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
        graphicsBaseProfile = renderer.profile;
        applyGraphics(graphicsFile.current);
        world = std::move(candidate);
        capture.release();
        layout.invalidateCollision();
        layout.freePoses.clear();
        layout.spawnAreas.clear();
        restoreFullscreenWindows();
        fullscreen = {};
        handover = {};
        relativeSurface.clear();
        editor.selected.clear();
        editor.activeHandle = -1;
        objectModels.clear();
        renderer.transforms.clear();
        originalCollision = world->collision;
        hands.end();
        auto transition = control.location["transition"];
        control.reset(world.get());
        control.location["transition"] = transition;
        control.endLoad(true);
        world->defaultSpawn = spawn.id;
        float lens = camera.fov;
        camera = {};
        camera.fov = lens;
        camera.position = spawn.position + Vec3{0, 1.7f, 0};
        camera.yaw = spawn.yaw;
        camera.pitch = spawn.pitch;
        layout.carried.clear();
        layout.desktop = nullptr;
        pendingArea.clear();
        movement = world->collision.empty() ? static_cast<sim::MotionStrategy *>(&fly) : &walk;
        if (active) {
            // These are existing clients entering a different world, not newly mapped
            // windows. Resolve authored/durable mounts before enabling spawn placement.
            capture.spawnNew = false;
            refresh();
            capture.spawnNew = true;
        }
        publish();
        ipc->complete(Json{{"revision", ipc->revision()}});
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
        ipc->fail(-32602, e.what());
        error(e);
        toggleAfterLoad = false;
        pendingArea.clear();
    }
}

void Runtime::returnToWorldWorkspace() {
    if (renderDepth)
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

void Runtime::toggle(const std::string &output) {
    if (renderDepth)
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
    cursor();
    publish();
}
void Runtime::exit() {
    if (renderDepth)
        throw std::runtime_error("world exit inside render pass");
    if (loading)
        loadCancelled = true;
    if (!active)
        return;
    active = false;
    if (session.recording) session.stopRecording();
    if (session.playing) stopReplay();
    offloadTick();
    input.clear();
    relativeSurface.clear();
    captureRequest = nullptr;
    editor.activeHandle = -1;
    objectCarry = false;
    pendingCursorHide.reset(); // an old hide request must not outlive cursor restoration
    cursorWarpPending = false;
    restoreFullscreenWindows();
    fullscreen = {};
    handover = {};
    freeCursor = layerCursor = false;
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
    if (renderDepth)
        throw std::runtime_error("focus inside render pass");
    if (auto frame = capture.workspace(id)) {
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
    if (renderDepth)
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
void Runtime::useLayers() {
    if (renderDepth)
        throw std::runtime_error("layer cursor inside render pass");
    cancelGrab();
    input.clear();
    keys.clear();
    smoothIntent = {};
    lookX = lookY = 0;
    typing = true;
    layerCursor = freeCursor = true;
    focused.clear();
    focusedParent.clear();
    relativeSurface.clear();
    g_pSeatManager->setPointerFocus(nullptr, {});
    g_pSeatManager->setKeyboardFocus(nullptr);
    cursor();
    publish();
}
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
void Runtime::handTick(float dt) {
    if (hands.gesture == interaction::Gesture::None)
        return;
    auto e = capture.find(hands.pose.id);
    if (!e) {
        hands.end();
        gestureButton = 0;
        return;
    }
    if (hands.update(camera, pointerRay(), dt, world->collision)) {
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
            for (auto &f : capture.workspaces)
                if (auto h = surfaces::intersect(f.surface, pointerCamera().position, pointerRay());
                    h && h->distance < 12 && (!wall || h->distance < wall->second))
                    wall = {{f.workspaceId, h->distance}};
            if (held && wall) {
                auto w = held->window.lock();
                layout.release(held->instance, world.get());
                auto r = g_pKeybindManager->m_dispatchers.at("movetoworkspacesilent")(
                    std::to_string(wall->first) +
                    ",address:" + std::format("{:x}", reinterpret_cast<uintptr_t>(w.get())));
                if (!r.success)
                    throw std::runtime_error("drop workspace failed");
                auto tiled = Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_DISABLE, w);
                if (!tiled)
                    throw std::runtime_error("client tiling rejected: " + tiled.error().message);
            } else if (held) {
                layout.carried.clear();
                if (layout.binding(held->instance.workspaceId, world.get()))
                    layout.mount(held->instance, world.get(), hands.pose);
            }
        }
        if (g != interaction::Gesture::Carry)
            if (auto held = capture.find(id)) {
                layout.carried.clear();
                if (layout.binding(held->instance.workspaceId, world.get()))
                    layout.mount(held->instance, world.get(), hands.pose);
            }
        refresh();
        publish();
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
         event.button == config["input"]["resize_button"].get<uint32_t>() ||
         event.button == config["input"]["roll_button"].get<uint32_t>())) {
        if (!hit)
            return;
        auto e = capture.find(hit->first);
        if (auto wall = capture.workspace(hit->first)) {
            // Pull the actual topmost native client under the workspace-wall hit.
            float x = hit->second.u * monitor->m_size.x + monitor->m_position.x,
                  y = hit->second.v * monitor->m_size.y + monitor->m_position.y;
            for (auto it = capture.entries.rbegin(); it != capture.entries.rend(); ++it)
                if (it->instance.workspaceId == wall->workspaceId && !layout.pulled(it->instance, world.get())) {
                    auto box = it->window->logicalBox();
                    if (box && x >= box->x && x < box->x + box->w && y >= box->y && y < box->y + box->h) {
                        e = &*it;
                        auto s = wall->surface;
                        s.id = e->surface.id;
                        s.pixelWidth = e->surface.pixelWidth;
                        s.pixelHeight = e->surface.pixelHeight;
                        s.width = wall->surface.width * box->w / monitor->m_size.x;
                        s.height = wall->surface.height * box->h / monitor->m_size.y;
                        s.center = wall->surface.center +
                                   wall->surface.right() *
                                       ((box->x - monitor->m_position.x + box->w / 2) / monitor->m_size.x - .5f) *
                                       wall->surface.width +
                                   wall->surface.up() *
                                       (.5f - (box->y - monitor->m_position.y + box->h / 2) / monitor->m_size.y) *
                                       wall->surface.height +
                                   wall->surface.normal() * .06f;
                        e->surface = s;
                        auto h = surfaces::intersect(s, pointerCamera().position, pointerRay());
                        if (h)
                            hit = {{s.id, *h}};
                        break;
                    }
                }
        }
        if (!e || !e->parentId.empty() || editor.locked(world->id, e->instance))
            return;
        cancelGrab();
        typing = false;
        keys.clear();
        smoothIntent = {};
        if (layout.binding(e->instance.workspaceId, world.get()))
            layout.pull(e->instance, world.get());
        auto g = event.button == config["input"]["carry_button"].get<uint32_t>()    ? interaction::Gesture::Carry
                 : event.button == config["input"]["resize_button"].get<uint32_t>() ? interaction::Gesture::CornerResize
                                                                                    : interaction::Gesture::Roll;
        if ((g == interaction::Gesture::Resize || g == interaction::Gesture::CornerResize) &&
            Fullscreen::controller()->isFullscreen(e->window.lock(), Fullscreen::FSMODE_MAXIMIZED))
            Fullscreen::controller()->setFullscreenMode(e->window.lock(), Fullscreen::FSMODE_NONE,
                                                        Fullscreen::FSMODE_NONE);
        hands.begin(g, e->surface, hit->second, camera);
        layout.freePoses[e->surface.id] = e->surface;
        gestureButton = event.button;
        return;
    }
    if (down && !typing && config["tools"]["enabled"] == true && editor.slot == 2 && editor.windowMode == 0) {
        if (hit && capture.find(hit->first)) {
            tagged = hit->first;
            layout.freePoses[tagged] = capture.find(tagged)->surface;
            layout.carried.clear();
            return;
        }
        if (!tagged.empty())
            if (auto e = capture.find(tagged))
                if (auto wall = worldHit()) {
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
        return;
    }
    if (down && editor.slot != 1) {
        toolClick(event.button == 273);
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
                    if (e.surface.id != fullscreen.id && e.parentId.empty()) {
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
        editor.activeHandle = -1;
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
                                       {"description", bind->description.empty() ? bind->handler + " " + bind->arg
                                                                                 : bind->description},
                                       {"submap", bind->submap.name}});
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
        auto rel = point - camera.position, forward = camera.forward(), right = normalized(cross(forward, {0, 1, 0})),
             up = cross(right, forward);
        float depth = dot(rel, forward), tan = std::tan(camera.fov * .01745329252f / 2),
              aspect = monitor->m_size.x / monitor->m_size.y;
        if (depth > .01f) {
            pointerX = .5f + dot(rel, right) / (2 * depth * tan * aspect);
            pointerY = .5f - dot(rel, up) / (2 * depth * tan);
        }
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
    editor.activeHandle = -1;
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
    if (renderDepth)
        throw std::runtime_error("capture inside render pass");
    if (captureWrite.valid())
        throw ipc::Error(-32004, "capture is still being written");
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
    auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    auto path = captureDirectory / ("capture-" + std::to_string(stamp) + ".json");
    Json surfaces = Json::array();
    for (auto &e : capture.entries)
        surfaces.push_back({{"id", e.surface.id},
                            {"title", e.surface.title},
                            {"position", {e.surface.center.x, e.surface.center.y, e.surface.center.z}},
                            {"rotation", e.surface.rotation},
                            {"size", {e.surface.width, e.surface.height}},
                            {"parentId", e.parentId},
                            {"visible", e.visible}});
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
                       {"pitch", frameCamera.pitch},
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
    captureWrite =
        std::async(std::launch::async, [path, image, value = std::move(value), pixels = std::move(pixels)]() mutable {
            auto started = std::chrono::steady_clock::now();
            render::writePNG(image, pixels.width, pixels.height, pixels.rgba);
            value["frame"]["pngWriteMs"] =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            interaction::atomicJSON(path, value, 4 * 1048576);
            return path.string();
        });
}
namespace {
interaction::Transform objectTransform(const interaction::Tools &, const world::World &, const std::string &);
}
void Runtime::action(interaction::Action a) {
    if (a.name == "record.toggle") {
        if (a.pressed && a.edge && !session.playing) sessionMethod(session.recording ? "recording.stop" : "recording.start", Json::object());
        return;
    }
    if (a.name == "wheel") {
        if (zoomHeld) zoom = std::clamp(zoom * std::pow(1.12f,a.value),1.f,20.f);
        else if (editor.slot != 1) {editor.wheel(a.value>0?1:-1);editor.activeHandle=-1;publish();}
        else axis({.timeMs=0,.axis=WL_POINTER_AXIS_VERTICAL_SCROLL,.delta=-a.value*20.,.deltaDiscrete=int32_t(-a.value*120)});
        return;
    }
    auto held = [&](uint32_t key) {
        if (a.pressed)
            keys.insert(key);
        else
            keys.erase(key);
    };
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
    else if (a.name == "up")
        held(KEY_SPACE);
    else if (a.name == "down")
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
        if (a.name == "carry" && a.pressed && editor.slot == 4 && !aim())
            if (auto object = objectAim()) {
                editor.selected = object->first;
                editor.kind = "object";
                objectCarry = true;
                objectDistance = object->second.distance;
                dragPoint = object->second.point - objectTransform(editor, *world, editor.selected).position;
                return;
            }
        if (objectCarry && !a.pressed) {
            objectCarry = false;
            saveEditor();
            return;
        }
        button({.timeMs = 0,
                .button = code,
                .state = a.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED},
               true);
    } else if (a.name == "primary" || a.name == "secondary") {
        if (editor.activeHandle >= 0 && !a.pressed) {
            editor.activeHandle = -1;
            saveEditor();
            return;
        }
        if (editor.slot != 1) {
            if (a.pressed)
                toolClick(a.name == "secondary");
            return;
        }
        uint32_t code = a.name == "primary" ? 272 : 273;
        button({.timeMs = 0,
                .button = code,
                .state = a.pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED},
               false);
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
                    editor.selected = object->first;
                    editor.kind = "object";
                    selectedTransform = objectTransform(editor, *world, editor.selected);
                } else
                    selectedTransform = objectTransform(editor, *world, editor.selected);
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
                auto b =
                    layout.binding(monitor->m_activeWorkspace ? int(monitor->m_activeWorkspace->m_id) : 0, world.get());
                Json next = {
                    {"worldId", world->id},
                    {"areaId", b ? b->at("areaId").get<std::string>() : ""},
                    {"objectId", editor.selected},
                    {"position",
                     {selectedTransform.position.x, selectedTransform.position.y, selectedTransform.position.z}},
                    {"rotation", selectedTransform.rotation},
                    {"scale", {selectedTransform.scale.x, selectedTransform.scale.y, selectedTransform.scale.z}}};
                auto it = std::find_if(editor.objects.begin(), editor.objects.end(), [&](auto &v) {
                    return v["worldId"] == world->id && v["objectId"] == editor.selected;
                });
                if (it != editor.objects.end())
                    *it = next;
                else if (editor.objects.size() < 128)
                    editor.objects.push_back(next);
                toolsTick();
                saveEditor();
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
                if (layout.binding(e->instance.workspaceId, world.get()))
                    layout.mount(e->instance, world.get(), hands.pose);
            }
        } else {
            auto old = hands.gesture != interaction::Gesture::None ? hands.pose : e->surface;
            auto next = old;
            auto q = old.rotation;
            float s = std::sin(a.value * .1308997f), c = std::cos(a.value * .1308997f);
            next.rotation = {q[0] * c + q[1] * s, q[1] * c - q[0] * s, q[2] * c + q[3] * s, q[3] * c - q[2] * s};
            if (interaction::sweepSafe(old, next, world->collision)) {
                layout.freePoses[e->surface.id] = next;
                if (hands.gesture != interaction::Gesture::None) {
                    auto x = dot(hands.offset, old.right()), y = dot(hands.offset, old.up());
                    hands.pose = next;
                    hands.offset = next.right() * x + next.up() * y;
                } else if (layout.binding(e->instance.workspaceId, world.get()))
                    layout.mount(e->instance, world.get(), next);
            }
        }
    } else if (!a.pressed || !a.edge)
        return;
    else if (a.name == "exit")
        leaveInteraction();
    else if (a.name == "menu") {
        input.clear();
        control.openMenu();
    } else if (a.name == "use")
        useAim(false);
    else if (a.name == "focus")
        useAim(true);
    else if (a.name == "debug")
        debug = !debug;
    else if (a.name == "capture")
        deferInput([] { runtime->debugCapture(); });
    else if (a.name == "noclip") {
        noclip = !noclip;
        camera.verticalSpeed = 0;
    } else if (a.name == "fly") {
        movement = movement == &walk ? static_cast<sim::MotionStrategy *>(&fly) : &walk;
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
        editor.activeHandle = -1;
        objectCarry = false;
        editor.slot = tool = std::stoi(a.name.substr(4));
        tagged.clear();
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
bool belongs(const world::World &w, unsigned n, unsigned root) {
    for (int i = n; i >= 0 && size_t(i) < w.nodeParents.size(); i = w.nodeParents[i])
        if (unsigned(i) == root)
            return true;
    return false;
}
Vec3 objectOrigin(const world::World &w, const std::string &id) {
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
interaction::Transform objectTransform(const interaction::Tools &editor, const world::World &w, const std::string &id) {
    interaction::Transform t;
    t.position = objectOrigin(w, id);
    for (auto &o : editor.objects)
        if (o["worldId"] == w.id && o["objectId"] == id) {
            t.position = {o["position"][0], o["position"][1], o["position"][2]};
            t.rotation = o["rotation"].get<std::array<float, 4>>();
            t.scale = {o["scale"][0], o["scale"][1], o["scale"][2]};
            break;
        }
    return t;
}
std::array<float, 4> turn(std::array<float, 4> q, Vec3 axis, float angle) {
    auto u = axis * std::sin(angle / 2);
    float w = std::cos(angle / 2);
    Vec3 v{q[0], q[1], q[2]};
    auto n = v * w + u * q[3] + cross(u, v);
    return {n.x, n.y, n.z, w * q[3] - dot(u, v)};
}
std::optional<Vec3> planePoint(Vec3 origin, Vec3 ray, Vec3 center, Vec3 normal) {
    float d = dot(ray, normal);
    if (std::abs(d) < .001f)
        return {};
    float distance = dot(center - origin, normal) / d;
    if (distance <= 0)
        return {};
    return origin + ray * distance;
}
float axisPoint(Vec3 origin, Vec3 ray, Vec3 center, Vec3 axis) {
    auto v = center - origin;
    float a = dot(axis, ray);
    return std::abs(1 - a * a) > .01f ? (a * dot(v, ray) - dot(v, axis)) / (1 - a * a) : 0;
}
} // namespace
void Runtime::saveEditor() {
    if (session.playing) return;
    auto value = editor.saved();
    if (value == lastEditor)
        return;
    interaction::atomicJSON(editorFile, value);
    lastEditor = value;
}
std::optional<std::pair<std::string, interaction::WorldHit>> Runtime::objectAim() {
    if (!world)
        return {};
    std::optional<std::pair<std::string, interaction::WorldHit>> result;
    float nearest = 100000;
    for (auto &m : world->meshes) {
        auto model = renderer.transforms.contains(m.node) ? renderer.transforms.at(m.node) : Mat4::identity();
        for (size_t i = 0; i + 2 < m.vertices.size(); i += 3) {
            Triangle t{model.point(m.vertices[i].position), model.point(m.vertices[i + 1].position),
                       model.point(m.vertices[i + 2].position)};
            auto h = interaction::raycast(pointerCamera().position, pointerRay(), std::span<const Triangle>(&t, 1));
            if (!h || h->distance >= nearest)
                continue;
            nearest = h->distance;
            std::string id = "node-" + std::to_string(m.node);
            for (auto &[name, root] : world->objectNodes)
                if (belongs(*world, m.node, root)) {
                    id = name;
                    break;
                }
            result = {{id, *h}};
        }
    }
    return result;
}
std::optional<interaction::WorldHit> Runtime::worldHit() {
    if (!world)
        return {};
    auto mesh = objectAim();
    auto collision = interaction::raycast(pointerCamera().position, pointerRay(), world->collision, &world->collisionIndex);
    if (mesh && (!collision || mesh->second.distance < collision->distance))
        return mesh->second;
    return collision;
}
void Runtime::toolClick(bool secondary) {
    if (!world)
        return;
    if (editor.slot == 2) {
        if (editor.windowMode == 0) {
            if (secondary) {
                tagged.clear();
                return;
            }
            button({.timeMs = 0, .button = 272, .state = WL_POINTER_BUTTON_STATE_PRESSED}, false);
            return;
        }
        if (auto h = aim())
            if (auto e = capture.find(h->first)) {
                editor.toggleLock(world->id, e->instance);
                saveEditor();
                publish();
            }
        return;
    }
    if (editor.slot == 3) {
        if (secondary) {
            editor.finishPath();
            saveEditor();
            return;
        }
        if (auto h = worldHit()) {
            auto b =
                layout.binding(monitor->m_activeWorkspace ? int(monitor->m_activeWorkspace->m_id) : 0, world.get());
            editor.point(world->id, b ? b->at("areaId").get<std::string>() : "", h->point + h->normal * .03f);
            saveEditor();
            publish();
        }
        return;
    }
    if (editor.slot != 4)
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
        auto handles = interaction::gizmo(selectedTransform.position, editor.submode());
        if (auto picked = interaction::pickHandle(handles, pointerCamera().position, pointerRay())) {
            if (editor.kind == "window")
                if (auto e = capture.find(editor.selected))
                    if (editor.locked(world->id, e->instance))
                        return;
            editor.activeHandle = *picked;
            dragStart = selectedTransform;
            if (editor.kind == "window")
                if (auto e = capture.find(editor.selected)) {
                    dragPixelWidth = e->surface.pixelWidth;
                    dragPixelHeight = e->surface.pixelHeight;
                }
            auto h = handles[*picked];
            std::array<Vec3, 3> axes = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}};
            dragNormal = h.axis < 3 ? axes[h.axis] : camera.forward();
            if (editor.positionMode == 1)
                dragPoint =
                    planePoint(pointerCamera().position, pointerRay(), selectedTransform.position, dragNormal).value_or(h.end) -
                    selectedTransform.position;
            else
                dragPoint = {axisPoint(pointerCamera().position, pointerRay(), selectedTransform.position, dragNormal), .5f,
                             .5f};
            pointerX = pointerY = .5f;
            keys.clear();
            smoothIntent = {};
            return;
        }
    }
    if (auto h = aim())
        if (auto e = capture.find(h->first)) {
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
        editor.selected = h->first;
        editor.kind = "object";
        selectedTransform = objectTransform(editor, *world, editor.selected);
        publish();
    }
}
void Runtime::toolsTick() {
    if (!world)
        return;
    if (originalCollision.empty())
        originalCollision = world->collision;
    if (objectCarry) {
        selectedTransform = objectTransform(editor, *world, editor.selected);
        selectedTransform.position = camera.position + pointerRay() * objectDistance - dragPoint;
    }
    if (editor.activeHandle >= 0) {
        auto handles = interaction::gizmo(dragStart.position, editor.submode());
        if (size_t(editor.activeHandle) >= handles.size()) {
            editor.activeHandle = -1;
            return;
        }
        auto h = handles[editor.activeHandle];
        selectedTransform = dragStart;
        float delta = axisPoint(pointerCamera().position, pointerRay(), dragStart.position, dragNormal) - dragPoint.x;
        if (std::abs(dot(dragNormal, camera.forward())) > .95f)
            delta = (pointerX - .5f) * length(dragStart.position - camera.position) * 2;
        if (keys.contains(KEY_LEFTSHIFT))
            delta = std::round(delta / .1f) * .1f;
        if (editor.positionMode == 0)
            selectedTransform.position += dragNormal * delta;
        else if (editor.positionMode == 1) {
            auto p = planePoint(pointerCamera().position, pointerRay(), dragStart.position, dragNormal);
            float angle = p ? std::atan2(dot(cross(dragPoint, *p - dragStart.position), dragNormal),
                                         dot(dragPoint, *p - dragStart.position))
                            : (pointerX - .5f) * 6.283185f;
            if (keys.contains(KEY_LEFTSHIFT))
                angle = std::round(angle / .2617994f) * .2617994f;
            selectedTransform.rotation = turn(dragStart.rotation, dragNormal, angle);
        } else {
            float factor = std::clamp(1 + delta, .05f, 20.f);
            if (h.uniform)
                factor = std::clamp(1 + (pointerX - .5f - pointerY + .5f) * 3, .05f, 20.f);
            if (keys.contains(KEY_LEFTSHIFT))
                factor = std::max(.1f, std::round(factor / .1f) * .1f);
            if (h.uniform)
                selectedTransform.scale = dragStart.scale * factor;
            else if (h.axis == 0)
                selectedTransform.scale.x = dragStart.scale.x * factor;
            else if (h.axis == 1)
                selectedTransform.scale.y = dragStart.scale.y * factor;
            else
                selectedTransform.scale.z = dragStart.scale.z * factor;
        }
    }
    selectedTransform.scale = {std::clamp(selectedTransform.scale.x, .01f, 100.f),
                               std::clamp(selectedTransform.scale.y, .01f, 100.f),
                               std::clamp(selectedTransform.scale.z, .01f, 100.f)};
    if (editor.activeHandle >= 0 || objectCarry) {
        if (editor.kind == "window") {
            if (auto e = capture.find(editor.selected)) {
                auto next = e->surface;
                next.center = selectedTransform.position;
                next.rotation = selectedTransform.rotation;
                next.yaw = 0;
                next.width = std::clamp(selectedTransform.scale.x, .1f, 12.f);
                next.height = std::clamp(selectedTransform.scale.y, .1f, 8.f);
                if (interaction::sweepSafe(e->surface, next, world->collision)) {
                    layout.freePoses[e->surface.id] = next;
                    layout.carried.clear();
                    if (editor.positionMode == 2 && e->window->m_target) {
                        auto w = e->window.lock();
                        auto box = w->m_target->position();
                        // Configure against the gesture baseline: the world pose
                        // updates before the client acknowledges its new pixels.
                        // Feeding those pending pixels back cancels the resize.
                        auto width = std::clamp(double(next.width / dragStart.scale.x * dragPixelWidth), 160.,
                                                monitor->m_size.x),
                             height = std::clamp(double(next.height / dragStart.scale.y * dragPixelHeight), 100.,
                                                 monitor->m_size.y);
                        if (std::abs(box.w - width) > 1 || std::abs(box.h - height) > 1) {
                            Fullscreen::controller()->setFullscreenMode(w, Fullscreen::FSMODE_NONE,
                                                                        Fullscreen::FSMODE_NONE);
                            Config::Actions::floatWindow(Config::Actions::TOGGLE_ACTION_ENABLE, w);
                            Config::Actions::resize({width, height}, false, w);
                            w->sendWindowSize(true);
                        }
                    }
                    if (layout.binding(e->instance.workspaceId, world.get()))
                        layout.mount(e->instance, world.get(), next);
                }
            } else
                editor.selected.clear();
        } else if (editor.kind == "object") {
            auto b =
                layout.binding(monitor->m_activeWorkspace ? int(monitor->m_activeWorkspace->m_id) : 0, world.get());
            Json next = {{"worldId", world->id},
                         {"areaId", b ? b->at("areaId").get<std::string>() : ""},
                         {"objectId", editor.selected},
                         {"position",
                          {selectedTransform.position.x, selectedTransform.position.y, selectedTransform.position.z}},
                         {"rotation", selectedTransform.rotation},
                         {"scale", {selectedTransform.scale.x, selectedTransform.scale.y, selectedTransform.scale.z}}};
            auto it = std::find_if(editor.objects.begin(), editor.objects.end(), [&](auto &o) {
                return o["worldId"] == world->id && o["objectId"] == editor.selected;
            });
            if (it != editor.objects.end())
                *it = next;
            else if (editor.objects.size() < 128)
                editor.objects.push_back(next);
        }
    }
    renderer.transforms.clear();
    for (auto &o : editor.objects)
        if (o["worldId"] == world->id) {
            auto node = editorNode(*world, o["objectId"]);
            if (!node)
                continue;
            auto t = objectTransform(editor, *world, o["objectId"]);
            auto matrix = t.matrix(objectOrigin(*world, o["objectId"]));
            for (size_t n = 0; n < world->nodeParents.size(); n++)
                if (belongs(*world, n, *node))
                    renderer.transforms[n] = matrix;
        }
    if (renderer.transforms != objectModels) {
        objectModels = renderer.transforms;
        world->collision = originalCollision;
        for (size_t i = 0; i < world->collision.size(); i++)
            if (i < world->collisionNodes.size() && renderer.transforms.contains(world->collisionNodes[i])) {
                auto m = renderer.transforms.at(world->collisionNodes[i]);
                auto &t = world->collision[i];
                t = {m.point(t.a), m.point(t.b), m.point(t.c)};
            }
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        layout.invalidateCollision();
        world->collisionIndex.rebuild(world->collision);
        world->visualTriangles.clear();
        for (const auto &mesh : world->meshes) {
            auto it = renderer.transforms.find(mesh.node);
            auto matrix = it == renderer.transforms.end() ? Mat4::identity() : it->second;
            for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3)
                world->visualTriangles.push_back({matrix.point(mesh.vertices[i].position),
                                                  matrix.point(mesh.vertices[i + 1].position),
                                                  matrix.point(mesh.vertices[i + 2].position)});
        }
        world->visualIndex.rebuild(world->visualTriangles);
        renderer.collisionChanged();
    }
    renderer.lines.clear();
    for (auto &path : editor.paths)
        if (path["worldId"] == world->id) {
            auto lines = interaction::pathLines(path);
            renderer.lines.insert(renderer.lines.end(), lines.begin(), lines.end());
        }
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
    if (editor.slot == 4 && !editor.selected.empty()) {
        if (editor.activeHandle < 0) {
            if (editor.kind == "window") {
                if (auto e = capture.find(editor.selected))
                    selectedTransform.position = e->surface.center;
                else
                    editor.selected.clear();
            } else
                selectedTransform = objectTransform(editor, *world, editor.selected);
        }
        for (auto &h : interaction::gizmo(selectedTransform.position, editor.submode())) {
            Vec3 color = h.axis == 0   ? Vec3{1, .2f, .2f}
                         : h.axis == 1 ? Vec3{.2f, 1, .3f}
                         : h.axis == 2 ? Vec3{.2f, .5f, 1}
                                       : Vec3{1, .9f, .6f};
            renderer.lines.push_back({h.start, h.end, color});
            if (h.shape == "arrow") {
                auto axis = normalized(h.end - h.start), side = normalized(cross(axis, camera.forward()));
                if (length(side) < .1f)
                    side = {1, 0, 0};
                renderer.lines.push_back({h.end, h.end - axis * .18f + side * .09f, color});
                renderer.lines.push_back({h.end, h.end - axis * .18f - side * .09f, color});
            }
            if (h.shape == "cube") {
                auto c = h.uniform ? selectedTransform.position : h.end;
                std::array<Vec3, 8> p;
                for (int i = 0; i < 8; i++)
                    p[i] = c + Vec3{(i & 1) ? .065f : -.065f, (i & 2) ? .065f : -.065f, (i & 4) ? .065f : -.065f};
                for (int i = 0; i < 8; i++)
                    for (int bit : {1, 2, 4})
                        if (!(i & bit))
                            renderer.lines.push_back({p[i], p[i | bit], color});
            }
        }
    }
}
std::optional<Json> Runtime::method(const std::string &method, const Json &wire) {
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
    params.erase("_pid");
    params.erase("_protocol");
    if (method.starts_with("recording.") || method.starts_with("replay.")) return sessionMethod(method, params);
    if (method == "_renderer.channel") {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        if (params.at("generation") != offloadGeneration || !offloadRequest.value("enabled", false))
            close(params.at("fd").get<int>());
        else offload.attach(params.at("fd"), offloadRequest.at("width"), offloadRequest.at("height"), offloadGeneration);
        return Json::object();
    }
    if (method == "_renderer.state") {
        if (params.at("generation") == offloadGeneration) offloadPolicy = params;
        return Json::object();
    }
    if (method == "graphics.get" || method == "graphics.set") {
        if (renderDepth)
            throw ipc::Error(-32004, "graphics update during render");
        if (method == "graphics.set") {
            if (params.size() != 1 || !params.contains("patch") || !params["patch"].is_object())
                throw ipc::Error(-32602, "graphics patch required");
            if (!ipc::protocol::atLeast(protocol, "0.10"))
                for (auto key : {"offload", "offload_power_cap_w", "scale_min", "scale_max"})
                    if (params["patch"].contains(key)) throw ipc::Error(-32602, "setting requires IPC 0.10");
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
        Json response{
            {"config", config["graphics"]},         {"effective", renderer.graphics},
            {"overrides", graphicsFile.current},    {"path", graphicsFile.path.string()},
            {"error", graphicsFile.error},          {"presets", render::graphicsPresets()},
            {"options", render::graphicsOptions()}, {"unsupported", {"ssr", "runtime_shadows", "contact_shadows"}},
            {"timings", renderer.timings()}};
        if (ipc::protocol::atLeast(protocol, "0.10")) response["performance"] = performance();
        else for (auto section : {"config", "effective", "overrides", "options"})
            for (auto key : {"offload", "offload_power_cap_w", "scale_min", "scale_max"}) response[section].erase(key);
        return response;
    }
    if (method == "input.get") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        auto response = Json{{"config", input.config}, {"path", inputFile.path.string()},
                             {"steals", steals(input.config)}, {"conflicts", interaction::inputConflicts(input.config)}};
        return ipc::protocol::atLeast(protocol, "0.9") ? response : interaction::legacyInputNames(response);
    }
    if (method == "overlay.style") {
        if (renderDepth || params.size() != 2 || !params.contains("leaseId") || !params["leaseId"].is_string() ||
            !control.ownsLease(actor, params["leaseId"]) || !params.contains("style"))
            throw ipc::Error(-32602, "overlay style needs owning shell lease");
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        renderer.applyOverlayStyle(params["style"]);
        return Json{{"style", renderer.overlayStyle}};
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
            next = interaction::validateInput(requested);
        } catch (const std::exception &e) {
            throw ipc::Error(-32602, e.what());
        }
        if (method == "input.preview") {
            auto response = Json{{"config", next}, {"steals", steals(next)}, {"conflicts", interaction::inputConflicts(next)}};
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
    if (method == "_layout.restore") {
        layout.restore(params);
        publish();
        return Json::object();
    }
    if (method == "_operation.cancel") {
        loadCancelled = true;
        return Json::object();
    }
    if (method == "_connection.closed") {
        bindings.erase(actor);
        control.disconnect(actor);
        publish();
        return Json::object();
    }
    if (locked())
        throw ipc::Error(-32005, "session locked");
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
        for (auto &e : capture.entries)
            if (layout.pulled(e.instance, nullptr))
                live.push_back(e.instance);
        layout.recall(live);
        typing = false;
        keys.clear();
        cancelGrab();
        refresh();
        publish();
        return Json{{"revision", ipc->revision()}};
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
        if (method == "surface.return")
            layout.release(e->instance, world.get());
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
        return Json{{"profile", world ? renderer.profile : Json(nullptr)},
                    {"quality", renderer.intelQuality ? "intel" : "high"}};
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
        auto result = control.execute(method, params, actor, client, world.get(), camera, outputs);
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
void Runtime::tick() {
    if (renderDepth != 0)
        throw std::runtime_error("deferred work inside compositor render pass");
    auto updateStarted = std::chrono::steady_clock::now();
    if (capturePending) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        if (renderer.completed())
            debugCapture();
    }
    if (captureWrite.valid() && captureWrite.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto path = captureWrite.get();
            ipc->notify("runtime.notice",
                        {{"level", "info"}, {"code", "capture.saved"}, {"message", "Saved JSON + PNG: " + path}});
        } catch (const std::exception &e) {
            ipc->notify("runtime.notice", {{"level", "warning"}, {"code", "capture.failed"}, {"message", e.what()}});
        }
    }
    if (renderDepth != 0)
        throw std::runtime_error("deferred work inside compositor render pass");
    ipc->poll();
    if (auto next = graphicsFile.poll())
        applyGraphics(*next);
    if (!session.playing) if (auto next = inputFile.poll())
        applyInput(*next);
    renderer.debug = debug;
    replayBeforeTick();
    for (auto &a : input.tick(inputClock()))
        action(a);
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
    safe([&] { workspaceLocation(); });
    finishLoad();
    if (locked() && active)
        exit();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;
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
    control.tick(dt, camera, world.get());
    if (control.uiVisible()) {
        keys.clear();
        smoothIntent = {};
        lookX = lookY = 0;
        cancelGrab();
        typing = false;
    }
    if (active && monitor)
        transitionTick(dt);
    if (active && monitor && layout.desktop.is_null() && fullscreen.phase != 2) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        if (!ownsInput()) {
            if (monitor != Desktop::focusState()->monitor())
                control.hold(false);
            keys.clear();
            cancelGrab();
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
            intent.speed = (movement == &walk ? config["player"]["walk_speed"].get<float>() / 3.5f
                                              : config["player"]["fly_speed"].get<float>() / 5.f) *
                           (keys.contains(KEY_LEFTSHIFT) ? config["player"]["run_multiplier"].get<float>() : 1.f);
        }
        float lookT = config["player"]["look_inertia"];
        float blend = lookT > 0 ? 1 - std::exp(-dt / lookT) : 1;
        camera.yaw += lookX * blend;
        camera.pitch = std::clamp(camera.pitch + lookY * blend, -1.48f, 1.48f);
        lookX *= 1 - blend;
        lookY *= 1 - blend;
        camera.fov = std::max(3.f, baseFov / (zoomHeld ? zoom : 1.f));
        renderer.debug = debug;
        renderer.showAreaMarkers = input.config["show_area_markers"];
        renderer.waypoint = control.location["waypoint"];
        renderer.route = control.location["route"];
        renderer.useDistance = input.config["useDistance"];
        renderer.scopeVignette = zoomHeld && input.config["scopeVignette"].get<bool>();
        float moveT = config["player"]["move_inertia"];
        float mb = moveT > 0 ? 1 - std::exp(-dt / moveT) : 1;
        smoothIntent.forward += (intent.forward - smoothIntent.forward) * mb;
        smoothIntent.right += (intent.right - smoothIntent.right) * mb;
        smoothIntent.up += (intent.up - smoothIntent.up) * mb;
        intent.forward = smoothIntent.forward;
        intent.right = smoothIntent.right;
        intent.up = smoothIntent.up;
        accumulator = std::min(accumulator + double(dt), 4.0 / 60.0);
        while (accumulator >= 1.0 / 60.0) {
            if (!loading && !control.driving() && !control.uiVisible() && !handover.phase && fullscreen.phase == 0) {
                if (noclip) {
                    auto speed = intent.speed;
                    intent.speed = config["player"]["fly_speed"].get<float>() / 5.f;
                    fly.step(camera, intent, 1.f / 60.f, {});
                    intent.speed = speed;
                } else
                    movement->step(camera, intent, 1.f / 60.f, world->collision, &world->collisionIndex);
            }
            accumulator -= 1.0 / 60.0;
        }
        if (config["player"]["respawn_below"].get<float>() > 0 &&
            camera.position.y < world->spawn().position.y + 1.7f - config["player"]["respawn_below"].get<float>()) {
            auto s = world->spawn();
            camera.eyeHeight = 1.7f;
            camera.position = s.position + Vec3{0, 1.7f, 0};
            camera.verticalSpeed = 0;
            keys.clear();
            smoothIntent = {};
            cancelGrab();
        }
        handTick(dt);
        toolsTick();
        auto captureStarted = std::chrono::steady_clock::now();
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        captureGPU.begin();
        refresh();
        captureGPU.end();
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
        if (typing && !focused.empty() && !g_pSeatManager->m_seatGrab)
            if (auto e = capture.find(focused))
                if (g_pSeatManager->m_state.keyboardFocus != e->window->resource())
                    g_pSeatManager->setKeyboardFocus(e->window->resource());
        renderer.pointerX = (typing || editor.activeHandle >= 0) ? pointerX : .5f;
        renderer.pointerY = (typing || editor.activeHandle >= 0) ? pointerY : .5f;
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
        g_pHyprRenderer->damageMonitor(monitor.lock());
        monitor->scheduleFrame();
    }
    replayAfterTick();
    renderer.updateMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - updateStarted).count();
    cursor();
    auto ipcStarted = std::chrono::steady_clock::now();
    if (ipcStarted >= nextPublish || !control.events.empty() || !layout.events.empty()) {
        nextPublish = ipcStarted + std::chrono::milliseconds(33);
        publish();
    }
    offloadTick();
    publishHUD();
    ipc->poll();
    renderer.gpuCaptureMs = captureGPU.available ? captureGPU.milliseconds : -1;
    renderer.ipcMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ipcStarted).count();
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
            auto surfaces = runtime->capture.visible();
            if (runtime->fullscreen.phase == 1 || runtime->fullscreen.phase == 3) {
                auto fs = runtime->capture.find(runtime->fullscreen.id);
                if (fs) {
                    std::erase_if(surfaces, [&](auto &s) { return s.id == fs->surface.id; });
                    surfaces.push_back(fs->surface);
                }
            }
            runtime->renderer.toolPrompt = nullptr;
            auto prompt = [&](Vec3 point, std::string text) {
                runtime->renderer.toolPrompt = {{"position", {point.x, point.y, point.z}}, {"text", text}};
            };
            if (runtime->editor.slot == 4) {
                if (auto object = runtime->objectAim())
                    prompt(object->second.point, "LMB SELECT / WHEEL " + runtime->editor.submode());
            }
            if (runtime->editor.slot == 2 && runtime->editor.windowMode == 0 && !runtime->tagged.empty()) {
                if (auto hit = runtime->worldHit())
                    prompt(hit->point, "LMB MOUNT HERE");
            }
            auto &r = runtime->renderer;
            r.externalWorldFbo = runtime->offload.fbo();
            auto view = r.externalWorldFbo ? runtime->offload.camera() : runtime->camera;
            auto frames = r.frames;
            r.draw(fb->getFBID(), data.currentFB->m_size.x, data.currentFB->m_size.y, view,
                   surfaces, runtime->typing && !runtime->layerCursor);
            if (r.frames != frames) {
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
    if (lua_gettop(state) >= 5) {
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
        {"grabbed", runtime->grabbed},
        {"gesture", int(runtime->hands.gesture)},
        {"activeHandle", runtime->editor.activeHandle},
        {"positionMode", runtime->editor.positionMode},
        {"selectedScale",
         {runtime->selectedTransform.scale.x, runtime->selectedTransform.scale.y, runtime->selectedTransform.scale.z}},
        {"superHeld", runtime->superHeld},
        {"ownsInput", ownsInput()},
        {"layerCursor", runtime->layerCursor},
        {"debug", runtime->debug},
        {"overlays", runtime->renderer.overlayDiagnostics},
        {"overlayStyle", runtime->renderer.overlayStyle},
        {"timings", runtime->renderer.timings()},
        {"captureDirectory", runtime->captureDirectory.string()},
        {"instances", runtime->layout.instances},
        {"aim", aim() ? aim()->first : ""},
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
    j["captures"] = Json::array();
    for (auto &e : runtime->capture.entries)
        j["captures"].push_back(
            {{"id", e.surface.id},
             {"title", e.surface.title},
             {"parentId", e.parentId},
             {"floating", e.window->m_isFloating},
             {"commits", *e.commits},
             {"visible", e.visible},
             {"size", {e.surface.width, e.surface.height}},
             {"collision", runtime->world && e.visible && surfaces::overlaps(e.surface, runtime->world->collision)},
             {"rotation", e.surface.rotation},
             {"pixels", {e.surface.pixelWidth, e.surface.pixelHeight}},
             {"position", {e.surface.center.x, e.surface.center.y, e.surface.center.z}}});
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
    if (auto p = getenv("HYPRUNE_EDITOR_FILE"))
        runtime->editorFile = p;
    else if (auto p = getenv("XDG_STATE_HOME"))
        runtime->editorFile = std::filesystem::path(p) / "hyprune/editor.json";
    else
        runtime->editorFile = std::filesystem::path(getenv("HOME")) / ".local/state/hyprune/editor.json";
    if (std::filesystem::exists(runtime->editorFile))
        runtime->editor.restore(interaction::readJSON(runtime->editorFile));
    runtime->lastEditor = runtime->editor.saved();
    const char *installed = getenv("HYPRUNE_WORLD_DIR");
    runtime->installed = installed ? installed : std::string(getenv("HOME")) + "/.local/share/hyprune/worlds";
    const char *dir = getenv("XDG_RUNTIME_DIR");
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!dir || !sig)
        throw std::runtime_error("Hyprune: missing session environment");
    runtime->ipc = std::make_unique<hyprune::ipc::Bridge>(
        HYPRUNE_DAEMON_PATH, [](auto &method, auto &params) { return runtime->method(method, params); });
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
    methods.insert({{"workspace.bind", "world.control"},
                    {"workspace.focus", "surface.control"},
                    {"workspace.unfocus", "surface.control"},
                    {"surface.pull", "surface.control"},
                    {"surface.place", "surface.control"},
                    {"surface.return", "surface.control"},
                    {"surfaces.recall", "surface.control"},
                    {"placements.list", "state.read"},
                    {"placements.remove", "surface.control"},
                    {"data.object.bind", "data.publish"},
                    {"atmosphere.get", "state.read"},
                    {"atmosphere.set", "world.control"},
                    {"overlay.style", "shell.control"},
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
                    {"input.preview", "state.read"},
                    {"input.set", "input.control"},
                    {"input.capture", "input.control"},
                    {"input.capture.cancel", "input.control"},
                    {"movement.set", "movement.control"},
                    {"runtime.exit", "runtime.control"}});
    runtime->ipc->advertise(std::move(methods));
    runtime->deferInput([] { runtime->applyGraphics(runtime->graphicsFile.current); });
    HyprlandAPI::addDispatcherV2(handle, "hyprune.toggle", [](std::string arg) {
        deferToggle(arg);
        return SDispatchResult{};
    });
    HyprlandAPI::addLuaFunction(handle, "hyprune", "toggle", luaToggle);
    HyprlandAPI::addLuaFunction(handle, "hyprune", "camera", luaCamera);
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
    runtime->listeners.push_back(Event::bus()->m_events.window.openEarly.listen([](PHLWINDOW window) {
        // This event follows static rules/launch-token workspace routing and
        // precedes newTarget(). Change only the initial layout flag: no render,
        // configure, focus or cursor call is allowed in this synchronous hook.
        // A static float rule sees the initial workspace and leaks to clients
        // redirected elsewhere. Test the final workspace and output instead.
        if (runtime->active && window->m_monitor == runtime->monitor && window->m_workspace &&
            window->m_workspace->m_id == runtime->worldWorkspace)
            window->m_isFloating = true;
    }));
    runtime->listeners.push_back(Event::bus()->m_events.render.stage.listen([](eRenderStage stage) {
        safe([&] {
            if (stage == RENDER_PRE)
                ++runtime->renderDepth;
            if (stage == RENDER_POST && runtime->renderDepth > 0)
                --runtime->renderDepth;
            if (stage == RENDER_POST_WINDOWS && runtime->active && !locked() &&
                g_pHyprRenderer->m_renderData.pMonitor == runtime->monitor)
                g_pHyprRenderer->addPassElement(makeUnique<Pass>());
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
            if (runtime->active && runtime->layerCursor && runtime->monitor == Desktop::focusState()->monitor() &&
                !runtime->control.uiVisible() && !interactiveLayerAt(pos)) {
                info.cancelled = true;
                runtime->deferInput([] { g_pSeatManager->setPointerFocus(nullptr, {}); });
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
                if (owned)
                    info.cancelled = true;
                bool scoped = runtime->active && !locked() && runtime->monitor == Desktop::focusState()->monitor() &&
                              (!runtime->freeCursor || runtime->layerCursor);
                bool bindingScoped = runtime->active && !locked();
                if (!bindingScoped && !owned)
                    return;
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
                if (!down && runtime->buttons.contains(event.button)) {
                    info.cancelled = true;
                    runtime->inputEvent(runtime->inputMode(), key, mods, false, 0);
                    runtime->deferInput([event] { runtime->button(event, false); });
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
            if (runtime->layerCursor && !runtime->control.uiVisible()) {
                if (!interactiveLayerAt(Pointer::mgr()->position()))
                    info.cancelled = true;
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
                        if (a.name == "wheel") {
                            if (runtime->zoomHeld) {
                                runtime->zoom = std::clamp(runtime->zoom * std::pow(1.12f, a.value), 1.f, 20.f);
                            } else if (runtime->editor.slot != 1) {
                                runtime->editor.wheel(a.value > 0 ? 1 : -1);
                                runtime->editor.activeHandle = -1;
                                runtime->publish();
                            } else
                                runtime->axis(event);
                        } else
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
            if (!runtime->tickDeferred)
                runtime->tickDeferred = g_pEventLoopManager->doLater([] {
                    if (!runtime)
                        return;
                    runtime->tickDeferred = 0;
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
                    safe([] { runtime->offloadPump(); });
                });
            self->updateTimeout(std::chrono::milliseconds(runtime->offload.connected() ? 4 : 100));
        }, nullptr);
    g_pEventLoopManager->addTimer(runtime->offloadTimer);
    runtime->publish();
    return {"hyprune-core", "World-driven desktop runtime", "Hyprune contributors", "0.3.0"};
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
