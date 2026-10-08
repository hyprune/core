#include "Server.hpp"
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <poll.h>
#include <regex>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
namespace hyprune::ipc {
namespace {
constexpr size_t MaxFrame = 1048576, MaxQueue = 4 * 1048576;
using Clock = std::chrono::steady_clock;
void check(bool ok, int code, const char *message) {
    if (!ok)
        throw Error(code, message);
}
void exact(const Json &j, std::initializer_list<const char *> required,
           std::initializer_list<const char *> optional = {}) {
    check(j.is_object(), -32602, "params must be an object");
    std::set<std::string> keys;
    for (auto k : required) {
        check(j.contains(k), -32602, "missing parameter");
        keys.insert(k);
    }
    for (auto k : optional)
        keys.insert(k);
    for (auto it = j.begin(); it != j.end(); ++it)
        check(keys.contains(it.key()), -32602, "unknown parameter");
}
void directory(const std::string &path) {
    if (mkdir(path.c_str(), 0700) < 0 && errno != EEXIST)
        throw std::runtime_error("cannot create IPC directory");
    struct stat s{};
    if (lstat(path.c_str(), &s) || !S_ISDIR(s.st_mode) || s.st_uid != getuid() || (s.st_mode & 077) != 0)
        throw std::runtime_error("unsafe IPC directory");
}
} // namespace
std::string randomId() {
    unsigned char bytes[32];
    if (getrandom(bytes, sizeof bytes, 0) != sizeof bytes)
        throw std::runtime_error("getrandom failed");
    static const char *digits = "0123456789abcdef";
    std::string id;
    for (auto b : bytes) {
        id += digits[b >> 4];
        id += digits[b & 15];
    }
    return id;
}
Server::Server(const std::string &runtime, const std::string &instance, std::vector<LaunchGrant> grants,
               Handler handler)
    : tokens_(std::move(grants)), handler_(std::move(handler)) {
    if (runtime.empty() || runtime[0] != '/' || !std::regex_match(instance, std::regex("[A-Za-z0-9_.-]+")) ||
        instance == "." || instance == "..")
        throw std::runtime_error("unsafe IPC environment");
    directory(runtime + "/hyprune");
    directory(runtime + "/hyprune/" + instance);
    path_ = runtime + "/hyprune/" + instance + "/control.sock";
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path_.size() >= sizeof address.sun_path)
        throw std::runtime_error("IPC path too long");
    std::strcpy(address.sun_path, path_.c_str());
    struct stat s{};
    if (lstat(path_.c_str(), &s) == 0) {
        if (!S_ISSOCK(s.st_mode) || s.st_uid != getuid())
            throw std::runtime_error("unsafe existing IPC path");
        int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
        int result = connect(probe, (sockaddr *)&address, sizeof address);
        int error = errno;
        close(probe);
        if (result == 0 || error != ECONNREFUSED)
            throw std::runtime_error("IPC listener already exists");
        if (unlink(path_.c_str()))
            throw std::runtime_error("cannot remove stale IPC socket");
    }
    fd_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd_ < 0)
        throw std::runtime_error("socket failed");
    mode_t previous = umask(0077);
    int bound = bind(fd_, (sockaddr *)&address, sizeof address);
    umask(previous);
    if (bound || chmod(path_.c_str(), 0600) || listen(fd_, 16)) {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error("IPC bind/listen failed");
    }
    session_ = randomId();
}
Server::~Server() {
    for (auto &p : peers_)
        close(p.fd);
    if (fd_ >= 0) {
        close(fd_);
        unlink(path_.c_str());
    }
}
Json Server::snapshot() const {
    return {{"sessionId", session_}, {"revision", revision_}, {"state", state_}};
}
Json Server::visibleState(const Peer &p) const {
    if (p.protocol == "0.6")
        return state_;
    Json state = state_;
    state.erase("keymap");
    state.erase("editor");
    if (state.contains("presentation") && state["presentation"].contains("fov"))
        state["presentation"]["fov"] = std::max(30., state["presentation"]["fov"].get<double>());
    if (p.protocol == "0.5")
        return state;
    if (state.is_object()) {
        state.erase("interaction");
        state.erase("apps");
    }
    if (state.contains("mounts") && state["mounts"].contains("placements"))
        std::erase_if(state["mounts"]["placements"].get_ref<Json::array_t &>(),
                      [](auto &p) { return p.contains("pose"); });
    if (p.protocol == "0.4")
        return state;
    if (state.is_object()) {
        state.erase("mounts");
        state.erase("presentation");
        if (p.protocol == "0.3")
            return state;
        state.erase("spatial");
        state.erase("workspaces");
        if (p.protocol == "0.2")
            return state;
        state.erase("location");
        state.erase("integrations");
    }
    return state;
}
Json Server::snapshot(const Peer &p) const {
    return {{"sessionId", session_}, {"revision", revision_}, {"state", visibleState(p)}};
}
void Server::notify(const std::string &event, Json params) {
    if (event != "runtime.notice") {
        params["sessionId"] = session_;
        params["revision"] = revision_;
    }
    for (auto &p : peers_)
        if (p.hello && !p.closing && p.protocol != "0.1" && !p.subscription.empty() &&
            ((p.protocol == "0.6" || p.protocol == "0.5" || p.protocol == "0.4") ||
             (event != "mounts.changed" && event != "presentation.changed"))) {
            auto projected = params;
            if (p.protocol != "0.6" && p.protocol != "0.5" && event == "mounts.changed" &&
                projected.contains("mounts") && projected["mounts"].contains("placements"))
                std::erase_if(projected["mounts"]["placements"].get_ref<Json::array_t &>(),
                              [](auto &placement) { return placement.contains("pose"); });
            queue(p, {{"jsonrpc", "2.0"}, {"method", event}, {"params", projected}});
        }
}
void Server::publish(Json state) {
    if (state == state_)
        return;
    if (state.dump().size() > MaxFrame - 4096)
        throw Error(-32008, "snapshot resource limit");
    state_ = std::move(state);
    ++revision_;
}
void Server::queue(Peer &p, const Json &value) {
    auto s = value.dump();
    if (s.size() > MaxFrame || p.output.size() + s.size() + 4 > MaxQueue) {
        p.closing = true;
        p.output.clear();
        return;
    }
    uint32_t size = s.size();
    for (int shift = 24; shift >= 0; shift -= 8)
        p.output += char(size >> shift);
    p.output += s;
}
void Server::frame(Peer &p, const std::string &bytes) {
    Json id = nullptr, request;
    try {
        std::vector<std::set<std::string>> stack;
        auto callback = [&](int depth, Json::parse_event_t event, Json &parsed) {
            if (depth > 64)
                throw std::runtime_error("too deep");
            if (event == Json::parse_event_t::object_start)
                stack.emplace_back();
            if (event == Json::parse_event_t::key && !stack.back().insert(parsed.get<std::string>()).second)
                throw std::runtime_error("duplicate key");
            if (event == Json::parse_event_t::object_end)
                stack.pop_back();
            return true;
        };
        request = Json::parse(bytes, callback);
    } catch (...) {
        queue(p, {{"jsonrpc", "2.0"}, {"id", nullptr}, {"error", {{"code", -32700}, {"message", "invalid JSON"}}}});
        p.closing = true;
        return;
    }
    try {
        if (request.is_object() && request.contains("id") && request["id"].is_string() &&
            !request["id"].get<std::string>().empty() && request["id"].get<std::string>().size() <= 128)
            id = request["id"];
        check(!id.is_null() && request.size() == 4 && request.contains("jsonrpc") && request["jsonrpc"] == "2.0" &&
                  request.contains("method") && request["method"].is_string() && request.contains("params") &&
                  request["params"].is_object(),
              -32600, "invalid request profile");
        check(p.ids.insert(id.get<std::string>()).second, -32600, "request ID reused");
        check(p.ids.size() <= 65536, -32008, "connection request limit");
        std::string method = request["method"];
        const auto &params = request["params"];
        Json result;
        if (!p.hello) {
            check(method == "session.hello", -32600, "hello required");
            exact(params, {"protocols", "client", "capabilities"}, {"token"});
            check(params["protocols"].is_array() && !params["protocols"].empty(), -32602, "invalid protocols");
            std::set<std::string> protocols;
            for (auto &value : params["protocols"]) {
                check(value.is_string(), -32602, "invalid protocol");
                auto protocol = value.get<std::string>();
                check(!protocol.empty() && protocol.size() <= 256 && protocols.insert(protocol).second, -32602,
                      "invalid protocol list");
            }
            check(protocols.contains("0.6") || protocols.contains("0.5") || protocols.contains("0.4") ||
                      protocols.contains("0.3") || protocols.contains("0.2") || protocols.contains("0.1"),
                  -32001, "incompatible protocol");
            p.protocol = protocols.contains("0.6")   ? "0.6"
                         : protocols.contains("0.5") ? "0.5"
                         : protocols.contains("0.4") ? "0.4"
                         : protocols.contains("0.3") ? "0.3"
                         : protocols.contains("0.2") ? "0.2"
                                                     : "0.1";
            exact(params["client"], {"id", "name"});
            check(params["client"]["id"].is_string() && params["client"]["name"].is_string(), -32602,
                  "invalid client identity");
            auto clientId = params["client"]["id"].get<std::string>(),
                 clientName = params["client"]["name"].get<std::string>();
            check(clientId.size() <= 128 &&
                      std::regex_match(clientId, std::regex("[a-z][a-z0-9-]*(\\.[a-z][a-z0-9-]*)+")) &&
                      !clientName.empty() && clientName.size() <= 256,
                  -32602, "invalid client identity");
            check(params["capabilities"].is_array(), -32602, "invalid capabilities");
            const std::set<std::string> known{"state.read",    "world.control",   "surface.control", "movement.control",
                                              "shell.control", "runtime.control", "data.publish",    "input.control"};
            std::set<std::string> requested;
            for (auto &cap : params["capabilities"]) {
                check(cap.is_string(), -32602, "invalid capability");
                auto name = cap.get<std::string>();
                check(known.contains(name) && (name != "input.control" || p.protocol == "0.6") &&
                          (p.protocol != "0.1" || name != "data.publish") && requested.insert(name).second,
                      -32602, "invalid capability list");
            }
            if (params.contains("token")) {
                check(params["token"].is_string() &&
                          std::regex_match(params["token"].get<std::string>(), std::regex("[a-f0-9]{64}")),
                      -32602, "invalid token format");
                bool valid = false;
                for (auto it = tokens_.begin(); it != tokens_.end(); ++it)
                    if (it->expires > Clock::now() && it->token == params["token"].get<std::string>() &&
                        it->client == params["client"]["id"].get<std::string>()) {
                        for (auto &c : params["capabilities"])
                            if (it->capabilities.contains(c.get<std::string>()))
                                p.grants.insert(c.get<std::string>());
                        tokens_.erase(it);
                        valid = true;
                        break;
                    }
                check(valid, -32002, "invalid launch token");
            }
            p.hello = true;
            p.client = clientId;
            p.actor = randomId();
            Json methods = Json::array({"session.hello", "session.ping"});
            if (p.grants.contains("state.read"))
                for (auto s : {"state.subscribe", "state.snapshot", "state.unsubscribe"})
                    methods.push_back(s);
            const std::set<std::string> legacy{"world.load",  "surface.focus", "movement.set", "runtime.exit",
                                               "shell.claim", "shell.release", "shell.overlay"};
            for (auto &[m, grant] : methods_)
                if ((p.protocol == "0.6" || !m.starts_with("input.")) && p.grants.contains(grant) &&
                    (p.protocol != "0.1" || legacy.contains(m)) &&
                    ((p.protocol == "0.6" || p.protocol == "0.5" || p.protocol == "0.4") ||
                     ((m != "workspace.bind" && m != "workspace.focus" && m != "workspace.unfocus" &&
                       m != "surface.pull" && m != "surface.place" && m != "surface.return" && m != "surfaces.recall" &&
                       m != "placements.list" && m != "placements.remove" && m != "data.object.bind" &&
                       m != "camera.fov" && m != "atmosphere.get" && m != "atmosphere.set") &&
                      (p.protocol == "0.3" || (m != "shell.bind" && m != "workspace.activate")))))
                    methods.push_back(m);
            result = {{"protocol", p.protocol},
                      {"sessionId", session_},
                      {"capabilities", p.grants},
                      {"methods", methods},
                      {"events",
                       p.grants.contains("state.read")
                           ? (p.protocol == "0.6" ? Json::array({"state.delta", "location.transition", "ui.opened",
                                                                 "ui.closed", "data.updated", "mounts.changed",
                                                                 "presentation.changed", "runtime.notice"})
                              : p.protocol != "0.1"
                                  ? (p.protocol == "0.6" || p.protocol == "0.5" || p.protocol == "0.4")
                                        ? Json::array({"state.delta", "location.transition", "ui.opened", "ui.closed",
                                                       "data.updated", "mounts.changed", "presentation.changed"})
                                        : Json::array({"state.delta", "location.transition", "ui.opened", "ui.closed",
                                                       "data.updated"})
                                  : Json::array({"state.delta"}))
                           : Json::array()}};
        } else if (method == "session.hello")
            throw Error(-32600, "hello already completed");
        else if (method == "session.ping") {
            exact(params, {});
            result = {{"monotonicMs",
                       std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()}};
        } else if (method.starts_with("state.") &&
                   (method == "state.subscribe" || method == "state.snapshot" || method == "state.unsubscribe")) {
            check(p.grants.contains("state.read"), -32601, "method not advertised");
            if (method == "state.subscribe") {
                exact(params, {"topics"});
                check(params["topics"] == Json::array({"state"}), -32602, "unsupported topics");
                check(p.subscription.empty(), -32004, "already subscribed");
                p.subscription = "subscription-" + std::to_string(++subscriptions_);
                p.baseline = revision_;
                result = {{"subscriptionId", p.subscription}, {"snapshot", snapshot(p)}};
            } else {
                exact(params, {"subscriptionId"});
                check(!p.subscription.empty() && params["subscriptionId"] == p.subscription, -32003,
                      "unknown subscription");
                if (method == "state.unsubscribe") {
                    p.subscription.clear();
                    result = {{"ok", true}};
                } else {
                    // Keep already-written/partial frames, discard only wholly unsent deltas.
                    std::string retained;
                    for (size_t at = 0; at + 4 <= p.output.size();) {
                        uint32_t n = 0;
                        for (int i = 0; i < 4; i++)
                            n = (n << 8) | (unsigned char)p.output[at + i];
                        bool discard = false;
                        if (at >= p.written) {
                            auto frame = Json::parse(p.output.substr(at + 4, n));
                            discard = frame.value("method", "") == "state.delta";
                        }
                        if (!discard)
                            retained.append(p.output, at, n + 4);
                        at += n + 4;
                    }
                    p.output = std::move(retained);
                    p.baseline = revision_;
                    result = {{"snapshot", snapshot(p)}};
                }
            }
        } else {
            check(p.protocol == "0.6" || !method.starts_with("input."), -32601, "method not advertised for protocol");
            auto it = methods_.find(method);
            check(it != methods_.end() && p.grants.contains(it->second), -32601, "method not advertised");
            const std::set<std::string> legacy{"world.load",  "surface.focus", "movement.set", "runtime.exit",
                                               "shell.claim", "shell.release", "shell.overlay"};
            check(p.protocol != "0.1" || legacy.contains(method), -32601, "method not advertised for protocol");
            check((p.protocol == "0.6" || p.protocol == "0.5" || p.protocol == "0.4") ||
                      ((method != "workspace.bind" && method != "workspace.focus" && method != "workspace.unfocus" &&
                        method != "surface.pull" && method != "surface.place" && method != "surface.return" &&
                        method != "surfaces.recall" && method != "placements.list" && method != "placements.remove" &&
                        method != "data.object.bind" && method != "camera.fov" && method != "atmosphere.get" &&
                        method != "atmosphere.set") &&
                       (p.protocol == "0.3" || (method != "shell.bind" && method != "workspace.activate"))),
                  -32601, "method not advertised for protocol");
            Json command = params;
            check(!command.contains("_actor") && !command.contains("_client") && !command.contains("_pid") &&
                      !command.contains("_protocol"),
                  -32602, "reserved parameter");
            if (contextual_) {
                command["_actor"] = p.actor;
                command["_client"] = p.client;
                command["_pid"] = p.pid;
                command["_protocol"] = p.protocol;
            }
            result = handler_(method, command);
        }
        queue(p, {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
    } catch (const Error &e) {
        queue(p, {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", e.code}, {"message", e.what()}}}});
        if (!p.hello)
            p.closing = true;
    } catch (const Json::exception &) {
        queue(p,
              {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32602}, {"message", "invalid parameter type"}}}});
    } catch (...) {
        queue(p, {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", -32603}, {"message", "operation failed"}}}});
    }
}
bool Server::connected(const std::string &actor) const {
    for (const auto &p : peers_)
        if (p.actor == actor && !p.closing) {
            pollfd descriptor{p.fd, POLLRDHUP, 0};
            if (::poll(&descriptor, 1, 0) > 0 && (descriptor.revents & (POLLRDHUP | POLLHUP | POLLERR)))
                return false;
            char byte;
            auto n = recv(p.fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
            return n > 0 || (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        }
    return false;
}
void Server::flush() {
    // Also used while an asynchronous plugin operation is pending: loading events
    // must be observable before its completion. Never read or recurse into handlers.
    for (auto &p : peers_)
        if (!p.output.empty()) {
            auto n = send(p.fd, p.output.data() + p.written, std::min<size_t>(65536, p.output.size() - p.written),
                          MSG_NOSIGNAL);
            if (n > 0) {
                p.written += n;
                if (p.written == p.output.size()) {
                    p.output.clear();
                    p.written = 0;
                }
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                p.closing = true;
                p.output.clear();
                p.written = 0;
            }
        }
}
void Server::poll() {
    for (int i = 0; i < 8; i++) {
        int fd = accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
            break;
        ucred credential{};
        socklen_t size = sizeof credential;
        if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credential, &size) || credential.uid != getuid() ||
            peers_.size() >= 32) {
            close(fd);
            continue;
        }
        Peer peer;
        peer.fd = fd;
        peer.pid = credential.pid;
        peers_.push_back(std::move(peer));
    }
    auto now = Clock::now();
    for (auto &p : peers_) {
        if ((!p.hello && now - p.accepted > std::chrono::seconds(5)) ||
            (!p.input.empty() && now - p.frameStart > std::chrono::seconds(10))) {
            p.closing = true;
            p.output.clear();
        }
        if (!p.closing) {
            char buffer[16384];
            ssize_t count = recv(p.fd, buffer, sizeof buffer, 0);
            if (count > 0) {
                if (p.input.empty())
                    p.frameStart = now;
                p.input.append(buffer, count);
            } else if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
                p.closing = true;
                p.output.clear();
            }
            for (int frames = 0; frames < 16 && p.input.size() >= 4 && !p.closing; frames++) {
                uint32_t length = 0;
                for (int i = 0; i < 4; i++)
                    length = (length << 8) | (unsigned char)p.input[i];
                if (!length || length > MaxFrame) {
                    p.closing = true;
                    p.output.clear();
                    break;
                }
                if (p.input.size() < length + 4)
                    break;
                auto bytes = p.input.substr(4, length);
                p.input.erase(0, length + 4);
                p.frameStart = now;
                frame(p, bytes);
            }
            if (!p.subscription.empty() && p.baseline != revision_ &&
                now - p.lastDelta >= std::chrono::milliseconds(34) && p.output.empty()) {
                queue(p, {{"jsonrpc", "2.0"},
                          {"method", "state.delta"},
                          {"params",
                           {{"subscriptionId", p.subscription},
                            {"sessionId", session_},
                            {"baseRevision", p.baseline},
                            {"revision", revision_},
                            {"changes", visibleState(p)}}}});
                p.baseline = revision_;
                p.lastDelta = now;
            }
        }
        if (!p.output.empty()) {
            auto n = send(p.fd, p.output.data() + p.written, std::min<size_t>(65536, p.output.size() - p.written),
                          MSG_NOSIGNAL);
            if (n > 0) {
                p.written += n;
                if (p.written == p.output.size()) {
                    p.output.clear();
                    p.written = 0;
                }
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                p.closing = true;
                p.output.clear();
            }
        }
    }
    std::erase_if(peers_, [&](Peer &p) {
        if (p.closing && p.output.empty()) {
            if (contextual_ && p.hello) {
                try {
                    handler_("_connection.closed", {{"_actor", p.actor}, {"_client", p.client}});
                } catch (...) {
                }
            }
            close(p.fd);
            return true;
        }
        return false;
    });
}
} // namespace hyprune::ipc
