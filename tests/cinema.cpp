// Demo camera driver (sim/Cinema): monotone timing, centripetal curves addressed by arc
// length, tracks (uniform steps, continuous velocity across keys, eases, shortest-arc
// yaw, look-at, fov), the critically damped springs, follow shots of a walking and
// turning subject, and director blends.
#include "sim/Cinema.hpp"
#include <cmath>
#include <format>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <vector>
using namespace hyprune;
using namespace hyprune::sim::cinema;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
bool near(float a, float b, float eps = 1e-3f) { return std::abs(a - b) <= eps; }
bool near(Vec3 a, Vec3 b, float eps = 1e-3f) { return hyprune::length(a - b) <= eps; }
constexpr double Frame = 1. / 60;
// Per-frame eye steps of a pose sequence.
std::vector<float> steps(const std::vector<Pose> &poses) {
    std::vector<float> out;
    for (size_t i = 1; i < poses.size(); ++i)
        out.push_back(hyprune::length(poses[i].eye - poses[i - 1].eye));
    return out;
}
float cv(const std::vector<float> &x) {
    double mean = 0, var = 0;
    for (auto v : x)
        mean += v;
    mean /= x.size();
    for (auto v : x)
        var += (v - mean) * (v - mean);
    return float(std::sqrt(var / x.size()) / mean);
}
// Largest frame-to-frame change of the step, relative to the step (a velocity jump).
float jerk(const std::vector<float> &s, float floor = 1e-4f) {
    float worst = 0;
    for (size_t i = 1; i < s.size(); ++i)
        if (std::max(s[i], s[i - 1]) > floor)
            worst = std::max(worst, std::abs(s[i] - s[i - 1]) / std::max(s[i], s[i - 1]));
    return worst;
}
// Largest frame-to-frame velocity change (metres per frame per frame).
float accel(const std::vector<Pose> &p) {
    float worst = 0;
    for (size_t i = 1; i + 1 < p.size(); ++i)
        worst = std::max(worst, hyprune::length(p[i + 1].eye - p[i].eye * 2 + p[i - 1].eye));
    return worst;
}
std::vector<Pose> sample(const Track &t, double from, double to) {
    std::vector<Pose> out;
    for (double x = from; x <= to + 1e-9; x += Frame)
        out.push_back(t.at(x));
    return out;
}

void monotone() {
    Monotone m({0, 1, 2, 4}, {0, 1, 1.5, 4});
    check(near(float(m.at(0)), 0) && near(float(m.at(1)), 1) && near(float(m.at(2)), 1.5f) && near(float(m.at(4)), 4),
          "monotone cubic passes through its knots");
    check(m.at(-1) == 0 && m.at(9) == 4, "monotone cubic is constant outside the knots");
    double prev = -1;
    for (double x = 0; x <= 4; x += .01) {
        check(m.at(x) >= prev - 1e-9, "monotone data gives a monotone curve");
        prev = m.at(x);
    }
    Monotone rest({0, 1, 2}, {0, 1, 2}, {false, true, false});
    check(std::abs(rest.at(1.001) - rest.at(.999)) < 1e-5, "a rest knot has zero slope");
    Monotone line({0, 2}, {0, 4});
    check(near(float(line.at(.5)), 1), "two knots without rest are linear");
    Monotone flat({0, 1, 2}, {0, 1, 1});
    for (double x = 1; x <= 2; x += .05)
        check(near(float(flat.at(x)), 1), "a flat segment stays flat (no overshoot)");
}

void curve() {
    Curve straight({{0, 0, 0}, {1, 0, 0}, {3, 0, 0}, {6, 0, 0}});
    check(near(straight.length(), 6, 1e-3f), "a straight curve is as long as the line");
    for (float s = 0; s <= 6; s += .25f)
        check(near(straight.at(s), {s, 0, 0}, 2e-3f), std::format("a straight curve is addressed by arc length at {}", s));
    Curve bend({{0, 0, 0}, {4, 0, 0}, {4, 0, 4}, {8, 1, 4}, {8.2f, 1, 4.1f}});
    for (size_t i = 0; i < 5; ++i)
        check(near(bend.at(bend.knot(i)), std::vector<Vec3>{{0, 0, 0}, {4, 0, 0}, {4, 0, 4}, {8, 1, 4}, {8.2f, 1, 4.1f}}[i], 2e-3f),
              "the curve passes through its points");
    std::vector<float> d;
    Vec3 prev = bend.at(0);
    for (float s = .05f; s <= bend.length(); s += .05f) {
        auto q = bend.at(s);
        d.push_back(hyprune::length(q - prev));
        prev = q;
    }
    check(cv(d) < .01f, std::format("equal arc lengths give equal chord steps (cv {})", cv(d)));
    // Centripetal: no loop or cusp at a sharp, uneven corner (the step never reverses).
    Curve corner({{0, 0, 0}, {10, 0, 0}, {10.2f, 0, .2f}, {10.2f, 0, 10}});
    prev = corner.at(0);
    Vec3 dir = {1, 0, 0};
    for (float s = .02f; s <= corner.length(); s += .02f) {
        auto q = corner.at(s);
        auto step = hyprune::normalized(q - prev);
        check(dot(step, dir) > .5f, "a centripetal curve turns without looping");
        dir = step;
        prev = q;
    }
    Curve single({{1, 2, 3}});
    check(near(single.at(5), {1, 2, 3}) && single.length() == 0, "a one-point curve holds");
}

void track() {
    // Keys spaced in time like their distances: one speed all along, even round a bend.
    Track line({{0, {0, 2, 0}, Vec3{0, 2, -10}}, {1, {1, 2, 0}, Vec3{1, 2, -10}}, {3, {3, 2, 0}, Vec3{3, 2, -10}}}, 60);
    auto s = steps(sample(line, 0, 3));
    check(cv(s) < .01f && near(s[10], 1.f / 60, 1e-4f), std::format("a line keyed at its distances moves evenly (cv {})", cv(s)));
    std::vector<Key> keys;
    double t = 0;
    Vec3 last{0, 2, 0};
    for (int i = 0; i < 9; ++i) {
        Vec3 p{8 * std::sin(i * .5f), 2 + .3f * i, 8 * std::cos(i * .5f)};
        t += i ? hyprune::length(p - last) / 2 : 0;
        keys.push_back({t, p, Vec3{0, 1, 0}});
        last = p;
    }
    Track arc(keys, 60);
    auto a = steps(sample(arc, 0, arc.duration()));
    check(cv(a) < .05f, std::format("a bend keyed at a steady speed moves evenly (cv {})", cv(a)));
    // Uneven key times: the speed changes, but never jumps at a key (continuous velocity).
    Track uneven({{0, {0, 0, 0}, Vec3{0, 0, -5}}, {1, {4, 0, 0}, Vec3{4, 0, -5}}, {1.5, {5, 0, 1}, Vec3{5, 0, -5}},
                  {4, {5, 0, 8}, Vec3{5, 0, 0}}},
                 60);
    auto u = steps(sample(uneven, 0, 4));
    check(jerk(u) < .06f, std::format("velocity is continuous across keys (worst step change {})", jerk(u)));
    // Ease in and out: starts and stops at rest, smoothly.
    Track eased({{0, {0, 0, 0}, std::nullopt, 0, 0, std::nullopt, Ease::InOut}, {2, {10, 0, 0}, std::nullopt, 0, 0}}, 60);
    auto e = steps(sample(eased, 0, 2));
    check(e.front() < .01f && e.back() < .01f && e[60] > .12f, "an eased segment starts and ends at rest");
    float change = 0; // 15 m/s^2 at most: under 0.0042 m per frame per frame
    for (size_t i = 1; i < e.size(); ++i)
        change = std::max(change, std::abs(e[i] - e[i - 1]));
    check(change < .005f, std::format("an eased segment accelerates smoothly ({})", change));
    // Yaw takes the short way across +-pi; fov interpolates.
    Track turn({{0, {0, 0, 0}, std::nullopt, 3.0f, .1f, 50.f}, {1, {0, 0, 0}, std::nullopt, -3.0f, .3f, 70.f}}, 60);
    auto mid = turn.at(.5);
    check(near(std::abs(mid.yaw), std::numbers::pi_v<float>, .01f), std::format("yaw crosses pi the short way ({})", mid.yaw));
    check(near(mid.pitch, .2f) && near(mid.fov, 60, .01f), "pitch and fov interpolate");
    float worst = 0;
    auto turning = sample(turn, 0, 1);
    for (size_t i = 1; i < turning.size(); ++i)
        worst = std::max(worst, std::abs(wrap(turning[i].yaw - turning[i - 1].yaw)));
    check(worst < .01f, "yaw steps stay small across the wrap");
    // Look-at keys: the pose at a key looks at its target; missing fov is inherited.
    Track look({{0, {0, 1, 0}, Vec3{0, 1, -5}, 0, 0, 40.f}, {2, {5, 1, 0}, Vec3{5, 0, -5}}}, 60);
    auto end = look.at(2);
    auto want = lookAt({5, 1, 0}, {5, 0, -5}, 40);
    check(near(end.yaw, want.yaw) && near(end.pitch, want.pitch) && near(end.fov, 40), "a look-at key is matched");
    Track still({{0, {1, 2, 3}, std::nullopt, .5f, 0}}, 60);
    check(still.duration() == 0 && near(still.at(3).eye, {1, 2, 3}) && near(still.at(3).yaw, .5f), "one key holds");
    bool threw = false;
    try {
        Track bad({{1, {}}, {1, {}}}, 60);
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    check(threw, "key times must increase");
}

void springs() {
    // Exact: the same time in one step or sixty gives the same state.
    Spring a{0, 0}, b{0, 0};
    a.step(1, .3f, 1);
    for (int i = 0; i < 60; ++i)
        b.step(1, .3f, 1.f / 60);
    check(near(a.x, b.x, 1e-4f) && near(a.v, b.v, 1e-4f), "the spring is exact (frame rate independent)");
    // Critically damped: never overshoots a still goal, and settles.
    Spring c{0, 0};
    for (int i = 0; i < 600; ++i) {
        c.step(1, .3f, 1.f / 60);
        check(c.x <= 1 + 1e-5f, "a critically damped spring does not overshoot");
    }
    check(near(c.x, 1, 1e-4f), "the spring settles on its goal");
    Spring d{0, 0};
    d.step(5, 0, 1.f / 60);
    check(d.x == 5, "a zero half-life snaps");
}

void follow() {
    // A subject walking straight at 1.4 m/s along +x (heading pi/2): after settling, the
    // camera moves the same amount every frame, at the subject's speed.
    Follow f({});
    std::vector<Pose> poses;
    Subject s{{0, 0, 0}, std::numbers::pi_v<float> / 2};
    for (int i = 0; i < 360; ++i) {
        s.position.x += 1.4f / 60;
        poses.push_back(f.step(s, 1.f / 60, 60));
    }
    auto w = steps(std::vector<Pose>(poses.begin() + 120, poses.end()));
    check(cv(w) < .01f && near(w[0], 1.4f / 60, 1e-3f), std::format("following a steady walk moves evenly (cv {})", cv(w)));
    auto [eye, target] = f.goal(s.position, s.heading);
    check(eye.x < s.position.x - 3 && eye.z < s.position.z && near(eye.y, 1.9f), "the eye is behind and to the left, above");
    check(target.x > s.position.x, "the camera looks ahead of the subject");
    // A sharp 90 degree turn at a route corner: no step, the camera swings round.
    std::vector<Pose> turning;
    for (int i = 0; i < 240; ++i) {
        if (i == 60)
            s.heading = 0; // now walking +z
        if (i < 60)
            s.position.x += 1.4f / 60;
        else
            s.position.z += 1.4f / 60;
        turning.push_back(f.step(s, 1.f / 60, 60));
    }
    auto t = steps(turning);
    float most = 0;
    for (auto v : t)
        most = std::max(most, v);
    check(most < 3 * 1.4f / 60, std::format("a sharp turn swings the camera without a jump (max step {})", most));
    check(jerk(t) < .15f, std::format("the swing is smooth ({})", jerk(t)));
    float yawStep = 0;
    for (size_t i = 1; i < turning.size(); ++i)
        yawStep = std::max(yawStep, std::abs(wrap(turning[i].yaw - turning[i - 1].yaw)));
    check(yawStep < .05f, std::format("the view turns smoothly ({} rad per frame)", yawStep));
    // A teleport further than snap is a cut.
    s.position = {100, 0, 0};
    auto cut = f.step(s, 1.f / 60, 60);
    check(near(cut.eye, f.goal(s.position, s.heading).first, 1e-3f), "a teleport cuts");
    // World frame: a fixed offset in world axes, whatever the heading.
    FollowParams wp;
    wp.frame = FollowParams::Frame::World;
    wp.offset = {-.8f, 2.1f, 2.8f};
    wp.lookOffset = {.2f, 1, -1.5f};
    Follow world(wp);
    Subject ws{{5, 0, -50}, 2.5f};
    for (int i = 0; i < 300; ++i) {
        ws.position.z -= 1.2f / 60;
        ws.heading = 2.5f + .4f * std::sin(i * .1f); // wandering heading: no effect on the eye
        world.step(ws, 1.f / 60, 60);
    }
    auto settled = world.step(ws, 1.f / 60, 60);
    check(near(settled.eye, ws.position + wp.offset, .6f) && std::abs(settled.eye.x - (ws.position.x - .8f)) < 1e-3f,
          "a world-frame follow keeps its world offset");
    // Fixed eye: the camera stays put and pans to keep the subject in view.
    FollowParams fp;
    fp.frame = FollowParams::Frame::Fixed;
    fp.offset = {0, 2, 10};
    fp.lookOffset = {0, 1, 0};
    Follow tripod(fp);
    Subject ts{{-5, 0, 0}, 0};
    std::vector<Pose> pan;
    for (int i = 0; i < 300; ++i) {
        ts.position.x += 1.4f / 60;
        pan.push_back(tripod.step(ts, 1.f / 60, 60));
    }
    check(near(pan.back().eye, {0, 2, 10}) && near(pan.front().eye, {0, 2, 10}), "a fixed eye does not move");
    auto want = lookAt({0, 2, 10}, ts.position + Vec3{0, 1, 0}, 60);
    // The damped target trails a 1.4 m/s walker by about 0.4 m at 10 m.
    check(std::abs(wrap(pan.back().yaw - want.yaw)) < .06f, "a fixed eye pans after its subject");
    // Clip: an eye goal behind a wall is pulled in.
    Follow g({});
    auto clipped = g.step({{0, 0, 0}, 0}, 1.f / 60, 60, [](Vec3 target, Vec3 eye) { return target + (eye - target) * .5f; });
    auto full = g.goal({0, 0, 0}, 0);
    check(near(clipped.eye, full.second + (full.first - full.second) * .5f), "the clip function pulls the eye in");
}

void director() {
    Director d;
    Pose start{{0, 2, 2}, .3f, 0, 60};
    check(!d.active() && !d.tick(Frame, {}, 60), "an idle director drives nothing");
    // Blend from a held camera into a moving path: the first frame is the old pose, the
    // camera never jumps, and after the blend it is exactly on the path.
    Track path({{0, {0, 2, 0}, Vec3{0, 1, -10}}, {4, {8, 2, 0}, Vec3{8, 1, -10}}}, 60);
    d.path(path, .5, start);
    std::vector<Pose> poses{start};
    for (int i = 0; i < 300; ++i)
        poses.push_back(*d.tick(Frame, {}, 60));
    check(near(poses[1].eye, start.eye, .2f), "a blend starts at the old pose");
    float most = 0, change = 0;
    auto b = steps(poses);
    for (size_t i = 0; i < 200; ++i) { // the path itself stops dead at 4 s (no ease)
        most = std::max(most, b[i]);
        if (i)
            change = std::max(change, std::abs(b[i] - b[i - 1]));
    }
    check(most < .2f && change < .03f, std::format("the blend has no jump (max step {}, change {})", most, change));
    check(near(poses[120].eye, path.at(119 * Frame + Frame).eye, 1e-3f), "after the blend the camera is on the path");
    check(d.done() && !d.moving(), "a finished path is done and needs no frames");
    check(near(d.tick(Frame, {}, 60)->eye, {8, 2, 0}), "a finished path holds its last pose");
    // Path to follow: the old path keeps moving during the blend.
    Vec3 walker{8, 0, -4};
    auto lookup = [&](const std::string &id) -> std::optional<Subject> {
        if (id == "walker")
            return Subject{walker, 0};
        return std::nullopt;
    };
    Director e;
    e.path(Track({{0, {0, 2, 0}}, {10, {10, 2, 0}}}, 60), 0, start);
    for (int i = 0; i < 60; ++i)
        e.tick(Frame, lookup, 60);
    e.follow("walker", {}, .8, start);
    std::vector<Pose> f{*e.pose()};
    for (int i = 0; i < 120; ++i) {
        walker.z += 1.4f / 60;
        f.push_back(*e.tick(Frame, lookup, 60));
    }
    // 11 m in 0.8 s eased: about 100 m/s^2 at most, 0.029 m per frame per frame.
    check(accel(f) < .035f, std::format("path to follow cross-fades smoothly ({})", accel(f)));
    check(e.moving() && !e.lost(), "a follow keeps frames coming");
    e.follow("nobody", {}, 0, start);
    auto held = *e.tick(Frame, lookup, 60);
    check(e.lost() && near(held.eye, f.back().eye, .1f), "a missing subject holds the camera");
    e.stop();
    check(!e.active(), "stop releases the camera");
}
} // namespace
int main() {
    try {
        monotone();
        curve();
        track();
        springs();
        follow();
        director();
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
    std::cout << "PASS cinema\n";
}
