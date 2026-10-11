#include "control/Control.hpp"
#include "daemon/LayoutStore.hpp"
#include "interaction/Config.hpp"
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
        surfaces::Layout adopted;
        surfaces::Instance opened{"new-live", "kitty", "new terminal", 1};
        sim::Camera spawnCamera;
        auto homeSpawn = world::areaSpawn(*w, "sun-court");
        spawnCamera.position = homeSpawn.position + Vec3{0, 1.7f, 0};
        surfaces::Surface proposed;
        proposed.id = opened.id;
        proposed.logicalSize(1600, 900, .002f);
        proposed.center = spawnCamera.position + spawnCamera.forward() * 5;
        adopted.spawn(opened, w.get(), spawnCamera, proposed);
        auto adoptedPlane = adopted.locate(opened, w.get(), spawnCamera, 1600, 900);
        check(adoptedPlane && !surfaces::overlaps(*adoptedPlane, w->collision),
              "unbound new window missing or colliding");
        check(adopted.spawnAreas.at(opened.id) == "sun-court", "new window chose wrong home");
        // In front of the player on floor it can use; the home's free zone is only the fallback.
        check(length(adoptedPlane->center - spawnCamera.position) < 5.5f &&
                  dot(adoptedPlane->center - spawnCamera.position, spawnCamera.forward()) > 1 &&
                  surfaces::reachable(*adoptedPlane, *w),
              "spawn not ahead of the player on reachable floor");
        check(adopted.bindings.empty() && adopted.placements.empty(), "spawn rewrote native workspace or saved homes");
        {
            // A client resize that no longer fits its spot (a 4K tile is a 7.7 m quad) shrinks
            // in place, same aspect, instead of hiding the window; the fit is stable.
            surfaces::Layout grown = adopted;
            auto big = grown.locate(opened, w.get(), spawnCamera, 3840, 2160);
            check(big && !surfaces::overlaps(*big, w->collision), "resized window vanished or collides");
            check(big->width < 3840 * .002f && std::abs(big->width / big->height - 3840.f / 2160.f) < .01f,
                  "resized window not shrunk in place with its aspect");
            auto again = grown.locate(opened, w.get(), spawnCamera, 3840, 2160);
            check(again && length(again->center - big->center) < 1e-4f && std::abs(again->width - big->width) < 1e-4f,
                  "shrink-to-fit not stable across refreshes");
            // Recall brings room clients (no home binding) to the player like the rest.
            surfaces::Instance live[] = {opened};
            grown.recall(live);
            auto recalled = grown.locate(opened, w.get(), spawnCamera, 1600, 900);
            check(recalled && length(recalled->center - spawnCamera.position) < 6, "recalled room client vanished");
        }
        surfaces::Instance second = opened;
        second.id = "new-live-second";
        proposed.id = second.id;
        adopted.spawn(second, w.get(), spawnCamera, proposed);
        auto secondPlane = adopted.locate(second, w.get(), spawnCamera, 1600, 900);
        check(secondPlane && length(secondPlane->center - adoptedPlane->center) > .1f,
              "zone clamping stacked newly spawned windows");
        proposed.id = opened.id;
        auto outside = *w;
        outside.collision.clear();
        surfaces::Layout arrival;
        spawnCamera.position = {-100, 1.7f, 3};
        proposed.center = spawnCamera.position + spawnCamera.forward() * 5;
        arrival.spawn(opened, &outside, spawnCamera, proposed);
        auto arrivalPlane = arrival.locate(opened, &outside, spawnCamera, 1600, 900);
        check(arrivalPlane && length(arrivalPlane->center - proposed.center) < .001f &&
                  std::abs(arrivalPlane->width - proposed.width) < .001f,
              "off-view home zone hid/shrank arriving client's fallback");
        {
            // World-derived default homes: workspace_base + workspaceSlot.
            surfaces::Layout homes;
            homes.workspaceBase = 10;
            homes.reservedWorkspaces = {8};
            homes.deriveDefaults(nullptr);
            check(homes.defaults.empty(), "defaults without a world");
            homes.deriveDefaults(w.get());
            check(homes.bindings.empty() && homes.defaults.size() == 2, "default homes not derived");
            check(homes.binding(11, w.get()) && homes.binding(11, w.get())->at("areaId") == "sun-court" &&
                      homes.binding(12, nullptr) && homes.binding(12, nullptr)->at("areaId") == "array-hall",
                  "slot 01/02 not at 11/12");
            check(!homes.binding(8, w.get()) && !homes.binding(10, w.get()), "default home on room/base");
            auto mounts = homes.state(w.get());
            check(mounts["bindings"].size() == 2 && mounts["bindings"][0].size() == 3, "defaults missing from state");
            check(daemon::durableLayout(mounts, {11, 12})["bindings"].empty(), "defaults persisted as user data");
            surfaces::Instance desk{"desk", "kitty", "desk", 11};
            homes.pull(desk, w.get());
            auto placedAt = homes.place(desk, "", w.get());
            check(placedAt.starts_with("sun-court-") &&
                      homes.placements[0]["areaId"] == "sun-court",
                  "default home did not own its area tools");
            // An explicit user bind wins by area and by workspace ID.
            homes.bind(11, w->id, "array-hall", w.get());
            check(homes.binding(11, w.get())->at("areaId") == "array-hall" && homes.defaults.empty() &&
                      !homes.binding(12, w.get()),
                  "explicit bind did not override defaults");
            homes.bind(21, w->id, "sun-court", w.get());
            check(homes.bindings.size() == 2 && homes.defaults.empty(), "explicit area bind kept a default");
            mounts = homes.state(w.get());
            check(daemon::durableLayout(mounts, {})["bindings"] == homes.bindings, "explicit bindings not durable");
            // A default home can be rebound explicitly without an "area already bound" error.
            surfaces::Layout rebound;
            rebound.workspaceBase = 10;
            rebound.deriveDefaults(w.get());
            rebound.bind(15, w->id, "sun-court", w.get());
            check(rebound.binding(15, w.get()) && !rebound.binding(11, w.get()) &&
                      rebound.binding(12, w.get())->at("areaId") == "array-hall",
                  "explicit rebind of a default home");
            // IDs are global: another world's explicit 11 suppresses this world's slot 01 default.
            auto other = *w;
            other.id = "org.hyprune.other";
            surfaces::Layout shared;
            shared.workspaceBase = 10;
            shared.deriveDefaults(&other);
            shared.bind(11, other.id, "array-hall", &other);
            shared.deriveDefaults(w.get());
            check(shared.defaults.size() == 1 && shared.binding(12, w.get()) && !shared.binding(11, w.get()) &&
                      shared.binding(11, nullptr)->at("worldId") == other.id,
                  "cross-world explicit ID did not win");
            // Restored user data replaces explicit homes; defaults are recomputed.
            shared.restore({{"bindings", world::Json::array()}, {"placements", world::Json::array()}});
            shared.deriveDefaults(w.get());
            check(shared.defaults.size() == 2, "defaults after restore");
            // Owner numbering (defaults): room 20, homes from 21; slot "00" would hit the room.
            {
                auto config = interaction::defaultConfig();
                surfaces::Layout owner;
                owner.workspaceBase = config["windows"]["workspace_base"];
                owner.reservedWorkspaces = {config["windows"]["world_workspace"].get<int>()};
                owner.deriveDefaults(w.get());
                check(owner.binding(21, w.get())->at("areaId") == "sun-court" &&
                          owner.binding(22, w.get())->at("areaId") == "array-hall" && !owner.binding(20, w.get()),
                      "default numbering is not room 20 / homes 21+");
            }
            // Reserved IDs (room workspace, configured locations) are never defaults; base 0 disables.
            shared.workspaceBase = 7;
            shared.reservedWorkspaces = {8};
            shared.deriveDefaults(w.get());
            check(shared.defaults.size() == 1 && shared.binding(9, w.get()), "default on reserved workspace");
            shared.workspaceBase = 0;
            shared.deriveDefaults(w.get());
            check(shared.defaults.empty(), "workspace_base 0 must disable defaults");
        }
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
        surfaces::Layout custom;
        custom.bind(10, w->id, "sun-court", w.get());
        auto userPlane = *plane;
        userPlane.center = {4, 2, 1};
        userPlane.width = 1.4f;
        userPlane.height = .7875f;
        custom.mount(terminal, w.get(), userPlane);
        auto durable = world::Json{{"bindings", custom.bindings}, {"placements", custom.placements}};
        custom = {};
        custom.restore(durable);
        auto mounted = custom.locate(terminal, w.get(), camera, 1600, 900);
        check(mounted && std::abs(mounted->center.x - 4) < .001, "custom mount restore");
        auto cached = custom.locate(terminal, w.get(), camera, 1600, 900);
        check(cached && length(cached->center - mounted->center) < .001f, "stable recovered pose cache");
        auto resized = custom.locate(terminal, w.get(), camera, 800, 450);
        check(resized && std::abs(resized->width - 1.6f) < .001f, "pose cache ignored logical resize");
        auto triangles = w->collision;
        auto c = mounted->center;
        w->collision.push_back({c + Vec3{-10, -10, 0}, c + Vec3{10, -10, 0}, c + Vec3{0, 10, 0}});
        custom.invalidateCollision();
        auto recovered = custom.locate(terminal, w.get(), camera, 1600, 900);
        check(recovered && !surfaces::overlaps(*recovered, w->collision), "editor geometry left stale pose cache");
        w->collision = std::move(triangles);
        custom.invalidateCollision();
        custom.pull(terminal, w.get());
        auto pulled = custom.locate(terminal, w.get(), camera, 1600, 900);
        check(pulled && length(pulled->center - mounted->center) > 1, "saved custom pose overrode hand pull");
        custom.recall(std::span(&terminal, 1));
        auto recalled = custom.locate(terminal, w.get(), camera, 1600, 900);
        check(recalled && length(recalled->center - mounted->center) > 1, "saved custom pose overrode recall");
        durable["placements"][0]["pose"]["rotation"] = {0, 0, 0, 0};
        rejects([&] { custom.restore(durable); });
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
