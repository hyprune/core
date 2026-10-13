#pragma once
// Local avoidance between characters (RFC-0010 steering amendment). Residents, user
// characters and the companion see each other as movers: where they are and how fast they go
// at the start of the tick (the runtime's snapshot, so the update order changes nothing).
// An agent scores a few candidate motions (a lateral offset from its line and a speed factor)
// against everyone's predicted positions over a short horizon and takes the cheapest: a clear
// motion near its line at full pace, keeping to its right when two meet. Priorities break
// the symmetry: the lower one gives way (slows first, backs off when both are stuck), so two
// characters never wait for each other for good. Pure and deterministic: no clocks, no
// randomness; the same snapshot and dt give the same choice.
#include "Math.hpp"
#include <cmath>
#include <functional>
#include <vector>
namespace hyprune::sim {
struct Mover {
    int id = 0;          // the runtime's index; the agent skips its own
    Vec3 position{};     // root, world space
    Vec3 velocity{};     // m/s over the last tick (xz)
    float radius = .35f;
    int priority = 0;    // distinct; the lower gives way
    Vec3 facing{0, 0, 1};  // unit, xz: where it walks (or would, while it waits)
    bool waiting = false;  // held up on its way (by someone in it)
};
// The first predicted overlap of a motion with the movers.
struct Conflict {
    float time = 1e9f;      // seconds until clearance first drops under the margin
    float clearance = 1e9f; // the smallest clearance over the horizon (m, between bodies)
    int mover = -1;         // index into the movers of the worst one
    bool any() const { return mover >= 0; }
};
inline constexpr float steerHorizon = 2.f, steerStep = .25f, steerMargin = .12f, steerRange = 6.f, waitingSpeed = .8f;
inline float flatDistance(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.z - b.z); }
inline float flatSpeed(Vec3 v) { return std::hypot(v.x, v.z); }
// `own(t)`: the agent's root t seconds ahead under one candidate motion.
inline Conflict predict(const std::function<Vec3(float)> &own, float radius, const std::vector<Mover> &movers, int self,
                        float horizon = steerHorizon) {
    Conflict c;
    Vec3 here = own(0);
    for (size_t j = 0; j < movers.size(); ++j) {
        auto &m = movers[j];
        if (m.id == self || flatDistance(here, m.position) > steerRange + horizon * (flatSpeed(m.velocity) + 2))
            continue;
        // A mover held up is predicted on its way (where it wants to go), not standing.
        Vec3 velocity = m.waiting ? m.facing * std::max(flatSpeed(m.velocity), waitingSpeed) : m.velocity;
        for (float t = 0; t <= horizon + 1e-4f; t += steerStep) {
            Vec3 o = m.position + velocity * t;
            float clear = flatDistance(own(t), o) - radius - m.radius;
            if (clear < c.clearance)
                c.clearance = clear;
            if (clear < steerMargin) {
                if (t < c.time)
                    c.time = t, c.mover = int(j);
                break;
            }
        }
    }
    return c;
}
// The movers near a position (the agent's own excluded), for a quick "anyone about?" test.
inline bool anyNear(const std::vector<Mover> &movers, int self, Vec3 at, float range = steerRange) {
    for (auto &m : movers)
        if (m.id != self && flatDistance(at, m.position) < range)
            return true;
    return false;
}
// The cost of a candidate's conflict: an overlap now or soon costs most; a near miss a little.
inline float conflictCost(const Conflict &c) {
    if (!c.any())
        return c.clearance < .5f ? (.5f - c.clearance) * .5f : 0.f;
    return 20.f + 10.f * (steerHorizon - std::min(c.time, steerHorizon)) + 10.f * std::max(0.f, steerMargin - c.clearance);
}
} // namespace hyprune::sim
