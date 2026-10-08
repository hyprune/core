#include "interaction/Config.hpp"
#include "interaction/Hands.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>
using namespace hyprune;
int main() {
    float near = interaction::limitSpawnDistance(5, 1);
    assert(std::abs(near - .85f) < 1e-6f);
    assert(interaction::limitSpawnDistance(near, 4) == near);
    assert(interaction::limitSpawnDistance(interaction::limitSpawnDistance(5, 4), 1) == near);

    auto c = interaction::defaultConfig();
    assert(interaction::configPatch(c, {{"player", {{"walk_speed", 4.0}}}})["player"]["walk_speed"] == 4);
    for (auto patch :
         {world::Json{{"player", {{"walk_speed", -1}}}}, world::Json{{"player", {{"wat", 2}}}},
          world::Json{{"input", {{"typing_button", 2.5}}}}, world::Json{{"input", {{"typing_button", 272}}}},
          world::Json{{"tools", {{"slots", world::Json::array({"mount"})}}}}}) {
        bool rejected = false;
        try {
            interaction::configPatch(c, patch);
        } catch (...) {
            rejected = true;
        }
        assert(rejected);
    }
    surfaces::Surface s;
    s.center = {0, 1.7, -3};
    s.width = 2;
    s.height = 1;
    s.pixelWidth = 1000;
    s.pixelHeight = 500;
    sim::Camera camera;
    camera.position = {0, 1.7, 0};
    interaction::Hands h;
    h.begin(interaction::Gesture::Carry, s, {3, .75, .25});
    assert(h.update(camera, {0, 0, -1}, .1, {}));
    assert(std::abs(h.pose.center.x + .5) < .001 && std::abs(h.pose.center.y - 1.45) < .001);
    h.wheel(1);
    assert(h.targetDistance > 3);
    h.update(camera, {0, 0, -1}, 1, {});
    assert(h.pose.center.z < -3);
    std::vector<Triangle> wall = {{{-10, -10, -4}, {10, -10, -4}, {10, 10, -4}},
                                  {{-10, -10, -4}, {10, 10, -4}, {-10, 10, -4}}};
    for (int i = 0; i < 8; i++)
        h.wheel(2);
    auto before = h.pose.center;
    h.update(camera, {0, 0, -1}, 1, wall);
    assert(h.pose.center.z == before.z);
    h.begin(interaction::Gesture::Resize, s, {3, .75, .25});
    h.update(camera, normalized(Vec3{1, .8, -3}), .1, {});
    assert(h.pose.width > 1.9 && h.pose.height > 1.5);
    h.begin(interaction::Gesture::Roll, s, {3, .75, .5});
    h.update(camera, normalized(Vec3{0, .5, -3}), .1, {});
    assert(std::abs(h.pose.rotation[2]) > .6);
    auto q = interaction::orientation({1, 0, 0});
    assert(length(rotate(q, {0, 0, 1}) - Vec3{1, 0, 0}) < .001);
    // Discrete riser with a floor, top and vertical face; a tall wall remains blocked.
    auto stairs = wall;
    stairs.clear();
    auto quad = [&](Vec3 a, Vec3 b, Vec3 cc, Vec3 d) {
        stairs.push_back({a, b, cc});
        stairs.push_back({a, cc, d});
    };
    quad({-3, 0, 3}, {3, 0, 3}, {3, 0, -3}, {-3, 0, -3});
    quad({-3, .25, 0}, {3, .25, 0}, {3, .25, -3}, {-3, .25, -3});
    quad({-3, 0, 0}, {3, 0, 0}, {3, .25, 0}, {-3, .25, 0});
    camera = {};
    camera.position = {0, 1.7, 1};
    sim::Walk walk;
    for (int i = 0; i < 50; i++)
        walk.step(camera, {.forward = 1}, 1.f / 60, stairs);
    assert(camera.position.z < -.4 && camera.position.y > 1.94);
    sim::Fly fly;
    camera.position = {0, 1.7, -3};
    for (int i = 0; i < 30; ++i)
        fly.step(camera, {.forward = 1}, 1.f / 60, wall);
    assert(camera.position.z > -3.71);
    fly.step(camera, {.forward = 1}, .05f, {});
    assert(camera.position.z < -3.71);
    auto z = camera.position.z;
    fly.step(camera, {.forward = .01f}, .05f, {});
    assert(z - camera.position.z < .003f); // inertia tail must not move at full speed
    std::cout << "M3A config, hit offset, wheel, OBB sweep, independent client aspect, roll, arbitrary normal and "
                 "stair tests passed\n";
}
