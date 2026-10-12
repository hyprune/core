// Companion (RFC-0010 companions amendment): the follow slot, hysteresis, catching up,
// teleporting out of view, waiting hidden, keep-clear and blocked ways, flying, stay mode,
// still characters, facing the player and the focus relocation, against a small synthetic
// world (a floor with walls and boxes the callbacks refuse).
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
struct Box {
    float x0, z0, x1, z1;
    bool contains(Vec3 p, float grow = 0) const { return p.x > x0 - grow && p.x < x1 + grow && p.z > z0 - grow && p.z < z1 + grow; }
};
// A floor at y = 0 from -40 to 40 with walls (blocked boxes) and keep-clear boxes (no slot,
// walkable through).
struct Scene {
    std::vector<Box> walls, keepClear;
    float radius = .35f;
    bool solid(Vec3 p) const {
        if (std::abs(p.x) > 40 || std::abs(p.z) > 40)
            return true;
        for (auto &w : walls)
            if (w.contains(p, radius))
                return true;
        return false;
    }
    std::optional<Vec3> stand(Vec3 p) const {
        if (solid(p))
            return std::nullopt;
        return Vec3{p.x, 0, p.z};
    }
    bool leg(Vec3 a, Vec3 b) const {
        float d = std::hypot(b.x - a.x, b.z - a.z);
        int n = std::max(1, int(d / .25f));
        for (int i = 0; i <= n; ++i)
            if (solid(a + (b - a) * (float(i) / n)))
                return false;
        return true;
    }
};
struct Player {
    Vec3 feet{0, 0, 0};
    float heading = 0; // forward +z
    Vec3 eye() const { return feet + Vec3{0, 1.7f, 0}; }
    Vec3 forward() const { return {std::sin(heading), 0, std::cos(heading)}; }
    bool sees(Vec3 root, const Scene &s) const {
        Vec3 head = root + Vec3{0, 1.5f, 0};
        Vec3 d = head - eye();
        float l = length(d);
        if (l > 60 || dot(d / l, forward()) < std::cos(.73f + .17f)) // 84 deg wide, plus 10 deg
            return false;
        int n = int(l / .25f);
        for (int i = 1; i < n; ++i) { // occluded by a wall
            Vec3 q = eye() + d * (float(i) / n);
            for (auto &w : s.walls)
                if (w.contains(q))
                    return false;
        }
        return true;
    }
};
struct Fixture {
    Scene scene;
    Player player;
    sim::NpcAgent agent;
    sim::Companion companion;
    sim::CompanionView view;
    sim::NpcWorld world;
    bool look = false, airborne = false, animate = true, warped = false;
    float minDistance = 1e9f;
    bool everSeenPop = false;
    Fixture() {
        agent.walkSpeed = 1, agent.runSpeed = 3;
        agent.clips = {{"idle", 2}, {"walk", 1}, {"run", .7f}, {"idle_alt1", 2}, {"wave", 1}, {"talk", 1.5f}};
        agent.position = {1, 0, -2};
        agent.start();
        agent.position = {1, 0, -2};
        companion.idles = {"idle_alt1"};
        companion.teleport = false;
        companion.planner.stand = [this](Vec3 p) { return scene.stand(p); };
        companion.planner.leg = [this](Vec3 a, Vec3 b) { return scene.leg(a, b); };
    }
    sim::CompanionView makeView() {
        sim::CompanionView v;
        v.feet = player.feet;
        v.heading = player.heading;
        v.lookedAt = look;
        v.airborne = airborne;
        v.animate = animate;
        v.warped = warped;
        v.inView = [this](Vec3 p) { return player.sees(p, scene); };
        v.free = [this](Vec3 p) -> std::optional<Vec3> {
            for (auto &k : scene.keepClear)
                if (k.contains(p, scene.radius))
                    return std::nullopt;
            if (std::hypot(p.x - player.feet.x, p.z - player.feet.z) < 1)
                return std::nullopt;
            return scene.stand(p);
        };
        return v;
    }
    // Steps the world; `move` advances the player.
    void run(float seconds, std::function<void(float)> move = {}) {
        for (float t = 0; t < seconds; t += .02f) {
            if (move)
                move(.02f);
            Vec3 before = agent.position;
            bool wasHidden = companion.hidden();
            world.player = player.eye();
            world.present = true;
            companion.update(agent, .02f, makeView(), world);
            warped = false;
            bool jumped = std::hypot(agent.position.x - before.x, agent.position.z - before.z) > .5f;
            if (jumped && !wasHidden && player.sees(before, scene))
                everSeenPop = true; // vanished in view
            if (jumped && player.sees(agent.position, scene))
                everSeenPop = true; // appeared in view
            if (!companion.hidden())
                minDistance = std::min(minDistance, std::hypot(agent.position.x - player.feet.x, agent.position.z - player.feet.z));
        }
    }
    float distance() const { return std::hypot(agent.position.x - player.feet.x, agent.position.z - player.feet.z); }
    // Is it behind the player (in the half-space they face away from)?
    bool behind() const { return dot(agent.position - player.feet, player.forward()) < 0; }
};
} // namespace
int main() {
    try {
        // Planner: around a wall with a gap; nothing through a closed wall.
        {
            Scene s;
            s.walls = {{-10, 4, 3, 5}};
            sim::GridPlanner p;
            p.stand = [&](Vec3 q) { return s.stand(q); };
            p.leg = [&](Vec3 a, Vec3 b) { return s.leg(a, b); };
            auto path = p.plan({0, 0, 0}, {0, 0, 9});
            check(path && !path->empty(), "plans round a wall");
            Vec3 at{0, 0, 0};
            for (auto &q : *path) {
                check(s.leg(at, q), "every planned leg is walkable");
                at = q;
            }
            check(std::hypot(at.x, at.z - 9) < .01f, "ends at the goal");
            s.walls = {{-41, 4, 41, 5}};
            check(!p.plan({0, 0, 0}, {0, 0, 9}), "no way through a closed wall");
            check(p.expanded <= p.budget, "bounded expansions");
        }
        // Follows behind and beside at 1.5-2.5 m while the player walks, never within 1 m.
        {
            Fixture f;
            f.run(1);
            f.run(10, [&](float dt) { f.player.feet.z += 1.2f * dt; });
            f.run(3);
            check(f.distance() > 1.4f && f.distance() < 2.7f, std::format("follow distance {:.2f}", f.distance()));
            check(f.behind(), "behind the player");
            check(std::abs(f.agent.position.x - f.player.feet.x) > .5f, "beside, not straight behind");
            check(f.minDistance >= .95f, std::format("never within 1 m (min {:.2f})", f.minDistance));
            check(f.companion.state == sim::Companion::State::Idle && f.agent.pose.clip.starts_with("idle"), "idles at its slot");
            check(!f.everSeenPop, "never appeared or vanished in view");
        }
        // Hysteresis: small player motions don't make it shuffle (no frames).
        {
            Fixture f;
            f.agent.position = {4, 0, -4}; // walks to its slot first
            f.run(6);
            check(f.companion.state == sim::Companion::State::Idle, "settled");
            int moving = 0;
            f.run(3, [&](float dt) {
                f.player.feet.z += .1f * dt; // 0.3 m in 3 s
                moving += f.companion.moved;
            });
            check(moving == 0, std::format("stays put for 0.3 m ({} moving ticks)", moving));
        }
        // Catches up by running when the way is long.
        {
            Fixture f;
            f.run(3);
            bool ran = false;
            f.run(6, [&](float dt) {
                if (f.player.feet.z < 9)
                    f.player.feet.z += 4.5f * dt; // a sprint
                ran |= f.agent.pose.clip == "run" && f.companion.activity(f.agent) == "running";
            });
            check(ran, "ran to catch up");
            check(f.distance() < 3, std::format("caught up ({:.2f} m)", f.distance()));
            check(f.companion.teleports == 0, "no teleport within 20 m");
        }
        // Far: teleports out of view, 2-6 m from the player, behind them.
        {
            Fixture f;
            f.run(2);
            f.player.feet = {0, 0, 30};
            f.run(.2f);
            check(f.companion.teleports == 1, "teleported when more than 20 m away");
            check(f.distance() >= 1.9f && f.distance() <= 6.1f, std::format("lands 2-6 m away ({:.2f})", f.distance()));
            check(!f.player.sees(f.agent.position, f.scene), "out of view");
            check(!f.everSeenPop, "no pop in view");
        }
        // A warp (home switch, travel) teleports even when close.
        {
            Fixture f;
            f.run(2);
            f.player.feet = {8, 0, 0}, f.player.heading = 1.57f;
            f.warped = true;
            f.run(.1f);
            check(f.companion.teleports == 1, "a warp teleports");
            check(!f.player.sees(f.agent.position, f.scene), "out of view after a warp");
        }
        // Waits hidden while every spot is in view, appears as soon as one is not.
        {
            Fixture f;
            f.run(1);
            auto inView = true;
            f.player.feet = {0, 0, 30};
            f.scene.keepClear = {{-41, -41, 41, 41}}; // nothing free
            f.run(1);
            check(f.companion.hidden() && f.companion.activity(f.agent) == "waiting", "waiting hidden without a free spot");
            f.scene.keepClear.clear();
            f.run(.6f);
            check(!f.companion.hidden() && f.distance() < 6.1f && !f.player.sees(f.agent.position, f.scene), "placed once a spot exists");
            (void)inView;
        }
        // Keep-clear (and approach) boxes: its slot is never in one; it takes the other side.
        {
            Fixture f;
            f.scene.keepClear = {{0.2f, -4, 4, -.5f}}; // behind right
            f.agent.position = {-1, 0, -2};
            f.run(5);
            for (auto &k : f.scene.keepClear)
                check(!k.contains(f.agent.position), "not standing in keep-clear");
            check(f.agent.position.x < 0, "takes the free side");
        }
        // A blocked way: walks round a wall; with no way at all, teleports after 1.5 s.
        {
            Fixture f;
            f.run(2);
            f.scene.walls = {{-6, 1.5f, 6, 2.5f}};
            f.player.feet = {0, 0, 6};
            f.run(12);
            check(f.companion.teleports == 0 && f.distance() < 3, std::format("walked round the wall ({:.2f} m, {} teleports)", f.distance(), f.companion.teleports));
            Fixture g;
            g.run(2);
            g.scene.walls = {{-41, 1.5f, 41, 2.5f}};
            g.player.feet = {0, 0, 8};
            g.player.heading = 0; // looking away from the wall
            g.run(1);
            check(g.companion.teleports == 0, "waits before giving up");
            g.run(2);
            check(g.companion.teleports == 1 && g.agent.position.z > 2.5f, "teleported past the closed wall");
        }
        // Flying or climbing: waits where it is; landing far or unreachable teleports.
        {
            Fixture f;
            f.run(2);
            Vec3 at = f.agent.position;
            f.airborne = true;
            f.run(3, [&](float dt) { f.player.feet.z += 2 * dt; });
            check(std::hypot(f.agent.position.x - at.x, f.agent.position.z - at.z) < .05f, "waits while the player flies");
            f.scene.walls = {{-41, 1.5f, 41, 2.5f}}; // lands beyond a wall
            f.airborne = false;
            f.run(1.5f);
            check(f.companion.teleports == 0, "gives the landing 2 s");
            f.run(1);
            check(f.companion.teleports == 1, "teleports when it can't follow the landing");
        }
        // Stay: stands at its spot while the player walks away.
        {
            Fixture f;
            f.run(2);
            f.companion.mode = sim::Companion::Mode::Stay;
            f.companion.stay = f.agent.position;
            Vec3 at = f.agent.position;
            f.run(8, [&](float dt) { f.player.feet.z += 1.2f * dt; });
            check(std::hypot(f.agent.position.x - at.x, f.agent.position.z - at.z) < .05f, "stays");
            f.companion.mode = sim::Companion::Mode::Follow;
            f.run(12);
            check(f.distance() < 3, "follows again");
        }
        // Still characters (still, power, reduced motion): posed, teleport only, never in view.
        {
            Fixture f;
            f.animate = false;
            f.run(2);
            f.run(5, [&](float dt) { f.player.feet.z += 1.2f * dt; }); // 6 m ahead, the companion behind: out of view
            check(f.companion.teleports >= 1 && f.distance() < 4.5f, std::format("teleported along ({} times, {:.2f} m)", f.companion.teleports, f.distance()));
            int moving = 0;
            f.run(2, [&](float) { moving += f.companion.moved; });
            check(moving == 0, "no frames while still");
            check(!f.everSeenPop, "never in view");
        }
        // Looked at: turns its body to face the player; idles with variants now and then.
        {
            Fixture f;
            f.run(3);
            f.look = true;
            f.run(2);
            Vec3 d = f.player.feet - f.agent.position;
            float face = std::abs(std::remainder(std::atan2(d.x, d.z) - f.agent.heading, 6.2831853f));
            check(face < .1f, std::format("faces the player ({:.2f} rad off)", face));
            f.look = false;
            bool variant = false;
            f.run(25, [&](float) { variant |= f.agent.pose.clip == "idle_alt1"; });
            check(variant, "plays an idle variant within 25 s");
        }
        // After a world switch it waves once when the player looks at it.
        {
            Fixture f;
            f.companion.rejoin(true);
            f.run(1);
            check(f.companion.teleports == 1 && !f.player.sees(f.agent.position, f.scene), "rejoins out of view");
            f.player.heading += 3.14159f;
            f.look = true;
            bool waved = false;
            f.run(1, [&](float) { waved |= f.agent.pose.clip == "wave"; });
            check(waved, "waves when seen");
        }
        // Talking: faces the player and loops talk; ends back in idle.
        {
            Fixture f;
            f.run(3);
            f.agent.beginInteract(f.player.eye());
            f.run(1);
            check(f.agent.pose.clip == "talk" && f.companion.activity(f.agent) == "talking", "talks");
            f.agent.endInteract();
            f.run(.5f);
            check(f.companion.activity(f.agent) == "idle", "back to idle");
        }
        // Focus relocation: a spot out of a given view (the runtime passes the focus view).
        {
            Fixture f;
            f.run(3);
            auto v = f.makeView();
            v.inView = [&](Vec3 p) { return dot(p - f.player.feet, Vec3{0, 0, -1}) > 0; }; // everything behind is "in the focus view"
            auto spot = f.companion.outOfView(v);
            check(spot && spot->z > f.player.feet.z, "a spot out of the focused view");
        }
        std::cout << "PASS companion\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL companion: " << e.what() << '\n';
        return 1;
    }
}
