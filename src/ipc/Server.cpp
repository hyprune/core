#include "Server.hpp"
#include "Protocol.hpp"
#include "Text.hpp"
#include "WireAudit.hpp"
#include "interaction/Input.hpp"
#include <algorithm>
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
// IPC 0.26 lists up to 128 pocketed windows and mount instances; older peers get the first
// 16 (the carried window and the selected pocket item stay listed) and a count of at most 32.
void shortMountInstances(Json &mounts) {
    if (mounts.is_object() && mounts.contains("instances") && mounts["instances"].is_array() && mounts["instances"].size() > 16)
        mounts["instances"].erase(mounts["instances"].begin() + 16, mounts["instances"].end());
}
void shortPocket(Json &unplaced) {
    if (!unplaced.is_object())
        return;
    if (unplaced.contains("windows") && unplaced["windows"].is_array() && unplaced["windows"].size() > 16) {
        auto &windows = unplaced["windows"];
        auto selected = unplaced.value("selectedId", Json());
        auto chosen = std::find_if(windows.begin(), windows.end(), [&](const Json &w) { return w.value("surfaceId", Json()) == selected; });
        if (chosen != windows.end() && chosen - windows.begin() >= 16)
            std::rotate(windows.begin() + 15, chosen, chosen + 1);
        windows.erase(windows.begin() + 16, windows.end());
    }
    if (unplaced.contains("count") && unplaced["count"].is_number_integer() && unplaced["count"].get<int64_t>() > 32)
        unplaced["count"] = 32;
}
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
    audit_ = std::make_unique<WireAudit>();
}
bool Server::reissue(const LaunchGrant &grant) {
    std::erase_if(tokens_, [](const auto &g) { return g.expires <= Clock::now(); });
    if (grant.recipientPid <= 0 || tokens_.size() >= 64 ||
        !std::regex_match(grant.token, std::regex("[a-f0-9]{64}"))) return false;
    tokens_.push_back(grant);
    return true;
}
Server::~Server() {
    for (auto &p : peers_)
        close(p.fd);
    if (fd_ >= 0) {
        close(fd_);
        unlink(path_.c_str());
        // Drop the per-instance directory, and the shared one when this was the last.
        auto directory = path_.substr(0, path_.rfind('/'));
        rmdir(directory.c_str());
        rmdir(directory.substr(0, directory.rfind('/')).c_str());
    }
}
Json Server::snapshot() const {
    return {{"sessionId", session_}, {"revision", revision_}, {"state", state_}};
}
Json Server::visibleState(const Peer &p) const {
    Json state = state_;
    // Tool 9, the debug tool, stays core's own until a protocol names it: peers see the normal
    // tool (its slots still say what the buttons do) and no tool9 action in the keymap.
    // From IPC 0.26 it is named: editor.debugTool and the slot-9 definition. Older peers also
    // keep the 16-item pocket and mount instance lists and see no companion pace.
    if (!protocol::atLeast(p.protocol, "0.26") && state.is_object()) {
        if (state.contains("interaction") && state["interaction"].is_object() && state["interaction"].value("tool", 1) == 9)
            state["interaction"]["tool"] = 1;
        if (state.contains("editor") && state["editor"].is_object()) {
            auto &editor = state["editor"];
            if (editor.value("tool", 1) == 9) {
                editor["tool"] = 1;
                editor["submode"] = "normal";
            }
            editor.erase("debugTool");
            if (editor.contains("toolDefinitions") && editor["toolDefinitions"].is_array())
                std::erase_if(editor["toolDefinitions"].get_ref<Json::array_t &>(),
                              [](const Json &d) { return d.is_object() && d.value("slot", 0) == 9; });
            if (editor.contains("unplaced"))
                shortPocket(editor["unplaced"]);
        }
        if (state.contains("keymap"))
            state["keymap"] = interaction::withoutDebugTool(std::move(state["keymap"]));
        if (state.contains("mounts"))
            shortMountInstances(state["mounts"]);
        if (state.contains("companion") && state["companion"].is_object())
            state["companion"].erase("pace");
    }
    // IPC 0.27: workspace screens on object instances.
    if (!protocol::atLeast(p.protocol, "0.27") && state.is_object() && state.contains("objects") && state["objects"].is_object() &&
        state["objects"].contains("instances"))
        for (auto &i : state["objects"]["instances"])
            i.erase("screen");
    // IPC 0.24 (RFC-0013): the live state of bound machines.
    if (!protocol::atLeast(p.protocol, "0.24") && state.is_object())
        state.erase("live");
    // IPC 0.25 (RFC-0010 companions amendment): the companion key; older peers never see the
    // companion as the aimed character or in a running interaction.
    if (!protocol::atLeast(p.protocol, "0.25") && state.is_object()) {
        state.erase("companion");
        if (state.contains("npcs") && state["npcs"].is_object())
            for (auto key : {"aimed", "interaction"})
                if (state["npcs"].contains(key) && state["npcs"][key].is_object() && state["npcs"][key].value("objectId", "") == "companion")
                    state["npcs"][key] = nullptr;
    }
    // IPC 0.23 (RFC-0011 P2): NPC state, character fields of object instances, path fields
    // of editor paths and the `interact` reticle action; older peers keep the 0.22 shapes.
    if (!protocol::atLeast(p.protocol, "0.23") && state.is_object()) {
        state.erase("npcs");
        if (state.contains("objects") && state["objects"].is_object() && state["objects"].contains("instances"))
            for (auto &i : state["objects"]["instances"]) {
                i.erase("character");
                i.erase("accent");
                i["path"] = nullptr, i["activity"] = nullptr, i["seat"] = nullptr;
                if (i.value("state", "") == "suspended")
                    i["state"] = "active";
            }
        if (state.contains("editor") && state["editor"].is_object() && state["editor"].contains("paths"))
            for (auto &path : state["editor"]["paths"])
                for (auto key : {"use", "name", "speed", "stops"})
                    path.erase(key);
        if (state.contains("interaction") && state["interaction"].contains("reticle") &&
            state["interaction"]["reticle"].value("action", "") == "interact")
            state["interaction"]["reticle"]["action"] = "click";
    }
    if (!protocol::atLeast(p.protocol, "0.22") && state.is_object()) {
        state.erase("objects");
        if (state.contains("editor") && state["editor"].is_object())
            state["editor"] = interaction::beforeObjectsEditor(std::move(state["editor"]));
    }
    if (!protocol::atLeast(p.protocol, "0.21") && state.is_object()) {
        for (auto key : {"markers", "quests", "focus"})
            state.erase(key);
        if (state.contains("keymap"))
            state["keymap"] = interaction::withoutMarkersReveal(std::move(state["keymap"]));
        if (state.contains("spatial") && state["spatial"].contains("projection") &&
            state["spatial"]["projection"].is_object()) {
            auto &markers = state["spatial"]["projection"]["markers"];
            std::erase_if(markers.get_ref<Json::array_t &>(), [](const Json &m) { return m.contains("markerId"); });
        }
    }
    if (!protocol::atLeast(p.protocol, "0.20") && state.contains("spatial") && state["spatial"].contains("map") &&
        state["spatial"]["map"].is_object())
        state["spatial"]["map"].erase("levels");
    if (!protocol::atLeast(p.protocol, "0.19") && state.contains("interaction"))
        state["interaction"].erase("reticle");
    if (!protocol::atLeast(p.protocol, "0.15")) {
        if (state.contains("editor")) {
            for (auto key : {"unplaced", "carryFacing", "contentScale", "toolSlots", "toolDefinitions", "toolInput"})
                state["editor"].erase(key);
            if (state["editor"].value("submode", "") == "placement")
                state["editor"]["submode"] = "reserved";
        }
        if (state.contains("interaction") && state["interaction"].contains("tools"))
            for (auto &v : state["interaction"]["tools"])
                if (v == "position" || v == "placement")
                    v = "path";
        if (state.contains("keymap"))
            state["keymap"] = interaction::beforePlacementInput(state["keymap"]);
        if (state.contains("mounts")) {
            for (auto &v : state["mounts"]["placements"]) v.erase("content");
            if (state["mounts"].contains("instances")) std::erase_if(state["mounts"]["instances"].get_ref<Json::array_t &>(),
                [](const auto &v){ return !v.contains("areaId"); });
        }
    }
    if (!protocol::atLeast(p.protocol,"0.13") && state.contains("presentation")) {
        auto &view=state["presentation"];
        if (view.contains("profile") && view["profile"].is_object()) view["profile"].erase("outline");
        if (view.contains("fov")) view["fov"]=std::min(110.,view["fov"].get<double>());
    }
    if (!protocol::atLeast(p.protocol, "0.12")) {
        if (state.contains("editor") && state["editor"].value("mode", "") == "cursor") state["editor"]["mode"] = "interactive";
        if (state.contains("interaction") && state["interaction"].value("mode", "") == "cursor") state["interaction"]["mode"] = "typing";
    }
    if (!protocol::atLeast(p.protocol, "0.12") && state.contains("keymap"))
        state["keymap"] = interaction::withoutRecordingAction(state["keymap"]);
    if (!protocol::atLeast(p.protocol, "0.9") && state.contains("keymap"))
        state["keymap"] = hyprune::interaction::legacyInputNames(state["keymap"]);
    if (!protocol::atLeast(p.protocol, "0.11") && state.contains("movement")) {
        for (auto key : {"state", "ladderId", "prompt"})
            state["movement"].erase(key);
    }
    if (protocol::atLeast(p.protocol, "0.8"))
        return state;
    if (state.contains("keymap"))
        state["keymap"].erase("holds");
    if (protocol::atLeast(p.protocol, "0.6"))
        return state;
    state.erase("keymap");
    state.erase("editor");
    if (state.contains("presentation") && state["presentation"].contains("fov"))
        state["presentation"]["fov"] = std::max(30., state["presentation"]["fov"].get<double>());
    if (protocol::atLeast(p.protocol, "0.5")) {
        if (state.contains("apps") && state["apps"].size() > 16)
            state["apps"].erase(state["apps"].begin() + 16, state["apps"].end());
        return state;
    }
    if (state.is_object()) {
        state.erase("interaction");
        state.erase("apps");
    }
    if (state.contains("mounts") && state["mounts"].contains("placements"))
        std::erase_if(state["mounts"]["placements"].get_ref<Json::array_t &>(),
                      [](auto &p) { return p.contains("pose"); });
    if (protocol::atLeast(p.protocol, "0.4"))
        return state;
    if (state.is_object()) {
        state.erase("mounts");
        state.erase("presentation");
        if (protocol::atLeast(p.protocol, "0.3"))
            return state;
        state.erase("spatial");
        state.erase("workspaces");
        if (protocol::atLeast(p.protocol, "0.2"))
            return state;
        state.erase("location");
        state.erase("integrations");
    }
    return state;
}
Json Server::snapshot(const Peer &p) const {
    return {{"sessionId", session_}, {"revision", revision_}, {"state", visibleState(p)}};
}
void Server::notify(const std::string &event, Json params, const std::string &actor) {
    if (event != "runtime.notice") {
        params["sessionId"] = session_;
        params["revision"] = revision_;
    }
    if (event == "capture.taken") {
        // Debug capture IDs are shell business: 0.17 shells only, never older protocols.
        for (auto &p : peers_)
            if (p.hello && !p.closing && protocol::atLeast(p.protocol, "0.17") && p.grants.contains("shell.control") &&
                p.grants.contains("state.read") && !p.subscription.empty())
                queue(p, {{"jsonrpc", "2.0"}, {"method", event}, {"params", params}});
        return;
    }
    if (event == "quest.progress" || event == "focus.phase") {
        // RFC-0012 presentation hints: IPC 0.21 state readers with a state subscription.
        for (auto &p : peers_)
            if (p.hello && !p.closing && protocol::atLeast(p.protocol, "0.21") && p.grants.contains("state.read") &&
                !p.subscription.empty())
                queue(p, {{"jsonrpc", "2.0"}, {"method", event}, {"params", params}});
        return;
    }
    const bool hud = event.starts_with("hud.") || event.starts_with("zone.") || event == "tool.changed";
    if (hud || event == "ui.hud.cycle") {
        for (auto &p : peers_) {
            if (!p.hello || p.closing || !protocol::atLeast(p.protocol, "0.9") || !p.grants.contains("state.read") ||
                (!actor.empty() && actor != p.actor))
                continue;
            if (event == "ui.hud.cycle") {
                // Never broadcast shell-local commands, even if a caller forgets its target.
                if (actor.empty() || p.subscription.empty())
                    continue;
            } else {
                if (!p.hudTopics.contains(event))
                    continue;
                auto now = Clock::now();
                auto interval = std::chrono::milliseconds(event == "hud.frame" ? 250 : event == "hud.pose" ? 34 : 0);
                if (interval.count() && (now - p.hudLast[event] < interval || !p.output.empty()))
                    continue; // Latest sample wins; slow readers never build a telemetry backlog.
                p.hudLast[event] = now;
            }
            auto payload = params;
            if (!protocol::atLeast(p.protocol, "0.26")) {
                if (event == "hud.pose" && payload.value("tool", 1) == 9) {
                    payload["tool"] = 1;
                    payload["subtype"] = "normal";
                }
                if (event == "tool.changed" && payload.value("slot", 1) == 9) {
                    payload["slot"] = 1;
                    payload["name"] = "pointer";
                    payload["subtype"] = "normal";
                }
            }
            if (!protocol::atLeast(p.protocol, "0.15")) {
                if (event == "hud.pose" && payload.value("tool", 1) == 5) {
                    payload["tool"] = 1;
                    payload["subtype"] = "normal";
                }
                if (event == "tool.changed" && payload.value("slot", 1) == 5) {
                    payload["slot"] = 1;
                    payload["name"] = "pointer";
                    payload["subtype"] = "normal";
                }
            }
            if (hud)
                payload["subscriptionId"] = p.hudSubscription;
            queue(p, {{"jsonrpc", "2.0"}, {"method", event}, {"params", payload}});
        }
        return;
    }
    for (auto &p : peers_)
        if (p.hello && !p.closing && protocol::atLeast(p.protocol, "0.2") && !p.subscription.empty() &&
            (protocol::atLeast(p.protocol, "0.4") || (event != "mounts.changed" && event != "presentation.changed"))) {
            auto projected = params;
            if (!protocol::atLeast(p.protocol, "0.26") && event == "mounts.changed" && projected.contains("mounts"))
                shortMountInstances(projected["mounts"]);
            if (!protocol::atLeast(p.protocol, "0.15") && event == "mounts.changed" && projected.contains("mounts")) {
                for (auto &v : projected["mounts"]["placements"]) v.erase("content");
                if (projected["mounts"].contains("instances")) std::erase_if(projected["mounts"]["instances"].get_ref<Json::array_t &>(),
                    [](const auto &v){ return !v.contains("areaId"); });
            }
            if (!protocol::atLeast(p.protocol,"0.13") && event=="presentation.changed") {
                auto &view=projected["presentation"];
                if(view.contains("profile") && view["profile"].is_object())view["profile"].erase("outline");
                if(view.contains("fov"))view["fov"]=std::min(110.,view["fov"].get<double>());
            }
            if (!protocol::atLeast(p.protocol, "0.5") && event == "mounts.changed" && projected.contains("mounts") &&
                projected["mounts"].contains("placements"))
                std::erase_if(projected["mounts"]["placements"].get_ref<Json::array_t &>(),
                              [](auto &placement) { return placement.contains("pose"); });
            queue(p, {{"jsonrpc", "2.0"}, {"method", event}, {"params", projected}});
        }
}
void Server::publish(Json state) {
    if (state == state_)
        return;
    if (state.dump(-1, ' ', false, Json::error_handler_t::replace).size() > MaxFrame - 4096)
        throw Error(-32008, "snapshot resource limit");
    state_ = std::move(state);
    ++revision_;
}
void Server::queue(Peer &p, const Json &value, const std::string &method) {
    audit_->check(p.protocol, value, method);
    auto s = value.dump(-1, ' ', false, Json::error_handler_t::replace);
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
    std::string method;
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
        method = request["method"];
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
            auto selected = protocol::negotiate(protocols);
            check(selected.has_value(), -32001, "incompatible protocol");
            p.protocol = *selected;
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
                                              "shell.control", "runtime.control", "data.publish",    "input.control", "camera.author",
                                              "quest.control",  "objects.control"};
            std::set<std::string> requested;
            for (auto &cap : params["capabilities"]) {
                check(cap.is_string(), -32602, "invalid capability");
                auto name = cap.get<std::string>();
                check(known.contains(name) && (name != "quest.control" || protocol::atLeast(p.protocol, "0.21")) &&
                          (name != "objects.control" || protocol::atLeast(p.protocol, "0.22")) &&
                          (name != "camera.author" || protocol::atLeast(p.protocol,"0.13")) && (name != "input.control" || protocol::atLeast(p.protocol, "0.6")) &&
                          (protocol::atLeast(p.protocol, "0.2") || name != "data.publish") &&
                          requested.insert(name).second,
                      -32602, "invalid capability list");
            }
            if (params.contains("token")) {
                check(params["token"].is_string() &&
                          std::regex_match(params["token"].get<std::string>(), std::regex("[a-f0-9]{64}")),
                      -32602, "invalid token format");
                bool valid = false;
                for (auto it = tokens_.begin(); it != tokens_.end(); ++it)
                    if (it->expires > Clock::now() && (!it->recipientPid || it->recipientPid == p.pid) && it->token == params["token"].get<std::string>() &&
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
            if (p.grants.contains("state.read") && protocol::atLeast(p.protocol, "0.9"))
                for (auto m : {"hud.subscribe", "hud.unsubscribe"})
                    methods.push_back(m);
            for (auto &[m, grant] : methods_)
                if (p.grants.contains(grant) && protocol::methodAvailable(p.protocol, m))
                    methods.push_back(m);
            Json events = Json::array();
            if (p.grants.contains("state.read")) {
                events.push_back("state.delta");
                if (protocol::atLeast(p.protocol, "0.9"))
                    for (auto e : {"hud.frame", "hud.pose", "zone.entered", "zone.exited", "tool.changed", "ui.hud.cycle"})
                        events.push_back(e);
                if (protocol::atLeast(p.protocol, "0.2"))
                    for (auto event : {"location.transition", "ui.opened", "ui.closed", "data.updated"})
                        events.push_back(event);
                if (protocol::atLeast(p.protocol, "0.4"))
                    for (auto event : {"mounts.changed", "presentation.changed"})
                        events.push_back(event);
                if (protocol::atLeast(p.protocol, "0.6"))
                    events.push_back("runtime.notice");
                if (protocol::atLeast(p.protocol, "0.17") && p.grants.contains("shell.control"))
                    events.push_back("capture.taken");
                if (protocol::atLeast(p.protocol, "0.21"))
                    for (auto e : {"quest.progress", "focus.phase"})
                        events.push_back(e);
            }
            result = {{"protocol", p.protocol},
                      {"sessionId", session_},
                      {"capabilities", p.grants},
                      {"methods", methods},
                      {"events", events}};
        } else if (method == "session.hello")
            throw Error(-32600, "hello already completed");
        else if (method == "session.ping") {
            exact(params, {});
            result = {{"monotonicMs",
                       std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count()}};
        } else if (method == "hud.subscribe" || method == "hud.unsubscribe") {
            check(protocol::atLeast(p.protocol, "0.9") && p.grants.contains("state.read"), -32601, "method not advertised");
            if (method == "hud.subscribe") {
                exact(params, {"topics"});
                check(params["topics"].is_array() && !params["topics"].empty() && params["topics"].size() <= 5,
                      -32602, "choose 1-5 HUD topics");
                std::set<std::string> topics;
                for (auto &topic : params["topics"]) {
                    check(topic.is_string(), -32602, "invalid HUD topic");
                    auto name = topic.get<std::string>();
                    check(name == "hud.frame" || name == "hud.pose" || name == "zone.entered" ||
                          name == "zone.exited" || name == "tool.changed", -32602, "unknown HUD topic");
                    check(topics.insert(name).second, -32602, "duplicate HUD topic");
                }
                check(p.hudSubscription.empty(), -32004, "already subscribed");
                p.hudSubscription = "hud-" + std::to_string(++subscriptions_);
                p.hudTopics = std::move(topics);
                p.hudLast.clear();
                result = {{"subscriptionId", p.hudSubscription}};
            } else {
                exact(params, {"subscriptionId"});
                check(!p.hudSubscription.empty() && params["subscriptionId"] == p.hudSubscription,
                      -32003, "unknown subscription");
                p.hudSubscription.clear();
                p.hudTopics.clear();
                result = {{"ok", true}};
            }
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
            check(protocol::methodAvailable(p.protocol, method), -32601, "method not advertised for protocol");
            auto it = methods_.find(method);
            check(it != methods_.end() && p.grants.contains(it->second), -32601, "method not advertised");
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
            if (method.starts_with("input.") && !protocol::atLeast(p.protocol, "0.12"))
                result = interaction::withoutRecordingAction(std::move(result));
            if (method.starts_with("input.") && !protocol::atLeast(p.protocol, "0.21"))
                result = interaction::withoutMarkersReveal(std::move(result));
        }
        queue(p, {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}, method);
    } catch (const Error &e) {
        Json error{{"code", e.code}, {"message", boundedText(e.what(), MessageLimit)}};
        // Error data (the refusing placement rule) is an IPC 0.22 shape; older peers get none.
        if (e.data.is_object() && protocol::atLeast(p.protocol, "0.22"))
            error["data"] = e.data;
        queue(p, {{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}});
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
