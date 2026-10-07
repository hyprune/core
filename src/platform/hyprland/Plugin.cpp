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
#include "ipc/Server.hpp"
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
    sim::Walk walk;
    sim::Fly fly;
    sim::MotionStrategy *movement = &walk;
    std::shared_ptr<world::World> world;
    std::unique_ptr<ipc::Server> ipc;
    PHLMONITORREF monitor;
    PHLWINDOWREF savedFocus;
    bool active = false, typing = false, loading = false;
    std::string focused, lastError;
    std::set<uint32_t> keys;
    SP<CEventLoopTimer> timer;
    std::vector<CHyprSignalListener> listeners;
    CFunctionHook *motionHook = nullptr;
    uint64_t deferred = 0;
    std::chrono::steady_clock::time_point lastTick = std::chrono::steady_clock::now();
    std::filesystem::path installed;
    void publish();
    void toggle(const std::string &output = "");
    void exit();
    void tick();
    void focus(const std::string &, bool keyboard);
    void load(const std::string &, const std::string &spawn = "");
    Json method(const std::string &, const Json &);
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
void motion(void *self, const Vector2D &delta) {
    if (runtime && runtime->active && !locked()) {
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
    return best;
}
void Runtime::publish() {
    if (!ipc)
        return;
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
                   {{"modeId", movement == &walk ? "org.hyprune.walk" : "org.hyprune.fly"},
                    {"position", {camera.position.x, camera.position.y, camera.position.z}},
                    {"orientation", {cy * sp, sy * cp, -sy * sp, cy * cp}}}},
                  {"shell", {{"leases", Json::array()}}}});
}
void Runtime::load(const std::string &id, const std::string &spawnId) {
    if (id.empty() || id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") != std::string::npos ||
        id == "." || id == "..")
        throw ipc::Error(-32602, "invalid installed world ID");
    auto candidate = world::load(installed / id);
    if (candidate->id != id)
        throw ipc::Error(-32602, "installed ID mismatch");
    const auto spawn = candidate->spawn(spawnId);
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    renderer.prepare(candidate);
    world = std::move(candidate);
    world->defaultSpawn = spawn.id;
    camera = {};
    camera.position = spawn.position + Vec3{0, 1.7f, 0};
    camera.yaw = spawn.yaw;
    camera.pitch = spawn.pitch;
    movement = world->collision.empty() ? static_cast<sim::MotionStrategy *>(&fly) : &walk;
    publish();
}
void Runtime::toggle(const std::string &output) {
    if (active) {
        exit();
        return;
    }
    if (locked())
        return;
    if (!world)
        load("org.hyprune.atrium");
    PHLMONITOR chosen = Desktop::focusState()->monitor();
    if (!output.empty())
        for (auto &m : State::monitorState()->monitors())
            if (m->m_name == output)
                chosen = m;
    if (!chosen)
        throw std::runtime_error("no output");
    monitor = chosen;
    savedFocus = Desktop::focusState()->window();
    active = true;
    typing = false;
    keys.clear();
    lastTick = std::chrono::steady_clock::now();
    // This entry is only called by the event loop or deferred dispatcher.
    // setCursorHidden can recursively render on hardware-cursor outputs.
    g_pHyprRenderer->setCursorHidden(true);
    capture.refresh(chosen);
    publish();
}
void Runtime::exit() {
    if (!active)
        return;
    active = false;
    typing = false;
    focused.clear();
    keys.clear();
    capture.release();
    g_pHyprRenderer->setCursorHidden(false);
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
Json Runtime::method(const std::string &method, const Json &params) {
    if (locked())
        throw ipc::Error(-32005, "session locked");
    if (method == "runtime.exit") {
        if (!params.empty())
            throw ipc::Error(-32602, "unexpected params");
        exit();
    } else if (method == "world.load") {
        if (!params.contains("worldId") || params.size() > (params.contains("spawnId") ? 2u : 1u))
            throw ipc::Error(-32602, "invalid load params");
        load(params.at("worldId"), params.value("spawnId", ""));
    } else if (method == "movement.set") {
        if (params.size() != 1)
            throw ipc::Error(-32602, "invalid movement params");
        std::string mode = params.at("modeId");
        if (mode == "org.hyprune.walk" && world && !world->collision.empty())
            movement = &walk;
        else if (mode == "org.hyprune.fly")
            movement = &fly;
        else
            throw ipc::Error(-32007, "unsupported movement mode");
        camera.verticalSpeed = 0;
    } else if (method == "surface.focus") {
        if (params.size() != 2 || (params.at("intent") != "pointer" && params.at("intent") != "keyboard"))
            throw ipc::Error(-32602, "invalid focus params");
        focus(params.at("surfaceId"), params.at("intent") == "keyboard");
    }
    publish();
    return {{"revision", ipc->revision()}};
}
void Runtime::tick() {
    if (locked() && active)
        exit();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;
    if (active && monitor) {
        sim::Intent intent;
        if (!typing) {
            intent.forward = float(keys.contains(KEY_W)) - float(keys.contains(KEY_S));
            intent.right = float(keys.contains(KEY_D)) - float(keys.contains(KEY_A));
            intent.up = float(keys.contains(KEY_SPACE)) - float(keys.contains(KEY_LEFTCTRL));
            intent.jump = keys.contains(KEY_SPACE);
        }
        movement->step(camera, intent, dt, world->collision);
        if (camera.position.y < -30) {
            auto s = world->spawn();
            camera.position = s.position + Vec3{0, 1.7f, 0};
            camera.verticalSpeed = 0;
        }
        capture.refresh(monitor.lock());
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
    lua_pushstring(state, j.dump().c_str());
    return 1;
}
std::vector<ipc::LaunchGrant> grants() {
    std::vector<ipc::LaunchGrant> result;
    const char *fdEnv = getenv("HYPRUNE_LAUNCH_FD");
    if (!fdEnv)
        return result;
    int fd = std::stoi(fdEnv);
    char buffer[8192];
    ssize_t size = read(fd, buffer, sizeof buffer);
    close(fd);
    unsetenv("HYPRUNE_LAUNCH_FD");
    if (size <= 0)
        return result;
    auto j = Json::parse(buffer, buffer + size);
    for (auto &t : j)
        result.push_back({t.at("token"), t.at("client"), t.at("capabilities").get<std::set<std::string>>()});
    return result;
}
} // namespace
} // namespace hyprune::platform
using namespace hyprune::platform;
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}
APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE pluginHandle) {
    if (__hyprland_api_get_hash() != __hyprland_api_get_client_hash())
        throw std::runtime_error("Hyprune: Hyprland ABI mismatch");
    handle = pluginHandle;
    runtime = std::make_unique<Runtime>();
    const char *installed = getenv("HYPRUNE_WORLD_DIR");
    runtime->installed = installed ? installed : std::string(getenv("HOME")) + "/.local/share/hyprune/worlds";
    const char *dir = getenv("XDG_RUNTIME_DIR");
    const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!dir || !sig)
        throw std::runtime_error("Hyprune: missing session environment");
    runtime->ipc = std::make_unique<hyprune::ipc::Server>(
        dir, sig, grants(), [](auto &method, auto &params) { return runtime->method(method, params); });
    runtime->ipc->advertise({{"world.load", "world.control"},
                             {"surface.focus", "surface.control"},
                             {"movement.set", "movement.control"},
                             {"runtime.exit", "runtime.control"}});
    HyprlandAPI::addDispatcherV2(handle, "hyprune.toggle", [](std::string arg) {
        deferToggle(arg);
        return SDispatchResult{};
    });
    HyprlandAPI::addLuaFunction(handle, "hyprune", "toggle", luaToggle);
    HyprlandAPI::addLuaFunction(handle, "hyprune", "camera", luaCamera);
    for (auto &match : HyprlandAPI::findFunctionsByName(handle, "CPointerManager::move"))
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
            if (stage == RENDER_LAST_MOMENT && runtime->active && !locked() &&
                g_pHyprRenderer->m_renderData.pMonitor == runtime->monitor)
                g_pHyprRenderer->addPassElement(makeUnique<Pass>());
        });
    }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.mouse.move.listen([](Vector2D, Event::SCallbackInfo &info) {
            if (runtime->active && !locked())
                info.cancelled = true;
        }));
    runtime->listeners.push_back(
        Event::bus()->m_events.input.keyboard.key.listen([](IKeyboard::SKeyEvent event, Event::SCallbackInfo &info) {
            safe([&] {
                if (!runtime->active || locked())
                    return;
                if (event.keycode == KEY_ESC) {
                    info.cancelled = true;
                    if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED) {
                        runtime->typing = false;
                        runtime->focused.clear();
                        runtime->keys.clear();
                    }
                    return;
                }
                if (event.keycode == KEY_F12) {
                    info.cancelled = true;
                    if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED)
                        deferToggle("");
                    return;
                }
                if (runtime->typing)
                    return;
                if (event.keycode == KEY_W || event.keycode == KEY_A || event.keycode == KEY_S ||
                    event.keycode == KEY_D || event.keycode == KEY_SPACE || event.keycode == KEY_LEFTCTRL) {
                    info.cancelled = true;
                    if (event.state == WL_KEYBOARD_KEY_STATE_PRESSED)
                        runtime->keys.insert(event.keycode);
                    else
                        runtime->keys.erase(event.keycode);
                }
            });
        }));
    runtime->listeners.push_back(Event::bus()->m_events.input.mouse.button.listen([](IPointer::SButtonEvent event,
                                                                                     Event::SCallbackInfo &info) {
        safe([&] {
            if (!runtime->active || locked())
                return;
            info.cancelled = true;
            if (auto hit = aim()) {
                if (event.state == WL_POINTER_BUTTON_STATE_PRESSED)
                    runtime->focus(hit->first, true);
                if (auto *e = runtime->capture.find(hit->first)) {
                    g_pSeatManager->setPointerFocus(e->window->resource(), {hit->second.u * e->surface.pixelWidth,
                                                                            hit->second.v * e->surface.pixelHeight});
                    g_pSeatManager->sendPointerButton(event.timeMs, event.button, event.state);
                    g_pSeatManager->sendPointerFrame();
                }
            }
        });
    }));
    runtime->listeners.push_back(Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR monitor) {
        safe([&] {
            if (runtime->monitor == monitor)
                runtime->exit();
        });
    }));
    runtime->timer = makeShared<CEventLoopTimer>(
        std::chrono::milliseconds(16),
        [](SP<CEventLoopTimer> self, void *) {
            safe([] { runtime->tick(); });
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
