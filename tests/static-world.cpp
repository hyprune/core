// World package geometry is static: only nodes that own collision may move. A window
// seen from behind is not clickable but can be carried, turning to face the player.
#include "interaction/Hands.hpp"
#include "world/World.hpp"
#include <algorithm>
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
        // 3 group holding 4 (visual) and 5 (its collision); 6 standalone collision mesh (walk floor);
        // 7 a collision-only root outside the drawn scene (how packages keep their walk meshes).
        world::World w;
        w.nodeParents = {-1, 0, 0, 0, 3, 3, 0, -1};
        w.collisionNodes = {2, 2, 5, 6, 6, 6, 7};
        for (unsigned node : {1u, 2u, 4u}) {
            world::Mesh mesh;
            mesh.node = node;
            w.meshes.push_back(mesh);
        }
        world::markCollisionOwners(w);
        check(!world::movableNode(w, 6) && !world::movableNode(w, 7), "collision that is never drawn is static");
        check(!world::movableNode(w, 5), "a group's collision child is not a target of its own");
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
        auto still = before;
        still.rotation = interaction::orientation({0, 0, 1});
        still.yaw = 0;
        auto f = still.center + Vec3{0, 0, .03f};
        std::vector<Triangle> sheet{{f + Vec3{-.1f, -.1f, 0}, f + Vec3{.1f, -.1f, 0}, f + Vec3{0, .1f, 0}}};
        check(!interaction::sweepSafe(still, still, sheet), "a sheet within the 4 cm margin in front still blocks");
        sim::TriangleIndex index(world);
        t0 = std::chrono::steady_clock::now();
        check(!interaction::sweepSafe(before, hands.pose, world, &index), "the indexed sweep sees it too");
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        check(ms < 5, "the indexed flip sweep skips the far geometry (" + std::to_string(ms) + " ms)");

        // Same answers as testing every triangle at every step, for random carries through clutter.
        auto reference = [](const surfaces::Surface &a, const surfaces::Surface &b, std::span<const Triangle> mesh) {
            float angle = 2 * std::acos(std::clamp(std::abs(a.rotation[0] * b.rotation[0] + a.rotation[1] * b.rotation[1] +
                                                            a.rotation[2] * b.rotation[2] + a.rotation[3] * b.rotation[3]),
                                                   0.f, 1.f));
            int steps = std::max(1, int(std::ceil((length(b.center - a.center) + angle * std::max(a.width, a.height) +
                                                   std::abs(b.width - a.width) + std::abs(b.height - a.height)) /
                                                  .015f)));
            float sign = 0;
            for (int j = 0; j < 4; j++)
                sign += a.rotation[j] * b.rotation[j];
            for (int i = 1; i <= steps; i++) {
                float t = float(i) / steps, norm = 0;
                auto s = b;
                s.center = a.center + (b.center - a.center) * t;
                s.width = a.width + (b.width - a.width) * t;
                s.height = a.height + (b.height - a.height) * t;
                for (int j = 0; j < 4; j++) {
                    s.rotation[j] = a.rotation[j] * (1 - t) + b.rotation[j] * (sign < 0 ? -t : t);
                    norm += s.rotation[j] * s.rotation[j];
                }
                for (auto &q : s.rotation)
                    q /= std::sqrt(norm);
                s.yaw = a.yaw + (b.yaw - a.yaw) * t;
                if (surfaces::overlaps(s, mesh))
                    return false;
            }
            return true;
        };
        unsigned seed = 12345;
        auto random = [&](float lo, float hi) {
            seed = seed * 1664525u + 1013904223u;
            return lo + (hi - lo) * float(seed >> 8) / float(1u << 24);
        };
        std::vector<Triangle> clutter;
        for (int i = 0; i < 120; i++) {
            Vec3 p{random(-3, 3), random(0, 3), random(-3, 3)};
            float size = random(.05f, .6f);
            clutter.push_back({p, p + Vec3{random(-size, size), random(-size, size), random(-size, size)},
                               p + Vec3{random(-size, size), random(-size, size), random(-size, size)}});
        }
        sim::TriangleIndex clutterIndex(clutter);
        int safe = 0, blocked = 0;
        for (int i = 0; i < 300; i++) {
            auto a = before, b = before;
            a.center = {random(-2, 2), random(.5f, 2.5f), random(-2, 2)};
            b.center = a.center + Vec3{random(-.6f, .6f), random(-.3f, .3f), random(-.6f, .6f)};
            a.width = random(.3f, 1.6f), a.height = random(.2f, 1.f);
            b.width = random(.3f, 1.6f), b.height = random(.2f, 1.f);
            a.rotation = interaction::orientation({random(-1, 1), random(-.3f, .3f), random(-1, 1)});
            b.rotation = interaction::orientation({random(-1, 1), random(-.3f, .3f), random(-1, 1)});
            bool expect = reference(a, b, clutter);
            check(interaction::sweepSafe(a, b, clutter) == expect && interaction::sweepSafe(a, b, clutter, &clutterIndex) == expect,
                  "broad phase changed a sweep answer (case " + std::to_string(i) + ")");
            (expect ? safe : blocked)++;
        }
        check(safe > 20 && blocked > 20, "random sweeps cover both answers (" + std::to_string(safe) + " safe, " +
                                             std::to_string(blocked) + " blocked)");
        std::cout << "static world: collision-owning nodes only; back-face carry flips to face the player\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
