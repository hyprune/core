// Steering between characters (RFC-0010 steering amendment, sim/Steer.hpp): residents on
// opposing and crossing routes never overlap and both get through; in a corridor too narrow
// to pass, the lower priority backs off and nobody waits for good; a detour walks round a
// blocked stretch and rejoins the route; the companion makes way in a doorway; the gait keeps
// the feet in step with the ground at every speed; a stop facing a screen stands in front of
// it; and the same inputs give the same result whatever order the agents update in.
#include "sim/Companion.hpp"
#include <format>
#include <iostream>
#include <stdexcept>
using namespace hyprune;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
sim::Route line(std::vector<Vec3> points, bool closed = false) {
    return sim::Route::build(points, std::vector<sim::Segment>(points.size(), sim::Segment::Line), closed);
}
sim::NpcAgent agent(sim::Route route, int id, bool reverse = false) {
    sim::NpcAgent a;
    a.route = std::move(route);
    a.walkSpeed = 1;
    a.clips = {{"idle", 2}, {"walk", 1}};
    a.reverse = reverse;
    a.id = id, a.priority = 1000 - id;
    a.fidgetEvery = 1e9f;
    a.start();
    return a;
}
float flat(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.z - b.z); }
// Runs agents together for `seconds` at 60 Hz: one snapshot of everyone per tick, then each
// updates (in the given order). Returns the smallest distance between any two (centres).
struct Sim {
    std::vector<sim::NpcAgent> agents;
    std::function<bool(Vec3)> walkable;
    bool reversed = false;
    float closest = 1e9f;
    void tick(float dt) {
        std::vector<sim::Mover> movers;
        for (auto &a : agents)
            movers.push_back({a.id, a.position, a.velocity, a.radius, a.priority, Vec3{std::sin(a.heading), 0, std::cos(a.heading)},
                              a.mode == sim::NpcAgent::Mode::Walk && a.yielding});
        sim::NpcWorld w;
        w.player = {100, 1.7f, 100};
        w.walkable = walkable;
        w.movers = &movers;
        for (size_t k = 0; k < agents.size(); ++k)
            agents[reversed ? agents.size() - 1 - k : k].update(dt, w);
        for (size_t i = 0; i < agents.size(); ++i)
            for (size_t j = i + 1; j < agents.size(); ++j)
                closest = std::min(closest, flat(agents[i].position, agents[j].position));
    }
    void run(float seconds) {
        for (int i = 0; i < int(seconds * 60); ++i)
            tick(1.f / 60);
    }
};
} // namespace
int main() {
    try {
        constexpr float bodies = .7f, epsilon = .05f;
        // Opposing on one line in the open: they pass (keeping right) and never overlap.
        {
            Sim s;
            s.agents = {agent(line({{0, 0, 0}, {0, 0, 12}}), 0), agent(line({{0, 0, 0}, {0, 0, 12}}), 1, true)};
            s.agents[1].phase = 1, s.agents[1].start();
            s.run(6);
            check(s.closest >= bodies - epsilon, std::format("opposing: closest {:.3f} m", s.closest));
            check(s.agents[0].position.z > 4.5f && s.agents[1].position.z < 7.5f,
                  std::format("opposing: both got past ({:.2f}, {:.2f})", s.agents[0].position.z, s.agents[1].position.z));
        }
        // Crossing routes that meet at the same time: one slows, never an overlap, both cross.
        {
            Sim s;
            s.agents = {agent(line({{-6, 0, 0}, {6, 0, 0}}), 0), agent(line({{0, 0, -6}, {0, 0, 6}}), 1)};
            s.run(10);
            check(s.closest >= bodies - epsilon, std::format("crossing: closest {:.3f} m", s.closest));
            check(s.agents[0].position.x > 1 || s.agents[0].direction < 0, "crossing: the first got across");
            check(s.agents[1].position.z > 1 || s.agents[1].direction < 0, "crossing: the second got across");
        }
        // A 0.6 m corridor (z from -3 to 3), open beyond: too narrow to pass. The lower priority
        // backs off out of it and stands aside; the other goes through; nobody waits for good.
        {
            Sim s;
            s.walkable = [](Vec3 p) { return std::abs(p.z) > 3 || std::abs(p.x) < .3f; };
            float jump = 0;
            s.agents = {agent(line({{0, 0, -8}, {0, 0, 8}}), 0), agent(line({{0, 0, -8}, {0, 0, 8}}), 1, true)};
            s.agents[0].phase = .2f, s.agents[0].start();
            s.agents[1].phase = .8f, s.agents[1].start();
            bool backed = false;
            float through0 = -1, through1 = -1;
            for (int i = 0; i < 60 * 60; ++i) {
                Vec3 before = s.agents[1].position;
                s.tick(1.f / 60);
                jump = std::max(jump, flat(before, s.agents[1].position));
                backed |= s.agents[1].backoffLeft > 0;
                if (through0 < 0 && s.agents[0].position.z > 4)
                    through0 = i / 60.f;
                if (through1 < 0 && s.agents[1].position.z < -4 && through0 >= 0)
                    through1 = i / 60.f;
            }
            check(s.closest >= bodies - epsilon, std::format("corridor: closest {:.3f} m", s.closest));
            check(backed, "corridor: the lower priority backed off");
            check(jump < .05f, std::format("corridor: no jumps ({:.3f} m in a tick)", jump));
            check(through0 > 0 && through1 > 0, std::format("corridor: no deadlock, through at {:.1f} s and {:.1f} s", through0, through1));
        }
        // A detour round a blocked stretch (a box from z 4 to 6 on the line): walked round, rejoined.
        {
            Sim s;
            auto a = agent(line({{0, 0, 0}, {0, 0, 12}}), 0);
            a.detours = {{3.4f, 6.6f, {{1, 0, 4}, {1, 0, 6}, {0, 0, 6.6f}}}};
            a.start();
            s.agents = {a};
            bool inside = false, detoured = false;
            for (int i = 0; i < 60 * 14; ++i) {
                s.tick(1.f / 60);
                auto p = s.agents[0].position;
                inside |= std::abs(p.x) < .35f + .3f && p.z > 4 && p.z < 6;
                detoured |= s.agents[0].mode == sim::NpcAgent::Mode::Detour;
            }
            check(detoured && !inside, "detour: walked round the blocked stretch");
            check(std::abs(s.agents[0].position.x) < .01f, "detour: back on its line");
            // And the other way along the route, the same way round backwards.
            bool back = false;
            for (int i = 0; i < 60 * 14; ++i) {
                s.tick(1.f / 60);
                auto p = s.agents[0].position;
                inside |= std::abs(p.x) < .65f && p.z > 4 && p.z < 6;
                back |= s.agents[0].direction < 0 && s.agents[0].mode == sim::NpcAgent::Mode::Detour;
            }
            check(back && !inside, "detour: walked round backwards too");
        }
        // Deterministic: the same start gives bit-identical results whatever the update order.
        {
            auto scenario = [](bool reversed) {
                Sim s;
                s.reversed = reversed;
                s.agents = {agent(line({{-6, 0, 0}, {6, 0, 0}}), 0), agent(line({{0, 0, -6}, {0, 0, 6}}), 1),
                            agent(line({{-5, 0, -5}, {5, 0, 5}}), 2, true)};
                s.run(8);
                std::vector<float> out;
                for (auto &a : s.agents)
                    out.insert(out.end(), {a.position.x, a.position.z, a.heading, a.gaitPhase});
                return out;
            };
            check(scenario(false) == scenario(false) && scenario(false) == scenario(true), "deterministic, whatever the update order");
        }
        // Gait: walk 1 m/s (1 s clip), run 3 m/s (0.7 s clip). At every speed the shared cycle
        // advances with the ground (cycles x blended stride = distance): no foot sliding.
        {
            sim::NpcAgent a;
            a.walkSpeed = 1, a.runSpeed = 3;
            a.clips = {{"idle", 2}, {"walk", 1}, {"run", .7f}};
            for (float v : {.5f, 1.f, 1.6f, 2.f, 2.6f, 3.f, 4.5f}) {
                a.pose = {}, a.gaitPhase = 0;
                for (int i = 0; i < 30; ++i) // join the cycle (crossfade from idle done)
                    a.advance(1.f / 60, {}), a.gait(v, 1.f / 60);
                float t = std::clamp((v - 1) / 2, 0.f, 1.f);
                float stride = (1 - t) * 1 * 1 + t * 3 * .7f;
                float phase = a.gaitPhase, cycles = 0;
                for (int i = 0; i < 60; ++i) {
                    a.advance(1.f / 60, {}), a.gait(v, 1.f / 60);
                    cycles += std::fmod(a.gaitPhase - phase + 1, 1.f), phase = a.gaitPhase;
                }
                check(std::abs(cycles * stride - v) < .01f, std::format("gait at {} m/s: {:.3f} m of stride a second", v, cycles * stride));
                bool blended = a.pose.previous == "walk" && a.pose.clip == "run";
                check(t > 0 && t < 1 ? blended && std::abs(a.pose.fade - t) < 1e-4f : a.pose.clip == (t >= 1 ? "run" : "walk"),
                      std::format("gait at {} m/s: {} / {} weight {}", v, a.pose.previous, a.pose.clip, a.pose.fade));
                check(std::abs(a.pose.clipTime / (a.pose.clip == "run" ? .7f : 1.f) - a.gaitPhase) < 1e-4f, "gait: clips share the phase");
            }
            check(std::abs(a.topSpeed() - 3 * sim::NpcAgent::maxRunRate) < 1e-4f, "top speed: run at 1.8x");
        }
        // A stop facing a screen stands at its front spot, faces it, then walks back to the route.
        {
            Sim s;
            auto a = agent(line({{0, 0, 0}, {0, 0, 10}}), 0);
            sim::NpcStop stop;
            stop.at = a.route.points[0] + 5, stop.point = 0, stop.seconds = 1.5f;
            stop.face = Vec3{3, 1.5f, 5}, stop.spot = Vec3{1.2f, 0, 5}; // the screen at x 3 faces -x
            a.stops = {stop};
            a.start();
            s.agents = {a};
            bool stood = false, faced = false;
            for (int i = 0; i < 60 * 14; ++i) {
                s.tick(1.f / 60);
                auto &b = s.agents[0];
                if (b.mode == sim::NpcAgent::Mode::Stop && flat(b.position, {1.2f, 0, 5}) < .05f) {
                    stood = true;
                    faced |= std::abs(std::remainder(b.heading - 1.5707963f, 6.2831853f)) < .1f;
                }
            }
            check(stood && faced, "screen stop: stands in front of the screen, facing it");
            check(std::abs(s.agents[0].position.x) < .01f && s.agents[0].position.z > 6, "screen stop: back on the route and on");
        }
        // The companion standing in a doorway (a wall at z 0 with a 1.2 m gap) makes way for a
        // resident walking through; they never overlap and the resident gets through.
        {
            auto walls = [](Vec3 p) { return std::abs(p.z) < .35f + .1f && std::abs(p.x) > .6f - .35f; };
            sim::NpcAgent resident = agent(line({{0, 0, -6}, {0, 0, 6}}), 0);
            resident.phase = .25f, resident.start();
            sim::NpcAgent ash;
            ash.walkSpeed = 1, ash.runSpeed = 3;
            ash.clips = {{"idle", 2}, {"walk", 1}, {"run", .7f}};
            ash.position = {0, 0, 0}, ash.id = 1, ash.priority = -1;
            sim::Companion c;
            c.teleport = false;
            c.planner.stand = [&](Vec3 p) -> std::optional<Vec3> {
                if (walls(p) || std::abs(p.x) > 10 || std::abs(p.z) > 10)
                    return std::nullopt;
                return Vec3{p.x, 0, p.z};
            };
            c.planner.leg = [&](Vec3 a, Vec3 b) {
                for (int i = 0; i <= 20; ++i)
                    if (walls(a + (b - a) * (i / 20.f)))
                        return false;
                return true;
            };
            sim::CompanionView v;
            v.feet = {0, 0, 1.5f}; // the player just through the door, standing
            v.inView = [](Vec3) { return false; };
            v.free = [&](Vec3 p) { return c.planner.stand(p); };
            float closest = 1e9f, through = -1;
            for (int i = 0; i < 60 * 20; ++i) {
                std::vector<sim::Mover> movers = {
                    {0, resident.position, resident.velocity, .35f, resident.priority,
                     Vec3{std::sin(resident.heading), 0, std::cos(resident.heading)}, resident.mode == sim::NpcAgent::Mode::Walk && resident.yielding},
                    {1, ash.position, ash.velocity, .35f, -1, Vec3{std::sin(ash.heading), 0, std::cos(ash.heading)}, false}};
                sim::NpcWorld w;
                w.player = {20, 1.7f, 20};
                w.walkable = [&](Vec3 p) { return !walls(p); };
                w.movers = &movers;
                c.slot = Vec3{0, 0, 0}, c.slotFor = v.feet, c.slotAge = 0; // its slot is the doorway itself
                resident.update(1.f / 60, w);
                c.update(ash, 1.f / 60, v, w);
                closest = std::min(closest, flat(resident.position, ash.position));
                if (through < 0 && resident.position.z > 2)
                    through = i / 60.f;
            }
            check(c.madeWay >= 1, "doorway: the companion made way");
            check(closest >= bodies - epsilon, std::format("doorway: closest {:.3f} m", closest));
            check(through > 0, "doorway: the resident got through (no deadlock)");
        }
    } catch (const std::exception &e) {
        std::cerr << "FAIL npc-steer: " << e.what() << "\n";
        return 1;
    }
    std::cout << "PASS npc-steer\n";
    return 0;
}
