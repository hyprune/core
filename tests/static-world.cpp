// World package geometry is static: only nodes that own collision may move.
#include "world/World.hpp"
#include <iostream>
using namespace hyprune;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
} // namespace
int main(int argc, char **argv) {
    try {
        // Synthetic scene: 0 root group; 1 building (visual only); 2 crate (visual + own collision);
        // 3 group holding 4 (visual) and 5 (its collision); 6 standalone collision mesh (walk floor).
        world::World w;
        w.nodeParents = {-1, 0, 0, 0, 3, 3, 0};
        w.collisionNodes = {2, 2, 5, 6, 6, 6};
        world::markCollisionOwners(w);
        check(!world::movableNode(w, 1), "a visual-only building must be static");
        check(world::movableNode(w, 2), "a node with its own collision may move (collision moves with it)");
        check(world::movableNode(w, 3) && !world::movableNode(w, 4), "a group owns its collision child; its visual leaf alone does not");
        check(!world::movableNode(w, 99), "unknown nodes are static");

        // A real package: every rendered mesh without collision of its own is static.
        if (argc > 1) {
            auto atrium = world::load(argv[1]);
            size_t statics = 0;
            for (auto &m : atrium->meshes) {
                bool owns = false;
                for (auto c : atrium->collisionNodes)
                    for (int i = int(c); i >= 0; i = atrium->nodeParents[i])
                        owns = owns || unsigned(i) == m.node;
                check(world::movableNode(*atrium, m.node) == owns, "package mesh movability follows collision ownership");
                statics += !owns;
            }
            check(statics > 0, "fixture has static visual meshes");
        }

        std::cout << "static world: only collision-owning nodes may move\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
