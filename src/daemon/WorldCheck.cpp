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
