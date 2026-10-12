// World format 0.9 characters, paths, seats and keepClear (RFC-0011 P2): loader rules,
// package and library character assets, route geometry and walkability.
#include "interaction/Walkability.hpp"
#include "world/Character.hpp"
#include "world/Objects.hpp"
#include "world/Paths.hpp"
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <unistd.h>
using namespace hyprune;
namespace fs = std::filesystem;
namespace {
void check(bool value, const std::string &why) {
    if (!value)
        throw std::runtime_error(why);
}
world::Json read(const fs::path &p) { return world::Json::parse(std::ifstream(p)); }
} // namespace
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("usage: npc-world-tests FIXTURE");
        fs::path fixture = argv[1];
        auto w = world::load(fixture);
        // Assets: the package character directory and the inline seat asset.
        check(w->objectAssets.size() == 2, "two assets");
        auto &mannequin = w->objectAssets[0];
        check(mannequin.character && mannequin.meshes.empty() && mannequin.traits.contains("character") &&
                  mannequin.manifest.is_object() && mannequin.ref == "package/mannequin",
              "a package character directory loads as a skinned character");
        check(mannequin.radius > (mannequin.high.y - mannequin.low.y) * .5f, "character swept sphere grown for limbs");
        check(!w->objectAssets[1].character && w->objectAssets[1].traits.contains("seat") &&
                  w->objectAssets[1].traits.contains("collidable") && !w->objectAssets[1].collision.empty(),
              "inline seat asset with the collision shorthand");
        // Instances: two bound to routes (at their phase), one standing, the bench.
        check(w->objectInstances.size() == 4, "four instances");
        auto &walker = w->objectInstances[0], &second = w->objectInstances[1], &sitter = w->objectInstances[2];
        check(walker.character && walker.bound && std::abs(walker.position.x + 5) < 1e-3f && std::abs(walker.position.z - 5) < 1e-3f,
              "a bound character starts at its route's phase");
        auto line = world::routeOf(w->paths[1]);
        auto half = line.at(line.length() * .5f);
        check(second.bound && length(second.position - half) < 1e-3f, "phase 0.5 starts half way along");
        check(sitter.character && !sitter.bound && w->objectInstances[3].collisionCount > 0, "standing character, collidable bench");
        check(w->paths.size() == 4 && w->seats.size() == 1 && w->keepClear.size() == 1, "paths, seats and keepClear kept");
        check(world::pathUse(w->paths[3]) == "generic" && world::pathUse(w->paths[0]) == "route", "use defaults to generic");
        auto loop = world::routeOf(w->paths[0]);
        check(std::abs(loop.length() - 40) < 1e-3f && loop.points.size() == 5 && std::abs(loop.points[2] - 20) < 1e-3f,
              "closed route length and point distances");
        // Walkability against the fixture's collision.
        interaction::PlacementScene scene;
        scene.collision = w->collision;
        scene.index = &w->collisionIndex;
        scene.playable = &w->playable;
        interaction::Body body;
        check(interaction::checkRoute(scene, loop, body).empty(), "the loop is walkable");
        check(interaction::checkRoute(scene, world::routeOf(w->paths[2]), body).empty(), "a 0.2 m step is walkable");
        auto through = interaction::checkLeg(scene, {-4, 0, 0}, {4, 0, 0}, body);
        check(!through.empty() && through[0].rule == "walls", "a route through the block hits walls");
        auto off = interaction::checkLeg(scene, {0, 0, 12}, {0, 0, 14.8f}, body);
        check(!off.empty() && (off[0].rule == "playable" || off[0].rule == "walls"), "leaving the playable volume");
        check(interaction::checkSpot(scene, {0, 3, 3}, body).has_value(), "no floor in mid-air");
        check(!interaction::checkSpot(scene, {3, 0, 6}, body), "a free standing spot");
        // Loader rules: each edit of the fixture must be refused.
        auto copy = fs::temp_directory_path() / std::format("hn-npc-world-{}", getpid());
        fs::remove_all(copy);
        fs::copy(fixture, copy, fs::copy_options::recursive);
        auto base = read(fixture / "world.json");
        auto rejects = [&](const std::string &what, std::function<void(world::Json &)> edit) {
            auto j = base;
            edit(j);
            std::ofstream(copy / "world.json") << j.dump();
            try {
                world::load(copy);
            } catch (const std::exception &) {
                return;
            }
            throw std::runtime_error("accepted: " + what);
        };
        auto accepts = [&](const std::string &what, std::function<void(world::Json &)> edit) {
            auto j = base;
            edit(j);
            std::ofstream(copy / "world.json") << j.dump();
            try {
                return world::load(copy);
            } catch (const std::exception &e) {
                throw std::runtime_error("refused " + what + ": " + e.what());
            }
        };
        rejects("a 0.9 world labelled 0.8", [](auto &j) { j["formatVersion"] = "0.8"; });
        rejects("pose and path together", [](auto &j) { j["objects"][0]["pose"] = j["objects"][2]["pose"]; });
        rejects("an unbound character without a pose", [](auto &j) { j["objects"][2].erase("pose"); });
        rejects("an unknown path", [](auto &j) { j["objects"][0]["path"] = "nowhere"; });
        rejects("a character on a generic path", [](auto &j) { j["objects"][0]["path"] = "view"; });
        rejects("a bench on a route", [](auto &j) { j["objects"][3]["path"] = "loop", j["objects"][3].erase("pose"); });
        rejects("accent on a bench", [](auto &j) { j["objects"][3]["accent"] = {1, 0, 0}; });
        rejects("reverse without a path", [](auto &j) { j["objects"][2]["reverse"] = true; });
        rejects("a seat on a route-bound character", [](auto &j) { j["objects"][0]["seat"] = "ledge-seat"; });
        rejects("an unknown seat", [](auto &j) { j["objects"][2]["seat"] = "sofa"; });
        rejects("a seat stop at a missing anchor", [](auto &j) { j["paths"][0]["stops"][1]["seat"] = "bench-1/middle"; });
        rejects("a scaled character", [](auto &j) { j["objects"][2]["scale"] = 2; });
        rejects("a travel interaction (reserved)", [](auto &j) {
            j["objects"][0]["interactions"][0]["action"] = {{"type", "travel"}, {"path", "loop"}};
        });
        rejects("a guide along a generic path", [](auto &j) { j["objects"][0]["interactions"][2]["action"]["path"] = "view"; });
        rejects("a waypoint at an unknown anchor", [](auto &j) {
            j["objects"][0]["interactions"][1]["action"]["target"] = {{"anchor", "missing"}};
        });
        rejects("duplicate interaction ids", [](auto &j) { j["objects"][0]["interactions"][1]["id"] = "talk"; });
        rejects("a short arc", [](auto &j) { j["paths"][1]["points"][2] = {{"position", {-2, 0, -9.05}}, {"type", "arc"}}; });
        rejects("a stop past the last point", [](auto &j) { j["paths"][0]["stops"][0]["point"] = 9; });
        rejects("two stops at one point", [](auto &j) { j["paths"][0]["stops"][1]["point"] = 1; });
        rejects("a clip stop on a track", [](auto &j) { j["paths"][0]["use"] = "track", j["objects"][0].erase("path"); });
        rejects("stops on a generic path", [](auto &j) { j["paths"][3]["stops"] = {{{"point", 0}}}; });
        rejects("an inverted keepClear box", [](auto &j) { j["keepClear"][0]["max"][1] = -1; });
        rejects("animations twice (shorthand and trait)", [](auto &j) {
            j["objectAssets"][1]["animations"] = world::Json::array();
            j["objectAssets"][1]["traits"]["decorative"] = {{"animations", world::Json::array()}};
        });
        rejects("a character combined with a seat", [](auto &j) {
            j["objectAssets"].push_back({{"id", "chair-person"}, {"scene", "characters/mannequin/object.glb"},
                                         {"traits", {{"character", {{"clips", {{"idle", world::Json::object()}, {"walk", world::Json::object()}}},
                                                                    {"height", 1.7}}},
                                                     {"seat", {{"anchors", {{{"id", "a"}, {"position", {0, 0, 0}}, {"yaw", 0}, {"height", .4}}}}}}}}});
        });
        rejects("seventeen characters", [](auto &j) {
            for (int i = 0; i < 15; ++i) {
                auto o = j["objects"][2];
                o["id"] = std::format("extra-{}", i);
                o.erase("seat");
                j["objects"].push_back(o);
            }
        });
        rejects("nine characters on one path", [](auto &j) {
            for (int i = 0; i < 8; ++i) {
                auto o = j["objects"][0];
                o["id"] = std::format("loop-{}", i);
                o.erase("interactions");
                j["objects"].push_back(o);
            }
        });
        rejects("a missing library", [](auto &j) {
            j["objectAssets"][0] = {{"id", "mannequin"}, {"library", "com.example.cast@1.0.0"}, {"object", "mannequin"}};
        });
        accepts("sixteen characters", [](auto &j) {
            for (int i = 0; i < 13; ++i) {
                auto o = j["objects"][2];
                o["id"] = std::format("extra-{}", i);
                o.erase("seat");
                j["objects"].push_back(o);
            }
        });
        // Library references resolve against the installed library roots.
        auto libraries = copy / "libraries";
        fs::create_directories(libraries / "com.example.cast@1.0.0/objects");
        fs::copy(fixture / "characters/mannequin", libraries / "com.example.cast@1.0.0/objects/mannequin", fs::copy_options::recursive);
        auto j = base;
        j["objectAssets"][0] = {{"id", "mannequin"}, {"library", "com.example.cast@1.0.0"}, {"object", "mannequin"}};
        std::ofstream(copy / "world.json") << j.dump();
        world::LoadOptions options;
        options.libraryRoots = {fs::path(copy / "nowhere"), libraries};
        auto lib = world::load(copy, options);
        check(lib->objectAssets[0].character && lib->objectAssets[0].ref == "library/com.example.cast@1.0.0/mannequin",
              "a library character resolves in the second root");
        try {
            world::load(copy);
            throw std::runtime_error("a missing library loaded");
        } catch (const std::runtime_error &e) {
            check(std::string(e.what()).starts_with("world.library.missing: com.example.cast@1.0.0"), e.what());
        }
        // Older formats reject the 0.9 fields.
        auto old = base;
        old["formatVersion"] = "0.8";
        for (auto key : {"paths", "seats", "keepClear"})
            old.erase(key);
        old["objectAssets"] = world::Json::array({base["objectAssets"][1]});
        old["objectAssets"][0].erase("traits");
        old["objects"] = world::Json::array({base["objects"][3]});
        std::ofstream(copy / "world.json") << old.dump();
        check(world::load(copy)->objectInstances.size() == 1, "a 0.8 world still loads");
        old["objects"][0]["locked"] = true;
        std::ofstream(copy / "world.json") << old.dump();
        bool refused = false;
        try {
            world::load(copy);
        } catch (const std::exception &) {
            refused = true;
        }
        check(refused, "0.8 rejects 0.9 instance fields");
        fs::remove_all(copy);
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
    std::cout << "PASS world 0.9 NPCs: character assets (package, library), bound instances, paths, seats, keepClear, walkability, loader rules\n";
}
