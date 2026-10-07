#include "surfaces/Surface.hpp"
#include "world/Schema.hpp"
#include "world/World.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace hyprune;
namespace {
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f, const char *message) {
    bool caught = false;
    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }
    check(caught, message);
}
} // namespace
int main(int argc, char **argv) {
    if (argc < 2)
        return 1;
    auto temp = std::filesystem::temp_directory_path() / ("hyprune-unit-" + std::to_string(getpid()));
    try {
        auto w = world::load(argv[1]);
        check(w->meshes.size() == 6, "fixture mesh count");
        check(!w->collision.empty(), "explicit collision loaded");
        check(w->materials[0].lightmap == 0, "lightmap contract");
        auto invalid = w->manifest;
        invalid["unknownField"] = true;
        rejects([&] { world::validateManifest(invalid); }, "unknown manifest fields accepted");
        invalid = w->manifest;
        invalid["spawns"][0]["pose"]["position"] = {0, 0};
        rejects([&] { world::validateManifest(invalid); }, "malformed vec3 accepted");
        for (auto path : {"../scene.gltf", "/etc/passwd", "scene.gltf?x", "a//b", "file:///etc/passwd", "%2e%2e/x"})
            rejects([&] { world::safePath(argv[1], path); }, "unsafe path accepted");
        std::filesystem::copy(argv[1], temp, std::filesystem::copy_options::recursive);
        std::filesystem::create_symlink("/etc/passwd", temp / "outside");
        rejects([&] { world::safePath(temp, "outside"); }, "symlink accepted");
        auto altered = w->manifest;
        altered["spawns"][0]["pose"]["position"] = {-5.8, 0, 3};
        {
            std::ofstream f(temp / "world.json");
            f << altered;
        }
        rejects([&] { world::load(temp); }, "blocked spawn accepted");
        {
            std::ofstream f(temp / "world.json");
            f << w->manifest;
        }
        world::Json gltf;
        {
            std::ifstream f(temp / "scene.gltf");
            f >> gltf;
        }
        gltf["meshes"][0]["primitives"][0]["attributes"].erase("TEXCOORD_1");
        {
            std::ofstream f(temp / "scene.gltf");
            f << gltf;
        }
        rejects([&] { world::load(temp); }, "missing UV1 accepted");
        std::vector<Triangle> mesh = {{{-20, 0, -20}, {20, 0, 20}, {20, 0, -20}},
                                      {{-20, 0, -20}, {-20, 0, 20}, {20, 0, 20}},
                                      {{-20, 0, -2}, {20, 4, -2}, {20, 0, -2}},
                                      {{-20, 0, -2}, {-20, 4, -2}, {20, 4, -2}}};
        sim::Camera camera;
        camera.position = {0, 1.7f, 2};
        sim::Walk walk;
        for (int i = 0; i < 240; i++)
            walk.step(camera, {1, 0, 0, false}, 1.f / 60.f, mesh);
        check(camera.position.z >= -1.705f && camera.position.z < -1.6f, "capsule tunneled through wall");
        check(std::abs(camera.position.y - 1.7f) < .01f, "capsule fell through floor");
        check(camera.grounded, "walk not grounded");
        walk.step(camera, {0, 0, 0, true}, 1.f / 60.f, mesh);
        check(camera.position.y > 1.7f, "jump failed");
        sim::Fly fly;
        auto before = camera.position;
        fly.step(camera, {0, 0, 1, false}, .05f, mesh);
        check(camera.position.y > before.y, "fly strategy failed");
        surfaces::Surface plane;
        plane.center = {0, 2, -2};
        plane.width = 4;
        plane.height = 2;
        auto hit = surfaces::intersect(plane, {0, 2, 0}, {0, 0, -1});
        check(hit && std::abs(hit->u - .5f) < .001f && std::abs(hit->v - .5f) < .001f, "surface center mapping");
        check(!surfaces::intersect(plane, {0, 2, -4}, {0, 0, 1}), "backface picked");
        check(!surfaces::intersect(plane, {10, 2, 0}, {0, 0, -1}), "outside hit accepted");
        std::filesystem::remove_all(temp);
        std::cout << "PASS: schema, world, paths, UV1, spawn, capsule wall/floor/jump, fly, picking\n";
        return 0;
    } catch (const std::exception &e) {
        std::filesystem::remove_all(temp);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
