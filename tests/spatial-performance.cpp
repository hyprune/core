#include "interaction/Hands.hpp"
#include "sim/Camera.hpp"
#include <chrono>
#include <iostream>
#include <random>
#undef NDEBUG
#include <cassert>
using namespace hyprune;
using Clock = std::chrono::steady_clock;
int main(int argc, char **argv) {
    std::vector<Triangle> small{{{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}}, {{-10, 0, -10}, {10, 0, 10}, {-10, 0, 10}}};
    auto dense = small;
    for (int i = 0; i < 10000; ++i) {
        float x = 1000 + (i % 100) * 2, z = 1000 + (i / 100) * 2;
        dense.push_back({{x, 0, z}, {x + 1, 0, z}, {x, 0, z + 1}});
    }
    Vec3 origin{0, 1, 0};
    if (argc > 1) {
        auto world = world::load(argv[1]);
        dense = world->collision;
        origin = world->spawns[0].position + Vec3{0, 1, 0};
    }
    sim::TriangleIndex index(dense);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> random(-20, 20);
    for (int n = 0; n < 1000; ++n) {
        Vec3 p{random(rng), random(rng), random(rng)}, d = normalized(Vec3{random(rng), random(rng), random(rng)});
        auto a = interaction::raycast(p, d, dense), b = interaction::raycast(p, d, dense, &index);
        assert(bool(a) == bool(b));
        if (a)
            assert(std::abs(a->distance - b->distance) < 1e-5f);
        if (n < 100) {
            sim::Camera a, b;
            a.position = b.position = p;
            sim::resolveCapsule(a, dense);
            sim::resolveCapsule(b, dense, &index);
            assert(length(a.position - b.position) < 1e-5f && a.grounded == b.grounded);
        }
    }
    auto measure = [&](const std::vector<Triangle> &mesh, const sim::TriangleIndex *tree = nullptr) {
        double ray = 0, capsule = 0;
        auto begin = Clock::now();
        for (int n = 0; n < 300; ++n) {
            auto hit = interaction::raycast(origin, {0, -1, 0}, mesh, tree);
            if (argc == 1 && (!hit || std::abs(hit->distance - 1) > 1e-5))
                throw std::runtime_error("ray changed");
        }
        ray = std::chrono::duration<double, std::milli>(Clock::now() - begin).count() / 300;
        begin = Clock::now();
        sim::Walk walk;
        for (int n = 0; n < 30; ++n) {
            sim::Camera camera;
            camera.position = origin + Vec3{0, .7f, 0};
            walk.step(camera, sim::Intent{}, 1.f / 60, mesh, tree);
            if (argc == 1 && std::abs(camera.position.y - 1.7f) > .05f)
                throw std::runtime_error("capsule changed");
        }
        capsule = std::chrono::duration<double, std::milli>(Clock::now() - begin).count() / 30;
        return std::pair{ray, capsule};
    };
    auto a = measure(dense), b = measure(dense, &index);
    auto candidates = index.ray(origin, {0, -1, 0}).size();
    std::cout << world::Json{{"triangles", dense.size()},       {"rayCandidates", candidates},
                             {"linearRayMs", a.first},          {"indexedRayMs", b.first},
                             {"linearWalkMs", a.second},        {"indexedWalkMs", b.second},
                             {"raySpeedup", a.first / b.first}, {"walkSpeedup", a.second / b.second}}
                     .dump(2)
              << '\n';
    if (argc == 1)
        assert(candidates < 16);
}
