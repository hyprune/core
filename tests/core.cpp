#include "surfaces/Surface.hpp"
#include "world/World.hpp"
#include <iostream>
int main(int argc, char **argv) {
    if (argc < 2)
        return 1;
    try {
        auto w = hyprune::world::load(argv[1]);
        std::cout << w->id << ": " << w->meshes.size() << " meshes\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
