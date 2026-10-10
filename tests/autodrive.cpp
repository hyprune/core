// Autodrive walks routes with the player's walker: stairs and ramps climb like
// keyboard walking, a ledge above step height stops it, fly keeps its line check.
#include "control/Control.hpp"
#include <cmath>
#include <iostream>
using namespace hyprune;
using Json = ipc::Json;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
// Horizontal quad at height y, wound to face up.
void floorQuad(std::vector<Triangle> &m, float x0, float x1, float z0, float z1, float y) {
    m.push_back({{x0, y, z0}, {x0, y, z1}, {x1, y, z1}});
    m.push_back({{x0, y, z0}, {x1, y, z1}, {x1, y, z0}});
}
// Vertical quad in the plane z, facing +z (towards the lower floor).
void riser(std::vector<Triangle> &m, float x0, float x1, float y0, float y1, float z) {
    m.push_back({{x0, y0, z}, {x1, y0, z}, {x1, y1, z}});
    m.push_back({{x0, y0, z}, {x1, y1, z}, {x0, y1, z}});
}
std::shared_ptr<world::World> stairs() {
    auto w = std::make_shared<world::World>();
    w->manifest = Json::object();
    auto &m = w->collision;
    floorQuad(m, -6, 6, -3, 10, 0);                 // lower floor
    for (int i = 0; i < 10; i++) {                   // 10 steps, 0.18 m rise, 0.3 m run, towards -z
        float top = (i + 1) * .18f, z0 = -(i + 1) * .3f, z1 = -i * .3f;
        floorQuad(m, -1, 1, z0, z1, top);
        riser(m, -1, 1, i * .18f, top, z1);
    }
    floorQuad(m, -1, 1, -6, -3, 1.8f);               // stair landing
    floorQuad(m, -6, 6, -10, -6, 1.8f);              // upper floor
    // Ramp beside the stairs: 1.8 m over 6 m (17 degrees).
    m.push_back({{3, 0, 0}, {3, 1.8f, -6}, {5, 1.8f, -6}});
    m.push_back({{3, 0, 0}, {5, 1.8f, -6}, {5, 0, 0}});
    // A 0.5 m ledge (above the 0.35 m step height) on the far left.
    floorQuad(m, -6, -3, -2.5f, -1, .5f);
    riser(m, -6, -3, 0, .5f, -1);
    w->collisionIndex.rebuild(w->collision);
    return w;
}
struct Run {
    sim::Camera camera;
    bool stoppedEarly;
};
Run drive(world::World &w, Vec3 from, std::vector<Vec3> route, const sim::MotionStrategy *walker, float seconds = 15) {
    control::Control control;
    control.reset(&w);
    sim::Camera camera;
    camera.position = from;
    auto call = [&](std::string m, Json p) {
        return control.execute(m, p, "a", "org.test.client", &w, camera, Json::array({"output"}));
    };
    Json points = Json::array();
    for (auto p : route)
        points.push_back({{"position", {p.x, p.y, p.z}}});
    call("route.set", {{"points", points}});
    call("autodrive.start", {{"speed", 3.5}});
    sim::Intent walking;
    walking.stepHeight = .35f;
    float t = 0;
    for (; t < seconds && control.driving(); t += 1.f / 60)
        control.tick(1.f / 60, camera, &w, walker, walking);
    check(!control.driving(), "autodrive did not finish");
    auto end = route.back();
    return {camera, std::hypot(camera.position.x - end.x, camera.position.z - end.z) > .1f};
}
} // namespace
int main() {
    try {
        auto w = stairs();
        sim::Walk walk;
        // Route points are eye height on the walking graph; the straight segment cuts the steps.
        auto up = drive(*w, {0, 1.7f, 5}, {{0, 1.7f, 1}, {0, 3.5f, -8}}, &walk);
        check(!up.stoppedEarly, "autodrive stopped on the stairs at z=" + std::to_string(up.camera.position.z));
        check(std::abs(up.camera.position.y - 3.5f) < .05f, "stairs: not on the upper floor, y=" +
                                                                 std::to_string(up.camera.position.y));
        auto down = drive(*w, {0, 3.5f, -8}, {{0, 3.5f, -4}, {0, 1.7f, 4}}, &walk);
        check(!down.stoppedEarly && std::abs(down.camera.position.y - 1.7f) < .05f, "stairs down");
        auto ramp = drive(*w, {4, 1.7f, 4}, {{4, 1.7f, 1}, {4, 3.5f, -8}}, &walk);
        check(!ramp.stoppedEarly && std::abs(ramp.camera.position.y - 3.5f) < .05f, "ramp");
        auto ledge = drive(*w, {-4.5f, 1.7f, 4}, {{-4.5f, 2.2f, -2}}, &walk, 6);
        check(ledge.stoppedEarly && ledge.camera.position.z > -1 && ledge.camera.position.y < 1.75f,
              "a 0.5 m ledge must stop autodrive, z=" + std::to_string(ledge.camera.position.z));
        // Fly autodrive (no walker) keeps the straight, obstruction-checked line.
        auto fly = drive(*w, {0, 1.7f, 5}, {{0, 1.7f, 1}, {0, 3.5f, -8}}, nullptr);
        check(fly.stoppedEarly && fly.camera.position.z > -.5f, "fly autodrive tunnelled into the stairs");
        std::cout << "autodrive: stairs up/down, ramp, ledge stop, fly line check\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
