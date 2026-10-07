#include "world/World.hpp"
#include <iostream>
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("usage: hyprune-world-check PACKAGE");
        auto w = hyprune::world::load(argv[1]);
        size_t bytes = 0;
        for (auto &mesh : w->meshes)
            bytes += mesh.vertices.size() * sizeof(hyprune::world::Vertex);
        for (size_t i = 0; i < w->images.size(); i++) {
            bool color = false;
            for (auto &m : w->materials)
                color |= m.slots[0].texture == int(i) || m.slots[4].texture == int(i);
            int x = w->images[i].width, y = w->images[i].height;
            while (true) {
                bytes += size_t(x) * y * 4 * (color ? 2 : 1);
                if (x == 1 && y == 1)
                    break;
                x = std::max(1, x / 2);
                y = std::max(1, y / 2);
            }
        }
        for (auto &level : w->environmentHigh)
            bytes += size_t(level.size) * level.size * 6 * 8;
        if (bytes > 512 * 1024 * 1024)
            throw std::runtime_error("world GPU budget exceeded including mip/color/environment views");
        std::cout << hyprune::world::Json{{"worldId", w->id},
                                          {"areas", w->areas},
                                          {"anchors", w->anchors.size()},
                                          {"objects", w->objects.size()},
                                          {"placementZones", w->placementZones.size()},
                                          {"estimatedGpuBytes", bytes},
                                          {"collisionTriangles", w->collision.size()}}
                         .dump(2)
                  << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
