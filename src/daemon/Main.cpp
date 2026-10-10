#include "LayoutStore.hpp"
#include "offload/Supervisor.hpp"
#include "ipc/Server.hpp"
#include "ipc/GrantIssuer.hpp"
#include <csignal>
#include <cstdio>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
namespace {
using namespace hyprune::ipc;
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
        hyprune::offload::Supervisor renderer(HYPRUNE_RENDERER_PATH, [&](Json j, int channel) {
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
                server->publish(j.at("state"));
            } else if (j.at("type") == "event") {
                server->notify(j.at("event"), j.at("params"), j.value("actor", ""));
                server->flush();
            } else if (j.at("type") == "methods") {
                server->advertise(j.at("methods").get<std::map<std::string, std::string>>());
                advertised = true;
            }
        };
        auto handler = [&](const std::string &method, const Json &params) -> Json {
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
                            throw Error(j["error"]["code"], j["error"]["message"]);
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
