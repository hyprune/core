#include "control/Control.hpp"
#include <iostream>
using namespace hyprune;
using Json = ipc::Json;
void check(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "fixture argument");
        auto world = world::load(argv[1]);
        control::Control control;
        sim::Camera camera;
        control.reset(world.get());
        auto call = [&](std::string m, Json p = Json::object(), std::string actor = "a") {
            return control.execute(m, p, actor, "org.test.client", world.get(), camera, Json::array({"output"}));
        };
        auto rejects = [&](std::string m, Json p, int code, std::string actor = "a") {
            try {
                call(m, p, actor);
            } catch (const ipc::Error &e) {
                check(e.code == code, "wrong error");
                return;
            }
            throw std::runtime_error("accepted invalid command " + m);
        };
        call("camera.set", {{"position", {0, 1.7, 4}}, {"orientation", {0, 0, 0, 1}}});
        check(call("camera.get")["position"][2] == 4, "camera round trip");
        rejects("camera.set", {{"position", {0, 1, 0}}, {"orientation", {0, 0, 0, 2}}}, -32602);
        call("waypoint.set", {{"id", "a"}, {"label", "A"}, {"position", {0, 1.7, 3}}});
        check(!control.location["waypoint"].is_null(), "waypoint");
        rejects("route.set", {{"points", Json::array()}}, -32602);
        call("route.set", {{"points", Json::array({Json{{"position", {0, 1.7, 2}}}})}});
        call("autodrive.start", {{"speed", 3}});
        for (int i = 0; i < 100; i++)
            control.tick(.016f, camera, world.get());
        check(!control.driving() && std::abs(camera.position.z - 2) < .01, "autodrive arrival");
        call("travel.start", {{"to", {{"position", {1, 1.7, 2}}}}, {"durationMs", 500}});
        rejects("travel.start", {{"to", {{"position", {2, 1.7, 2}}}}}, -32004);
        for (int i = 0; i < 40; i++)
            control.tick(.016f, camera, world.get());
        check(control.location["transition"]["phase"] == "arrived", "travel completion");
        check(std::abs(camera.position.x - 1) < .01, "travel destination");
        auto lease = call("shell.claim", {{"outputs", {"output"}}})["leaseId"];
        rejects("ui.open", {{"leaseId", lease}, {"outputId", "output"}, {"panel", "overview"}}, -32004, "b");
        rejects("shell.overlay",
                {{"leaseId", lease}, {"outputId", "output"}, {"visible", true}, {"exclusiveInput", true}}, -32007);
        control.associate("a", true);
        call("shell.overlay",
             {{"leaseId", lease}, {"outputId", "output"}, {"visible", true}, {"exclusiveInput", true}});
        check(control.exclusive(), "verified overlay not exclusive");
        control.associate("a", false);
        check(!control.exclusive() && !control.uiVisible(), "association loss retained input");
        rejects("shell.overlay",
                {{"leaseId", lease}, {"outputId", "output"}, {"visible", true}, {"exclusiveInput", true}}, -32007);
        call("ui.open", {{"leaseId", lease}, {"outputId", "output"}, {"panel", "overview"}});
        check(control.uiVisible(), "ui open");
        call("ui.close", {{"leaseId", lease}, {"outputId", "output"}});
        check(!control.uiVisible(), "ui close");
        control.hold(true);
        for (int i = 0; i < 3; i++)
            control.tick(.1f, camera, world.get());
        check(!control.uiVisible(), "hold opened too early");
        for(int i=0;i<7;i++) control.tick(.1f, camera, world.get());
        check(control.uiVisible(), "hold deadline");
        control.hold(false);
        call("data.publish",
             {{"sourceId", "org.test.client.metrics"}, {"sequence", uint64_t(1)}, {"values", {{"cpuPercent", 12}}}});
        rejects("data.publish",
                {{"sourceId", "org.test.client.metrics"}, {"sequence", uint64_t(2)}, {"values", Json::object()}},
                -32008);
        rejects("data.publish",
                {{"sourceId", "org.other.metrics"}, {"sequence", uint64_t(1)}, {"values", Json::object()}}, -32002);
        control.disconnect("a");
        check(control.leases.empty() && control.sources.empty(), "disconnect cleanup");
        auto removed = call("shell.claim", {{"outputs", {"output"}}});
        control.retainOutputs(Json::array());
        check(control.leases.empty(), "output removal retained lease");
        control.beginLoad({{"worldId", "org.hyprune.next"}}, "output");
        auto transition = control.location["transition"];
        check(transition["phase"] == "loading" && transition["progress"].is_null(), "indeterminate loading");
        control.endLoad(false);
        check(control.location["transition"]["id"] == transition["id"] &&
                  control.location["transition"]["phase"] == "failed",
              "load failure identity");
        std::cout << "PASS: camera, routes, travel, ownership, hold timing, data grants and disconnect cleanup\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
