// NPC agent (RFC-0010 runtime rules, RFC-0011 routes, stops, seats, interactions): route
// following by arc length, stops, the occasional fidget, sitting, glancing, stepping aside,
// facing the player during an interaction and guiding.
#include "sim/Npc.hpp"
#include <format>
#include <iostream>
#include <stdexcept>
using namespace hyprune;
using Mode = sim::NpcAgent::Mode;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
sim::Route line(std::vector<Vec3> points, bool closed = false, sim::Segment type = sim::Segment::Line) {
    return sim::Route::build(points, std::vector<sim::Segment>(points.size(), type), closed);
}
sim::NpcAgent agent(sim::Route route) {
    sim::NpcAgent a;
    a.route = std::move(route);
    a.walkSpeed = 1;
    a.clips = {{"idle", 2}, {"walk", 1}, {"wave", 1}, {"nod", .5f}, {"sit", 1}, {"sit_idle", 3}};
    a.fidgetEvery = 20;
    a.start();
    return a;
}
sim::NpcWorld away() {
    sim::NpcWorld w;
    w.player = {30, 1.7f, 30};
    w.present = true;
    return w;
}
float run(sim::NpcAgent &a, const sim::NpcWorld &w, float seconds, std::function<bool()> until = {}) {
    float t = 0;
    for (; t < seconds; t += .01f) {
        a.update(.01f, w);
        if (until && until())
            break;
    }
    return t;
}
} // namespace
int main() {
    try {
        auto far = away();
        // Walks by arc length at the walk clip's speed, playing walk at speed / clip speed.
        {
            auto a = agent(line({{0, 0, 0}, {0, 0, 10}}));
            run(a, far, 1);
            check(a.pose.clip == "walk" && a.position.z > .95f && a.position.z < 1.05f, std::format("walks 1 m/s, at {}", a.position.z));
            a.rate = 1.5f;
            run(a, far, 1);
            check(a.position.z > 2.4f && a.position.z < 2.6f && std::abs(a.clipRate - 1.5f) < .05f, "instance speed scales pace and clip rate");
        }
        // Phase: starts at that fraction of a closed route; a loop takes length / speed.
        {
            auto r = line({{0, 0, 0}, {4, 0, 0}, {4, 0, 4}, {0, 0, 4}}, true);
            check(std::abs(r.length() - 16) < 1e-3f, "closed square is 16 m");
            auto a = r;
            sim::NpcAgent b = agent(r);
            b.phase = .5f;
            b.start();
            check(std::abs(b.position.x - 4) < 1e-3f && std::abs(b.position.z - 4) < 1e-3f, "phase 0.5 starts half way");
            float t = run(b, far, 40, [&] { return b.s > 15.9f; });
            check(t > 7.5f && t < 10.5f, std::format("walks the rest of the loop at speed, {} s", t));
        }
        // Curves follow the tool 3 geometry.
        {
            auto r = line({{0, 0, 0}, {4, 0, 0}, {4, 0, 4}}, false, sim::Segment::Curve);
            check(r.samples.size() == 49 && std::abs(r.at(r.points[1]).x - 4) < 1e-3f, "24 samples per segment through the points");
        }
        // Stops: arrives exactly at the point, plays the stop's clip once, idles, then walks on.
        {
            auto a = agent(line({{0, 0, 0}, {0, 0, 2}, {0, 0, 6}}));
            a.stops = {{a.route.points[1], 1, 2.5f, "wave"}};
            run(a, far, 4, [&] { return a.mode == Mode::Stop; });
            check(a.mode == Mode::Stop && std::abs(a.position.z - 2) < 1e-3f && a.pose.clip == "wave" && !a.pose.loop, "stops at the point with its clip");
            float t = run(a, far, 5, [&] { return a.mode == Mode::Walk; });
            check(t > 2.3f && t < 2.7f && a.pose.clip == "idle", std::format("holds the stop for its seconds ({})", t));
            run(a, far, .5f);
            check(a.position.z > 2.2f && a.pose.clip == "walk", "walks on");
        }
        // Fidget: only with the player near, played through once, at most every fidgetEvery.
        {
            auto a = agent(line({{0, 0, 0}, {0, 0, 1}, {0, 0, 6}}));
            a.clips["idle_fidget"] = 1.5f;
            a.stops = {{a.route.points[1], 1, 6}};
            a.sinceFidget = 100;
            auto near = far;
            near.player = {1, 1.7f, 1};
            run(a, far, 3, [&] { return a.mode == Mode::Stop; });
            run(a, far, .5f);
            check(a.pose.clip == "idle", "no fidget with nobody near");
            run(a, near, .1f);
            check(a.pose.clip == "idle_fidget", "fidgets with the player near");
            run(a, near, 1.6f);
            check(a.pose.clip == "idle", "plays it through once");
            for (int i = 0; i < 300; ++i)
                a.update(.01f, near), check(a.pose.clip != "idle_fidget", "at most one fidget per fidgetEvery");
        }
        // Seats: approach, align, sit, sit_idle with the root lifted, stand, walk back and on.
        {
            auto a = agent(line({{0, 0, 0}, {0, 0, 3}, {0, 0, 8}}));
            a.seats = {{"bench/left", {1.2f, 0, 3}, -1.5708f, .3f, .6f}}; // faces -X, approach from x = 0.6
            a.stops = {{a.route.points[1], 1, 2, "", std::nullopt, 0}};
            a.seatKnown = true, a.seatHeight = -.06f, a.seatSink = .1f;
            int reserved = 0, released = 0;
            a.reserveSeat = [&](int) { return ++reserved, true; };
            a.releaseSeat = [&](int) { ++released; };
            run(a, far, 10, [&] { return a.mode == Mode::Seated; });
            check(a.mode == Mode::Seated && reserved == 1, "reaches the seat and sits");
            check(std::abs(a.position.x - 1.2f) < .01f && std::abs(a.position.z - 3) < .01f, "on the anchor");
            check(std::abs(std::remainder(a.heading + 1.5708f, 6.2832f)) < .17f, "facing the anchor's yaw");
            check(std::abs(a.seatOffset - .36f) < .01f && a.pose.clip == "sit_idle", "root lifted by height - seatHeight, sit_idle");
            check(a.activity() == "sitting" && a.seatRef() == "bench/left", "reports sitting on the seat");
            run(a, far, 10, [&] { return a.mode == Mode::Walk; });
            check(a.mode == Mode::Walk && released == 1 && std::abs(a.seatOffset) < 1e-4f, "stands, walks back, releases the seat");
            run(a, far, 1);
            check(a.position.z > 3.5f && std::abs(a.position.x) < .05f, "back on the route");
            // A seat it doesn't fit (feet would hang over 0.45 m): a plain stop.
            auto b = agent(line({{0, 0, 0}, {0, 0, 3}, {0, 0, 8}}));
            b.seats = {{"high", {1.2f, 0, 3}, -1.5708f, 1.f, .6f}};
            b.stops = {{b.route.points[1], 1, 2, "", std::nullopt, 0}};
            b.seatKnown = true, b.seatHeight = -.06f, b.seatSink = .1f;
            run(b, far, 4, [&] { return b.mode != Mode::Walk; });
            check(b.mode == Mode::Stop, "an unfit seat is a plain stop");
        }
        // Glance: within 4 m and in front, clamped to 45 degrees, eased; never behind or far.
        {
            auto a = agent({});
            a.home = {0, 0, 0}, a.homeHeading = 0;
            a.start();
            auto w = far;
            w.player = {2, 1.6f, 1}; // ahead-left (+X is the character's left)
            run(a, w, 1);
            check(a.pose.lookYaw > .7f && a.pose.lookYaw <= .786f, std::format("looks towards the player, clamped ({})", a.pose.lookYaw));
            w.player = {0, 1.6f, -2};
            run(a, w, 1);
            check(std::abs(a.pose.lookYaw) < .02f, "not at a player behind it");
            w.player = {0, 1.6f, 6};
            run(a, w, 1);
            check(std::abs(a.pose.lookYaw) < .02f, "not beyond 4 m");
            w.player = {0, 1.6f, 2};
            w.glance = false;
            run(a, w, 1);
            check(std::abs(a.pose.lookYaw) < .02f && std::abs(a.pose.lookPitch) < .02f, "not while the player reads a window");
        }
        // Step aside: a player standing on the route ahead; passes by on walkable floor.
        {
            auto a = agent(line({{0, 0, 0}, {0, 0, 12}}));
            auto w = far;
            w.player = {.1f, 1.7f, 4};
            float closest = 99;
            run(a, w, 10, [&] {
                closest = std::min(closest, std::hypot(a.position.x - w.player.x, a.position.z - w.player.z));
                return a.position.z > 7;
            });
            check(a.position.z > 7 && closest > .6f, std::format("steps aside and passes ({} m closest)", closest));
            run(a, w, 3);
            check(std::abs(a.lateral) < .05f, "back on the line afterwards");
            // No room on either side: waits instead.
            auto b = agent(line({{0, 0, 0}, {0, 0, 12}}));
            w.walkable = [](Vec3 p) { return std::abs(p.x) < .2f; };
            run(b, w, 6);
            check(b.position.z < 3.5f && b.yielding && b.pose.clip == "idle", "waits when it can't step aside");
        }
        // Open routes are followed to the end and back.
        {
            auto a = agent(line({{0, 0, 0}, {0, 0, 2}}));
            run(a, far, 3);
            check(a.direction == -1 && a.position.z < 2, "turns back at the end");
        }
        // Interaction: stops, faces the player (at most 180 deg/s), nods, resumes after.
        {
            auto a = agent(line({{0, 0, 0}, {0, 0, 10}}));
            run(a, far, 1);
            float z = a.position.z;
            a.beginInteract({0, 1.7f, -2});
            check(a.mode == Mode::Interact && a.pose.clip == "nod", "nods");
            run(a, far, 1.2f);
            check(std::abs(a.position.z - z) < 1e-4f && std::cos(a.heading) < -.98f, "stands and faces the player");
            a.endInteract();
            run(a, far, 2);
            check(a.mode == Mode::Walk && a.position.z > z + .3f, "resumes its route");
        }
        // Guide: to the guide path, along it ahead of a following player, wave, back, resume.
        {
            auto a = agent(line({{0, 0, 0}, {0, 0, 10}}));
            run(a, far, 1);
            Vec3 left = a.position;
            a.beginGuide(line({{1, 0, 1}, {6, 0, 1}}));
            auto w = far;
            w.player = a.position + Vec3{-1.5f, 1.7f, 0}; // asked from close by, then follows
            float t = run(a, w, 30, [&] {
                w.player = a.position + Vec3{-1.5f, 1.7f, 0};
                return a.guidePhase == 2;
            });
            check(a.guidePhase == 2 && std::abs(a.position.x - 6) < .01f && a.pose.clip == "wave", std::format("guides to the end and waves ({} s)", t));
            run(a, w, 30, [&] { return a.mode == Mode::Walk; });
            check(a.mode == Mode::Walk && std::hypot(a.position.x - left.x, a.position.z - left.z) < .2f, "walks back and resumes");
            // Waits while the player falls behind; cancelled 30 m away.
            auto b = agent(line({{0, 0, 0}, {0, 0, 10}}));
            b.beginGuide(line({{0, 0, 0}, {0, 0, -20}}));
            auto lag = far;
            lag.player = {0, 1.7f, 9};
            run(b, lag, 10);
            check(b.guidePhase == 1 && b.guideS < 3.5f, "waits for a player more than 6 m behind");
            lag.player = {0, 1.7f, 40};
            run(b, lag, 1);
            check(b.guidePhase >= 3, "gives up when the player is 30 m away");
        }
        // Suspended (route unwalkable): idles where it stands.
        {
            auto a = agent(line({{0, 0, 0}, {0, 0, 10}}));
            run(a, far, 1);
            Vec3 at = a.position;
            a.suspend();
            run(a, far, 2);
            check(a.activity() == "suspended" && length(a.position - at) < 1e-5f && a.pose.clip == "idle", "suspended idles in place");
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
    std::cout << "PASS NPC agent: route by arc length, stops, fidget, seats, glance, step aside, interaction, guide, suspend\n";
}
