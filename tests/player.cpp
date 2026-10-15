// Player settings (IPC 0.19 player.get/set) validation and the walking view bob.
#include "interaction/Config.hpp"
#include "interaction/Hands.hpp"
#include "render/OverlayStyle.hpp"
#include "sim/ViewBob.hpp"
#include <iostream>
using namespace hyprune;
using Json = world::Json;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
bool rejects(const Json &patch) {
    try {
        interaction::configPatch(interaction::defaultConfig(), patch);
        return false;
    } catch (const std::exception &) {
        return true;
    }
}
void settings() {
    auto c = interaction::defaultConfig();
    auto &p = c["player"];
    check(p["walk_speed"] == 4.0 && p["run_speed"] == 10.0 && !p.contains("run_multiplier"), "walk/run defaults");
    check(p["view_bob"] == "subtle" && p["reticle_style"] == "cross_dot" && p["reticle_colour"] == "theme", "enum defaults");
    check(p["reach_grab"] == 15.0 && p["reach_mount"] == 8.0 && p["reach_placement"] == 12.0, "reach defaults");
    check(c["graphics"]["fov"] == 65.0, "fov default");
    // Every wire key is in the player section and every numeric key has a range.
    for (auto &key : interaction::playerSettingsKeys())
        check(p.contains(key.get<std::string>()), "wire key missing from defaults: " + key.get<std::string>());
    for (auto &[key, range] : interaction::playerRanges().items()) {
        check(p[key] >= range[0] && p[key] <= range[1], "default outside its range: " + key);
        check(rejects({{"player", {{key, range[1].get<double>() + 1}}}}), "above range accepted: " + key);
        check(rejects({{"player", {{key, range[0].get<double>() - .01}}}}), "below range accepted: " + key);
    }
    check(interaction::playerSettingsKeys().size() == 19, "nineteen player settings");
    // IPC 0.29 feel settings: look/move smoothing, the bob's head tilt and carry easing.
    for (auto &key : interaction::feelPlayerKeys()) {
        auto keys = interaction::playerSettingsKeys();
        check(std::find(keys.begin(), keys.end(), key) != keys.end(), "feel key not a player setting: " + key.get<std::string>());
    }
    check(p["look_inertia"] == .03 && p["move_inertia"] == .05 && p["view_bob_roll"] == .4 && p["carry_turn"] == .11 &&
              p["carry_follow"] == .04, "feel defaults");
    // Separate walk and run speeds; the old multiplier is a Lua alias for run_speed.
    auto next = interaction::configPatch(c, {{"player", {{"walk_speed", 3.0}, {"run_speed", 12.5}}}});
    check(next["player"]["walk_speed"] == 3.0 && next["player"]["run_speed"] == 12.5, "walk/run patch");
    next = interaction::configPatch(c, {{"player", {{"walk_speed", 2.0}, {"run_multiplier", 2.0}}}});
    check(next["player"]["run_speed"] == 4.0 && !next["player"].contains("run_multiplier"), "run_multiplier alias uses the patched walk speed");
    check(rejects({{"player", {{"run_multiplier", 5}}}}), "alias range");
    // Enums, colour, booleans.
    for (auto bob : {"off", "subtle", "normal"})
        check(interaction::configPatch(c, {{"player", {{"view_bob", bob}}}})["player"]["view_bob"] == bob, "view_bob option");
    for (auto style : interaction::playerOptions()["reticle_style"])
        check(interaction::configPatch(c, {{"player", {{"reticle_style", style}}}})["player"]["reticle_style"] == style, "style option");
    check(rejects({{"player", {{"view_bob", "strong"}}}}) && rejects({{"player", {{"view_bob", true}}}}), "unknown bob level");
    check(rejects({{"player", {{"reticle_style", "square"}}}}), "unknown reticle style");
    check(interaction::configPatch(c, {{"player", {{"reticle_colour", {1, .5, 0}}}}})["player"]["reticle_colour"] == Json({1, .5, 0}), "rgb colour");
    for (auto bad : {Json("red"), Json({1, 2, 0}), Json({1, 0}), Json(nullptr)})
        check(rejects({{"player", {{"reticle_colour", bad}}}}), "bad reticle colour accepted");
    check(rejects({{"player", {{"reticle_outline", 1}}}}) && rejects({{"player", {{"reticle_dynamic", "yes"}}}}), "booleans");
    // Field of view: 30..120.
    check(interaction::configPatch(c, {{"graphics", {{"fov", 120}}}})["graphics"]["fov"] == 120, "fov 120");
    check(rejects({{"graphics", {{"fov", 121}}}}) && rejects({{"graphics", {{"fov", 29}}}}), "fov range");
    // Overlay style: reticle colours are part of the style and validated like the others.
    auto style = render::overlayStylePatch(render::overlayDefaults(), {{"validColor", {0, 1, 0}}});
    check(style["validColor"] == Json({0, 1, 0}) && style.contains("reticleColor") && style.contains("invalidColor"), "reticle colours");
    bool rejected = false;
    try {
        render::overlayStylePatch(render::overlayDefaults(), {{"invalidColor", {2, 0, 0}}});
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "overlay colour range");
}
struct Run {
    float maxDown = 0, maxSide = 0, maxUp = 0, firstStep = 0;
    int dips = 0;
};
Run walk(sim::ViewBob &bob, float seconds, float speed, sim::ViewBob::Level level, bool active = true, float dt = 1 / 60.f) {
    Run r;
    float previous = 0;
    bool falling = false;
    for (int i = 0; i * dt < seconds; ++i) {
        bob.update(dt, speed, 4, 10, level, active, 0);
        float y = bob.offset.y;
        if (i == 0)
            r.firstStep = std::abs(y);
        r.maxDown = std::max(r.maxDown, -y);
        r.maxUp = std::max(r.maxUp, y);
        r.maxSide = std::max(r.maxSide, std::abs(bob.offset.x) + std::abs(bob.offset.z));
        if (y < previous)
            falling = true;
        else if (falling && y > previous) {
            ++r.dips;
            falling = false;
        }
        previous = y;
    }
    return r;
}
void viewBob() {
    {
        sim::ViewBob bob;
        auto r = walk(bob, 3, 4, sim::ViewBob::Off);
        check(r.maxDown == 0 && r.maxSide == 0, "off never moves the eye");
    }
    sim::ViewBob bob;
    auto subtle = walk(bob, 4, 4, sim::ViewBob::Subtle);
    check(subtle.maxUp == 0, "the eye never rises above standing height");
    check(subtle.maxDown > .008f && subtle.maxDown <= sim::ViewBob::SubtleDip + 1e-5f, "subtle dip about 1 cm");
    check(subtle.maxSide == 0, "subtle is vertical only");
    check(subtle.firstStep < .001f, "starting walking eases in, no pop");
    // One dip per footstep near 1.9 Hz at walk speed: ~7.6 in 4 s (first one easing in).
    check(subtle.dips >= 6 && subtle.dips <= 8, "walking step rate " + std::to_string(subtle.dips));
    // Stopping eases out and then settles exactly to zero.
    float last = 0;
    for (int i = 0; i < 60; ++i) {
        float before = bob.offset.y;
        bob.update(1 / 60.f, 0, 4, 10, sim::ViewBob::Subtle, true, 0);
        last = std::max(last, std::abs(bob.offset.y - before));
    }
    check(last < .003f, "stopping has no snap");
    for (int i = 0; i < 120; ++i)
        bob.update(1 / 60.f, 0, 4, 10, sim::ViewBob::Subtle, true, 0);
    check(bob.offset.y == 0 && bob.envelope == 0 && bob.phase == 0, "settles to exactly zero");
    // Running: strongly reduced amplitude, only a modestly faster step rate.
    sim::ViewBob run;
    auto running = walk(run, 4, 10, sim::ViewBob::Subtle);
    check(running.maxDown < subtle.maxDown * .4f, "running bobs much less than walking");
    check(running.dips <= 11, "running step rate stays near 2.5 Hz");
    // Normal: bigger dip plus a small sideways sway at half the step rate.
    sim::ViewBob normal;
    auto n = walk(normal, 4, 4, sim::ViewBob::Normal);
    check(n.maxDown > subtle.maxDown * 1.8f && n.maxSide > .004f && n.maxSide < .008f, "normal: dip and sway");
    // Not walking (flying, climbing, airborne, reduced motion): eases out to nothing.
    auto air = walk(normal, 2, 4, sim::ViewBob::Normal, false);
    check(normal.offset.y == 0 && air.maxUp == 0, "inactive settles to zero");
    // Frame-rate independent: 30 and 144 fps reach the same envelope.
    sim::ViewBob a, b;
    walk(a, 2, 4, sim::ViewBob::Subtle, true, 1 / 30.f);
    walk(b, 2, 4, sim::ViewBob::Subtle, true, 1 / 144.f);
    check(std::abs(a.envelope - b.envelope) < .0005f, "bob envelope depends on frame rate");
}
// A carried window eases its turn toward facing you and its grab point onto the ray; zero
// times keep it rigid.
void carry() {
    for (float time : {0.f, .11f}) {
        interaction::Hands hands;
        surfaces::Surface s;
        s.center = {0, 1.5f, -2};
        s.width = 1.6f, s.height = .9f, s.pixelWidth = 1600, s.pixelHeight = 900;
        sim::Camera cam;
        cam.position = {0, 1.5f, 0};
        hands.begin(interaction::Gesture::Carry, s, {.distance = 2, .u = .5f, .v = .5f}, cam);
        hands.turnTime = time, hands.followTime = time / 2;
        cam.yaw = .5f; // turn the head right
        hands.update(cam, cam.forward(), 1 / 98.f, {});
        float first = hands.turn;
        for (int i = 0; i < 98; ++i)
            hands.update(cam, cam.forward(), 1 / 98.f, {});
        if (time == 0)
            check(std::abs(first + .5f) < 1e-5f, "rigid carry turns at once");
        else {
            check(first > -.1f && first < 0, "eased carry lags the turn");
            check(std::abs(hands.turn + .5f) < 2e-3f, "eased carry settles facing you");
        }
        auto aim = cam.position + cam.forward() * 2;
        check(length(hands.grab - aim) < 2e-3f, "grab point settles on the ray");
    }
}
void bobRoll() {
    sim::ViewBob bob;
    float most = 0;
    for (int i = 0; i < 400; ++i) {
        bob.update(1 / 98.f, 4, 4, 10, sim::ViewBob::Subtle, true, 0, .4f);
        most = std::max(most, std::abs(bob.roll));
    }
    check(most > .006f && most < .0071f, "head tilt reaches 0.4 degrees");
    for (int i = 0; i < 400; ++i)
        bob.update(1 / 98.f, 0, 4, 10, sim::ViewBob::Subtle, true, 0, .4f);
    check(bob.roll == 0, "head tilt fades out with the bob");
}
} // namespace
int main() {
    try {
        settings();
        viewBob();
        carry();
        bobRoll();
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
    std::cout << "PASS player settings and view bob\n";
}
