#include "LayoutStore.hpp"
#include "offload/Supervisor.hpp"
#include "ipc/Server.hpp"
#include "ipc/Bridge.hpp"
#include "ipc/GrantIssuer.hpp"
#include "quests/Engine.hpp"
#include "content/Engine.hpp"
#include "control/Control.hpp"
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cmath>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/wait.h>
namespace {
using namespace hyprune::ipc;
void check(bool ok, const Error &error) {
    if (!ok)
        throw error;
}
volatile sig_atomic_t running = 1;
void stop(int) {
    running = 0;
}
std::vector<LaunchGrant> grants() {
    std::vector<LaunchGrant> result;
    const char *fdEnv = getenv("HYPRUNE_LAUNCH_FD");
    if (!fdEnv)
        return result;
    int fd = std::stoi(fdEnv);
    char buffer[8192];
    ssize_t n = read(fd, buffer, sizeof buffer);
    close(fd);
    unsetenv("HYPRUNE_LAUNCH_FD");
    if (n <= 0)
        return result;
    for (auto &t : Json::parse(buffer, buffer + n))
        result.push_back({t.at("token"), t.at("client"), t.at("capabilities").get<std::set<std::string>>()});
    return result;
}
} // namespace
int main() {
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    try {
        const char *bridge = getenv("HYPRUNE_BRIDGE_FD");
        if (!bridge)
            throw std::runtime_error("hypruned needs inherited private bridge");
        int fd = std::stoi(bridge);
        std::string epoch;
        hyprune::daemon::LayoutStore layouts;
        bool layoutSent = false, persistReady = false, advertised = false;
        uint64_t command = 0;
        Server *server = nullptr;
        std::function<void()> renewGrants = [] {};
        // RFC-0012 quest engine: owns the `quests` and `focus` keys and their methods.
        double focusScale = 1;
        if (const char *scale = getenv("HYPRUNE_FOCUS_TIME_SCALE"))
            focusScale = std::clamp(std::atof(scale), 1., 600.);
        hyprune::quests::Engine quests(hyprune::quests::Engine::Paths::fromEnvironment(), focusScale);
        // RFC-0015 content engine (IPC 0.32): owns the `content` key and the content, sources and
        // registry methods; its jobs run the SDK CLI as child processes off the compositor.
        hyprune::content::Engine content;
        // Flushed on every exit path, including a plugin disconnect: core exit pauses a running session.
        struct QuestFlush {
            hyprune::quests::Engine &engine;
            ~QuestFlush() {
                try {
                    engine.flush(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch()).count(),
                                 true);
                } catch (...) {
                }
            }
        } questFlush{quests};
        Json pluginState = nullptr;
        auto steadyMs = [] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        };
        auto republish = [&] {
            quests.changed = false;
            content.changed = false;
            if (server && !pluginState.is_null())
                server->publish(content.decorate(quests.decorate(pluginState, steadyMs())));
        };
        auto receive = [&]() -> Json {
            char buffer[196609];
            auto n = recv(fd, buffer, sizeof buffer, 0);
            if (n == 0)
                throw std::runtime_error("plugin disconnected");
            if (n < 0)
                return nullptr;
            auto j = Json::parse(buffer, buffer + n);
            if (epoch.empty()) {
                if (j.at("type") != "hello" || j.at("version") != 1)
                    throw std::runtime_error("bridge version mismatch");
                epoch = j.at("epoch");
            } else if (j.at("epoch") != epoch)
                throw std::runtime_error("epoch mismatch");
            return j;
        };
        auto send = [&](Json j) {
            j["epoch"] = epoch;
            auto bytes = j.dump(-1, ' ', false, Json::error_handler_t::replace);
            if (::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL) != (ssize_t)bytes.size())
                throw std::runtime_error("bridge write failed");
        };
        const char *rendererPath = getenv("HYPRUNE_RENDERER");
        hyprune::offload::Supervisor renderer(rendererPath && rendererPath[0] == '/' ? rendererPath : HYPRUNE_RENDERER_PATH, [&](Json j, int channel) {
            j["epoch"] = epoch;
            if (!hyprune::offload::sendPacket(fd, j, channel >= 0 ? std::vector<int>{channel} : std::vector<int>{}))
                throw std::runtime_error("renderer bridge backpressure");
        });
        auto apply = [&](const Json &j) {
            if (j.at("type") == "renderer.request") renderer.request(j.at("request"));
            if (j.at("type") == "layout.ready")
                persistReady = true;
            if (j.at("type") == "hello" && !layoutSent) {
                send({{"type", "layout"}, {"layout", layouts.load()}});
                layoutSent = true;
            }
            if (j.at("type") == "state") {
                if (persistReady && j.at("state").contains("mounts"))
                    layouts.save(hyprune::daemon::durableLayout(j.at("state").at("mounts"),
                                                                j.value("derivedBindings", Json::array())));
                pluginState = j.at("state");
                quests.pluginState(pluginState);
                republish();
            } else if (j.at("type") == "facts") {
                quests.facts(j.at("facts"));
            } else if (j.at("type") == "event") {
                server->notify(j.at("event"), j.at("params"), j.value("actor", ""));
                server->flush();
            } else if (j.at("type") == "methods") {
                auto methods = j.at("methods").get<std::map<std::string, std::string>>();
                methods.merge(hyprune::quests::Engine::methods());
                methods.merge(hyprune::content::Engine::methods());
                methods.insert({"preview.render", "objects.control"}); // RFC-0016, rendered by hyprune-preview
                server->advertise(std::move(methods));
                advertised = true;
            }
        };
        std::function<void()> questOutput = [] {};
        std::function<Json(const std::string &, const Json &)> forward;
        // RFC-0016 preview.render: the plugin describes what is loaded (preview.scene), and
        // hyprune-preview renders it in its own process; this loop keeps the bridge alive.
        uint64_t previews = 0;
        auto preview = [&](const Json &params) -> Json {
            auto bad = [](const char *message) { return Error(-32602, message); };
            for (auto &[key, value] : params.items())
                if (!key.starts_with("_") && key != "pose" && key != "fov" && key != "size")
                    throw bad("unknown parameter");
            check(params.contains("pose") && params["pose"].is_object(), bad("pose required"));
            auto &pose = params["pose"];
            check(pose.contains("position") && pose["position"].is_array() && pose["position"].size() == 3, bad("pose.position is [x, y, z]"));
            for (auto &v : pose["position"])
                check(v.is_number() && std::isfinite(v.get<double>()) && std::abs(v.get<double>()) <= 100000, bad("pose.position out of bounds"));
            double yaw = pose.value("yaw", 0.), pitch = pose.value("pitch", 0.), fov = params.value("fov", 70.);
            check(std::isfinite(yaw) && std::abs(yaw) <= 360 && std::isfinite(pitch) && std::abs(pitch) <= 89 && fov >= 20 && fov <= 120, bad("yaw, pitch or fov out of range"));
            int w = 1024, h = 576;
            if (params.contains("size")) {
                auto &sz = params["size"];
                check(sz.is_array() && sz.size() == 2 && sz[0].is_number_integer() && sz[1].is_number_integer(), bad("size is [width, height]"));
                w = sz[0], h = sz[1];
                check(w >= 64 && w <= 1024 && h >= 64 && h <= 1024, bad("size: 64 to 1024 px"));
            }
            auto scene = forward("preview.scene", Json::object());
            scene["camera"] = {{"position", pose["position"]}, {"yaw", yaw}, {"pitch", pitch}, {"fov", fov}};
            scene["width"] = w, scene["height"] = h;
            std::filesystem::path dir = std::filesystem::path(getenv("XDG_RUNTIME_DIR")) / "hyprune" / "previews";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            std::filesystem::permissions(dir, std::filesystem::perms::owner_all, ec);
            // Previews live 60 s.
            for (auto &f : std::filesystem::directory_iterator(dir, ec))
                if (std::filesystem::last_write_time(f, ec) < std::filesystem::file_time_type::clock::now() - std::chrono::seconds(60))
                    std::filesystem::remove(f, ec);
            auto stem = "preview-" + std::to_string(getpid()) + "-" + std::to_string(++previews);
            auto scenePath = dir / (stem + ".json"), png = dir / (stem + ".png"), out = dir / (stem + ".out");
            {
                std::ofstream f(scenePath);
                f << scene.dump();
            }
            const char *tool = getenv("HYPRUNE_PREVIEW");
            std::string exe = tool && tool[0] == '/' ? tool : HYPRUNE_PREVIEW_PATH;
            pid_t child = fork();
            if (child == 0) {
                int fd = ::open(out.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
                if (fd >= 0) dup2(fd, 1);
                execl(exe.c_str(), exe.c_str(), scenePath.c_str(), png.c_str(), (char *)nullptr);
                _exit(127);
            }
            check(child > 0, Error(-32603, "preview renderer did not start"));
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
            int status = 0;
            bool done = false;
            while (!done && std::chrono::steady_clock::now() < deadline) {
                pollfd p{fd, POLLIN, 0};
                ::poll(&p, 1, 20);
                renderer.tick();
                send({{"type", "heartbeat"}});
                if (auto j = receive(); !j.is_null())
                    apply(j);
                done = waitpid(child, &status, WNOHANG) == child;
            }
            if (!done) {
                kill(child, SIGKILL);
                waitpid(child, &status, 0);
            }
            std::filesystem::remove(scenePath, ec);
            Json info = nullptr;
            try { info = Json::parse(std::ifstream(out)); } catch (...) {}
            std::filesystem::remove(out, ec);
            if (!done || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || !info.is_object())
                throw Error(-32603, done ? "the preview could not be rendered" : "the preview took longer than 25 s", Json{{"retryable", true}, {"rule", "preview.failed"}});
            return Json{{"path", png.string()}, {"width", w}, {"height", h},
                        {"pose", {{"position", pose["position"]}, {"yaw", yaw}, {"pitch", pitch}}}, {"fov", fov}, {"renderMs", info.value("renderMs", 0.)}};
        };
        auto handler = [&](const std::string &method, const Json &params) -> Json {
            if (hyprune::quests::Engine::handles(method)) {
                auto result = quests.call(method, params, steadyMs(), std::time(nullptr));
                republish();
                if (result.contains("revision"))
                    result["revision"] = server->revision();
                questOutput();
                return result;
            }
            if (hyprune::content::Engine::handles(method)) {
                auto result = content.call(method, params);
                republish();
                if (result.contains("revision"))
                    result["revision"] = server->revision();
                return result;
            }
            if (method == "preview.render")
                return preview(params);
            return forward(method, params);
        };
        forward = [&](const std::string &method, const Json &params) -> Json {
            auto id = std::to_string(++command);
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(29);
            send({{"type", "command"},
                  {"id", id},
                  {"method", method},
                  {"params", params},
                  {"deadlineMs",
                   std::chrono::duration_cast<std::chrono::milliseconds>(deadline.time_since_epoch()).count()}});
            bool cancelled = false;
            while (running && std::chrono::steady_clock::now() < deadline) {
                pollfd p{fd, POLLIN, 0};
                ::poll(&p, 1, 50);
                renderer.tick();
                renewGrants();
                send({{"type", "heartbeat"}});
                if (!cancelled && method != "_connection.closed" && params.contains("_actor") &&
                    !server->connected(params.at("_actor"))) {
                    send({{"type", "cancel"}});
                    cancelled = true;
                }
                if (auto j = receive(); !j.is_null()) {
                    if (j.at("type") == "reply" && j.at("id") == id) {
                        if (j.contains("error"))
                            throw Error(j["error"]["code"], j["error"]["message"], j["error"].value("data", Json()));
                        // A lease or overlay reply's revision is the state revision that shows it.
                        // The plugin counts its own; this server also counts the state it adds
                        // (quests, content jobs, connections), so its deltas ran ahead and a shell
                        // comparing the two closed the Director it had just opened. The plugin's
                        // state preceded this reply on the bridge: the server's revision has it.
                        if (hyprune::control::Control::methods().contains(method) || method == "shell.bind")
                            return hyprune::ipc::Bridge::serverResult(j, server->revision());
                        return j.at("result");
                    }
                    apply(j);
                }
            }
            // Fail closed: disconnecting the private bridge makes the plugin
            // discard pending work, rather than commit after an error reply.
            shutdown(fd, SHUT_RDWR);
            running = 0;
            throw Error(-32006, "plugin command deadline");
        };
        const char *dir = getenv("XDG_RUNTIME_DIR");
        const char *sig = getenv("HYPRLAND_INSTANCE_SIGNATURE");
        if (!dir || !sig)
            throw std::runtime_error("missing session environment");
        std::fprintf(stderr, "hypruned: starting session %s\n", sig);
        auto launchGrants = grants();
        int renewalFd = getenv("HYPRUNE_GRANT_FD") ? std::stoi(getenv("HYPRUNE_GRANT_FD")) : -1;
        unsetenv("HYPRUNE_GRANT_FD");
        GrantIssuer issuer(renewalFd, launchGrants);
        Server endpoint(dir, sig, launchGrants, handler);
        std::fprintf(stderr, "hypruned: IPC ready; private bridge connected\n");
        server = &endpoint;
        renewGrants = [&] { issuer.poll(endpoint); };
        endpoint.contextual(true);
        questOutput = [&] {
            for (auto &[event, params] : quests.events)
                endpoint.notify(event, params);
            quests.events.clear();
            if (auto message = quests.sync(); message && !epoch.empty())
                send({{"type", "quests.sync"}, {"sync", *message}});
        };
        auto heartbeat = std::chrono::steady_clock::now();
        while (running) {
            pollfd p{fd, POLLIN, 0};
            ::poll(&p, 1, 4);
            for (int i = 0; i < 32; i++) {
                auto j = receive();
                if (j.is_null())
                    break;
                apply(j);
            }
            if (!epoch.empty() && std::chrono::steady_clock::now() - heartbeat > std::chrono::milliseconds(100)) {
                send({{"type", "heartbeat"}});
                heartbeat = std::chrono::steady_clock::now();
            }
            renderer.tick();
            issuer.poll(endpoint);
            quests.tick(steadyMs(), std::time(nullptr));
            content.tick();
            if (quests.changed || content.changed)
                republish();
            for (auto &[event, params] : content.notices)
                endpoint.notify(event, params);
            content.notices.clear();
            questOutput();
            // The plugin flushes its method table on its next bridge tick. Until
            // it arrives, clients wait in the listen backlog: a hello answered
            // now would advertise no methods for the whole connection.
            if (advertised)
                endpoint.poll();
        }
        std::fprintf(stderr, "hypruned: stopping\n");
        close(fd);
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "hypruned: %s\n", e.what());
        return 1;
    }
}
