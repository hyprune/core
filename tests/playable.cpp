#include "world/Playable.hpp"
#include "interaction/Config.hpp"
#include "surfaces/Layout.hpp"
#include "world/Schema.hpp"
#include <iostream>
using namespace hyprune;
using Json = nlohmann::json;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    check(failed, "invalid input accepted");
}
Json box(Vec3 a, Vec3 b) {
    return {{"type", "box"}, {"min", {a.x, a.y, a.z}}, {"max", {b.x, b.y, b.z}}};
}
int main() {
    try {
        check(world::loadPlayable(Json::object()).contains({1e5f, 1e5f, 1e5f}), "legacy unlimited");
        Json j = {{"playable", {{"volumes", Json::array({box({-5, 0, -5}, {5, 10, 5})})}, {"maxFlyHeight", 4}}}};
        auto b = world::loadPlayable(j);
        check(b.contains({0, 4, 0}) && !b.contains({0, 4.1f, 0}), "height cap");
        check(b.contains({5.4f, 4.4f, 0}, .5f) && !b.contains({5.6f, 0, 0}, .5f), "allowance expands boundaries");
        Vec3 p{0, 2, 0};
        float early = 0, late = 0;
        for (int i = 0; i < 1000; i++) {
            auto q = b.constrain(p, p + Vec3{.1f, 0, 0}, 0, 1.f / 60);
            if (i == 1)
                early = q.x - p.x;
            if (i == 150)
                late = q.x - p.x;
            p = q;
            check(b.contains(p), "fly escapes");
        }
        check(p.x > 4.99f && early > late, "soft approach reaches boundary");
        auto slide = b.constrain({5, 2, 0}, {5.1f, 2, 1}, 0, 1.f / 60);
        check(slide.x <= 5 && slide.z > 0, "tangent movement");
        auto recovered = b.constrain({12, 2, 0}, {14, 2, 0}, 0, 1.f / 60);
        check(recovered.x < 12 && recovered.x > 11.9f, "soft recovery no teleport");
        // Adjacent areas are a union, inside the global envelope. Gaps remain blocked.
        j["areas"] = Json::array({{{"playable", {{"volumes", Json::array({box({-5, 0, -5}, {0, 8, 5})})}}}},
                                  {{"playable", {{"volumes", Json::array({box({0, 0, -5}, {5, 8, 5})})}}}}});
        b = world::loadPlayable(j);
        auto crossed = b.constrain({-.1f, 2, 0}, {.1f, 2, 0}, 0, 1.f / 60);
        check(crossed.x > .099f && !b.contains({0, 5, 0}), "connected areas plus global cap");
        j["areas"][1]["playable"]["volumes"][0]["min"][0] = 1;
        b = world::loadPlayable(j);
        check(b.constrain({-.1f, 2, 0}, {2, 2, 0}, 0, 1.f / 60).x <= 0, "cannot jump disconnected gap");
        Json hull = {{"type", "convex"}, {"points", {{0, 0, 0}, {4, 0, 0}, {0, 4, 0}, {0, 0, 4}}}};
        b = world::loadPlayable({{"playable", {{"volumes", Json::array({hull})}}}});
        check(b.contains({1, 1, 1}) && !b.contains({2, 2, 2}), "convex slanted face");
        p = {1, 1, 1};
        for (int i = 0; i < 120; i++)
            p = b.constrain(p, p + Vec3{.1f, .1f, .1f}, 0, 1.f / 60);
        check(b.contains(p) && p.x + p.y + p.z > 3.95f, "convex fly stops");
        hull["points"][3] = {1, 1, 0};
        rejects([&] { world::loadPlayable({{"playable", {{"volumes", Json::array({hull})}}}}); });
        rejects([&] { world::loadPlayable({{"playable", {{"volumes", Json::array({box({1, 0, 0}, {0, 1, 1})})}}}}); });
        world::World world;
        world.areas = Json::array({{{"id", "room"}, {"playable", j["playable"]}}});
        surfaces::Layout layout;
        check(!layout.state(&world)["areas"][0].contains("playable") && world.areas[0].contains("playable"),
              "IPC strips author-only bounds without mutating world");
        auto config = interaction::defaultConfig();
        check(config["player"]["fly_bounds_allowance"] == .5, "default allowance");
        rejects([&] { interaction::configPatch(config, {{"player", {{"fly_bounds_allowance", -1}}}}); });
        rejects([&] { interaction::configPatch(config, {{"player", {{"fly_bounds_allowance", 21}}}}); });
        std::cout
            << "PASS playable: boxes, hulls, height, allowance, union, gaps, soft clamp/recovery, legacy, validation\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
