#include "interaction/Walkability.hpp"
#include "world/Catalog.hpp"
#include "world/Character.hpp"
#include "world/Paths.hpp"
#include "world/World.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("usage: hyprune-world-check PACKAGE");
        // World 0.9 library references resolve in the installed libraries (RFC-0011).
        auto roots = hyprune::world::CatalogRoots::fromEnvironment();
        hyprune::world::LoadOptions options;
        options.libraryRoots.push_back(roots.userLibraries);
        for (auto &r : roots.systemLibraries)
            options.libraryRoots.push_back(r);
        auto w = hyprune::world::load(argv[1], options);
        for (auto &warning : w->warnings) std::cerr << "warning: " << warning << '\n';
        // Without sRGB-decode control an image used as colour and data needs two copies.
        auto bytes = hyprune::world::gpuBytes(*w, false, w->environmentHigh);
        if (bytes > hyprune::world::gpuBudget)
            throw std::runtime_error("world GPU budget exceeded including mip/color/environment views");
        auto npcs = [&] {
                                               // World 0.9 (RFC-0011): characters, route walkability against
                                               // collision, the playable union and approach zones, and seat legs.
                                               namespace hw = hyprune::world;
                                               namespace hi = hyprune::interaction;
                                               hi::PlacementScene scene;
                                               scene.collision = w->collision;
                                               scene.index = &w->collisionIndex;
                                               scene.playable = &w->playable;
                                               scene.approaches = hi::approachZones(*w);
                                               for (auto &k : w->keepClear)
                                                   scene.keepClear.push_back(hi::aabbBox({k["min"][0], k["min"][1], k["min"][2]},
                                                                                         {k["max"][0], k["max"][1], k["max"][2]}));
                                               hw::Json routes = hw::Json::array(), characters = hw::Json::array();
                                               for (auto &p : w->paths) {
                                                   if (hw::pathUse(p) != "route")
                                                       continue;
                                                   auto route = hw::routeOf(p);
                                                   hw::Json problems = hw::Json::array();
                                                   for (auto &problem : hi::checkRoute(scene, route, {}))
                                                       problems.push_back({{"rule", problem.rule}, {"message", problem.message}});
                                                   // Stops: not inside keepClear volumes; seat approach legs walkable.
                                                   for (auto &s : p.value("stops", hw::Json::array())) {
                                                       auto at = route.at(route.points.at(s["point"].get<size_t>()));
                                                       for (auto &k : scene.keepClear)
                                                           if (hi::boxDistance(k, at + hyprune::Vec3{0, .5f, 0}) <= 0)
                                                               problems.push_back({{"rule", "keep-clear"}, {"message", "stop at point " + std::to_string(s["point"].get<int>()) + " is in a keepClear volume"}});
                                                   }
                                                   routes.push_back({{"id", p["id"]}, {"length", route.length()}, {"walkable", problems.empty()},
                                                                     {"problems", problems}});
                                               }
                                               for (auto &o : w->objectInstances)
                                                   if (o.character)
                                                       characters.push_back({{"id", o.id}, {"asset", w->objectAssets[o.asset].id}, {"bound", o.bound},
                                                                             {"position", {o.position.x, o.position.y, o.position.z}}});
                                               return hw::Json{{"characters", characters}, {"routes", routes},
                                                               {"seats", w->seats.size()}, {"keepClear", w->keepClear.size()}};
                                           }();
        std::cout << hyprune::world::Json{{"worldId", w->id}, {"warnings", w->warnings},
                                          {"areas", w->areas},
                                          {"anchors", w->anchors.size()},
                                          {"objects", w->objects.size()},
                                          {"placementZones", w->placementZones.size()},
                                          {"estimatedGpuBytes", bytes},
                                          {"collisionTriangles", w->collision.size()},
                                          {"worldObjects", [&] {
                                               // World 0.8 placed objects: assets (swept radius, animations) and instances.
                                               hyprune::world::Json assets = hyprune::world::Json::array(), list = hyprune::world::Json::array();
                                               for (auto &a : w->objectAssets) {
                                                   size_t triangles = 0;
                                                   for (auto &m : a.meshes) triangles += m.vertices.size() / 3;
                                                   assets.push_back({{"id", a.id}, {"animations", a.animations.size()}, {"meshes", a.meshes.size()},
                                                                     {"triangles", triangles}, {"proxyTriangles", a.collision.size()},
                                                                     {"sweptRadius", a.radius}, {"unlit", a.unlit}});
                                               }
                                               for (auto &o : w->objectInstances)
                                                   list.push_back({{"id", o.id}, {"asset", w->objectAssets[o.asset].id}, {"regions", o.regions},
                                                                   {"ambientUp", {o.ambientUp.x, o.ambientUp.y, o.ambientUp.z}},
                                                                   {"ambientDown", {o.ambientDown.x, o.ambientDown.y, o.ambientDown.z}},
                                                                   {"sun", o.sun}});
                                               return hyprune::world::Json{{"assets", assets}, {"instances", list}};
                                           }()},
                                          {"npcs", npcs},
                                          {"blueprintLevels", [&] {
                                               auto levels = hyprune::world::Json::array();
                                               if (w->map.is_object() && w->map.contains("levels"))
                                                   for (auto &l : w->map["levels"])
                                                       levels.push_back({{"id", l["id"]}, {"minY", l["minY"]}, {"maxY", l["maxY"]},
                                                                         {"bytes", l["svg"].get<std::string>().size()}});
                                               return levels;
                                           }()}}
                         .dump(2)
                  << '\n';
        // Release gate: authored routes are walkable (RFC-0011 "Walkability").
        int failed = 0;
        for (auto &r : npcs["routes"])
            if (!r["walkable"].get<bool>()) {
                ++failed;
                std::cerr << "route " << r["id"].get<std::string>() << " is not walkable: " << r["problems"][0]["message"].get<std::string>() << '\n';
            }
        return failed ? 1 : 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
