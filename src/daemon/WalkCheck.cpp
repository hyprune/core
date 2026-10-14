// hyprune-walk-check: run walking probes against a world package with core's own walker.
// World tools (fall-through sweeps) generate the probes; this reports where each one ends.
#include "control/Control.hpp"
#include "interaction/Hands.hpp"
#include "sim/Camera.hpp"
#include "world/Catalog.hpp"
#include "world/World.hpp"
#include <fstream>
#include <iostream>
namespace {
using namespace hyprune;
using Json = ipc::Json;
constexpr const char *usage = "usage: hyprune-walk-check PACKAGE PROBES.json\n"
                              "  probes: [{id, from: eye [x,y,z], to: eye [x,y,z], mode: walk|autodrive,\n"
                              "            speed: m/s, dt: frame seconds, seconds, settle}]\n";
Vec3 vec(const Json &j) { return {j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()}; }
Json array(Vec3 v) { return Json::array({v.x, v.y, v.z}); }
// One probe: walk (keyboard forward at a fixed heading, the speed as walk or run speed)
// or autodrive (route.set to the target, the player's walker), frames of `dt`, then
// up to `settle` seconds standing still, so a capsule that lost its floor shows as falling.
Json probe(const world::World &w, const Json &p) {
    const sim::Walk walk;
    const Vec3 from = vec(p.at("from")), to = vec(p.at("to"));
    const std::string mode = p.value("mode", "walk");
    const float speed = p.value("speed", 4.f), dt = std::clamp(p.value("dt", 1.f / 60), 1e-3f, .05f);
    const float seconds = p.value("seconds", 3.f), settle = p.value("settle", 3.5f);
    const float stepHeight = p.value("stepHeight", .35f);
    sim::Camera c;
    c.position = from;
    float minY = from.y;
    bool leftPlayable = false;
    Json crossed = nullptr;
    Vec3 last = from, stood = from;
    // Between two frames the capsule axis must not pass through a collision surface
    // (knee and chest height): the walker keeps it 0.3 m clear, so a crossing is tunnelling.
    auto observe = [&] {
        minY = std::min(minY, c.position.y);
        if (c.grounded)
            stood = c.position;
        leftPlayable = leftPlayable || !w.playable.contains(c.position);
        Vec3 move = c.position - last;
        float travel = length(move);
        if (crossed.is_null() && travel > 1e-4f)
            for (float h : {.6f, 1.2f}) {
                Vec3 axis = last + Vec3{0, h - c.eyeHeight, 0};
                auto hit = interaction::raycast(axis, move / travel, w.collision, &w.collisionIndex);
                if (hit && hit->distance < travel) {
                    crossed = {{"point", array(hit->point)}, {"normal", array(hit->normal)}};
                    break;
                }
            }
        last = c.position;
    };
    // Settle on the start floor first: a start without support is not a fall-through.
    sim::Intent idle;
    idle.stepHeight = stepHeight;
    for (int k = 0; k < 15; ++k)
        walk.step(c, idle, 1.f / 60, w.collision, &w.collisionIndex);
    const bool startGrounded = c.grounded && from.y - c.position.y < .1f;
    const Vec3 start = c.position;
    last = stood = start;
    if (mode == "autodrive") {
        control::Control control;
        control.reset(&w);
        auto call = [&](const std::string &method, const Json &params) {
            return control.execute(method, params, "walk-check", "org.hyprune.walk-check", &w, c, Json::array({"output"}));
        };
        call("route.set", {{"points", Json::array({Json{{"position", array(to)}}})}});
        call("autodrive.start", {{"speed", speed}});
        sim::Intent walking;
        walking.stepHeight = stepHeight;
        for (float t = 0; t < seconds && control.driving(); t += dt) {
            control.tick(dt, c, &w, &walk, walking);
            observe();
        }
    } else {
        sim::Intent intent;
        intent.forward = 1;
        intent.speed = speed / 3.5f;
        intent.stepHeight = stepHeight;
        c.yaw = std::atan2(to.x - start.x, -(to.z - start.z));
        for (float t = 0; t < seconds; t += dt) {
            walk.step(c, intent, dt, w.collision, &w.collisionIndex);
            observe();
        }
    }
    // Settle until it has stood for 0.2 s (a drop has landed) or `settle` runs out.
    for (float t = 0, standing = 0; t < settle && standing < .2f; t += 1.f / 60) {
        walk.step(c, idle, 1.f / 60, w.collision, &w.collisionIndex);
        observe();
        standing = c.grounded ? standing + 1.f / 60 : 0;
    }
    return {{"id", p.value("id", Json())}, {"startGrounded", startGrounded}, {"start", array(start)},
            {"end", array(c.position)}, {"grounded", c.grounded}, {"minY", minY}, {"lastGrounded", array(stood)},
            {"playable", w.playable.contains(c.position)}, {"leftPlayable", leftPlayable},
            {"crossed", crossed}};
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << usage;
        return 2;
    }
    try {
        auto roots = world::CatalogRoots::fromEnvironment();
        world::LoadOptions options;
        options.libraryRoots.push_back(roots.userLibraries);
        for (auto &r : roots.systemLibraries)
            options.libraryRoots.push_back(r);
        auto w = world::load(argv[1], options);
        std::ifstream in(argv[2]);
        if (!in)
            throw std::runtime_error(std::string("cannot read ") + argv[2]);
        auto probes = Json::parse(in);
        // One JSON object per line, in probe order.
        for (auto &p : probes)
            std::cout << probe(*w, p).dump() << '\n';
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
