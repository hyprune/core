#include "control/Control.hpp"
#include "ipc/Protocol.hpp"
#include "ipc/Server.hpp"
#include "ipc/GrantIssuer.hpp"
#include "quests/Engine.hpp"
#include "world/Catalog.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
using namespace hyprune::ipc;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Client {
    int fd;
    explicit Client(const std::string &path) {
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strcpy(addr.sun_path, path.c_str());
        check(connect(fd, (sockaddr *)&addr, sizeof addr) == 0, "connect");
        timeval timeout{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    }
    ~Client() { close(fd); }
    void sendBytes(std::string bytes, bool split = false) {
        uint32_t n = htonl(bytes.size());
        std::string frame((char *)&n, 4);
        frame += bytes;
        for (size_t offset = 0; offset < frame.size();) {
            size_t size = split ? 1 : frame.size() - offset;
            auto n = send(fd, frame.data() + offset, size, MSG_NOSIGNAL);
            check(n > 0, "send");
            offset += n;
        }
    }
    void sendJson(Json j, bool split = false) { sendBytes(j.dump(), split); }
    Json receive() {
        auto exact = [&](char *b, size_t size) {
            size_t got = 0;
            while (got < size) {
                auto n = recv(fd, b + got, size - got, 0);
                check(n > 0, "response timed out/closed");
                got += n;
            }
        };
        uint32_t size;
        exact((char *)&size, 4);
        size = ntohl(size);
        check(size <= 1048576, "oversized response");
        std::string bytes(size, '\0');
        exact(bytes.data(), size);
        return Json::parse(bytes);
    }
};
Json req(std::string id, std::string method, Json params = Json::object()) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
}
Json hello(const std::string &token = "") {
    Json p = {{"protocols", {"0.1"}},
              {"client", {{"id", "org.hyprune.test"}, {"name", "Test"}}},
              {"capabilities", {"state.read"}}};
    if (!token.empty())
        p["token"] = token;
    return req("hello", "session.hello", p);
}
void renewals(const std::string &runtime) {
    std::vector<LaunchGrant> original{{randomId(), "org.hyprune.test", {"state.read"}}};
    Server server(runtime, "renew", original, [](auto &,auto &){return Json{{"ok",true}};});
    int pair[2];check(socketpair(AF_UNIX,SOCK_SEQPACKET,0,pair)==0,"renew socketpair");
    GrantIssuer issuer(pair[0],original);
    auto issue=[&](Json j){auto bytes=j.dump();send(pair[1],bytes.data(),bytes.size(),0);issuer.poll(server);char b[1024];auto n=recv(pair[1],b,sizeof b,0);check(n>0,"renew response");return Json::parse(b,b+n).at("ok").get<bool>();};
    auto token=randomId();
    check(!issue({{"client","org.hyprune.unknown"},{"token",token},{"pid",getpid()}}),"unknown identity renewal");
    check(!issue({{"client","org.hyprune.test"},{"token",token},{"pid",getpid()},{"capabilities",{"world.control"}}}),"renewal escalation");
    check(issue({{"client","org.hyprune.test"},{"token",token},{"pid",getpid()+1}}),"PID-bound token issuance");
    auto attempt=[&](const std::string &t){Client c(server.path());c.sendJson(hello(t));for(int i=0;i<3;i++)server.poll();return c.receive();};
    check(attempt(token).contains("error"),"wrong PID used renewal token");
    token=randomId();check(issue({{"client","org.hyprune.test"},{"token",token},{"pid",getpid()}}),"fresh renewal");
    check(attempt(token).contains("result"),"renewal authentication");
    check(attempt(token).contains("error"),"renewal token reused");
    auto expired=LaunchGrant{randomId(),"org.hyprune.test",{"state.read"},getpid()};expired.expires=std::chrono::steady_clock::now()-std::chrono::seconds(1);
    check(server.reissue(expired),"expiry fixture");check(attempt(expired.token).contains("error"),"expired renewal accepted");
    close(pair[1]);
}
void hudStreams(const std::string &runtime) {
    auto token = randomId();
    Server server(runtime, "hud", {{token, "org.hyprune.test", {"state.read"}}},
                  [](auto &, auto &p) { return Json{{"actor", p.at("_actor")}}; });
    server.contextual(true);
    server.advertise({{"camera.get", "state.read"}});
    Client c(server.path());
    auto request = [&](Json j) {
        c.sendJson(j);
        for (int i = 0; i < 3; ++i) server.poll();
        return c.receive();
    };
    auto h = hello(token); h["params"]["protocols"] = {"0.9"};
    check(request(h)["result"]["protocol"] == "0.9", "HUD negotiation");
    auto actor = request(req("actor", "camera.get"))["result"]["actor"].get<std::string>();
    auto unread = [&] { pollfd p{c.fd, POLLIN, 0}; return ::poll(&p, 1, 5) > 0; };
    server.notify("hud.pose", {{"heading", 0}}); server.flush();
    check(!unread(), "telemetry without opt-in");
    auto sub = request(req("hud", "hud.subscribe", {{"topics", {"hud.pose", "zone.entered"}}}))["result"]["subscriptionId"];
    server.notify("hud.frame", {{"fps", 60}}); server.flush();
    check(!unread(), "unrequested telemetry stream");
    server.notify("hud.pose", {{"heading", 1}}); server.flush();
    auto pose = c.receive();
    check(pose["params"]["subscriptionId"] == sub && pose["params"]["heading"] == 1, "HUD subscription scope");
    server.notify("hud.pose", {{"heading", 2}}); server.flush();
    check(!unread(), "HUD exceeded 30 Hz");
    server.notify("zone.entered", {{"zone", "home"}}); server.flush();
    check(c.receive()["method"] == "zone.entered", "zone event delivery");
    request(req("state", "state.subscribe", {{"topics", {"state"}}}));
    server.notify("ui.hud.cycle", {{"sequence", 1}}); server.flush();
    check(!unread(), "HUD cycle broadcast leaked");
    server.notify("ui.hud.cycle", {{"sequence", 1}}, "wrong-actor"); server.flush();
    check(!unread(), "HUD cycle actor mismatch");
    server.notify("ui.hud.cycle", {{"sequence", 1}}, actor); server.flush();
    check(c.receive()["params"]["sequence"] == 1, "targeted cycle delivery");
    request(req("unsub", "hud.unsubscribe", {{"subscriptionId", sub}}));
    server.notify("zone.entered", {{"zone", "home"}}); server.flush();
    check(!unread(), "HUD unsubscribe did not stop delivery");
}
// Exercise every negotiated version, not a hand-picked latest pair. Appending
// Supported automatically includes the next release in the inheritance matrix.
void inheritance(const std::string &runtime) {
    using namespace hyprune::ipc::protocol;
    check(atLeast("0.10", "0.9") && !atLeast("0.9", "0.10") && atLeast("1.0", "0.99"), "numeric versions");
    check(exactly("0.8", "0.8") && !exactly("0.8", "0.7"), "exact versions");
    for (auto invalid :
         {"", "0", ".8", "0.", "0.8.1", "00.8", "0.08", "+0.8", "0.-1", "0.8x", " 0.8", "4294967296.0", "0.4294967296"})
        check(!parse(invalid) && !atLeast(invalid, "0.1") && !exactly(invalid, invalid),
              "malformed version passed gate");
    check(negotiate(Supported) == Latest, "latest negotiation");
    check(!negotiate(std::array{"9.9", "invalid"}), "unknown offer negotiated");
    check(negotiate(std::array{"9.9", "0.1", "invalid"}) == "0.1", "compatible fallback");

    auto methods = hyprune::control::Control::methods();
    for (auto m :
         {"world.load",      "surface.focus",       "movement.set",      "runtime.exit",       "workspace.bind",
          "workspace.focus", "workspace.unfocus",   "surface.pull",      "surface.place",      "surface.return",
          "surfaces.recall", "placements.list",     "placements.remove", "data.object.bind",   "camera.fov",
          "atmosphere.get",  "atmosphere.set",      "shell.bind",        "workspace.activate", "overlay.style",
          "graphics.get",    "graphics.set",        "input.get",         "input.preview",      "input.set",
          "input.capture",   "input.capture.cancel", "player.get",       "player.set",
          "surface.pocket",  "surface.unpocket"})
        methods[m] = "state.read"; // Version eligibility is independent of grants.
    for (auto &[m, grant] : hyprune::quests::Engine::methods())
        methods[m] = "state.read";
    methods["markers.reveal"] = "state.read";
    for (auto &[m, grant] : hyprune::world::objectMethods())
        methods[m] = "state.read";
    const std::set<std::string> allGrants{"state.read",      "world.control", "surface.control", "movement.control",
                                          "runtime.control", "shell.control", "data.publish",    "input.control", "camera.author",
                                          "quest.control",   "objects.control"};
    std::vector<LaunchGrant> grants;
    for (size_t i = 0; i < Supported.size(); ++i)
        grants.push_back({randomId(), "org.hyprune.test", allGrants});
    Server server(runtime, "inheritance", grants, [](auto &method, auto &) -> Json {
        if (method == "objects.place") // a refused placement names its rule in error data (0.22 only)
            throw Error(-32007, "walls: inside a wall", Json{{"retryable", false}, {"rule", "walls"}});
        return Json{{"ok", true}};
    });
    server.advertise(methods);
    const Json mounts{{"placements", Json::array({Json{{"pose", true}}, Json{{"anchorId", "wall"}}})},
                      {"instances",Json::array({Json{{"surfaceId","free"}},Json{{"surfaceId","home"},{"areaId","room"}}})}};
    server.publish({{"movement", {{"state", "climbing"}, {"ladderId", "hatch"}, {"prompt", "W/S climb"}}},
                    {"runtime", Json::object()},
                    {"location", Json::object()},
                    {"integrations", {{"sources",Json::array({{{"values",{{"outline",true}}}}})}}},
                    {"spatial", Json{{"map", {{"svg", "<svg/>"}, {"sha256", std::string(64, 'a')}, {"bounds", {0, 0, 1, 1}},
                                             {"worldToMap", {1, 0, 0, 1, 0, 0}},
                                             {"levels", Json::array({Json{{"id", "ground"}, {"name", "Ground"}, {"minY", -2}, {"maxY", 4},
                                                                          {"svg", "<svg/>"}, {"sha256", std::string(64, 'b')}}})}}}}},
                    {"workspaces", Json::object()},
                    {"mounts", mounts},
                    {"presentation", {{"fov", 10},{"profile",{{"outline",{{"enabled",true}}}}}}},
                    {"interaction", {{"mode", "cursor"}, {"reticle", {{"state", "valid"}, {"action", "interact"}}}}},
                    {"apps", Json::array_t(17, Json::object())},
                    {"editor", {{"mode", "cursor"}, {"selectedKind", "node"}, {"submode", "universal"},
                                {"gizmo", {{"space", "world"}, {"pivot", "base"}, {"snap", false}}},
                                {"unplaced", {{"count", 2}, {"preview", true}, {"selectedId", "pocket-1"},
                                              {"windows", Json::array({Json{{"surfaceId", "app"}}})},
                                              {"objects", Json::array({Json{{"itemId", "pocket-1"}}})}}},
                                {"paths", Json::array({Json{{"id", "path-1"}, {"worldId", "w"}, {"areaId", ""}, {"closed", false},
                                                            {"points", Json::array()}, {"use", "route"}, {"name", nullptr},
                                                            {"speed", nullptr}, {"stops", Json::array()}}})}}},
                    {"objects", {{"catalogRevision", 1},
                                 {"instances", Json::array({Json{{"id", "npc"}, {"state", "suspended"}, {"character", true},
                                                                 {"accent", {1, 0, 0}}, {"path", "user/path-1"},
                                                                 {"activity", "suspended"}, {"seat", nullptr}}})}}},
                    {"npcs", {{"motion", "auto"}, {"state", "animating"}}},
                    {"live", {{"instances", Json::array()}}},
                    {"keymap", {{"holds", Json::array({Json{{"action", "menu.open"}}})},
                                {"config", {{"modes", {{"world", {{"menu", {"Tab[L]"}}, {"markers.reveal", {"Tab"}}}}}}}}}},
                    {"markers", {{"reveal", nullptr}, {"list", Json::array()}}},
                    {"quests", {{"enabled", true}}},
                    {"focus", {{"state", "idle"}}}});
    std::atomic<bool> sendMounts = false;
    std::jthread polling([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            server.poll();
            if (sendMounts.exchange(false))
                server.notify("mounts.changed", {{"mounts", mounts}});
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    Json previousMethods = Json::array(), previousEvents = Json::array(), previousState = Json::object();
    size_t index = 0;
    for (auto version : Supported) {
        Client c(server.path());
        auto h = hello(grants[index++].token);
        h["params"]["protocols"] = {version};
        auto requested = allGrants;
        if (!atLeast(version, "0.2"))
            requested.erase("data.publish");
        if (!atLeast(version, "0.6"))
            requested.erase("input.control");
        if (!atLeast(version,"0.13")) requested.erase("camera.author");
        if (!atLeast(version, "0.21")) {
            // quest.control is unknown before 0.21: requesting it fails the hello.
            Client early(server.path());
            auto refused = hello(); // no token: the capability check comes first
            refused["params"]["protocols"] = {version};
            refused["params"]["capabilities"] = {"state.read", "quest.control"};
            early.sendJson(refused);
            check(early.receive().contains("error"), "quest.control accepted before 0.21");
            requested.erase("quest.control");
        }
        if (!atLeast(version, "0.22")) {
            Client early(server.path());
            auto refused = hello();
            refused["params"]["protocols"] = {version};
            refused["params"]["capabilities"] = {"state.read", "objects.control"};
            early.sendJson(refused);
            check(early.receive().contains("error"), "objects.control accepted before 0.22");
            requested.erase("objects.control");
        }
        h["params"]["capabilities"] = requested;
        c.sendJson(h);
        auto response = c.receive().at("result");
        check(response["protocol"] == version && response["capabilities"] == Json(requested), "version grants");
        auto advertised = response.at("methods"), events = response.at("events");
        for (const auto &m : previousMethods)
            check(std::find(advertised.begin(), advertised.end(), m) != advertised.end(),
                  "inherited method disappeared");
        for (const auto &e : previousEvents)
            check(std::find(events.begin(), events.end(), e) != events.end(), "inherited event disappeared");
        for (const auto &[method, grant] : methods) {
            const bool available = std::find(advertised.begin(), advertised.end(), method) != advertised.end();
            check(available == methodAvailable(version, method), "advertisement disagrees with eligibility");
            check(!available || methodAvailable("0.99", method), "future version lost method");
            c.sendJson(req(method, method));
            auto result = c.receive();
            if (available && method == "objects.place")
                check(result["error"]["code"] == -32007 && result["error"]["data"]["rule"] == "walls",
                      "0.22 refusal lost its rule");
            else
                check(available ? result.contains("result") : result["error"]["code"] == -32601,
                      "dispatch disagrees with advertisement");
        }
        c.sendJson(req("sub", "state.subscribe", {{"topics", {"state"}}}));
        auto state = c.receive().at("result").at("snapshot").at("state");
        for (auto it = previousState.begin(); it != previousState.end(); ++it)
            check(state.contains(it.key()), ("inherited state section disappeared: " + it.key() + " at " + version).c_str());
        if (atLeast(version, "0.4")) {
            check(state["mounts"]["instances"].size() == (atLeast(version,"0.15") ? 2 : 1), "unzoned instance legacy projection");
            check(state["mounts"]["placements"].size() == (atLeast(version, "0.5") ? 2 : 1),
                  "snapshot pose projection");
            sendMounts = true;
            auto event = c.receive();
            check(event["method"] == "mounts.changed" && event["params"]["mounts"] == state["mounts"],
                  "mount event lost pose or disagrees with snapshot");
        }
        if (atLeast(version, "0.6"))
            check(state["apps"].size() == 17 && state["presentation"]["fov"] == 10,
                  "latest state inherited old limits");
        if (atLeast(version, "0.8"))
            check(state["keymap"]["holds"].size() == 1, "latest state lost hold timing");
        check(state["movement"].contains("ladderId") == atLeast(version, "0.11"), "climbing version projection");
        if (atLeast(version, "0.11"))
            check(state["movement"]["state"] == "climbing", "climbing state lost");
        if (atLeast(version, "0.6")) {
            check(state["editor"]["mode"] == (atLeast(version, "0.12") ? "cursor" : "interactive"), "cursor editor projection");
            check(state["interaction"]["mode"] == (atLeast(version, "0.12") ? "cursor" : "typing"), "cursor interaction projection");
            check(state["interaction"].contains("reticle") == atLeast(version, "0.19"), "reticle state version projection");
        }
        check(methodAvailable(version, "player.set") == atLeast(version, "0.19"), "player settings floor");
        check(methodAvailable(version, "surface.pocket") == atLeast(version, "0.20") &&
                  methodAvailable(version, "surface.unpocket") == atLeast(version, "0.20"),
              "pocket floor");
        if (atLeast(version, "0.3")) // spatial (with the map) exists from 0.3
            check(state.at("spatial").at("map").contains("levels") == atLeast(version, "0.20"),
                  "blueprint levels version projection");
        for (auto key : {"markers", "quests", "focus"})
            check(state.contains(key) == atLeast(version, "0.21"), "quest state version projection");
        if (atLeast(version, "0.6"))
            check(state["keymap"]["config"]["modes"]["world"].contains("markers.reveal") == atLeast(version, "0.21"),
                  "markers.reveal keymap projection");
        check((std::find(events.begin(), events.end(), "quest.progress") != events.end()) == atLeast(version, "0.21") &&
                  (std::find(events.begin(), events.end(), "focus.phase") != events.end()) == atLeast(version, "0.21"),
              "quest events floor");
        for (auto m : {"markers.reveal", "quests.create", "focus.start", "guidance.get"})
            check(methodAvailable(version, m) == atLeast(version, "0.21"), "quest methods floor");
        check(state.contains("objects") == atLeast(version, "0.22"), "objects state version projection");
        // IPC 0.23 (RFC-0011 P2): NPC state, character fields, path fields, the interact reticle.
        bool npcs = atLeast(version, "0.23");
        check(state.contains("npcs") == npcs, "npcs state version projection");
        check(state.contains("live") == atLeast(version, "0.24"), "live state version projection (RFC-0013)");
        if (atLeast(version, "0.22")) {
            auto &i = state["objects"]["instances"][0];
            check(i.contains("character") == npcs && i.contains("accent") == npcs &&
                      i["path"] == (npcs ? Json("user/path-1") : Json(nullptr)) && i["activity"] == (npcs ? Json("suspended") : Json(nullptr)) &&
                      i["state"] == (npcs ? "suspended" : "active"),
                  "character instance projection");
        }
        if (atLeast(version, "0.6"))
            check(state["editor"]["paths"][0].contains("use") == npcs && state["editor"]["paths"][0].contains("stops") == npcs,
                  "editor path projection");
        if (atLeast(version, "0.19"))
            check(state["interaction"]["reticle"]["action"] == (npcs ? "interact" : "click"), "interact reticle projection");
        for (auto m : {"npcs.spawn", "npcs.bind", "npcs.disable", "npcs.remove", "paths.list", "paths.set", "paths.remove",
                       "paths.draw", "paths.finish", "interaction.choose"})
            check(methodAvailable(version, m) == npcs, "npcs methods floor");
        for (auto m : {"objects.catalog", "objects.place", "objects.configure", "libraries.install", "libraries.list"})
            check(methodAvailable(version, m) == atLeast(version, "0.22"), "objects methods floor");
        if (atLeast(version, "0.6")) {
            auto &e = state["editor"];
            bool latest = atLeast(version, "0.22");
            check(e["selectedKind"] == (latest ? "node" : "object") && e["submode"] == (latest ? "universal" : "move") &&
                      e.contains("gizmo") == latest,
                  "objects editor projection");
            if (atLeast(version, "0.15")) // the pocket exists from 0.15
                check(e["unplaced"].contains("objects") == latest && e["unplaced"]["count"] == (latest ? 2 : 1) &&
                          e["unplaced"]["selectedId"] == (latest ? "pocket-1" : "app"),
                      "objects pocket projection");
        }
        previousMethods = advertised;
        previousEvents = events;
        if (atLeast(version,"0.4")) check(state["presentation"]["profile"].contains("outline")==atLeast(version,"0.13"),"ink compatibility projection");
        if(atLeast(version,"0.2"))check(state["integrations"]["sources"][0]["values"]["outline"]==true,"compatibility rewrote integration values");
        previousState = state;
    }
}
} // namespace
int main() {
    char pattern[] = "/tmp/hn-ipc-XXXXXX";
    char *dir = mkdtemp(pattern);
    if (!dir)
        return 1;
    std::atomic<bool> stop = false;
    std::thread loop;
    try {
        inheritance(dir);
        hudStreams(dir);
        renewals(dir);
        auto token = randomId(), token02 = randomId(), token03 = randomId(), token04 = randomId(), token05 = randomId();
        auto token06 = randomId(), token07 = randomId(), token08 = randomId();
        std::atomic<int> disconnected = 0;
        std::atomic<bool> notice = false;
        Server server(dir, "test",
                      {{token06, "org.hyprune.test", {"state.read"}},
                       {token07, "org.hyprune.test", {"state.read"}},
                       {token08, "org.hyprune.test", {"state.read"}},
                       {token, "org.hyprune.test", {"state.read"}},
                       {token02, "org.hyprune.test", {"state.read", "data.publish"}},
                       {token03, "org.hyprune.test", {"state.read"}},
                       {token04, "org.hyprune.test", {"state.read"}},
                       {token05, "org.hyprune.test", {"state.read"}}},
                      [&](const auto &method, const auto &params) {
                          check(params.contains("_actor") && params.contains("_client"), "missing trusted context");
                          if (method == "_connection.closed")
                              ++disconnected;
                          return Json{{"actor", params["_actor"]}, {"client", params["_client"]}};
                      });
        server.contextual(true);
        server.advertise({{"graphics.get", "state.read"},
                          {"graphics.set", "world.control"},
                          {"camera.get", "state.read"},
                          {"data.publish", "data.publish"},
                          {"placements.list", "state.read"}});
        server.publish({{"runtime", {{"active", false}}},
                        {"location", {{"waypoint", nullptr}}},
                        {"integrations", {{"sources", Json::array()}}},
                        {"spatial", Json::object()},
                        {"workspaces", Json::object()},
                        {"mounts", {{"placements", Json::array({Json{{"pose", true}}})}}},
                        {"interaction", {{"mode", "typing"}}},
                        {"keymap", {{"holds", Json::array()}}},
                        {"apps", Json::array_t(17, Json{{"surfaceId", "desktop-window-1"},
                                                        {"workspaceId", 1},
                                                        {"title", "Terminal"},
                                                        {"appClass", "kitty"},
                                                        {"icon", "kitty"}})},
                        {"presentation", Json::object()}});
        loop = std::thread([&] {
            while (!stop) {
                server.poll();
                if (notice.exchange(false))
                    server.notify("runtime.notice", {{"level", "warning"},
                                                     {"code", "placement.relocated"},
                                                     {"message", "Tool moved to a free mount"}});
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        {
            Client c(server.path());
            c.sendJson(hello(token), true);
            auto h = c.receive();
            check(h["result"]["protocol"] == "0.1", "split hello");
            c.sendJson(req("sub", "state.subscribe", {{"topics", {"state"}}}));
            auto result = c.receive();
            check(result.contains("result"), "subscribe");
            check(!result["result"]["snapshot"]["state"].contains("location"), "0.2 state leaked to 0.1");
            auto sub = result["result"]["subscriptionId"];
            c.sendJson(req("sub2", "state.subscribe", {{"topics", {"state"}}}));
            check(c.receive()["error"]["code"] == -32004, "duplicate subscription");
            c.sendJson(req("snap", "state.snapshot", {{"subscriptionId", sub}}));
            check(c.receive()["result"]["snapshot"]["revision"] == 1, "snapshot baseline");
            c.sendJson(req("ping1", "session.ping"));
            c.sendJson(req("ping2", "session.ping"));
            check(c.receive()["id"] == "ping1", "coalesced first");
            check(c.receive()["id"] == "ping2", "coalesced second");
            c.sendJson(req("ping1", "session.ping"));
            check(c.receive()["error"]["code"] == -32600, "reused request ID");
            c.sendJson(req("unknown", "camera.set"));
            check(c.receive()["error"]["code"] == -32601, "unadvertised method");
        }
        {
            Client c(server.path());
            c.sendJson(hello(token));
            check(c.receive()["error"]["code"] == -32002, "token replay");
        }
        {
            Client c(server.path());
            c.sendJson(hello());
            check(c.receive()["result"]["capabilities"].empty(), "anonymous granted capability");
            c.sendJson(req("read", "state.subscribe", {{"topics", {"state"}}}));
            check(c.receive()["error"]["code"] == -32601, "anonymous state leak");
        }
        {
            Client c(server.path());
            c.sendBytes("{\"id\":\"a\",\"id\":\"b\"}");
            check(c.receive()["error"]["code"] == -32700, "duplicate JSON key");
        }
        {
            Client c(server.path());
            uint32_t size = htonl(1048577);
            send(c.fd, &size, 4, MSG_NOSIGNAL);
            char b;
            check(recv(c.fd, &b, 1, 0) == 0, "oversized frame not closed");
        }
        {
            Client c(server.path());
            c.sendJson(req("first", "session.ping"));
            check(c.receive()["error"]["code"] == -32600, "hello sequencing");
        }
        {
            Client c(server.path());
            auto h = hello();
            h["params"]["capabilities"] = {"state.read", "state.read"};
            c.sendJson(h);
            check(c.receive()["error"]["code"] == -32602, "duplicate grant request accepted");
        }
        {
            Client c(server.path());
            auto h = hello();
            h["params"]["client"]["name"] = std::string(256, 'x');
            c.sendJson(h);
            check(c.receive().contains("result"), "valid maximum client name rejected");
        }
        {
            Client c(server.path());
            auto h = hello(token02);
            h["params"]["protocols"] = {"0.1", "0.2"};
            h["params"]["capabilities"] = {"state.read", "data.publish"};
            c.sendJson(h);
            auto result = c.receive();
            check(result["result"]["protocol"] == "0.2", "minor negotiation");
            c.sendJson(req("sub02", "state.subscribe", {{"topics", {"state"}}}));
            check(c.receive()["result"]["snapshot"]["state"].contains("location"), "0.2 state missing");
            c.sendJson(req("camera", "camera.get"));
            auto camera = c.receive();
            check(camera["result"]["client"] == "org.hyprune.test", "authenticated identity");
            check(!camera["result"]["actor"].get<std::string>().empty(), "connection identity");
            c.sendJson(req("spoof", "camera.get", {{"_actor", "forged"}}));
            check(c.receive()["error"]["code"] == -32602, "actor spoof accepted");
            c.sendJson(req("pub", "data.publish"));
            check(c.receive().contains("result"), "publishing grant absent");
        }
        for (auto [protocol, tokenValue] :
             {std::pair{"0.3", token03}, std::pair{"0.4", token04}, std::pair{"0.5", token05}}) {
            Client c(server.path());
            auto h = hello(tokenValue);
            h["params"]["protocols"] = {protocol};
            c.sendJson(h);
            auto negotiated = c.receive()["result"];
            check(negotiated["protocol"] == protocol, "M2 negotiation");
            bool m2 = protocol::atLeast(protocol, "0.4");
            check(std::find(negotiated["methods"].begin(), negotiated["methods"].end(), "placements.list") !=
                          negotiated["methods"].end()
                      ? m2
                      : !m2,
                  "M2 method version isolation");
            c.sendJson(req("version-sub", "state.subscribe", {{"topics", {"state"}}}));
            auto state = c.receive()["result"]["snapshot"]["state"];
            check(state.contains("spatial") && state.contains("mounts") == m2 && state.contains("presentation") == m2,
                  "frozen projection isolation");
            check(state.contains("interaction") == (std::string(protocol) == "0.5") &&
                      state.contains("apps") == (std::string(protocol) == "0.5"),
                  "hands state leaked into frozen version");
            if (std::string(protocol) == "0.5")
                check(state["apps"].size() == 16, "0.6 desktop budget leaked into 0.5");
            if (m2)
                check(state["mounts"]["placements"].size() == (std::string(protocol) == "0.5" ? 1 : 0),
                      "user pose leaked into frozen mounts");
            c.sendJson(req("placement-list", "placements.list"));
            auto response = c.receive();
            check(m2 ? response.contains("result") : response["error"]["code"] == -32601, "M2 direct method isolation");
            notice = true;
            auto event = c.receive();
            check(event["method"] == "runtime.notice" && event["params"].size() == 3, "frozen notice fields widened");
        }
        for (const auto *version : {"0.6", "0.7", "0.8"}) {
            Client c(server.path());
            auto h = hello(std::string(version) == "0.8" ? token08 : std::string(version) == "0.7" ? token07 : token06);
            h["params"]["protocols"] = {version};
            c.sendJson(h);
            auto response = c.receive()["result"];
            bool graphics = std::string(version) != "0.6";
            check(response["protocol"] == version, "graphics protocol negotiation");
            auto methods = response["methods"];
            check((std::find(methods.begin(), methods.end(), "graphics.get") != methods.end()) == graphics,
                  "graphics capability version isolation");
            check(std::find(methods.begin(), methods.end(), "graphics.set") == methods.end(),
                  "graphics write without grant");
            c.sendJson(req("gfx", "graphics.get"));
            auto get = c.receive();
            check(graphics ? get.contains("result") : get["error"]["code"] == -32601, "graphics request isolation");
            c.sendJson(req("holds-sub", "state.subscribe", {{"topics", {"state"}}}));
            auto snapshot = c.receive()["result"]["snapshot"]["state"];
            check(snapshot["keymap"].contains("holds") == (std::string(version) == "0.8"),
                  "hold state leaked into frozen protocol");
            c.sendJson(req("set", "graphics.set", {{"patch", {{"quality", "low"}}}}));
            check(c.receive()["error"]["code"] == -32601, "graphics write unauthorized");
        }
        for (int i = 0; i < 100 && disconnected < 2; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        check(disconnected >= 2, "disconnect ownership cleanup callback missing");
        stop = true;
        loop.join();
        std::filesystem::remove_all(dir);
        std::cout << "PASS: framing, auth, token replay, snapshots, IDs, malformed input, limits\n";
        return 0;
    } catch (const std::exception &e) {
        stop = true;
        if (loop.joinable())
            loop.join();
        std::filesystem::remove_all(dir);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
