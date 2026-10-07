#include "surfaces/Layout.hpp"
#include "world/Mounts.hpp"
#include <iostream>
using namespace hyprune;
void check(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("invalid operation accepted");
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    try {
        auto w = world::load(argv[1]);
        check(w->areas.size() == 2 && w->anchors.size() == 5, "home metadata");
        surfaces::Layout layout;
        layout.bind(10, w->id, "sun-court", w.get());
        layout.bind(11, w->id, "array-hall", w.get());
        rejects([&] { layout.bind(12, w->id, "sun-court", w.get()); });
        surfaces::Instance terminal{"terminal", "kitty", "M2 terminal", 10};
        layout.pull(terminal, w.get());
        sim::Camera camera;
        auto plane = layout.locate(terminal, w.get(), camera, 1600, 900);
        check(plane.has_value() && !surfaces::overlaps(*plane, w->collision), "carry collision");
        layout.instances = world::Json::array();
        auto id = layout.place(terminal, "sun-court-slot-01-operator-desk-01", w.get());
        plane = layout.locate(terminal, w.get(), camera, 1600, 900);
        check(plane && !surfaces::overlaps(*plane, w->collision), "desk collision");
        rejects([&] { layout.place(terminal, "array-hall-slot-02-banner-01", w.get()); });
        auto saved = world::Json{{"bindings", layout.bindings}, {"placements", layout.placements}};
        surfaces::Layout restored;
        restored.restore(saved);
        terminal.id = "relaunched";
        plane = restored.locate(terminal, w.get(), camera, 1600, 900);
        check(plane && std::abs(plane->center.x + 2.7f) < .001f, "placement relaunch");
        // Changed package: removed mount; no stale pose/identity is used.
        std::erase_if(w->anchors.get_ref<world::Json::array_t &>(), [&](auto &a) { return a.at("id") == id; });
        restored.instances = world::Json::array();
        plane = restored.locate(terminal, w.get(), camera, 1600, 900);
        check(plane && !surfaces::overlaps(*plane, w->collision) && !restored.events.empty(),
              "invalid restore recovery");
        // A thin wall cuts the middle of a large tool, with no rectangle corner inside it.
        std::vector<Triangle> wall = {{{-.2f, 0, 0}, {.2f, 3, 0}, {.2f, 0, 0}},
                                      {{-.2f, 0, 0}, {-.2f, 3, 0}, {.2f, 3, 0}}};
        surfaces::Surface box;
        box.center = {0, 1.5, 0};
        box.width = 3.2;
        box.height = 1.8;
        check(surfaces::overlaps(box, wall), "triangle OBB overlap");
        check(surfaces::pushOut(box, wall) && box.center.z >= .049, "normal pushout margin");
        camera.position = {0, 1.7, .2};
        camera.yaw = 0;
        box = surfaces::carryPlane(camera, 3.2, 1.8, wall);
        check(!surfaces::overlaps(box, wall) && box.center.z > .04, "carried tool crossed wall");
        surfaces::Surface tilted;
        tilted.rotation = {std::sin(-.35f), 0, 0, std::cos(-.35f)};
        tilted.center = {0, 1.5, -2};
        auto normal = tilted.normal();
        auto hit = surfaces::intersect(tilted, tilted.center + normal * 3, normal * -1);
        check(hit && std::abs(hit->u - .5) < .001 && std::abs(hit->v - .5) < .001, "tilted picking");
        // Continuous 1:4 slope, ascending and descending: no stair stepping claimed.
        std::vector<Triangle> ramp = {{{-3, 0, 5}, {3, 3, -7}, {3, 0, 5}}, {{-3, 0, 5}, {-3, 3, -7}, {3, 3, -7}}};
        camera = {};
        camera.position = {0, 2.0f, 4};
        sim::Walk walk;
        for (int i = 0; i < 150; i++)
            walk.step(camera, {1, 0, 0, false}, 1.f / 60, ramp);
        check(camera.position.y > 3.2 && camera.grounded, "ascending slope");
        for (int i = 0; i < 60; i++)
            walk.step(camera, {-1, 0, 0, false}, 1.f / 60, ramp);
        check(camera.position.y < 3.3 && camera.grounded, "descending slope");
        std::cout << "PASS M2: area ownership, persistence/relaunch, invalid placement recovery, full OBB, carry wall "
                     "clamp, tilted picking, slopes\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
