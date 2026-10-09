#include "interaction/Content.hpp"
#include "interaction/Tools.hpp"
#include "surfaces/Layout.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>
using namespace hyprune;
int main() {
    world::World world;
    world.id = "org.hyprune.test";
    sim::Camera camera;
    camera.position = {0, 2, 0};
    surfaces::Layout layout;
    surfaces::Instance a{"a", "kitty", "First", 20}, b{"b", "kitty", "Second", 20};
    surfaces::Surface s;
    s.id = a.id;
    s.center = {0, 2, -2};
    s.logicalSize(1800, 1000, .002f);
    assert(layout.spawn(a, &world, camera, s, 1));
    assert(layout.locate(a, &world, camera, 1800, 1000));
    assert(!layout.instances.back().contains("areaId")); // Unzoned after warp: omit, never empty.
    s.id = b.id;
    assert(!layout.spawn(b, &world, camera, s, 1));
    assert(layout.unplaced.contains(b.id) && !layout.locate(b, &world, camera, 1800, 1000));
    auto at = layout.freePoses[a.id];
    layout.stash(a, at);
    assert(layout.unplaced.contains(a.id) && layout.pocketOrder.front() == a.id);
    assert(!layout.locate(a, &world, camera, 1800, 1000));
    assert(layout.vacant(at, a.id));
    layout.mount(a, &world, at);
    assert(!layout.unplaced.contains(a.id) && layout.hasPlacement(a, &world));
    auto sizing = world::Json{{"baseSize", {1800, 1000}}, {"logicalSize", {900, 500}}, {"worldSize", {3.6, 2.0}}};
    layout.setContent(a, &world, sizing);
    layout.mount(a, &world, at);
    surfaces::Layout restored;
    restored.restore({{"bindings", layout.bindings}, {"placements", layout.placements}});
    assert(restored.contentFor(a, &world) && *restored.contentFor(a, &world) == sizing);
    auto scaled = restored.locate(a, &world, camera, 900, 500);
    assert(scaled && std::abs(scaled->width - 3.6f) < .001f && std::abs(scaled->height - 2) < .001f);
    a.workspaceId = 1;
    assert(!restored.hasPlacement(a, &world)); // no implicit adoption on desktop workspaces
    a.workspaceId = 20;
    layout.stash(a, at);
    layout.stash(b, at);
    assert(layout.pocketOrder[0] == b.id && layout.pocketOrder[1] == a.id);
    layout.stash(a, at);
    assert(layout.pocketOrder.size() == 2 && layout.pocketOrder[0] == a.id);
    interaction::Hands hands;
    at.yaw = .6f;
    at.rotation = interaction::orientation({.3f, .2f, 1.f});
    hands.begin(interaction::Gesture::Carry, at, {2, .7f, .3f}, camera);
    auto grab = hands.pose.center + hands.offset;
    hands.facing(true, camera);
    assert(length(hands.pose.normal() - Vec3{0, 0, 1}) < .001f);
    assert(length(hands.pose.center + hands.offset - grab) < .001f);
    camera.yaw = .4f;
    camera.pitch = .5f;
    assert(hands.update(camera, camera.forward(), .1, {}));
    assert(std::abs(hands.pose.normal().y) < .001f);
    hands.facing(false, camera);
    auto q = hands.pose.rotation;
    camera.yaw = .9f;
    hands.update(camera, camera.forward(), .1, {});
    assert(hands.pose.rotation == q);
    hands.facing(true, camera);
    assert(dot(hands.pose.normal(), {std::sin(-camera.yaw), 0, std::cos(camera.yaw)}) > .999f);
    interaction::Tools tools;
    for (int slot = 1; slot <= 5; ++slot) {
        tools.slot = slot;
        auto declared = tools.slots();
        assert(declared.size() == 4);
        for (auto key : {"primary", "secondary", "alternate", "cycle"})
            assert(declared.contains(key));
    }
    tools.slot = 4;
    auto worldHandles = tools.handles({}, {0, .70710678f, 0, .70710678f});
    tools.localAxes = true;
    auto localHandles = tools.handles({}, {0, .70710678f, 0, .70710678f});
    assert(length(worldHandles[0].end - Vec3{1, 0, 0}) < .001f);
    assert(length(localHandles[0].end - Vec3{0, 0, -1}) < .001f);
    std::cout << "PASS placement queue, pocket ordering, durable client size, workspace scope and facing toggle\n";
}
