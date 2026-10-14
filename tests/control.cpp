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
        const auto previous = camera;
        for (float roll : {0.1f, 1.5707963f, -2.3f}) {
            sim::Camera lens; lens.roll=roll; lens.yaw=.3f; lens.pitch=.4f;
            call("camera.photo", {{"position",{2,3,4}},{"orientation",lens.orientation()},
                 {"fov",15},{"lensShift",{.3,-.2}},{"renderScale",.5}});
            auto actual=call("camera.get");
            check(actual["held"] && std::abs(actual["roll"].get<float>()-roll)<1e-5f,"photo roll lost");
            check(actual["fov"]==15 && actual["lensShift"]==Json::array({.3f,-.2f}),"photo lens lost");
            check(control.driving(),"photo camera does not freeze locomotion");
            for (float x : {.1f,.5f,.9f}) for(float y:{.2f,.7f}) {
                auto ray=camera.ray(x,y,1.777f);
                auto point=camera.project(camera.position+ray*5,1.777f);
                check(std::abs(point.x-x)<1e-5 && std::abs(point.y-y)<1e-5,"rolled shifted ray/project mismatch");
                auto clip=(camera.projection(1.777f)*camera.view()).point(camera.position+ray*5);
                float depth=dot(ray*5,camera.forward());
                check(std::abs((clip.x/depth+1)/2-x)<1e-5 && std::abs((1-clip.y/depth)/2-y)<1e-5,"matrix projection differs from markers");
            }
            rejects("camera.release",Json::object(),-32002,"other");
            rejects("camera.set",{{"position",{0,0,0}},{"orientation",{0,0,0,1}}},-32005);
            call("camera.release");
            check(camera.orientation()==previous.orientation() && camera.fov==previous.fov,"photo release changed original camera");
        }
        Json photo={{"position",{2,3,4}},{"orientation",{0,0,0,1}},{"fov",140},{"renderScale",1}};
        call("camera.photo",photo); control.disconnect("a",camera);
        check(!control.photoHeld() && camera.position.x==previous.position.x,"disconnect leaked camera hold");
        photo["fov"]=141; rejects("camera.photo",photo,-32602);
        photo["fov"]=4; rejects("camera.photo",photo,-32602);
        photo["fov"]=5; photo["lensShift"]={3,0}; rejects("camera.photo",photo,-32602);
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
        // The holder claiming its output again (its HUD came back after a session lock or a
        // display power cycle) renews the lease; another connection is still refused.
        call("ui.open", {{"leaseId", lease}, {"outputId", "output"}, {"panel", "overview"}});
        control.events.clear();
        auto renewed = call("shell.claim", {{"outputs", {"output"}}})["leaseId"];
        check(renewed != lease && control.leases.size() == 1 && control.leases[0]["id"] == renewed &&
                  control.ownsLease("a", renewed.get<std::string>()) && !control.ownsLease("a", lease.get<std::string>()),
              "a repeated claim from the holder is not a renewal");
        check(!control.uiVisible() && !control.events.empty() && control.events.back().first == "ui.closed",
              "the renewed lease's open panel did not close");
        rejects("shell.claim", {{"outputs", {"output"}}}, -32004, "b");
        rejects("shell.release", {{"leaseId", lease}}, -32004);
        lease = renewed;
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
        control.disconnect("a",camera);
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
