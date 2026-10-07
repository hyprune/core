#include "control/Control.hpp"
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/SessionLockManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/plugins/HookSystem.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprutils/memory/UniquePtr.hpp>
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
    std::string pendingSpawn, pendingToggle;
    bool toggleAfterLoad = false;
    std::chrono::steady_clock::time_point loadDeadline;
    double accumulator = 0;
    std::string focused, lastError, grabbed;
    std::set<uint32_t> buttons;
    void cancelGrab();
    std::set<uint32_t> keys;
    SP<CEventLoopTimer> timer;
    std::vector<CHyprSignalListener> listeners;
    CFunctionHook *motionHook = nullptr;
    uint64_t deferred = 0, tickDeferred = 0;
    std::chrono::steady_clock::time_point lastTick = std::chrono::steady_clock::now();
    std::filesystem::path installed;
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
bool ownsInput() {
    return runtime && runtime->active && !runtime->loading && !runtime->control.uiVisible() && !locked() &&
           runtime->monitor == Desktop::focusState()->monitor();
}
void motion(void *self, const Vector2D &delta) {
    if (ownsInput()) {
        runtime->camera.yaw += float(delta.x) * .0025f;
        runtime->camera.pitch = std::clamp(runtime->camera.pitch - float(delta.y) * .0025f, -1.48f, 1.48f);
        return;
    }
    originalMove(self, delta);
}
std::optional<std::pair<std::string, surfaces::Hit>> aim() {
    std::optional<std::pair<std::string, surfaces::Hit>> best;
    for (auto &entry : runtime->capture.entries)
        if (auto h = surfaces::intersect(entry.surface, runtime->camera.position, runtime->camera.forward());
            h && (!best || h->distance < best->second.distance))
            best = {{entry.surface.id, *h}};
    if (best && runtime->world)
        for (const auto &t : runtime->world->collision) {
            auto direction = runtime->camera.forward();
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
void Runtime::cancelGrab() {
    if (!buttons.empty() && capture.find(grabbed)) {
        auto now =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
        for (auto button : buttons)
            g_pSeatManager->sendPointerButton(now, button, WL_POINTER_BUTTON_STATE_RELEASED);
        g_pSeatManager->sendPointerFrame();
    }
    buttons.clear();
    grabbed.clear();
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
    float sy = std::sin(-camera.yaw / 2), cy = std::cos(-camera.yaw / 2), sp = std::sin(camera.pitch / 2),
          cp = std::cos(camera.pitch / 2);
    ipc->publish({{"runtime",
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
        const auto spawn = candidate->spawn(pendingSpawn);
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        renderer.prepare(candidate);
        world = std::move(candidate);
        auto transition = control.location["transition"];
        control.reset(world.get());
        control.location["transition"] = transition;
        control.endLoad(true);
        world->defaultSpawn = spawn.id;
        camera = {};
        camera.position = spawn.position + Vec3{0, 1.7f, 0};
        camera.yaw = spawn.yaw;
        camera.pitch = spawn.pitch;
        movement = world->collision.empty() ? static_cast<sim::MotionStrategy *>(&fly) : &walk;
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
    } catch (const std::exception &e) {
        control.endLoad(false, loadCancelled || locked());
        publish();
        ipc->fail(-32602, e.what());
        error(e);
        toggleAfterLoad = false;
    }
}

void Runtime::toggle(const std::string &output) {
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
    monitor = chosen;
    control.outputId = "output-" + std::to_string(chosen->m_id);
    savedFocus = Desktop::focusState()->window();
    active = true;
    typing = false;
    keys.clear();
    lastTick = std::chrono::steady_clock::now();
    // Keep Hyprland's cursor lifecycle untouched. A frozen native cursor and
    // the rendered reticle are intentional in M0; hardware cursor rendering
    // must be tested on a dedicated DRM session before adding cursor gates.
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    capture.refresh(chosen);
    publish();
}
void Runtime::exit() {
    if (loading)
        loadCancelled = true;
    if (!active)
        return;
    active = false;
    control.stop();
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
    auto *entry = capture.find(id);
    if (!entry)
        throw ipc::Error(-32003, "unknown surface");
    auto w = entry->window.lock();
    if (keyboard && w->m_workspace && !w->m_workspace->isVisible())
        throw ipc::Error(-32007, "hidden workspace keyboard focus is unsupported");
    if (keyboard) {
        Desktop::focusState()->rawWindowFocus(w, Desktop::FOCUS_REASON_CLICK);
        g_pSeatManager->setKeyboardFocus(w->resource());
        typing = true;
        keys.clear();
        focused = id;
    }
    g_pSeatManager->setPointerFocus(w->resource(), {entry->surface.pixelWidth * .5, entry->surface.pixelHeight * .5});
    publish();
}
std::optional<Json> Runtime::method(const std::string &method, const Json &wire) {
    Json params = wire;
    std::string actor = params.value("_actor", ""), client = params.value("_client", "");
    params.erase("_actor");
    params.erase("_client");
    if (method == "_operation.cancel") {
        loadCancelled = true;
        return Json::object();
    }
    if (method == "_connection.closed") {
        control.disconnect(actor);
        publish();
        return Json::object();
    }
    if (locked())
        throw ipc::Error(-32005, "session locked");
    if (control::Control::methods().contains(method)) {
        if ((method == "travel.start" || method == "autodrive.start") && !active)
            throw ipc::Error(-32005, "world mode inactive");
        auto outputs = monitor ? Json::array({"output-" + std::to_string(monitor->m_id)}) : Json::array();
        auto result = control.execute(method, params, actor, client, world.get(), camera, outputs);
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
    finishLoad();
    if (locked() && active)
        exit();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;
    control.tick(dt, camera, world.get());
    if (control.uiVisible()) {
        keys.clear();
        cancelGrab();
        typing = false;
    }
    if (active && monitor) {
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
        if (!typing) {
            intent.forward = float(keys.contains(KEY_W)) - float(keys.contains(KEY_S));
            intent.right = float(keys.contains(KEY_D)) - float(keys.contains(KEY_A));
            intent.up = float(keys.contains(KEY_SPACE)) - float(keys.contains(KEY_LEFTCTRL));
            intent.jump = keys.contains(KEY_SPACE);
        }
        accumulator = std::min(accumulator + double(dt), 4.0 / 60.0);
        while (accumulator >= 1.0 / 60.0) {
            if (!loading && !control.driving() && !control.uiVisible())
                movement->step(camera, intent, 1.f / 60.f, world->collision);
            accumulator -= 1.0 / 60.0;
        }
        if (camera.position.y < -30) {
            auto s = world->spawn();
            camera.position = s.position + Vec3{0, 1.7f, 0};
            camera.verticalSpeed = 0;
        }
        capture.refresh(monitor.lock());
        if (!focused.empty() && !capture.find(focused)) {
            typing = false;
            focused.clear();
            keys.clear();
        }
        if (typing)
            if (auto hit = aim())
                if (auto *e = capture.find(hit->first)) {
                    auto r = e->window->resource();
                    g_pSeatManager->setPointerFocus(
                        r, {hit->second.u * e->surface.pixelWidth, hit->second.v * e->surface.pixelHeight});
                    g_pSeatManager->sendPointerMotion(
                        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count(),
                        {hit->second.u * e->surface.pixelWidth, hit->second.v * e->surface.pixelHeight});
                    g_pSeatManager->sendPointerFrame();
                }
        for (auto &e : capture.entries)
            e.surface.focused = e.surface.id == focused;
        g_pHyprRenderer->damageMonitor(monitor.lock());
        monitor->scheduleFrame();
    }
    publish();
    ipc->poll();
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
            std::vector<surfaces::Surface> surfaces;
            for (auto &e : runtime->capture.entries)
                surfaces.push_back(e.surface);
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
              {"error", runtime->lastError},
              {"active", runtime->active}};
    j["captures"] = Json::array();
    for (auto &e : runtime->capture.entries)
        j["captures"].push_back({{"id", e.surface.id},
                                 {"title", e.surface.title},
                                 {"commits", *e.commits},
                                 {"position", {e.surface.center.x, e.surface.center.y, e.surface.center.z}}});
    lua_pushstring(state, j.dump().c_str());
    return 1;
}

} // namespace
} // namespace hyprune::platform
using namespace hyprune::platform;
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
    auto methods = hyprune::control::Control::methods();
    methods.insert({{"world.load", "world.control"},
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
    for (auto &match : HyprlandAPI::findFunctionsByName(handle, "CPointerManager4move"))
        if (match.demangled.find("CPointerManager::move(") != std::string::npos) {
            runtime->motionHook = HyprlandAPI::createFunctionHook(handle, match.address, (void *)motion);
            if (runtime->motionHook && runtime->motionHook->hook())
                originalMove = (Move)runtime->motionHook->m_original;
            break;
        }
    if (!originalMove)
        throw std::runtime_error("Hyprune: pointer motion hook unavailable");
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
        Event::bus()->m_events.input.mouse.move.listen([](Vector2D, Event::SCallbackInfo &info) {
            if (ownsInput())
                info.cancelled = true;
        }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.keyboard.key.listen([](IKeyboard::SKeyEvent event, Event::SCallbackInfo &info) {
            safe([&] {
                if (!runtime->active || locked() || runtime->monitor != Desktop::focusState()->monitor())
                    return;
                auto keyboard = g_pSeatManager->m_keyboard.lock();
                if (!keyboard || !keyboard->m_xkbSymState)
                    return;
                auto sym = xkb_keysym_to_lower(xkb_state_key_get_one_sym(keyboard->m_xkbSymState, event.keycode + 8));
                if (sym == XKB_KEY_Tab && !runtime->typing) {
                    runtime->control.hold(event.state == WL_KEYBOARD_KEY_STATE_PRESSED);
                    info.cancelled = true;
                    return;
                }
                uint32_t action = 0;
                switch (sym) {
                case XKB_KEY_w:
                    action = KEY_W;
                    break;
                case XKB_KEY_s:
                    action = KEY_S;
                    break;
                case XKB_KEY_a:
                    action = KEY_A;
                    break;
                case XKB_KEY_d:
                    action = KEY_D;
                    break;
                case XKB_KEY_space:
                    action = KEY_SPACE;
                    break;
                case XKB_KEY_Control_L:
                    action = KEY_LEFTCTRL;
                    break;
                case XKB_KEY_Escape:
                    action = KEY_ESC;
                    break;
                case XKB_KEY_F12:
                    action = KEY_F12;
                    break;
                default:
                    break;
                }
                if (action == KEY_ESC) {
                    info.cancelled = true;
                    if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                        if (runtime->loading)
                            runtime->loadCancelled = true;
                        runtime->control.stop();
                        runtime->cancelGrab();
                        runtime->typing = false;
                        runtime->focused.clear();
                        runtime->keys.clear();
                    }
                    return;
                }
                if (action == KEY_F12) {
                    info.cancelled = true;
                    if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED)
                        deferToggle("");
                    return;
                }
                if (runtime->loading) {
                    info.cancelled = true;
                    return;
                }
                if (runtime->control.uiVisible())
                    return;
                if (action && event.state == WL_KEYBOARD_KEY_STATE_PRESSED && runtime->control.driving())
                    runtime->control.stop();
                if (runtime->typing)
                    return;
                if (xkb_state_mod_name_is_active(keyboard->m_xkbState, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) ||
                    xkb_state_mod_name_is_active(keyboard->m_xkbState, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE)) {
                    runtime->keys.clear();
                    return;
                }
                if (action == KEY_W || action == KEY_A || action == KEY_S || action == KEY_D || action == KEY_SPACE ||
                    action == KEY_LEFTCTRL) {
                    info.cancelled = true;
                    if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED)
                        runtime->keys.insert(action);
                    else
                        runtime->keys.erase(action);
                }
            });
        }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.mouse.button.listen([](IPointer::SButtonEvent event, Event::SCallbackInfo &info) {
            safe([&] {
                if (!ownsInput())
                    return;
                info.cancelled = true;
                auto hit = aim();
                if (!runtime->grabbed.empty())
                    if (auto *held = runtime->capture.find(runtime->grabbed)) {
                        if (auto h = hyprune::surfaces::intersect(held->surface, runtime->camera.position,
                                                                  runtime->camera.forward(), false))
                            hit = {{held->surface.id, *h}};
                    }
                if (event.state == WL_POINTER_BUTTON_STATE_PRESSED && hit) {
                    if (runtime->grabbed.empty()) {
                        runtime->focus(hit->first, true);
                        runtime->grabbed = hit->first;
                    }
                    runtime->buttons.insert(event.button);
                }
                if (!runtime->grabbed.empty())
                    if (auto *e = runtime->capture.find(runtime->grabbed)) {
                        if (hit && hit->first == runtime->grabbed)
                            g_pSeatManager->setPointerFocus(
                                e->window->resource(),
                                {hit->second.u * e->surface.pixelWidth, hit->second.v * e->surface.pixelHeight});
                        g_pSeatManager->sendPointerButton(event.timeMs, event.button, event.state);
                        g_pSeatManager->sendPointerFrame();
                    }
                if (event.state == WL_POINTER_BUTTON_STATE_RELEASED) {
                    runtime->buttons.erase(event.button);
                    if (runtime->buttons.empty())
                        runtime->grabbed.clear();
                }
            });
        }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.mouse.axis.listen([](IPointer::SAxisEvent event, Event::SCallbackInfo &info) {
            safe([&] {
                if (!ownsInput())
                    return;
                info.cancelled = true;
                if (runtime->control.uiVisible())
                    return;
                if (runtime->typing) {
                    g_pSeatManager->sendPointerAxis(event.timeMs, event.axis, event.delta, event.deltaDiscrete / 120,
                                                    event.deltaDiscrete, event.source, event.relativeDirection);
                    g_pSeatManager->sendPointerFrame();
                }
            });
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
    return {"hyprune-core", "World-driven desktop runtime", "Hyprune contributors", "0.0.1"};
}
APICALL EXPORT void PLUGIN_EXIT() {
    if (!runtime)
        return;
    if (runtime->deferred)
        g_pEventLoopManager->removeDoLater(runtime->deferred);
    if (runtime->tickDeferred)
        g_pEventLoopManager->removeDoLater(runtime->tickDeferred);
    runtime->listeners.clear();
    if (runtime->timer)
        g_pEventLoopManager->removeTimer(runtime->timer);
    runtime->timer.reset();
    if (g_pHyprRenderer)
        g_pHyprRenderer->m_renderPass.removeAllOfType("Hyprune");
    safe([] { runtime->exit(); });
    if (runtime->motionHook)
        HyprlandAPI::removeFunctionHook(handle, runtime->motionHook);
    originalMove = nullptr;
    if (Render::GL::g_pHyprOpenGL) {
        Render::GL::g_pHyprOpenGL->makeEGLCurrent();
        runtime->renderer.destroy();
    }
    runtime.reset();
}
