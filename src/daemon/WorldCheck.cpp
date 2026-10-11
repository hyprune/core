#include "world/World.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("usage: hyprune-world-check PACKAGE");
        auto w = hyprune::world::load(argv[1]);
        for (auto &warning : w->warnings) std::cerr << "warning: " << warning << '\n';
        // Without sRGB-decode control an image used as colour and data needs two copies.
        auto bytes = hyprune::world::gpuBytes(*w, false, w->environmentHigh);
        if (bytes > hyprune::world::gpuBudget)
            throw std::runtime_error("world GPU budget exceeded including mip/color/environment views");
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
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
