#include <fstream>
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
// IPC 0.26: tool 9 named on the wire (editor.debugTool, the slot-9 definition, tool9), the
// companion's pace and 128-item pocket and mount instance lists; 0.25 peers keep the 0.25 view.
// IPC 0.27: object instances carry screen; 0.26 peers see the 0.26 instance shape.
void workspaceScreens(const std::string &runtime) {
    std::vector<LaunchGrant> grants{{randomId(), "org.hyprune.test", {"state.read"}}, {randomId(), "org.hyprune.test", {"state.read"}}};
    Server server(runtime, "ipc027", grants, [](auto &, auto &) { return Json{{"ok", true}}; });
    Json screen = {{"workspace", 23}, {"home", "screen-u-7c2e91"}, {"aspect", "16:9"}, {"size", {3.2, 1.8}}};
    server.publish({{"objects", {{"instances", Json::array({Json{{"id", "u-7c2e91"}, {"screen", screen}},
                                                           Json{{"id", "lamp"}, {"screen", nullptr}}})}}}});
    size_t index = 0;
    for (std::string version : {"0.26", "0.27"}) {
        const bool latest = version == "0.27";
        Client c(server.path());
        auto request = [&](Json j) {
            c.sendJson(j);
            for (int i = 0; i < 3; ++i)
                server.poll();
            return c.receive();
        };
        auto h = hello(grants[index++].token);
        h["params"]["protocols"] = {version};
        check(request(h)["result"]["protocol"] == version, "0.27 negotiation");
        auto state = request(req("sub", "state.subscribe", {{"topics", {"state"}}}))["result"]["snapshot"]["state"];
        auto &instances = state["objects"]["instances"];
        check(instances[0].contains("screen") == latest && instances[1].contains("screen") == latest, "screen projection");
        check(!latest || instances[0]["screen"] == screen, "screen kept for 0.27");
    }
}
void debugToolPacePocket(const std::string &runtime) {
    std::vector<LaunchGrant> grants{{randomId(), "org.hyprune.test", {"state.read"}}, {randomId(), "org.hyprune.test", {"state.read"}}};
    Server server(runtime, "ipc026", grants, [](auto &, auto &) { return Json{{"ok", true}}; });
    Json windows = Json::array(), instances = Json::array(), definitions = Json::array();
    for (int i = 0; i < 40; ++i)
        windows.push_back({{"surfaceId", "surface-" + std::to_string(i)}});
    for (int i = 0; i < 20; ++i)
        instances.push_back({{"surfaceId", "placed-" + std::to_string(i)}});
    for (int slot : {1, 2, 3, 4, 5, 9})
        definitions.push_back({{"slot", slot}});
    Json debugTool = {{"layer", "collision"}, {"label", "Collision & backstops"}, {"index", 2}, {"count", 10},
                      {"legend", Json::array({Json{{"label", "floor"}, {"colour", {0.3, 0.9, 0.4}}}})}, {"card", nullptr}};
    Json mounts = {{"instances", instances}};
    server.publish({{"interaction", {{"tool", 9}}},
                    {"editor", {{"tool", 9}, {"submode", "collision"}, {"debugTool", debugTool}, {"toolDefinitions", definitions},
                                {"unplaced", {{"count", 41}, {"selectedId", "surface-30"}, {"windows", windows}, {"objects", Json::array()}}}}},
                    {"keymap", {{"config", {{"modes", {{"world", {{"tool5", {"5"}}, {"tool9", {"9"}}}}}}}}}},
                    {"mounts", mounts},
                    {"companion", {{"mode", "follow"}, {"pace", "run"}}}});
    size_t index = 0;
    for (std::string version : {"0.25", "0.26"}) {
        const bool latest = version == "0.26";
        Client c(server.path());
        auto request = [&](Json j) {
            c.sendJson(j);
            for (int i = 0; i < 3; ++i)
                server.poll();
            return c.receive();
        };
        auto h = hello(grants[index++].token);
        h["params"]["protocols"] = {version};
        check(request(h)["result"]["protocol"] == version, "0.26 negotiation");
        auto state = request(req("sub", "state.subscribe", {{"topics", {"state"}}}))["result"]["snapshot"]["state"];
        auto &e = state["editor"];
        check(state["interaction"]["tool"] == (latest ? 9 : 1) && e["tool"] == (latest ? 9 : 1) &&
                  e["submode"] == (latest ? "collision" : "normal"),
              "tool 9 projection");
        check(e.contains("debugTool") == latest && (!latest || e["debugTool"] == debugTool), "editor.debugTool projection");
        check(e["toolDefinitions"].size() == (latest ? 6u : 5u) && e["toolDefinitions"].back()["slot"] == (latest ? 9 : 5),
              "slot-9 definition projection");
        check(state["keymap"]["config"]["modes"]["world"].contains("tool9") == latest, "tool9 keymap projection");
        check(state["companion"].contains("pace") == latest && state["companion"]["mode"] == "follow", "companion pace projection");
        auto &listed = e["unplaced"]["windows"];
        check(listed.size() == (latest ? 40u : 16u) && e["unplaced"]["count"] == (latest ? 41 : 32), "pocket list projection");
        check(std::any_of(listed.begin(), listed.end(), [](const Json &w) { return w["surfaceId"] == "surface-30"; }),
              "the selected pocketed window stays listed");
        check(state["mounts"]["instances"].size() == (latest ? 20u : 16u) && state["mounts"]["instances"][0]["surfaceId"] == "placed-0",
              "mount instance projection");
        server.notify("mounts.changed", {{"mounts", mounts}});
        server.flush();
        auto event = c.receive();
        check(event["method"] == "mounts.changed" && event["params"]["mounts"]["instances"].size() == (latest ? 20u : 16u),
              "mounts.changed instance projection");
        request(req("hud", "hud.subscribe", {{"topics", {"hud.pose", "tool.changed"}}}));
        server.notify("tool.changed", {{"slot", 9}, {"name", "debug"}, {"subtype", "collision"}});
        server.flush();
        auto changed = c.receive()["params"];
        check(changed["slot"] == (latest ? 9 : 1) && changed["name"] == (latest ? "debug" : "pointer"), "tool.changed projection");
        server.notify("hud.pose", {{"tool", 9}, {"subtype", "collision"}});
        server.flush();
        auto pose = c.receive()["params"];
        check(pose["tool"] == (latest ? 9 : 1) && pose["subtype"] == (latest ? "collision" : "normal"), "hud.pose projection");
    }
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
// IPC 0.33 (RFC-0016): session.pair before hello waits for the user's answer (once, always,
// deny); the token binds the assistant profile (a fixed allowlist, a filtered state, limits,
// pause); always-approvals are remembered without a token and revoked; disconnect closes.
void assistantPairing(const std::string &runtime) {
    std::string approvals = runtime + "/approvals.json";
    setenv("HYPRUNE_APPROVALS", approvals.c_str(), 1);
    auto shellToken = randomId();
    Server server(runtime, "ipc033", {{shellToken, "org.hyprune.shell", {"state.read", "shell.control"}}},
                  [](auto &, auto &) { return Json{{"ok", true}}; });
    server.contextual(true);
    server.advertise({{"objects.place", "objects.control"}, {"objects.catalog", "state.read"}, {"surface.focus", "surface.control"},
                      {"camera.set", "movement.control"}, {"data.publish", "data.publish"}, {"input.set", "input.control"}, {"world.load", "world.control"}});
    server.publish({{"objects", {{"instances", Json::array()}}}, {"surfaces", Json::array({Json{{"title", "secret mail"}}})},
                    {"live", {{"values", {{"long", std::string(100, 'x')}}}}}, {"builder", nullptr}});
    auto pump = [&] { for (int i = 0; i < 3; ++i) server.poll(); };
    auto unread = [](Client &c) { pollfd p{c.fd, POLLIN, 0}; return ::poll(&p, 1, 20) > 0; };
    auto call = [&](Client &c, Json j) { c.sendJson(j); pump(); return c.receive(); };
    Client shell(server.path());
    auto h = hello(shellToken); h["params"]["protocols"] = {"0.33"}; h["params"]["client"]["id"] = "org.hyprune.shell";
    h["params"]["capabilities"] = {"state.read", "shell.control"};
    auto shellHello = call(shell, h)["result"];
    check(std::find(shellHello["methods"].begin(), shellHello["methods"].end(), "connections.answer") != shellHello["methods"].end(), "shell gets connections.*");
    call(shell, req("sub", "state.subscribe", {{"topics", {"state"}}}));
    auto connections = [&] { return call(shell, req(randomId().substr(0, 8), "state.snapshot", {{"subscriptionId", "subscription-1"}}))["result"]["snapshot"]["state"]["connections"]; };
    Json pairing = {{"protocols", {"0.33"}}, {"client", {{"id", "com.hyprune.mcp"}, {"name", "Hyprune MCP server"}}},
                    {"agent", {{"name", "Example Chat"}, {"version", "1.4"}}}, {"profile", "assistant"}};
    // Once: the answer waits for the user; the token binds the profile.
    Client a(server.path());
    a.sendJson(req("pair", "session.pair", pairing)); pump();
    check(!unread(a), "pairing answered before the user did");
    auto pending = connections()["pending"];
    check(pending.size() == 1 && pending[0]["agent"]["name"] == "Example Chat" && pending[0]["clientId"] == "com.hyprune.mcp", "pending request in state");
    call(shell, req("answer", "connections.answer", {{"requestId", pending[0]["requestId"]}, {"decision", "once"}}));
    auto paired = a.receive();
    check(paired["result"]["profile"] == "assistant" && paired["result"]["remembered"] == false, "pairing result");
    auto ah = hello(paired["result"]["token"]); ah["params"]["protocols"] = {"0.33"}; ah["params"]["client"]["id"] = "com.hyprune.mcp";
    ah["params"]["capabilities"] = {"state.read", "surface.control"};
    auto aHello = call(a, ah)["result"];
    auto has = [](const Json &list, const char *m) { return std::find(list.begin(), list.end(), m) != list.end(); };
    check(aHello["profile"] == "assistant" && has(aHello["methods"], "objects.place") && has(aHello["methods"], "assistant.report") &&
              !has(aHello["methods"], "surface.focus") && !has(aHello["methods"], "hud.subscribe") && !has(aHello["capabilities"], "surface.control") &&
              aHello["events"] == Json::array({"state.delta"}),
          "assistant hello: the allowlist, not the request");
    for (auto m : {"surface.focus", "camera.set", "data.publish", "input.set"}) {
        auto r = call(a, req(m, m, Json::object()));
        check(r["error"]["code"] == -32002 && r["error"]["data"]["rule"] == "profile.assistant", "refused outside the profile");
    }
    auto seen = call(a, req("s", "state.subscribe", {{"topics", {"state"}}}))["result"]["snapshot"]["state"];
    check(!seen.contains("surfaces") && seen.contains("objects") && seen["live"]["values"]["long"].get<std::string>().size() <= 64, "filtered state");
    check(call(a, req("p1", "objects.place", {{"ref", "user/x"}}))["result"]["ok"] == true, "assistant places");
    auto live = connections()["live"];
    check(live.size() == 1 && live[0]["changes"] == 1 && live[0]["paused"] == false, "live connection counted");
    auto actor = live[0]["actor"];
    call(shell, req("pause", "connections.pause", {{"actor", actor}, {"paused", true}}));
    auto paused = call(a, req("p2", "objects.place", {{"ref", "user/x"}}));
    check(paused["error"]["data"]["rule"] == "assistant.paused" && paused["error"]["data"]["retryable"] == true, "pause refuses changes");
    check(call(a, req("p3", "objects.place", {{"ref", "user/x"}, {"dryRun", true}})).contains("result"), "dry runs pass a pause");
    call(shell, req("resume", "connections.pause", {{"actor", actor}, {"paused", false}}));
    int refused = 0;
    for (int i = 0; i < 25; ++i)
        refused += call(a, req("burst" + std::to_string(i), "objects.place", {{"ref", "user/x"}})).contains("error");
    check(refused >= 5, "20 changes a second");
    auto report = call(a, req("r", "assistant.report", {{"changes", 40}, {"action", "placed the bench"}}))["result"];
    check(report["changes"] == 40 && report["paused"] == false && connections()["live"][0]["action"] == "placed the bench", "report");
    // Deny.
    Client b(server.path());
    b.sendJson(req("pair", "session.pair", pairing)); pump();
    call(shell, req("deny", "connections.answer", {{"requestId", connections()["pending"][0]["requestId"]}, {"decision", "deny"}}));
    check(b.receive()["error"]["data"]["rule"] == "pair.denied", "deny");
    // Always: remembered by client id and agent label, without a token.
    auto other = pairing; other["agent"] = {{"name", "Tool X"}};
    Client c(server.path());
    c.sendJson(req("pair", "session.pair", other)); pump();
    call(shell, req("always", "connections.answer", {{"requestId", connections()["pending"][0]["requestId"]}, {"decision", "always"}}));
    auto token = c.receive()["result"]["token"].get<std::string>();
    std::ifstream file(approvals);
    std::string saved((std::istreambuf_iterator<char>(file)), {});
    check(saved.find("Tool X") != std::string::npos && saved.find(token) == std::string::npos, "approval stored, never the token");
    Client d(server.path());
    auto remembered = call(d, req("pair", "session.pair", other))["result"];
    check(remembered["remembered"] == true && remembered["token"].is_string(), "remembered approval");
    check(call(shell, req("revoke", "connections.revoke", {{"clientId", "com.hyprune.mcp"}, {"agent", "Tool X"}}))["result"]["removed"] == true, "revoke");
    check(connections()["approvals"].empty(), "revoked");
    // A fourth prompt within the minute is refused.
    Client e(server.path());
    check(call(e, req("pair", "session.pair", other))["error"]["data"]["rule"] == "pair.limit", "three prompts a minute");
    // Disconnect closes the assistant's connection.
    check(call(shell, req("disconnect", "connections.disconnect", {{"actor", actor}}))["result"]["disconnected"] == true, "disconnect");
    pump();
    char byte;
    check(recv(a.fd, &byte, 1, 0) <= 0, "disconnected assistant closed");
    // Pairing needs 0.33, and an older shell sees no connections.
    Client f(server.path());
    auto old = pairing; old["protocols"] = {"0.32"};
    check(call(f, req("pair", "session.pair", old))["error"]["code"] == -32001, "pairing below 0.33");
    unsetenv("HYPRUNE_APPROVALS");
}
int main() {
    char pattern[] = "/tmp/hn-ipc-XXXXXX";
    char *dir = mkdtemp(pattern);
    if (!dir)
        return 1;
    std::atomic<bool> stop = false;
    std::thread loop;
    try {
        inheritance(dir);
        debugToolPacePocket(dir);
        workspaceScreens(dir);
        hudStreams(dir);
        renewals(dir);
        assistantPairing(dir);
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
