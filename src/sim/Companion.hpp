#pragma once
// The companion (RFC-0010 companions amendment): one character the user picks that follows
// them through every world. It drives an NpcAgent (sim/Npc.hpp) in its follow and stay modes;
// on a route the runtime runs the agent like a resident. Pure logic, no GL, no Hyprland: the
// runtime answers the questions about the world through callbacks (free standing spots, what
// the player can see), and the planner walks a grid of standing spots.
#include "Npc.hpp"
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <queue>
#include <string>
#include <vector>
namespace hyprune::sim {

// Lazy A* over a 0.5 m grid of standing spots around the start, smoothed with straight legs.
// Cells are evaluated only when reached, with the parent's floor height as the hint, so a
// stair climbs cell by cell (each within the 0.35 m step of the one before).
struct GridPlanner {
    float cell = .5f, range = 24;
    int budget = 500; // node expansions per plan
    // A standing root near p (floor within 0.35 m of p.y, the body clear), or none.
    std::function<std::optional<Vec3>(Vec3)> stand;
    // A straight walkable leg (shortcuts and the last step); null: any leg between spots.
    std::function<bool(Vec3, Vec3)> leg;
    int expanded = 0; // the last plan's expansions (diagnostics)

    // Waypoints from `from` (excluded) to within `tolerance` of `to` (the last one exactly `to`
    // when the leg to it is walkable), or none.
    std::optional<std::vector<Vec3>> plan(Vec3 from, Vec3 to, float tolerance = .4f) {
        expanded = 0;
        auto walkable = [&](Vec3 a, Vec3 b) { return !leg || leg(a, b); };
        if (flat(from, to) < 1e-3f)
            return std::vector<Vec3>{};
        if (walkable(from, to))
            return std::vector<Vec3>{to};
        struct Node {
            Vec3 at{};
            float g = 0;
            int parent = -1;
            bool ok = false;
        };
        std::map<std::pair<int, int>, int> index;
        std::vector<Node> nodes;
        std::vector<std::pair<int, int>> keys;
        int limit = int(range / cell);
        auto node = [&](int x, int z, float hintY) -> int {
            auto key = std::pair{x, z};
            if (auto it = index.find(key); it != index.end())
                return it->second;
            Node n;
            if (std::abs(x) <= limit && std::abs(z) <= limit)
                if (auto s = stand ? stand(Vec3{from.x + x * cell, hintY, from.z + z * cell}) : std::optional<Vec3>{})
                    n.at = *s, n.ok = true;
            n.g = 1e30f;
            nodes.push_back(n);
            keys.push_back(key);
            return index[key] = int(nodes.size()) - 1;
        };
        int start = node(0, 0, from.y);
        nodes[start].at = from, nodes[start].ok = true, nodes[start].g = 0;
        using Item = std::pair<float, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<>> open;
        open.push({flat(from, to), start});
        int goal = -1;
        while (!open.empty() && expanded < budget) {
            auto [f, i] = open.top();
            open.pop();
            if (f > nodes[i].g + flat(nodes[i].at, to) + 1e-4f)
                continue; // stale
            ++expanded;
            if (flat(nodes[i].at, to) <= std::max(tolerance, cell * .75f) && std::abs(nodes[i].at.y - to.y) < .5f) {
                goal = i;
                break;
            }
            auto [x, z] = keys[i];
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz) {
                    if (!dx && !dz)
                        continue;
                    int j = node(x + dx, z + dz, nodes[i].at.y);
                    if (!nodes[j].ok || std::abs(nodes[j].at.y - nodes[i].at.y) > .35f)
                        continue;
                    if (dx && dz) { // no corner cutting
                        int a = node(x + dx, z, nodes[i].at.y), b = node(x, z + dz, nodes[i].at.y);
                        if (!nodes[a].ok || !nodes[b].ok)
                            continue;
                    }
                    float g = nodes[i].g + flat(nodes[i].at, nodes[j].at) + std::abs(nodes[j].at.y - nodes[i].at.y);
                    if (g < nodes[j].g) {
                        nodes[j].g = g, nodes[j].parent = i;
                        open.push({g + flat(nodes[j].at, to), j});
                    }
                }
        }
        if (goal < 0)
            return std::nullopt;
        std::vector<Vec3> raw;
        for (int i = goal; i != start; i = nodes[i].parent)
            raw.push_back(nodes[i].at);
        std::reverse(raw.begin(), raw.end());
        if (walkable(raw.empty() ? from : raw.back(), to))
            raw.push_back(to);
        // Smooth: from each point, the furthest later point a straight leg reaches.
        std::vector<Vec3> out;
        Vec3 at = from;
        size_t i = 0;
        while (i < raw.size()) {
            size_t best = i;
            for (size_t j = std::min(raw.size() - 1, i + 12); j > i; --j)
                if (walkable(at, raw[j])) {
                    best = j;
                    break;
                }
            out.push_back(raw[best]);
            at = raw[best];
            i = best + 1;
        }
        return out;
    }
    static float flat(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.z - b.z); }
};

// What the companion sees each tick (follow and stay modes).
struct CompanionView {
    Vec3 feet{};              // the player's feet
    float heading = 0;        // their direction of travel (their view yaw while still); forward = (sin, 0, cos)
    bool warped = false;      // they jumped more than 3 m in one tick (home switch, travel, respawn)
    bool airborne = false;    // flying or climbing: the companion waits
    bool lookedAt = false;    // they aim at the companion
    bool animate = true;      // characters animate; false (still, power, reduced motion): teleports only
    std::function<bool(Vec3)> inView;               // the player can see a companion standing at this root
    std::function<std::optional<Vec3>(Vec3)> free;  // a free standing spot at p (floor-snapped), or none
};

struct Companion {
    // ---- configuration ----
    float followDistance = 2;
    std::vector<std::string> idles; // idle variants (the companion block), played now and then
    enum class Mode { Follow, Stay };
    Mode mode = Mode::Follow;
    Vec3 stay{};                    // the stay spot
    GridPlanner planner;
    static constexpr float startDistance = .8f, arriveDistance = .3f, personalSpace = 1.f, runFrom = 4, runUntil = 1.5f,
                           teleportDistance = 20, blockedSeconds = 1.5f, landedSeconds = 2, stillTeleport = 4;
    // ---- state ----
    enum class State { Idle, Moving, Waiting };
    State state = State::Idle;
    int side = 1;                    // +1: behind right, -1: behind left
    std::optional<Vec3> slot;
    Mode slotMode = Mode::Follow;    // the mode the slot was chosen in
    Vec3 slotFor{1e9f, 0, 0};        // feet the slot was chosen for
    float slotAge = 1e9f;
    std::vector<Vec3> waypoints;
    Vec3 plannedTo{1e9f, 0, 0};
    float sincePlan = 1e9f, blocked = 0, sinceAirborne = 1e9f, retry = 0;
    bool running = false, teleport = true, waveOnSight = false, moved = false;
    float idleClock = 0, nextIdle = 12;
    uint32_t seed = 0x9e3779b9u;
    int teleports = 0;

    bool hidden() const { return state == State::Waiting; }
    // Activity for the companion state (IPC 0.25).
    std::string activity(const NpcAgent &a) const {
        if (state == State::Waiting)
            return "waiting";
        if (a.mode == NpcAgent::Mode::Interact)
            return "talking";
        if (state == State::Moving)
            return mode == Mode::Stay ? "walking" : running ? "running" : "following";
        return "idle";
    }
    // After a world load or warp: rejoin out of view.
    void rejoin(bool wave) {
        teleport = true;
        waveOnSight = wave;
        slot.reset();
        waypoints.clear();
        state = State::Idle;
    }

    void update(NpcAgent &a, float dt, const CompanionView &v, const NpcWorld &w) {
        moved = false;
        if (dt <= 0)
            return;
        dt = std::min(dt, .1f);
        Vec3 before = a.position;
        float headingBefore = a.heading;
        a.advance(dt, w);
        if (a.mode == NpcAgent::Mode::Interact) {
            interact(a, dt);
        } else if (!v.animate)
            stillTick(a, dt, v);
        else
            tick(a, dt, v);
        moved = state != State::Waiting &&
                (std::hypot(a.position.x - before.x, a.position.z - before.z) > 1e-4f || std::abs(a.position.y - before.y) > 1e-4f ||
                 std::abs(a.heading - headingBefore) > 1e-4f) &&
                !teleported;
        teleported = false;
    }
    // A free spot out of view, 2-6 m from the player, with a walkable way to them; the one
    // nearest the follow slot (behind the player) first.
    std::optional<Vec3> outOfView(const CompanionView &v, int planBudget = 3) {
        Vec3 back{-std::sin(v.heading), 0, -std::cos(v.heading)};
        std::vector<std::pair<float, Vec3>> candidates;
        for (float r : {followDistance, 3.f, 4.f, 2.f, 5.f, 6.f})
            for (int k = 0; k < 16; ++k) {
                float angle = std::atan2(back.x, back.z) + (k % 2 ? 1 : -1) * float((k + 1) / 2) * .3927f;
                Vec3 p = v.feet + Vec3{std::sin(angle), 0, std::cos(angle)} * r;
                Vec3 ideal = v.feet + back * followDistance;
                candidates.push_back({std::hypot(p.x - ideal.x, p.z - ideal.z) + .01f * k, p});
            }
        std::stable_sort(candidates.begin(), candidates.end(), [](auto &x, auto &y) { return x.first < y.first; });
        int plans = 0;
        for (auto &[_, p] : candidates) {
            auto spot = v.free ? v.free(p) : std::optional<Vec3>{p};
            if (!spot || (v.inView && v.inView(*spot)))
                continue;
            if (plans++ >= planBudget)
                break;
            if (planner.plan(*spot, v.feet, 1.2f))
                return spot;
        }
        return std::nullopt;
    }

  private:
    bool teleported = false;
    float random(float lo, float hi) {
        seed = seed * 1664525u + 1013904223u;
        return lo + (hi - lo) * float(seed >> 8) / float(1u << 24);
    }
    static float flat(Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.z - b.z); }
    // The follow slot: behind and to one side, keeping the side while it is free.
    std::optional<Vec3> chooseSlot(const CompanionView &v) {
        if (mode == Mode::Stay)
            return stay;
        float behind = v.heading + 3.14159265f;
        auto at = [&](float angle, float distance) { return v.feet + Vec3{std::sin(angle), 0, std::cos(angle)} * distance; };
        constexpr float aside = .698f; // 40 deg
        for (float d : {followDistance, 1.5f, 2.5f})
            for (float a : {behind - side * aside, behind + side * aside, behind}) {
                if (auto s = v.free ? v.free(at(a, d)) : std::optional<Vec3>{at(a, d)}) {
                    float rel = std::remainder(a - behind, 6.2831853f);
                    if (std::abs(rel) > .1f)
                        side = rel < 0 ? 1 : -1;
                    return s;
                }
            }
        for (float d : {2.f, 3.f, 1.5f}) // anywhere near
            for (int k = 0; k < 12; ++k)
                if (auto s = v.free ? v.free(at(behind + k * .5236f, d)) : std::nullopt)
                    return s;
        return std::nullopt;
    }
    bool place(NpcAgent &a, Vec3 at, const CompanionView &v) {
        a.position = at;
        Vec3 to = v.feet - at;
        if (std::hypot(to.x, to.z) > .05f)
            a.heading = std::atan2(to.x, to.z);
        a.idle();
        waypoints.clear();
        blocked = 0;
        state = State::Idle;
        ++teleports;
        teleported = true;
        return true;
    }
    // Teleport out of view, or wait hidden until a spot exists (checked twice a second).
    void teleportTick(NpcAgent &a, float dt, const CompanionView &v) {
        retry -= dt;
        if (state == State::Waiting && retry > 0)
            return;
        retry = .5f;
        if (auto spot = outOfView(v)) {
            place(a, *spot, v);
            teleport = false;
            slot.reset();
        } else
            state = State::Waiting;
    }
    void interact(NpcAgent &a, float dt) {
        if (a.interactWith) {
            Vec3 d = *a.interactWith - a.position;
            if (std::hypot(d.x, d.z) > .05f)
                a.turnTowards(std::atan2(d.x, d.z), dt);
        }
        if (a.has("talk"))
            a.play("talk");
        else if (a.pose.loop || a.clipDone())
            a.idle();
    }
    // Still characters: posed, no walking; it only teleports, out of view, when its slot is far.
    void stillTick(NpcAgent &a, float dt, const CompanionView &v) {
        if (mode == Mode::Stay)
            return;
        slotAge += dt;
        if (!slot || flat(v.feet, slotFor) > .3f || slotAge > .5f)
            slot = chooseSlot(v), slotFor = v.feet, slotAge = 0;
        bool far = slot ? flat(a.position, *slot) > stillTeleport : flat(a.position, v.feet) > followDistance + stillTeleport;
        if (!(far || teleport || v.warped))
            return;
        if (state != State::Waiting && v.inView && v.inView(a.position) && !teleport && !v.warped)
            return; // never vanishes in view
        if (slot && v.inView && !v.inView(*slot)) {
            place(a, *slot, v);
            teleport = false;
            return;
        }
        teleportTick(a, dt, v);
    }
    void tick(NpcAgent &a, float dt, const CompanionView &v) {
        float toPlayer = flat(a.position, v.feet);
        if (mode == Mode::Follow) {
            if (v.warped)
                teleport = true;
            if (v.airborne) {
                sinceAirborne = 0;
                if (toPlayer > teleportDistance)
                    teleport = true;
                if (!teleport) { // waits where it is, looking at them
                    state = State::Idle;
                    waypoints.clear();
                    blocked = 0;
                    idleTick(a, dt, v);
                    return;
                }
            } else
                sinceAirborne += dt;
            if (toPlayer > teleportDistance || blocked > (sinceAirborne < 3 ? landedSeconds : blockedSeconds))
                teleport = true;
            if (teleport) {
                teleportTick(a, dt, v);
                return;
            }
        }
        // Rejoined after a world switch: one wave the first time the player looks at it.
        if (waveOnSight && v.lookedAt && a.has("wave")) {
            a.play("wave", false);
            waveOnSight = false;
            state = State::Idle;
            waypoints.clear();
        }
        if (a.pose.clip == "wave" && !a.pose.loop && !a.clipDone()) {
            standAndLook(a, dt, v, false);
            return;
        }
        slotAge += dt;
        bool crowded = mode == Mode::Follow && toPlayer < personalSpace;
        if (!slot || flat(v.feet, slotFor) > .3f || slotAge > .5f || crowded || slotMode != mode)
            slot = chooseSlot(v), slotFor = v.feet, slotAge = 0, slotMode = mode;
        if (!slot) { // nowhere free near the player: stand, look at them
            state = State::Idle;
            idleTick(a, dt, v);
            return;
        }
        float toSlot = flat(a.position, *slot);
        if (state == State::Idle &&
            (toSlot > startDistance || (mode == Mode::Follow && (toPlayer > followDistance + 1 || crowded)) ||
             (mode == Mode::Stay && toSlot > arriveDistance)))
            state = State::Moving, waypoints.clear(), plannedTo = {1e9f, 0, 0};
        if (state == State::Idle) {
            idleTick(a, dt, v);
            return;
        }
        if (toSlot < arriveDistance) {
            state = State::Idle;
            running = false;
            waypoints.clear();
            a.idle();
            return;
        }
        sincePlan += dt;
        if (waypoints.empty() || flat(plannedTo, *slot) > .5f) {
            if (sincePlan >= .5f || waypoints.empty()) {
                if (sincePlan < .5f && blocked > 0) { // failing: at most twice a second
                    blocked += dt;
                    standAndLook(a, dt, v);
                    return;
                }
                sincePlan = 0;
                plannedTo = *slot;
                if (auto path = planner.plan(a.position, *slot)) {
                    waypoints = *path;
                    blocked = 0;
                } else {
                    waypoints.clear();
                    blocked += dt;
                    standAndLook(a, dt, v);
                    return;
                }
            }
        }
        walk(a, dt);
    }
    void standAndLook(NpcAgent &a, float dt, const CompanionView &v, bool idle = true) {
        Vec3 d = v.feet - a.position;
        if (std::hypot(d.x, d.z) > .05f)
            a.turnTowards(std::atan2(d.x, d.z), dt, NpcAgent::turnRate * .6f);
        if (idle)
            a.idle();
    }
    // Along the waypoints: walk, or run while the way is long; clip rate follows the pace.
    void walk(NpcAgent &a, float dt) {
        float left = 0;
        Vec3 at = a.position;
        for (auto &p : waypoints)
            left += flat(at, p), at = p;
        if (left > runFrom)
            running = true;
        else if (left < runUntil)
            running = false;
        float walkSpeed = std::max(.1f, a.walkSpeed), runSpeed = a.runSpeed > 0 ? a.runSpeed : 0;
        float v = running ? (runSpeed > 0 ? runSpeed : walkSpeed * 1.5f) : walkSpeed;
        while (!waypoints.empty()) {
            Vec3 d = waypoints.front() - a.position;
            float dist = std::hypot(d.x, d.z);
            if (dist < .03f) {
                a.position = waypoints.front();
                waypoints.erase(waypoints.begin());
                continue;
            }
            float target = std::atan2(d.x, d.z);
            if (std::abs(std::remainder(target - a.heading, 6.2831853f)) > .9f) { // turn in place first
                a.turnTowards(target, dt);
                a.idle();
                return;
            }
            a.turnTowards(target, dt, NpcAgent::turnRate, 0);
            float step = std::min(dist, v * dt);
            float y0 = a.position.y;
            a.position = a.position + Vec3{d.x / dist, 0, d.z / dist} * step;
            a.position.y = y0 + (waypoints.front().y - y0) * std::min(1.f, step / std::max(dist, 1e-3f));
            if (running && runSpeed > 0)
                a.play("run", true, v / runSpeed);
            else
                a.play("walk", true, v / walkSpeed);
            return;
        }
        a.idle();
    }
    // At its slot: idle, the occasional variant, facing the player when looked at.
    void idleTick(NpcAgent &a, float dt, const CompanionView &v) {
        if (v.lookedAt) {
            Vec3 d = v.feet - a.position;
            if (std::hypot(d.x, d.z) > .05f)
                a.turnTowards(std::atan2(d.x, d.z), dt);
        }
        if (!a.pose.loop && !a.clipDone())
            return; // a variant or wave playing through
        idleClock += dt;
        std::vector<std::string> variants;
        for (auto &n : idles)
            if (a.has(n))
                variants.push_back(n);
        if (a.has("idle_fidget"))
            variants.push_back("idle_fidget");
        if (idleClock >= nextIdle && !variants.empty()) {
            idleClock = 0;
            nextIdle = random(8, 20);
            a.play(variants[size_t(random(0, float(variants.size()) - .001f))], false);
            return;
        }
        a.idle();
    }
};
} // namespace hyprune::sim
