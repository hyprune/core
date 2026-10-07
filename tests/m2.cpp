#include "control/Control.hpp"
#include "daemon/LayoutStore.hpp"
#include "surfaces/Layout.hpp"
#include "world/Mounts.hpp"
#include "world/Spatial.hpp"
#include <iostream>
using namespace hyprune;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("invalid operation accepted");
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    try {
        auto w = world::load(argv[1]);
        check(w->areas.size() == 2 && w->anchors.size() == 5, "home metadata");
        surfaces::Layout layout;
        layout.bind(10, w->id, "sun-court", w.get());
        layout.bind(11, w->id, "array-hall", w.get());
        rejects([&] { layout.bind(12, w->id, "sun-court", w.get()); });
        surfaces::Instance terminal{"terminal", "kitty", "M2 terminal", 10};
        layout.pull(terminal, w.get());
        sim::Camera camera;
        auto plane = layout.locate(terminal, w.get(), camera, 1600, 900);
        check(plane.has_value() && !surfaces::overlaps(*plane, w->collision), "carry collision");
        layout.instances = world::Json::array();
        auto id = layout.place(terminal, "sun-court-slot-01-operator-desk-01", w.get());
        plane = layout.locate(terminal, w.get(), camera, 1600, 900);
        check(plane && !surfaces::overlaps(*plane, w->collision), "desk collision");
        rejects([&] { layout.place(terminal, "array-hall-slot-02-banner-01", w.get()); });
        auto duplicate = terminal;
        duplicate.id = "duplicate";
        for (int n = 0; n < 3; n++) {
            layout.instances = world::Json::array();
            layout.locate(terminal, w.get(), camera, 1600, 900);
            layout.locate(duplicate, w.get(), camera, 1600, 900);
            check(layout.instances[0]["anchorId"] != layout.instances[1]["anchorId"], "duplicate rule occupied mount");
            check(layout.placements[0]["anchorId"] == id, "duplicate rule rewrote durable placement");
        }
        auto saved = world::Json{{"bindings", layout.bindings}, {"placements", layout.placements}};
        auto statePath = std::filesystem::temp_directory_path() / ("hyprune-layout-test-" + ipc::randomId());
        setenv("HYPRUNE_LAYOUT_FILE", statePath.c_str(), 1);
        {
            daemon::LayoutStore store;
            store.save(saved);
        }
        {
            daemon::LayoutStore store;
            check(store.load() == saved, "durable layout daemon restart");
        }
        std::filesystem::remove(statePath);
        unsetenv("HYPRUNE_LAYOUT_FILE");
        surfaces::Layout restored;
        restored.restore(saved);
        auto remote = *w;
        remote.id = "org.hyprune.remote";
        check(restored.pulled(terminal, nullptr), "global recall missed saved tool");
        restored.recall(std::span(&terminal, 1));
        check(restored.locate(terminal, &remote, camera, 1600, 900).has_value(), "cross-world recall invisible");
        check(restored.placements == saved["placements"], "recall rewrote durable layout");
        restored = surfaces::Layout{};
        restored.restore(saved);
        terminal.id = "relaunched";
        plane = restored.locate(terminal, w.get(), camera, 1600, 900);
        check(plane && std::abs(plane->center.x + 2.7f) < .001f, "placement relaunch");
        // Changed package: removed mount; no stale pose/identity is used.
        std::erase_if(w->anchors.get_ref<world::Json::array_t &>(), [&](auto &a) { return a.at("id") == id; });
        restored.instances = world::Json::array();
        plane = restored.locate(terminal, w.get(), camera, 1600, 900);
        check(plane && !surfaces::overlaps(*plane, w->collision) && !restored.events.empty(),
              "invalid restore recovery");
        // A thin wall cuts the middle of a large tool, with no rectangle corner inside it.
        std::vector<Triangle> wall = {{{-.2f, 0, 0}, {.2f, 3, 0}, {.2f, 0, 0}},
                                      {{-.2f, 0, 0}, {-.2f, 3, 0}, {.2f, 3, 0}}};
        surfaces::Surface box;
        box.center = {0, 1.5, 0};
        box.width = 3.2;
        box.height = 1.8;
        check(surfaces::overlaps(box, wall), "triangle OBB overlap");
        check(surfaces::pushOut(box, wall) && box.center.z >= .049, "normal pushout margin");
        camera.position = {0, 1.7, .2};
        camera.yaw = 0;
        box = surfaces::carryPlane(camera, 3.2, 1.8, wall);
        check(!surfaces::overlaps(box, wall) && box.center.z > .04, "carried tool crossed wall");
        // A small obstacle between support rays must stop the whole moving rectangle.
        std::vector<Triangle> sliver = {{{.32f, 1.35f, -1}, {.38f, 1.45f, -1}, {.38f, 1.35f, -1}}};
        camera.position = {0, 1.7f, 0};
        box = surfaces::carryPlane(camera, 3.2f, 1.8f, sliver);
        check(box.center.z > -.96f, "carry tunnelled between support rays");
        surfaces::Surface tilted;
        tilted.rotation = {std::sin(-.35f), 0, 0, std::cos(-.35f)};
        tilted.center = {0, 1.5, -2};
        auto normal = tilted.normal();
        auto hit = surfaces::intersect(tilted, tilted.center + normal * 3, normal * -1);
        check(hit && std::abs(hit->u - .5) < .001 && std::abs(hit->v - .5) < .001, "tilted picking");
        // Continuous 1:4 slope, ascending and descending: no stair stepping claimed.
        std::vector<Triangle> ramp = {{{-3, 0, 5}, {3, 3, -7}, {3, 0, 5}}, {{-3, 0, 5}, {-3, 3, -7}, {3, 3, -7}}};
        camera = {};
        camera.position = {0, 2.0f, 4};
        sim::Walk walk;
        for (int i = 0; i < 150; i++)
            walk.step(camera, {1, 0, 0, false}, 1.f / 60, ramp);
        check(camera.position.y > 3.2 && camera.grounded, "ascending slope");
        for (int i = 0; i < 60; i++)
            walk.step(camera, {-1, 0, 0, false}, 1.f / 60, ramp);
        check(camera.position.y < 3.3 && camera.grounded, "descending slope");
        auto routed = *w;
        routed.collision = ramp;
        routed.manifest["extensions"]["com.hyprune.lumen-reach.routes"] = {
            {"version", 1},
            {"nodes", world::Json::array({{{"id", "start"}, {"position", {0, 0, 5}}},
                                          {{"id", "bend"}, {"position", {0, 1.5, -1}}},
                                          {{"id", "finish"}, {"position", {0, 3, -7}}}})},
            {"edges", world::Json::array({{{"from", "start"}, {"to", "bend"}, {"width", 3}},
                                          {{"from", "bend"}, {"to", "finish"}, {"width", 3}}})}};
        world::loadSpatial(routed, argv[1]);
        check(routed.walkingNodes.size() == 3 && routed.places.size() == 3, "Lumen routing adapter");
        check(routed.places[1]["name"] == "bend", "unlabelled route node compatibility");
        auto &routeNodes = routed.manifest["extensions"]["com.hyprune.lumen-reach.routes"]["nodes"];
        routeNodes[0]["label"] = "Sun Court arrival";
        routeNodes[0]["waypoint"] = true;
        routeNodes[1]["waypoint"] = false;
        world::loadSpatial(routed, argv[1]);
        check(routed.walkingNodes.size() == 3 && routed.walkingEdges.size() == 2 && routed.places.size() == 2,
              "hidden waypoint removed pathfinding node or edge");
        check(routed.places[0]["id"] == "start" && routed.places[0]["name"] == "Sun Court arrival" &&
                  routed.places[1]["id"] == "finish" && routed.places[1]["name"] == "finish",
              "landmark labels or default waypoint visibility");
        for (auto bad : {world::Json("false"), world::Json(0), world::Json(nullptr)}) {
            routeNodes[1]["waypoint"] = bad;
            rejects([&] { world::loadSpatial(routed, argv[1]); });
        }
        routeNodes[1]["waypoint"] = false;
        for (auto bad : {world::Json(1), world::Json(""), world::Json(std::string(257, 'x'))}) {
            routeNodes[0]["label"] = bad;
            rejects([&] { world::loadSpatial(routed, argv[1]); });
        }
        routeNodes[0]["label"] = "Sun Court arrival";
        world::loadSpatial(routed, argv[1]);
        auto path = world::walkingRoute(routed, {0, 1.7, 5}, {0, 4.7, -7});
        check(path.size() == 3 && std::abs(path[1].y - 3.2f) < .001, "graph omitted ramp bend or feet offset");
        auto backwards = world::walkingRoute(routed, {0, 4.7, -7}, {0, 1.7, 5});
        check(backwards.size() == 3 && backwards[0].z == -7, "graph not bidirectional");
        control::Control drive;
        drive.reset(&routed);
        camera.position = {0, 1.7, 5};
        drive.execute("route.set", {{"points", world::Json::array({{{"position", {0, 4.7, -7}}}})}}, "test",
                      "org.hyprune.test", &routed, camera, world::Json::array());
        check(drive.location["route"].size() == 3, "Director one-point route bypassed graph");
        drive.execute("autodrive.start", {{"speed", 4}}, "test", "org.hyprune.test", &routed, camera,
                      world::Json::array());
        for (int i = 0; i < 500 && drive.driving(); i++)
            drive.tick(1.f / 60, camera, &routed);
        check(length(camera.position - Vec3{0, 4.7, -7}) < .01, "graph autodrive stopped on ramp");
        routed.manifest["extensions"]["com.hyprune.lumen-reach.routes"]["edges"][0]["to"] = "missing";
        rejects([&] { world::loadSpatial(routed, argv[1]); });
        std::cout << "PASS M2: area ownership, persistence/relaunch, invalid placement recovery, full OBB, carry wall "
                     "clamp, tilted picking, slopes\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
