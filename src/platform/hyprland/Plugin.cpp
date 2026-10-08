#include "control/Control.hpp"
#include "interaction/Config.hpp"
#include "interaction/Hands.hpp"
#include "world/Mounts.hpp"
#include "world/Spatial.hpp"
#include <hyprland/src/config/shared/actions/ConfigActions.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/LayerState.hpp>
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
#include <hyprutils/memory/UniquePtr.hpp>
#include <sstream>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#include "Capture.hpp"
#include "ipc/Bridge.hpp"
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
    Capture capture;
    surfaces::Layout layout;
    interaction::Hands hands;
    Json config = interaction::defaultConfig(), configured = config;
    std::set<std::string> graphicsOverrides;
    bool superHeld = false, altHeld = false, noclip = false, freeCursor = false, zoomHeld = false;
    float zoom = 1, baseFov = 60, lookX = 0, lookY = 0;
    sim::Intent smoothIntent;
    int tool = 1;
    std::string tagged;
    uint32_t gestureButton = 0;
    std::set<uint32_t> swallowedButtons, swallowedKeys;
    WP<CWLSurfaceResource> grabResource;
    std::string cursorState = "desktop";
    bool savedCursorHidden = false, cursorHidden = false;
    void cursor();
    void modeToggle();
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
    bool active = false, typing = false, loading = false, loadCancelled = false;
    int renderDepth = 0;
    std::future<std::shared_ptr<world::World>> pendingWorld;
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
    void toggle(const std::string &output = "");
    void exit();
    void tick();
    void focus(const std::string &, bool keyboard);
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
bool ownsInput() {
    return runtime && runtime->active && !runtime->loading && !runtime->control.uiVisible() && !locked() &&
           runtime->monitor == Desktop::focusState()->monitor() && runtime->layout.desktop.is_null() &&
           runtime->fullscreen.phase != 2 && !runtime->freeCursor && !runtime->handover.phase;
}
void motion(void *self, const Vector2D &delta) {
    if (runtime && runtime->active && runtime->control.uiVisible() && !runtime->directorSurface())
        return;
    if (ownsInput()) {
        if (runtime->typing && runtime->config["input"]["typing_cursor"] == true) {
            auto mon = runtime->monitor.lock();
            runtime->pointerX += float(delta.x) / float(mon->m_size.x);
            runtime->pointerY += float(delta.y) / float(mon->m_size.y);
            if ((runtime->pointerX < 0 || runtime->pointerX >= 1 || runtime->pointerY < 0 || runtime->pointerY >= 1) &&
                runtime->buttons.empty()) {
                auto global =
                    mon->m_position + Vector2D{runtime->pointerX * mon->m_size.x, runtime->pointerY * mon->m_size.y};
                for (auto &m : State::monitorState()->monitors())
                    if (m != mon && global.x >= m->m_position.x && global.y >= m->m_position.y &&
                        global.x < m->m_position.x + m->m_size.x && global.y < m->m_position.y + m->m_size.y) {
                        runtime->freeCursor = true;
                        runtime->deferInput([global] {
                            runtime->cursor();
                            Pointer::pointerController()->warpTo(global, true);
                        });
                        return;
                    }
            }
            runtime->pointerX = std::clamp(runtime->pointerX, 0.f, .9999f);
            runtime->pointerY = std::clamp(runtime->pointerY, 0.f, .9999f);
            return;
        }
        runtime->lookX += float(delta.x) * runtime->config["player"]["look_sensitivity"].get<float>();
        runtime->lookY -= float(delta.y) * runtime->config["player"]["look_sensitivity"].get<float>();
        return;
    }
    originalMove(self, delta);
}
Vec3 pointerRay() {
    auto forward = runtime->camera.forward(), right = normalized(cross(forward, {0, 1, 0})), up = cross(right, forward);
    float tan = std::tan(runtime->camera.fov * .01745329252f / 2),
          aspect = runtime->monitor ? runtime->monitor->m_pixelSize.x / float(runtime->monitor->m_pixelSize.y) : 1;
    return normalized(forward + right * (((runtime->typing ? runtime->pointerX : .5f) * 2 - 1) * tan * aspect) +
                      up * ((1 - (runtime->typing ? runtime->pointerY : .5f) * 2) * tan));
}
std::optional<std::pair<std::string, surfaces::Hit>> aim() {
    std::optional<std::pair<std::string, surfaces::Hit>> best;
    for (auto &surface : runtime->capture.visible())
        if (auto h = surfaces::intersect(surface, runtime->camera.position, pointerRay());
            h && (!best || h->distance < best->second.distance))
            best = {{surface.id, *h}};
    if (best && runtime->world)
        for (const auto &t : runtime->world->collision) {
            auto direction = pointerRay();
            auto edge1 = t.b - t.a, edge2 = t.c - t.a, p = cross(direction, edge2);
            float det = dot(edge1, p);
            if (std::abs(det) < 1e-7f)
                continue;
            float inv = 1 / det;
            auto offset = runtime->camera.position - t.a;
            float u = dot(offset, p) * inv;
            if (u < 0 || u > 1)
                continue;
            auto q = cross(offset, edge1);
            float v = dot(direction, q) * inv;
            if (v < 0 || u + v > 1)
                continue;
            float d = dot(edge2, q) * inv;
            if (d > 0 && d < best->second.distance - .01f)
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
        capture.refresh(monitor.lock(), world.get(), &layout, &camera);
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
    sim::resolveCapsule(test, world->collision);
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
        if (active && world)
            spatial["projection"] = {
                {"outputId", output},
                {"worldId", world->id},
                {"monotonicMs", std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch())
                                    .count()},
                {"markers",
                 world::projectMarkers(*world, camera, float(monitor->m_pixelSize.x) / monitor->m_pixelSize.y)}};
    }
    Json apps = Json::array();
    for (auto &e : capture.entries)
        if (e.parentId.empty())
            apps.push_back({{"surfaceId", e.surface.id},
                            {"workspaceId", e.instance.workspaceId},
                            {"title", e.instance.title},
                            {"appClass", e.instance.appClass},
                            {"icon", e.instance.appClass}});
    ipc->publish({{"interaction",
                   {{"mode", !layout.desktop.is_null() ? "workspace"
                             : fullscreen.phase == 2   ? "fullscreen"
                             : typing                  ? "typing"
                                                       : "movement"},
                    {"tool", tool},
                    {"tools", config["tools"]["slots"]},
                    {"noclip", noclip},
                    {"cursor", cursorState},
                    {"taggedSurfaceId", tagged.empty() ? Json(nullptr) : Json(tagged)}}},
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
                  {"integrations", {{"sources", control.sources}}}});
    for (auto &[event, params] : layout.events)
        ipc->notify(event, params);
    layout.events.clear();
    for (auto &[event, params] : control.events)
        ipc->notify(event, params);
    control.events.clear();
}
void Runtime::load(const std::string &id, const std::string &spawnId) {
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
    if (!loading || pendingWorld.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    loading = false;
    try {
        auto candidate = pendingWorld.get();
        if (loadCancelled || locked())
            throw ipc::Error(-32004, "world load cancelled");
        if (!ipc->healthy() || std::chrono::steady_clock::now() > loadDeadline)
            throw ipc::Error(-32006, "world load expired");
        auto spawn = pendingArea.empty() ? candidate->spawn(pendingSpawn) : world::areaSpawn(*candidate, pendingArea);
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        renderer.prepare(candidate);
        if (graphicsOverrides.contains("exposure"))
            renderer.profile["exposure"] = config["graphics"]["exposure"];
        if (graphicsOverrides.contains("bloom_strength"))
            renderer.profile["bloom"]["strength"] = config["graphics"]["bloom_strength"];
        world = std::move(candidate);
        capture.release();
        layout.freePoses.clear();
        restoreFullscreenWindows();
        fullscreen = {};
        handover = {};
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
        control.endLoad(false, loadCancelled || locked());
        publish();
        ipc->fail(e.code, e.what());
        error(e);
        toggleAfterLoad = false;
        pendingArea.clear();
    } catch (const std::exception &e) {
        control.endLoad(false, loadCancelled || locked());
        publish();
        ipc->fail(-32602, e.what());
        error(e);
        toggleAfterLoad = false;
        pendingArea.clear();
    }
}

void Runtime::toggle(const std::string &output) {
    if (active) {
        if (!layout.desktop.is_null()) {
            unfocus();
            return;
        }
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
    active = true;
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
    if (loading)
        loadCancelled = true;
    if (!active)
        return;
    active = false;
    pendingCursorHide.reset(); // an old hide request must not outlive cursor restoration
    restoreFullscreenWindows();
    fullscreen = {};
    handover = {};
    freeCursor = false;
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
    if (savedFocus && savedFocus->m_isMapped)
        Desktop::focusState()->rawWindowFocus(savedFocus.lock(), Desktop::FOCUS_REASON_OTHER);
    if (monitor)
        g_pHyprRenderer->damageMonitor(monitor.lock());
    publish();
}
void Runtime::focus(const std::string &id, bool keyboard) {
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
    if (!entry)
        throw ipc::Error(-32003, "unknown surface");
    auto w = entry->window.lock();
    if (keyboard && focused != id)
        cancelGrab();
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
        typing = true;
        keys.clear();
        smoothIntent = {};
        lookX = lookY = 0;
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
        if (hands.gesture == interaction::Gesture::Resize && e->window->m_target) {
            auto w = e->window.lock();
            auto box = w->m_target->position();
            double width = std::clamp(double(hands.pose.width / hands.start.width * hands.start.pixelWidth), 160.,
                                      monitor->m_size.x);
            double height = std::clamp(double(hands.pose.height / hands.start.height * hands.start.pixelHeight), 100.,
                                       monitor->m_size.y);
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
    if (!down && event.button == gestureButton) {
        auto id = hands.pose.id;
        auto g = hands.gesture;
        hands.end();
        gestureButton = 0;
        if (g == interaction::Gesture::Carry) {
            auto held = capture.find(id);
            std::optional<std::pair<int, float>> wall;
            for (auto &f : capture.workspaces)
                if (auto h = surfaces::intersect(f.surface, camera.position, pointerRay());
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
            if (auto h = surfaces::intersect(*plane, camera.position, pointerRay(), false))
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
                        auto h = surfaces::intersect(s, camera.position, pointerRay());
                        if (h)
                            hit = {{s.id, *h}};
                        break;
                    }
                }
        }
        if (!e || !e->parentId.empty())
            return;
        cancelGrab();
        typing = false;
        keys.clear();
        smoothIntent = {};
        if (layout.binding(e->instance.workspaceId, world.get()))
            layout.pull(e->instance, world.get());
        auto g = event.button == config["input"]["carry_button"].get<uint32_t>()    ? interaction::Gesture::Carry
                 : event.button == config["input"]["resize_button"].get<uint32_t>() ? interaction::Gesture::Resize
                                                                                    : interaction::Gesture::Roll;
        if (g == interaction::Gesture::Resize &&
            Fullscreen::controller()->isFullscreen(e->window.lock(), Fullscreen::FSMODE_MAXIMIZED))
            Fullscreen::controller()->setFullscreenMode(e->window.lock(), Fullscreen::FSMODE_NONE,
                                                        Fullscreen::FSMODE_NONE);
        hands.begin(g, e->surface, hit->second);
        layout.freePoses[e->surface.id] = e->surface;
        gestureButton = event.button;
        return;
    }
    if (down && config["tools"]["enabled"] == true && config["tools"]["slots"][tool - 1] == "mount") {
        if (hit && capture.find(hit->first)) {
            tagged = hit->first;
            layout.freePoses[tagged] = capture.find(tagged)->surface;
            layout.carried.clear();
            return;
        }
        if (!tagged.empty())
            if (auto e = capture.find(tagged))
                if (auto wall = interaction::raycast(camera.position, pointerRay(), *world)) {
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
    if (down && config["tools"]["enabled"] == true && config["tools"]["slots"][tool - 1] == "path")
        return;
    if (down && hit && capture.workspace(hit->first)) {
        focusWall(hit->first);
        return;
    }
    if (down && hit)
        if (auto t = capture.target(hit->first, hit->second.u, hit->second.v)) {
            if (buttons.empty()) {
                // A child press preserves its parent keyboard focus and popup grab serial.
                if (focused != t->parentId)
                    focus(t->parentId, true);
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
        zoom = std::clamp(zoom * std::pow(config["windows"]["wheel_factor"].get<float>(), steps), 1.f, 10.f);
        return;
    }
    if (hands.gesture == interaction::Gesture::Carry) {
        hands.wheel(steps * std::log(config["windows"]["wheel_factor"].get<float>()) / std::log(1.06f));
        return;
    }
    auto hit = aim();
    if (!hit)
        return;
    if (superHeld && !typing)
        if (auto e = capture.find(hit->first)) {
            hands.begin(interaction::Gesture::Carry, e->surface, hit->second);
            hands.wheel(steps);
            handTick(.1f);
            hands.end();
            return;
        }
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
    if (fullscreen.phase && !capture.find(fullscreen.id)) {
        restoreFullscreenWindows();
        fullscreen = {};
        cursor();
        return;
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
        s.yaw += (fullscreen.to.yaw - s.yaw) * t;
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
                layout.freePoses[fullscreen.id] = fullscreen.before;
                restoreFullscreenWindows();
                fullscreen = {};
                typing = false;
            }
        }
    }
}
std::optional<Json> Runtime::method(const std::string &method, const Json &wire) {
    Json params = wire;
    std::string actor = params.value("_actor", ""), client = params.value("_client", "");
    params.erase("_actor");
    params.erase("_client");
    int peerPid = params.value("_pid", -1);
    auto protocol = params.value("_protocol", "0.1");
    params.erase("_pid");
    params.erase("_protocol");
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
        if (protocol != "0.5")
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
            (protocol != "0.3" && protocol != "0.4" && protocol != "0.5"))
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
void Runtime::tick() {
    if (renderDepth != 0)
        throw std::runtime_error("deferred work inside compositor render pass");
    ipc->poll();
    if (!ipc->healthy()) {
        if (active)
            exit();
        return;
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
        if (!grabbed.empty() && !capture.find(grabbed))
            cancelGrab();
        sim::Intent intent;
        if (!typing && !handover.phase && fullscreen.phase == 0) {
            intent.forward = float(keys.contains(KEY_W)) - float(keys.contains(KEY_S));
            intent.right = float(keys.contains(KEY_D)) - float(keys.contains(KEY_A));
            intent.up = float(keys.contains(KEY_SPACE)) - float(keys.contains(KEY_LEFTCTRL));
            intent.jump = keys.contains(KEY_SPACE);
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
        camera.fov = std::max(30.f, baseFov / (zoomHeld ? zoom : 1.f));
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
                    movement->step(camera, intent, 1.f / 60.f, world->collision);
            }
            accumulator -= 1.0 / 60.0;
        }
        if (config["player"]["respawn_below"].get<float>() > 0 &&
            camera.position.y < world->spawn().position.y + 1.7f - config["player"]["respawn_below"].get<float>()) {
            auto s = world->spawn();
            camera.position = s.position + Vec3{0, 1.7f, 0};
            camera.verticalSpeed = 0;
            keys.clear();
            smoothIntent = {};
            cancelGrab();
        }
        handTick(dt);
        refresh();
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
        renderer.pointerX = typing ? pointerX : .5f;
        renderer.pointerY = typing ? pointerY : .5f;
        auto pointerHit = aim();
        if (!grabbed.empty())
            if (auto plane = capture.plane(grabbed))
                if (auto h = surfaces::intersect(*plane, camera.position, pointerRay(), false))
                    pointerHit = {{grabbed, *h}};
        if (typing) {
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
    cursor();
    publish();
    ipc->poll();
}
class Pass final : public IPassElement {
  public:
    std::vector<UP<IPassElement>> draw() override {
        safe([] {
            if (!runtime || !runtime->active || locked() || !runtime->layout.desktop.is_null() ||
                runtime->fullscreen.phase == 2)
                return;
            auto &data = g_pHyprRenderer->m_renderData;
            auto *fb = dynamic_cast<Render::GL::CGLFramebuffer *>(data.currentFB.get());
            if (!fb)
                return;
            auto surfaces = runtime->capture.visible();
            if (runtime->fullscreen.phase == 1 || runtime->fullscreen.phase == 3) {
                auto fs = runtime->capture.find(runtime->fullscreen.id);
                if (fs) {
                    std::erase_if(surfaces, [&](auto &s) { return s.id == fs->surface.id; });
                    surfaces.push_back(fs->surface);
                }
            }
            runtime->renderer.draw(fb->getFBID(), data.currentFB->m_size.x, data.currentFB->m_size.y, runtime->camera,
                                   surfaces, runtime->typing);
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
        if (old["graphics"]["fov"] != next["graphics"]["fov"]) {
            runtime->baseFov = next["graphics"]["fov"];
            runtime->camera.fov = runtime->baseFov;
        }
        if (old["graphics"]["quality"] != next["graphics"]["quality"]) {
            Render::GL::g_pHyprOpenGL->makeEGLCurrent();
            runtime->renderer.intelQuality =
                next["graphics"]["quality"] == "intel" ||
                (next["graphics"]["quality"] == "auto" &&
                 std::string(reinterpret_cast<const char *>(glGetString(GL_RENDERER))).find("Intel") !=
                     std::string::npos);
            if (runtime->world)
                runtime->renderer.prepare(runtime->world);
        }
        if (!runtime->renderer.profile.is_null()) {
            if (runtime->graphicsOverrides.contains("exposure"))
                runtime->renderer.profile["exposure"] = next["graphics"]["exposure"];
            if (runtime->graphicsOverrides.contains("bloom_strength"))
                runtime->renderer.profile["bloom"]["strength"] = next["graphics"]["bloom_strength"];
        }
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
    Json j = {{"position", {runtime->camera.position.x, runtime->camera.position.y, runtime->camera.position.z}},
              {"yaw", runtime->camera.yaw},
              {"pitch", runtime->camera.pitch},
              {"frames", runtime->renderer.frames},
              {"grabbed", runtime->grabbed},
              {"gesture", int(runtime->hands.gesture)},
              {"superHeld", runtime->superHeld},
              {"ownsInput", ownsInput()},
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
              {"active", runtime->active}};
    j["childSurfaces"] = Json::array();
    for (auto &child : runtime->capture.children)
        j["childSurfaces"].push_back(
            {{"id", child.surface.id},
             {"parentId", child.parentId},
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
    runtime = std::make_unique<Runtime>();
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
                    {"camera.fov", "movement.control"},
                    {"shell.bind", "shell.control"},
                    {"workspace.activate", "world.control"},
                    {"world.load", "world.control"},
                    {"surface.focus", "surface.control"},
                    {"movement.set", "movement.control"},
                    {"runtime.exit", "runtime.control"}});
    runtime->ipc->advertise(std::move(methods));
    HyprlandAPI::addDispatcherV2(handle, "hyprune.toggle", [](std::string arg) {
        deferToggle(arg);
        return SDispatchResult{};
    });
    HyprlandAPI::addLuaFunction(handle, "hyprune", "toggle", luaToggle);
    HyprlandAPI::addLuaFunction(handle, "hyprune", "camera", luaCamera);
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
    if (!originalKeyboardInput)
        throw std::runtime_error("Hyprune: event keyboard hook unavailable");
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
            if (runtime && runtime->active && runtime->typing && runtime->freeCursor && runtime->monitor) {
                auto m = runtime->monitor.lock();
                if (pos.x >= m->m_position.x && pos.y >= m->m_position.y && pos.x < m->m_position.x + m->m_size.x &&
                    pos.y < m->m_position.y + m->m_size.y) {
                    runtime->freeCursor = false;
                    runtime->pointerX = (pos.x - m->m_position.x) / m->m_size.x;
                    runtime->pointerY = (pos.y - m->m_position.y) / m->m_size.y;
                    runtime->deferInput([] { runtime->cursor(); });
                    info.cancelled = true;
                }
            }
            if (ownsInput() || (runtime->active && runtime->monitor == Desktop::focusState()->monitor() &&
                                runtime->layout.desktop.is_null() && runtime->fullscreen.phase != 2 &&
                                !runtime->freeCursor && !runtime->directorSurface()))
                info.cancelled = true;
        }));
    runtime->listeners.push_back(Event::bus()->m_events.input.keyboard.key.listen([](IKeyboard::SKeyEvent event,
                                                                                     Event::SCallbackInfo &info) {
        safe([&] {
            bool down = event.state == WL_KEYBOARD_KEY_STATE_PRESSED;
            if (!down && runtime->swallowedKeys.erase(event.keycode)) {
                info.cancelled = true;
                runtime->keys.erase(event.keycode);
            }
            if (!runtime->active || locked() || runtime->monitor != Desktop::focusState()->monitor())
                return;
            auto kb = runtime->eventKeyboard.lock();
            if (!kb)
                kb = g_pSeatManager->m_keyboard.lock();
            if (!kb || !kb->m_xkbSymState)
                return;
            auto sym = xkb_state_key_get_one_sym(kb->m_xkbSymState, event.keycode + 8);
            if (sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R)
                runtime->superHeld = down;
            if (sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R)
                runtime->altHeld = down;
            auto consume = [&] {
                info.cancelled = true;
                if (down)
                    runtime->swallowedKeys.insert(event.keycode);
            };
            if (sym == XKB_KEY_F12) {
                consume();
                if (down)
                    deferToggle("");
                return;
            }
            if (down && runtime->superHeld && runtime->altHeld &&
                (sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R || sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R)) {
                consume();
                runtime->deferInput([] { runtime->modeToggle(); });
                return;
            }
            if (!runtime->layout.desktop.is_null() || runtime->fullscreen.phase == 2)
                return;
            if (sym == XKB_KEY_Escape) {
                consume();
                if (down)
                    runtime->deferInput([] {
                        if (runtime->loading)
                            runtime->loadCancelled = true;
                        runtime->control.stop();
                        runtime->cancelGrab();
                        runtime->typing = false;
                        runtime->freeCursor = false;
                        runtime->focused.clear();
                        runtime->tagged.clear();
                        runtime->keys.clear();
                        runtime->smoothIntent = {};
                        runtime->lookX = runtime->lookY = 0;
                        runtime->cursor();
                    });
                return;
            }
            if (runtime->loading || runtime->handover.phase || runtime->fullscreen.phase) {
                consume();
                runtime->keys.clear();
                return;
            }
            if (runtime->control.uiVisible()) {
                auto surface = runtime->control.exclusive() ? runtime->directorSurface() : nullptr;
                if (!surface)
                    consume();
                else
                    runtime->deferInput([surface] { g_pSeatManager->setKeyboardFocus(surface); });
                return;
            }
            std::string action;
            for (auto &[name, key] : runtime->config["input"]["bindings"].items())
                if (sym == xkb_keysym_from_name(key.get<std::string>().c_str(), XKB_KEYSYM_CASE_INSENSITIVE))
                    action = name;
            uint32_t move = action == "forward"      ? KEY_W
                            : action == "back"       ? KEY_S
                            : action == "left"       ? KEY_A
                            : action == "right"      ? KEY_D
                            : action == "jump"       ? KEY_SPACE
                            : action == "down"       ? KEY_LEFTCTRL
                            : sym == XKB_KEY_Shift_L ? KEY_LEFTSHIFT
                                                     : 0;
            // Releases clear semantic movement even when mode or modifiers changed.
            if (!down && move)
                runtime->keys.erase(move);
            if (runtime->typing) {
                bool modified = runtime->superHeld || runtime->altHeld || sym == XKB_KEY_Super_L ||
                                sym == XKB_KEY_Super_R || sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R;
                consume();
                if (runtime->focused.empty() || runtime->freeCursor)
                    return;
                runtime->deferInput([event, kb, modified] {
                    if (modified && !g_pKeybindManager->onKeyEvent(event, kb))
                        return;
                    if (!runtime->typing)
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
                    else
                        runtime->appKeys.erase(event.keycode);
                });
                return;
            }
            if (runtime->superHeld || runtime->altHeld || sym == XKB_KEY_Super_L || sym == XKB_KEY_Super_R ||
                sym == XKB_KEY_Alt_L || sym == XKB_KEY_Alt_R) {
                consume();
                runtime->keys.clear();
                runtime->smoothIntent = {};
                runtime->deferInput([event, kb] { g_pKeybindManager->onKeyEvent(event, kb); });
                return;
            }
            if (sym == XKB_KEY_Tab) {
                consume();
                runtime->control.hold(down);
                return;
            }
            if (runtime->config["tools"]["enabled"] == true && sym >= XKB_KEY_1 && sym <= XKB_KEY_5) {
                consume();
                if (down) {
                    runtime->tool = int(sym - XKB_KEY_1) + 1;
                    runtime->tagged.clear();
                }
                return;
            }
            if (action == "zoom") {
                consume();
                runtime->zoomHeld = down;
                if (down)
                    runtime->zoom = 2.f;
                return;
            }
            if (action == "focus") {
                consume();
                if (down)
                    runtime->deferInput([] {
                        if (auto hit = aim())
                            if (runtime->capture.workspace(hit->first))
                                runtime->focusWall(hit->first);
                    });
                return;
            }
            if (action == "noclip") {
                consume();
                if (down)
                    runtime->deferInput([] {
                        runtime->noclip = !runtime->noclip;
                        runtime->keys.clear();
                        if (!runtime->noclip && runtime->world) {
                            auto candidate = runtime->camera;
                            sim::resolveCapsule(candidate, runtime->world->collision);
                            if (length(candidate.position - runtime->camera.position) > .2f) {
                                auto sp = runtime->world->spawn();
                                candidate.position = sp.position + Vec3{0, 1.7f, 0};
                            }
                            runtime->camera = candidate;
                        }
                    });
                return;
            }
            if (action == "fly") {
                consume();
                if (down)
                    runtime->deferInput([] {
                        runtime->movement = runtime->movement == &runtime->walk
                                                ? static_cast<sim::MotionStrategy *>(&runtime->fly)
                                                : &runtime->walk;
                        runtime->keys.clear();
                        runtime->smoothIntent = {};
                        runtime->camera.verticalSpeed = 0;
                    });
                return;
            }
            if (move) {
                consume();
                if (down) {
                    runtime->control.stop();
                    runtime->keys.insert(move);
                } else
                    runtime->keys.erase(move);
                return;
            }
            consume(); // World navigation must never type into an occluded desktop client.
        });
    }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.mouse.button.listen([](IPointer::SButtonEvent event, Event::SCallbackInfo &info) {
            bool down = event.state == WL_POINTER_BUTTON_STATE_PRESSED;
            bool held = !down && runtime->swallowedButtons.erase(event.button);
            bool back = runtime->active && runtime->monitor == Desktop::focusState()->monitor() &&
                        event.button == runtime->config["input"]["typing_button"].get<uint32_t>() &&
                        !runtime->layout.desktop.is_null();
            bool world = runtime->active && runtime->monitor == Desktop::focusState()->monitor() &&
                         runtime->layout.desktop.is_null() && runtime->fullscreen.phase != 2 && !runtime->freeCursor &&
                         !runtime->directorSurface();
            if (!held && !world && !back)
                return;
            info.cancelled = true;
            if (down)
                runtime->swallowedButtons.insert(event.button);
            bool super = runtime->superHeld;
            runtime->deferInput([event, super, back] {
                if (back) {
                    if (event.state == WL_POINTER_BUTTON_STATE_PRESSED)
                        runtime->unfocus();
                    return;
                }
                runtime->button(event, super);
            });
        }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.mouse.axis.listen([](IPointer::SAxisEvent event, Event::SCallbackInfo &info) {
            bool world = runtime->active && runtime->monitor == Desktop::focusState()->monitor() &&
                         runtime->layout.desktop.is_null() && runtime->fullscreen.phase != 2 && !runtime->freeCursor &&
                         !runtime->directorSurface();
            if (!world)
                return;
            info.cancelled = true;
            runtime->deferInput([event] { runtime->axis(event); });
        }));
    runtime->listeners.push_back(Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR monitor) {
        safe([&] {
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
    if (runtime->cursorHook)
        HyprlandAPI::removeFunctionHook(handle, runtime->cursorHook);
    originalHideCursor = nullptr;
    originalKeyboardInput = nullptr;
    originalMove = nullptr;
    if (Render::GL::g_pHyprOpenGL) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        runtime->renderer.destroy();
    }
    runtime.reset();
}
