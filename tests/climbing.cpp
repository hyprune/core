#include "sim/Climbing.hpp"
#include "interaction/Config.hpp"
#include "world/World.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace hyprune;
using namespace hyprune::sim;
int main(int argc, char **argv) {
    assert(argc == 2);
    auto w = world::load(argv[1]);
    assert(w->climbables.size() == 4);
    auto ladder = w->climbables[0];
    auto ladders = std::span<const Climbable>(&ladder, 1);
    for (auto mode : {ClimbMode::Grab, ClimbMode::Look, ClimbMode::Auto}) {
        Camera c;
        Climbing climb;
        c.position = ladder.bottom + Vec3{0, 1.7f, 0};
        c.pitch = .9f;
        auto tick = [&](Intent i, int count) {
            for (int n = 0; n < count; ++n)
                climb.step(c, i, 1.f / 60, ladders, mode, false, w->collision, &w->collisionIndex);
        };
        tick({}, 40); // apply mode switch cooldown
        if (mode == ClimbMode::Auto)
            assert(climb.use(c, ladders, mode));
        tick({.forward = 1}, 60);
        assert(climb.active() && c.position.y > 2.5f);
        if (mode != ClimbMode::Auto) {
            auto height = c.position.y;
            tick({}, 40);
            assert(std::abs(height - c.position.y) < .01f);
            if (mode == ClimbMode::Grab) {
                c.pitch = -1;
                tick({.forward = 1}, 20);
                assert(c.position.y > height + .2f);
            } else {
                c.pitch = -1;
                tick({.forward = 1}, 20);
                assert(c.position.y < height - .2f);
                c.pitch = .9f;
            }
        }
        for (int n = 0; n < 600 && climb.active(); ++n)
            tick({.forward = 1}, 1);
        assert(!climb.active() && c.position.y > 7.6f && c.position.z < 0);
        // E down from a top exit, or press into the face then backwards.
        if (mode == ClimbMode::Auto) {
            // An explicit E press works immediately despite automatic catch cooldown.
            assert(climb.use(c, ladders, mode));
        } else {
            climb.reset(c);
            c.position = ladder.top + Vec3{0, 1.5f, 0};
            tick({}, 40);
            tick({.forward = 1}, 1);
        }
        for (int n = 0; n < 600 && climb.active(); ++n)
            tick({.forward = -1}, 1);
        assert(!climb.active() && c.position.y < 1.8f && c.position.z > .9f);
        // Falling into the hatch catches without input in all three modes.
        climb.reset(c);
        c.position = ladder.top + Vec3{0, 4, 0};
        c.verticalSpeed = -25;
        for (int n = 0; n < 90 && !climb.active(); ++n)
            tick({}, 1);
        assert(climb.active() && c.verticalSpeed == 0);
        auto caught = c.position.y;
        tick({}, 40);
        assert(std::abs(c.position.y - caught) < .02f);
        tick({.jump = true}, 1);
        assert(!climb.active() && c.verticalSpeed > 0);
        float z = c.position.z;
        tick({}, 15);
        assert(c.position.z > z + .2f && !climb.active());
        // Disabling climb (fly/noclip runtime hook) drops the attachment and bob.
        climb.reset(c);
        assert(!climb.active() && c.climbBob == 0);
        Fly{}.step(c, {.forward = 1}, .05f, {});
        assert(!climb.active());
    }
    // Explicit E can regrab immediately after jump. The old outward impulse
    // must not reappear when that automatic traversal later reaches its exit.
    {
        Camera c;
        Climbing climb;
        c.position = ladder.bottom + Vec3{0, 1.7f, 0};
        assert(climb.use(c, ladders, ClimbMode::Auto));
        for (int n = 0; n < 30; ++n)
            climb.step(c, {}, 1.f / 60, ladders, ClimbMode::Auto, false, w->collision, &w->collisionIndex);
        climb.jump();
        climb.step(c, {}, 1.f / 60, ladders, ClimbMode::Auto, false, w->collision, &w->collisionIndex);
        assert(!climb.active());
        assert(climb.use(c, ladders, ClimbMode::Auto));
        for (int n = 0; n < 500 && climb.active(); ++n)
            climb.step(c, {}, 1.f / 60, ladders, ClimbMode::Auto, false, w->collision, &w->collisionIndex);
        assert(!climb.active());
        auto exit = c.position;
        for (int n = 0; n < 30; ++n)
            climb.step(c, {}, 1.f / 60, ladders, ClimbMode::Auto, false, w->collision, &w->collisionIndex);
        assert(length(c.position - exit) < .02f);
    }
    // Wide wall strafe clamps the entire capsule inside the authored width.
    Climbing climb;
    Camera c;
    auto wide = w->climbables[2];
    c.position = wide.bottom + Vec3{0, 1.7f, 0};
    for (int n = 0; n < 120; ++n)
        climb.step(c, {.forward = .3f, .right = 1}, 1.f / 60, {&wide, 1}, ClimbMode::Grab, false, w->collision,
                   &w->collisionIndex);
    assert(climb.active() && c.position.x > -4 && c.position.x <= -3.299f);
    assert(std::abs(c.climbBob) > .00001f);
    climb.step(c, {}, 1.f / 60, {&wide, 1}, ClimbMode::Grab, true, w->collision, &w->collisionIndex);
    assert(c.climbBob == 0);
    auto y = c.position.y;
    climb.step(c, {.crouch = true}, .05f, {&wide, 1}, ClimbMode::Grab, false, w->collision, &w->collisionIndex);
    assert(c.position.y < y);
    // Backside and a distant parallel rail cannot grab.
    climb.reset(c);
    c.position = {0, 3, -1};
    climb.step(c, {.forward = 1}, .05f, ladders, ClimbMode::Grab, false, {});
    assert(!climb.active());
    climb.reset(c);
    c.position = {0, 3, .3f}; // inside the catch prism, behind the climb face
    c.verticalSpeed = 1;      // rising: isolate manual grab from the intentional falling-volume catch
    climb.step(c, {.forward = 1}, .05f, ladders, ClimbMode::Grab, false, {});
    assert(!climb.active());
    // Angled motion follows the segment while every step keeps capsule clearance.
    auto angled = w->climbables[3];
    climb.reset(c);
    c.position = angled.bottom + Vec3{0, 1.7f, 0};
    for (int n = 0; n < 100; ++n) {
        climb.step(c, {.forward = 1}, 1.f / 60, {&angled, 1}, ClimbMode::Grab, false, w->collision, &w->collisionIndex);
        auto check = c;
        resolveCapsule(check, w->collision, &w->collisionIndex);
        assert(length(check.position - c.position) < .004f);
    }
    assert(climb.active() && c.position.z < 2.3f && c.position.y > 4);
    // Sweeps stop at thin collision even if endpoints are on opposite sides.
    std::vector<Triangle> wall{{{-2, -2, 0}, {2, -2, 0}, {2, 5, 0}}, {{-2, -2, 0}, {2, 5, 0}, {-2, 5, 0}}};
    c.position = {0, 2, 1};
    assert(!moveCapsule(c, {0, 2, -1}, wall));
    assert(c.position.z > .29f);
    // A blocked top exit holds on the safe side of a thin wall.
    climb.reset(c);
    c.position = ladder.bottom + Vec3{0, 1.7f, 0};
    auto blocked = wall;
    for (auto &t : blocked) {
        t.a.y -= 2;
        t.b.y -= 2;
        t.c.y += 5;
    }
    for (int n = 0; n < 400; ++n)
        climb.step(c, {.forward = 1}, 1.f / 60, ladders, ClimbMode::Grab, false, blocked);
    assert(climb.active() && c.position.z > .29f);
    climb.jump();
    climb.step(c, {}, 1.f / 60, ladders, ClimbMode::Grab, false, blocked);
    assert(!climb.active());
    // World loader semantic checks, beyond JSON-schema shape checks.
    auto temp = std::filesystem::temp_directory_path() / ("hyprune-climbing-test-" + std::to_string(getpid()));
    std::filesystem::copy(argv[1], temp, std::filesystem::copy_options::recursive);
    for (int mutation = 0; mutation < 8; ++mutation) {
        auto manifest = w->manifest;
        auto &l = manifest["climbables"][0];
        if (mutation == 0)
            l["facing"] = {0, 1, 0};
        if (mutation == 1)
            l["top"] = l["bottom"];
        if (mutation == 2)
            l["exits"]["top"] = {0, 99, 0};
        if (mutation == 3)
            manifest["climbables"].push_back(l);
        if (mutation == 4)
            l["width"] = 0;
        if (mutation == 5)
            manifest["formatVersion"] = "0.3";
        if (mutation == 6)
            l["bottom"] = {0, -.2, .55};
        if (mutation == 7)
            l["speed"] = 100;
        {
            std::ofstream out(temp / "world.json");
            out << manifest.dump();
        }
        bool rejected = false;
        try {
            world::load(temp);
        } catch (...) {
            rejected = true;
        }
        assert(rejected);
    }
    std::filesystem::remove_all(temp);
    auto config = interaction::defaultConfig();
    assert(config["movement"]["climb_mode"] == "grab");
    for (auto name : {"grab", "look-to-climb", "auto-traverse"})
        assert(interaction::configPatch(config, {{"movement", {{"climb_mode", name}}}})["movement"]["climb_mode"] ==
               name);
    for (auto bad : {world::Json("wrong"), world::Json(4), world::Json(nullptr)}) {
        bool rejected = false;
        try {
            interaction::configPatch(config, {{"movement", {{"climb_mode", bad}}}});
        } catch (...) {
            rejected = true;
        }
        assert(rejected);
    }
    std::cout << "PASS climbing: three modes, hold, pitch, fall catch, exits, jump, wide strafe, slide, angled "
                 "clearance, reduced motion, fly reset, sweeps, config\n";
}
