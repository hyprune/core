// World package geometry is static: only nodes that own collision may move. A window
// seen from behind is not clickable but can be carried, turning to face the player.
#include "interaction/Hands.hpp"
#include "world/World.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
using namespace hyprune;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
float dist(Vec3 a, Vec3 b) { return length(a - b); }
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

        // Back-face hits: a window at the origin facing -Z, camera at +Z looking -Z sees its back.
        surfaces::Surface s;
        s.id = "surface-1";
        s.center = {0, 1.7f, 0};
        s.width = 1.6f;
        s.height = .9f;
        s.rotation = {0, 1, 0, 0}; // 180 degrees about up: normal -Z
        sim::Camera camera;
        camera.position = {0, 1.7f, 3};
        Vec3 target{.3f, 1.8f, 0}; // grab off-centre so the grab point is checked
        Vec3 ray = normalized(target - camera.position);
        check(!surfaces::intersect(s, camera.position, ray), "clicks never hit a window from behind");
        auto back = surfaces::intersect(s, camera.position, ray, true, true);
        check(back && back->back && std::abs(back->distance - length(target - camera.position)) < 1e-4f, "carry sees the back face");
        auto front = surfaces::intersect(s, camera.position - Vec3{0, 0, 6}, normalized(target - (camera.position - Vec3{0, 0, 6})));
        check(front && !front->back, "front hits are unchanged");

        // Carry from behind: face the player, animated over 0.35 s around the grab point.
        interaction::Hands hands;
        hands.begin(interaction::Gesture::Carry, s, *back, camera);
        auto before = hands.pose;
        hands.facing(true, camera);
        hands.flipFrom(before, .35f);
        check(dist(hands.pose.normal(), before.normal()) < 1e-3f, "the flip starts from the grabbed orientation");
        check(dist(hands.pose.center, before.center) < 1e-3f, "no jump on the first frame");
        float halfway = 0;
        for (int i = 0; i < 30; i++) {
            hands.update(camera, ray, 1.f / 60, {});
            if (i == 9)
                halfway = hands.pose.normal().z;
        }
        check(halfway > -.99f && halfway < .99f, "the window turns over time, not at once");
        check(dist(hands.pose.normal(), {0, 0, 1}) < 1e-3f && hands.flip == 0, "after the flip the window faces the player");
        auto grabbed = surfaces::intersect(hands.pose, camera.position, ray);
        check(grabbed && std::abs(grabbed->u - back->u) < 1e-3f && std::abs(grabbed->v - back->v) < 1e-3f,
              "the grabbed point stays under the reticle");

        // Reduced motion: the same carry faces the player at once.
        interaction::Hands instant;
        instant.begin(interaction::Gesture::Carry, s, *back, camera);
        auto from = instant.pose;
        instant.facing(true, camera);
        instant.flipFrom(from, 0);
        check(instant.flip == 0 && dist(instant.pose.normal(), {0, 0, 1}) < 1e-3f, "reduced motion flips at once");

        // The sweep guarding the flip only tests geometry near it: the same answers, but no
        // longer proportional to the whole world (a Lumen Reach flip took ~0.5 s).
        std::vector<Triangle> world;
        for (int i = 0; i < 200000; i++) {
            float x = 100 + (i % 500) * .5f, z = 100 + (i / 500) * .5f;
            world.push_back({{x, 0, z}, {x + .4f, 0, z}, {x, .4f, z + .1f}});
        }
        auto t0 = std::chrono::steady_clock::now();
        check(interaction::sweepSafe(before, hands.pose, world), "far geometry never blocks the flip");
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        check(ms < 50, "the flip sweep ignores far geometry (" + std::to_string(ms) + " ms)");
        auto c = before.center;
        world.push_back({c + Vec3{-.3f, -.3f, .2f}, c + Vec3{.3f, -.3f, .2f}, c + Vec3{0, .3f, .2f}});
        check(!interaction::sweepSafe(before, hands.pose, world), "geometry in the flip's path still blocks it");
        std::cout << "static world: collision-owning nodes only; back-face carry flips to face the player\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
