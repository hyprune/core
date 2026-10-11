// World 0.8 placed world objects: loader rules, instancing data, animation
// maths (spin, orbit, bob), swept bounds, probe lighting, collision proxies at
// the instance pose and budgets. GL drawing is covered by the render test; the
// nested objects suite checks frames on demand.
#include "world/Animation.hpp"
#include "world/Format.hpp"
#include "world/Objects.hpp"
#include "world/World.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <unistd.h>
using namespace hyprune;
namespace {
int failures = 0;
void check(bool ok, const std::string &what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}
bool near(Vec3 a, Vec3 b, float eps = 1e-4f) { return length(a - b) <= eps; }
} // namespace
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    const std::filesystem::path fixture = argv[1];
    auto w = world::load(fixture);
    // Fixture: scene floor (lightmapped, collision), pillar (collision), box behind the
    // spawn; asset "armillary" placed as west (phase 0), centre (.33), east (.66, turned, 1.2x).
    check(w->objectAssets.size() == 1 && w->objectInstances.size() == 3, "one asset, three instances");
    const auto &asset = w->objectAssets[0];
    check(asset.animations.size() == 4 && asset.meshes.size() == 8, "four animations, eight rendered meshes (proxy not drawn)");
    // Asset nodes: 0 base, 1-4 ring frame (children of 5), 5 ring-equator, 6 ring-tilted, 7 moon, 8 buoy, 9 proxy.
    const std::vector<int> expected{-1, 0, 0, 0, 0, 0, 1, 2, 3, -1};
    for (size_t n = 0; n < expected.size(); ++n)
        check(asset.animationOf(n) == expected[n], "asset subtree assignment of node " + std::to_string(n));
    // Render-only: objects never enter the static picking triangles; proxies are static collision.
    check(w->meshes.size() == 3 && w->visualTriangles.size() == 36, "objects stay out of the scene's visual triangles");
    check(w->collision.size() == 24 + 3 * 12, "three proxies (12 triangles each) join static collision");
    size_t proxies = 0;
    for (size_t i = 0; i < w->collision.size(); ++i)
        proxies += w->collisionNodes[i] == world::objectCollisionNode;
    check(proxies == 36, "proxy triangles are tagged as object collision");
    for (size_t n = 0; n < w->nodeParents.size(); ++n)
        if (world::movableNode(*w, n))
            check(n == 0 || n == 1, "only the scene's own collision nodes are movable");
    // Proxy at the instance pose: east is turned 45° and scaled 1.2 about (3, 0, -6).
    Vec3 lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
    for (size_t i = 24 + 2 * 12; i < w->collision.size(); ++i)
        for (auto p : {w->collision[i].a, w->collision[i].b, w->collision[i].c})
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)},
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    float half = .45f * 1.2f * std::sqrt(2.f);
    check(near(lo, {3 - half, 0, -6 - half}, 1e-3f) && near(hi, {3 + half, .7f * 1.2f, -6 + half}, 1e-3f),
          "east proxy turned and scaled at its pose");

    // Animation maths: cycle = t·speed/period + phase + instance phase.
    const auto &ring = asset.animations[0], &tilted = asset.animations[1], &moon = asset.animations[2], &buoy = asset.animations[3];
    constexpr double pi = 3.141592653589793;
    check(ring.type == world::Animation::Spin && moon.type == world::Animation::Orbit && buoy.type == world::Animation::Bob,
          "types");
    check(std::abs(world::animation::angle(ring, 1) - pi / 2) < 1e-9, "quarter turn after a quarter period");
    check(std::abs(world::animation::angle(ring, 1, 2) - pi) < 1e-9, "speed multiplies");
    check(std::abs(world::animation::angle(ring, 0, 1, .25) - pi / 2) < 1e-9, "instance phase adds");
    check(std::abs(world::animation::angle(tilted, 0) - .3 * 2 * pi) < 1e-6, "asset phase adds");
    check(std::abs(world::animation::cycle(ring, 4e6 + 2) - .5) < 1e-6, "long-running clock stays periodic");
    // Spin about +X through the pivot (0, 1.5, 0): +Y -> +Z after a quarter turn (right-hand rule).
    auto spin = world::animation::matrix(ring, 1);
    check(near(spin.point({0, 2.3f, 0}), {0, 1.5f, .8f}), "spin quarter turn about the pivot line");
    check(near(spin.point(ring.pivot), ring.pivot), "spin keeps the pivot");
    // Orbit: translation only; the moon's origin (1.2, 1.5, 0) goes to (0, 1.5, -1.2).
    auto orbit = world::animation::matrix(moon, 2);
    check(near(moon.origin, {1.2f, 1.5f, 0}) && near(orbit.point(moon.origin), {0, 1.5f, -1.2f}), "orbit carries the origin");
    check(orbit.v[0] == 1 && orbit.v[5] == 1 && orbit.v[10] == 1 && orbit.v[1] == 0 && orbit.v[4] == 0, "orbit keeps orientation");
    // Bob: amplitude · sin along the axis.
    check(near(world::animation::matrix(buoy, .5).point({0, 2.6f, 0}), {0, 2.75f, 0}), "bob peaks a quarter period in");
    check(near(world::animation::matrix(buoy, 1.5).point({0, 2.6f, 0}), {0, 2.45f, 0}), "bob troughs at three quarters");
    check(near(world::animation::matrix(buoy, 0).point({0, 2.6f, 0}), {0, 2.6f, 0}), "t = 0 rests at the authored pose");
    // Reduced motion rests at t = 0: phases apply (the tilted ring is turned by 0.3).
    check(!near(world::animation::matrix(tilted, 0).point({.95f, 1.5f, 0}), {.95f, 1.5f, 0}), "t = 0 pose includes the phase");
    // Swept bounds: every instance's sphere holds every pose.
    for (const auto &o : w->objectInstances)
        for (const auto &mesh : asset.meshes) {
            int a = asset.animationOf(mesh.node);
            for (double t = 0; t < 24; t += .37)
                for (const auto &v : mesh.vertices) {
                    auto local = a < 0 ? v.position : world::animation::matrix(asset.animations[a], t, o.speed, o.phase).point(v.position);
                    check(length(o.model.point(local) - o.center) <= o.radius + 1e-3f, "swept sphere holds " + o.id);
                }
        }
    check(std::abs(w->objectInstances[2].radius - asset.radius * 1.2f) < 1e-4f, "radius scales with the instance");
    // Probe lighting: the warm lightmapped floor below, the sky above, open sun.
    for (const auto &o : w->objectInstances) {
        check(o.ambientDown.x > 2 * o.ambientDown.z && o.ambientDown.x > .3f, o.id + ": warm bounce from the baked floor");
        check(o.ambientUp.z > o.ambientUp.x, o.id + ": sky-blue ambient from above");
        check(o.sunVisibility == 1 && o.sun > .2f && o.sun < 4, o.id + ": open sun, calibrated strength");
        check(o.regions.empty(), o.id + ": no visibility regions");
    }

    // Variants: copies of the fixture with one change to world.json and/or the asset glTF.
    auto dir = std::filesystem::temp_directory_path() / ("hyprune-objects-" + std::to_string(getpid()));
    using Edit = std::function<void(world::Json &)>;
    auto variant = [&](const Edit &world, const Edit &gltf = {}, const Edit &scene = {}) {
        std::filesystem::remove_all(dir);
        std::filesystem::copy(fixture, dir, std::filesystem::copy_options::recursive);
        auto j = world::Json::parse(std::ifstream(fixture / "world.json"));
        world(j);
        std::ofstream(dir / "world.json") << j.dump();
        if (gltf) {
            auto g = world::Json::parse(std::ifstream(fixture / "objects/armillary.gltf"));
            gltf(g);
            std::ofstream(dir / "objects/armillary.gltf") << g.dump();
        }
        if (scene) {
            auto g = world::Json::parse(std::ifstream(fixture / "scene.gltf"));
            scene(g);
            std::ofstream(dir / "scene.gltf") << g.dump();
        }
    };
    auto accepts = [&](const char *what, const Edit &world, const Edit &gltf = {},
                       const Edit &scene = {}) -> std::shared_ptr<world::World> {
        variant(world, gltf, scene);
        try {
            return world::load(dir);
        } catch (const std::exception &e) {
            check(false, std::string(what) + ": " + e.what());
        }
        return nullptr;
    };
    auto rejects = [&](const char *what, const Edit &world, const Edit &gltf = {}) {
        variant(world, gltf);
        try {
            world::load(dir);
            check(false, std::string("accepted: ") + what);
        } catch (const std::exception &e) {
            std::cout << "rejects " << what << ": " << e.what() << '\n';
        }
    };
    auto anims = [](world::Json &j) -> world::Json & { return j["objectAssets"][0]["animations"]; };
    auto none = [](world::Json &) {};
    for (auto version : world::format::supported)
        if (!world::format::atLeast(version, "0.8"))
            rejects("objects below world 0.8", [&](auto &j) { j["formatVersion"] = std::string(version); });
    rejects("a library reference (reserved)", [](auto &j) { j["objectAssets"][0]["library"] = "org.example.armillary@1.0.0"; });
    rejects("an unknown asset", [](auto &j) { j["objects"][1]["asset"] = "orrery"; });
    rejects("duplicate object ids", [](auto &j) { j["objects"][1]["id"] = "west"; });
    rejects("duplicate asset ids", [](auto &j) { j["objectAssets"].push_back(j["objectAssets"][0]); });
    rejects("an unknown node name", [&](auto &j) { anims(j)[0]["node"] = "ring-polar"; });
    rejects("a repeated node name", none, [](auto &g) { g["nodes"][0]["name"] = "moon"; });
    rejects("two animations on one node", [&](auto &j) { anims(j)[1]["node"] = "ring-equator"; });
    rejects("an animated node inside another animated subtree", [&](auto &j) {
        anims(j).push_back({{"node", "ring-equator-0"}, {"type", "spin"}, {"axis", {0, 0, 1}}, {"pivot", {0, 1.5, 0}}, {"period", 3}});
    });
    rejects("a collision proxy inside an animated subtree", none, [](auto &g) { g["nodes"][7]["children"] = {9}; });
    rejects("a collision proxy without a mesh", none, [](auto &g) { g["nodes"][9].erase("mesh"); });
    rejects("bob with a pivot", [&](auto &j) { anims(j)[3]["pivot"] = {0, 0, 0}; });
    rejects("spin without a pivot", [&](auto &j) { anims(j)[0].erase("pivot"); });
    rejects("a non-unit axis", [&](auto &j) { anims(j)[0]["axis"] = {.5, .5, .5}; });
    rejects("an extension the world format forbids", none, [](auto &g) {
        g["extensionsUsed"] = g["extensionsRequired"] = {"KHR_draco_mesh_compression"};
    });
    rejects("an object obstructing the spawn", [](auto &j) { j["objects"][0]["pose"]["position"] = {0, .6, 0}; });
    rejects("more than 256 objects", [](auto &j) {
        for (int i = 0; i < 254; ++i) {
            auto o = j["objects"][0];
            o["id"] = "o" + std::to_string(i);
            j["objects"].push_back(o);
        }
    });
    rejects("more than 16 animations", [&](auto &j) {
        for (int i = 0; i < 13; ++i)
            anims(j).push_back(anims(j)[0]);
    });
    rejects("more than 32 assets", [](auto &j) {
        for (int i = 0; i < 32; ++i) {
            auto a = j["objectAssets"][0];
            a["id"] = "a" + std::to_string(i);
            j["objectAssets"].push_back(a);
        }
    });
    // Budgets: an asset counts once for the GPU however many instances place it;
    // every instance's proxy counts toward the static collision budget.
    if (auto many = accepts("256 instances", [](auto &j) {
            for (int i = 0; i < 253; ++i) {
                auto o = j["objects"][1];
                o["id"] = "o" + std::to_string(i);
                o["pose"]["position"] = {-12 + (i % 16) * 1.6, 0, -8 - (i / 16) * 1.6};
                j["objects"].push_back(o);
            }
        })) {
        check(many->objectInstances.size() == 256 && many->collision.size() == 24 + 256 * 12, "256 proxies collide");
        check(world::gpuBytes(*many, true, {}) == world::gpuBytes(*w, true, {}), "asset GPU bytes count once");
    }
    if (auto authored = accepts("authored ambient", [](auto &j) { j["objects"][0]["ambient"] = {.1, .2, .3}; })) {
        auto &o = authored->objectInstances[0];
        check(o.authoredAmbient && near(o.ambientUp, {.1f, .2f, .3f}) && near(o.ambientDown, {.1f, .2f, .3f}), "ambient override");
    }
    if (auto unlit = accepts("unlit asset", [](auto &j) { j["objectAssets"][0]["lighting"]["mode"] = "unlit"; })) {
        check(unlit->objectAssets[0].unlit, "unlit mode");
        for (auto &m : unlit->objectAssets[0].meshes)
            check(unlit->materials[m.material].unlit, "unlit asset materials draw unlit");
        for (auto &m : unlit->meshes)
            check(!unlit->materials[m.material].unlit, "scene materials keep their lighting");
    }
    // Derived regions: camera cells (no nodes, visible from nowhere) plus owner areas.
    // An object takes the owners of the static geometry it overlaps, never a cell.
    auto cell = world::Json{{"id", "cell-west"}, {"bounds", {{"min", {-5, -1, -8}}, {"max", {-1, 4, -4}}}},
                            {"nodes", world::Json::array()}, {"visibleFrom", world::Json::array()}};
    if (auto owned = accepts("visibility of the geometry around an object", [&](auto &j) {
            j["visibility"] = {{"areas", {cell, {{"id", "owner-floor"}, {"bounds", {{"min", {50, 50, 50}}, {"max", {51, 51, 51}}}},
                                                 {"nodes", {0}}, {"visibleFrom", {"cell-west"}}}}}};
        }))
        for (auto &o : owned->objectInstances)
            check(o.regions == std::vector<int>{1}, o.id + ": takes the floor's owner area, not the camera cell");
    if (auto cells = accepts("camera cells alone", [&](auto &j) { j["visibility"] = {{"areas", {cell}}}; }))
        for (auto &o : cells->objectInstances)
            check(o.regions.empty(), o.id + ": a cell owning nothing leaves the object to the frustum");
    // A roof over the east instance shades it from the sun (static geometry only).
    if (auto shaded = accepts("a roof over one instance", none, {}, [](auto &g) {
            g["nodes"].push_back({{"mesh", 0}, {"translation", {3, 4, -6}}, {"scale", {.15, 1, .15}}});
            g["scenes"][0]["nodes"].push_back(g["nodes"].size() - 1);
        })) {
        auto &east = shaded->objectInstances[2], &west = shaded->objectInstances[0];
        check(east.sunVisibility < .5f && east.sun < west.sun && west.sunVisibility == 1,
              "roof shades the east instance only (" + std::to_string(east.sunVisibility) + ", " + std::to_string(west.sunVisibility) + ")");
    }
    accepts("no objects", [](auto &j) { j.erase("objects"); });
    accepts("assets without instances", [](auto &j) { j["objects"] = world::Json::array(); });
    std::filesystem::remove_all(dir);
    if (failures)
        return 1;
    std::cout << "PASS world 0.8 objects: loader rules, instancing data, spin/orbit/bob, swept bounds, probe lighting, proxies, budgets\n";
    return 0;
}
