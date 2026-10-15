// RFC-0011 P1 core: object manifest checks, the catalog (user folder, libraries),
// library archives, refs, the editor overlay migration, catalog asset loading and the
// support rule. Fixture: tests/fixtures/catalog.py.
#include "interaction/Placement.hpp"
#include "interaction/InputFile.hpp"
#include "interaction/Tools.hpp"
#include "world/Catalog.hpp"
#include "world/Objects.hpp"
#include "world/Schema.hpp"
#include "world/World.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <unistd.h>
namespace fs = std::filesystem;
using namespace hyprune;
using world::Json;
namespace {
int checked = 0;
void check(bool ok, const char *what) {
    ++checked;
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}
template <class E, class F> bool throws(F f) {
    try {
        f();
    } catch (const E &) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}
int sh(const std::string &command) { return std::system(command.c_str()); }
const world::CatalogEntry *find(const world::CatalogSnapshot &s, const std::string &ref) {
    auto it = s.refs.find(ref);
    return it == s.refs.end() ? nullptr : &it->second;
}
void manifests() {
    namespace o = world::objects;
    for (auto good : {"CC0-1.0", "MIT OR Apache-2.0", "(MIT AND CC-BY-4.0) OR GPL-2.0-only", "LicenseRef-mine",
                      "GPL-2.0-or-later WITH Classpath-exception-2.0", "Apache-2.0+"})
        check(o::validSpdx(good), good);
    for (auto bad : {"", "Bogus-1.0", "MIT AND", "(MIT", "MIT OR OR CC0-1.0", "LicenseRef-", "MIT WITH MIT", "Apache-2.0 +"})
        check(!o::validSpdx(bad), bad);
    Json m{{"formatVersion", "0.9"}, {"id", "lamp"}, {"version", "1.0.0"}, {"name", "Lamp"}, {"category", "lighting"},
           {"scene", "object.glb"}, {"license", "CC0-1.0"},
           {"attribution", Json::array({{{"path", "."}, {"author", "a"}, {"license", "CC0-1.0"}}})},
           {"bounds", {{"min", {-.2, 0, -.2}}, {"max", {.2, 1.6, .2}}}}, {"traits", {{"movable", Json::object()}}}};
    check(o::checkObject(m, {}).valid, "minimal manifest");
    auto bad = m;
    bad["bounds"]["min"][1] = .3;
    check(!o::checkObject(bad, {}).valid && o::checkObject(bad, {}).errors[0].starts_with("bounds: min y must be 0"), "floor origin");
    bad = m;
    bad["traits"]["mount"] = Json::object();
    check(o::checkObject(bad, {}).errors[0] == "traits.mount: needs object format 0.11 (workspace screens); rejected in object format 0.9",
          "reserved trait named plainly");
    bad = m;
    bad["traits"]["mover"] = Json::object();
    check(o::checkObject(bad, {}).errors[0].starts_with("traits.mover: needs object format 0.10"), "mover needs 0.10");
    bad = m;
    bad["traits"]["character"] = {{"clips", {{"walk", Json::object()}, {"idle", Json::object()}}}, {"height", 1.7}};
    check(!o::checkObject(bad, {}).valid, "character and movable can't combine");
    bad = m;
    bad["formatVersion"] = "0.8";
    check(o::checkObject(bad, {}).errors[0] == "formatVersion: \"0.8\" is not an object format (0.9, 0.10, 0.11)", "format named");
    bad = m;
    bad["extra"] = 1;
    check(!o::checkObject(bad, {}).valid, "closed manifest");
    bad = m;
    bad["license"] = "Nope";
    check(o::checkObject(bad, {}).errors == std::vector<std::string>{"license: invalid SPDX expression"}, "SPDX license");
    std::vector<o::File> files{{"object.json", 10}, {"object.glb", 10}, {"run.sh", 3}};
    o::Options options;
    options.files = &files;
    options.directory = "other";
    auto r = o::checkObject(m, options);
    check(!r.valid && r.errors[0] == "id: \"lamp\" differs from its directory \"other\"", "directory name");
    options.thumbnail = o::Thumbnail{128, 128, 100};
    options.directory = "lamp";
    r = o::checkObject(m, options);
    check(std::find(r.errors.begin(), r.errors.end(), "thumbnail: 128 x 128, must be 256 x 256") != r.errors.end(), "thumbnail size");
    options = {};
    options.measured.gpuBytes = o::Limits::gpuBytes + 1;
    check(!o::checkObject(m, options).valid, "GPU budget");
}
void refs() {
    auto r = world::parseRef("library/com.hyprune.starter@0.1.0/server-rack");
    check(r && r->source == "library" && r->library == "com.hyprune.starter" && r->version == "0.1.0" && r->id == "server-rack",
          "library ref");
    r = world::parseRef("library/com.example.kit@1.2.3-rc.1+build.5/crate");
    check(r && r->version == "1.2.3-rc.1+build.5" && r->id == "crate", "prerelease library ref");
    check(world::parseRef("user/lamp")->source == "user" && world::parseRef("package/armillary")->source == "package", "user and package refs");
    for (auto bad : {"lamp", "user/Lamp", "user/../x", "library/kit@1.0.0/x", "library/com.example.kit@1.0/x", "package/", "other/x"})
        check(!world::parseRef(bad), bad);
    check(world::versionLess("1.0.0-rc.1", "1.0.0") && world::versionLess("1.2.0", "1.10.0") && !world::versionLess("2.0.0", "1.9.9"),
          "semver order");
    check(world::objectMethods().size() == 24 && world::objectMethods().at("objects.place") == "objects.control" &&
              world::objectMethods().at("companion.set") == "objects.control" && world::objectMethods().at("companion.mode") == "objects.control" &&
              world::objectMethods().at("objects.catalog") == "state.read" && world::objectMethods().at("npcs.spawn") == "objects.control" &&
              world::objectMethods().at("paths.list") == "state.read" && world::objectMethods().at("interaction.choose") == "world.control",
          "method grants");
}
void catalog(const fs::path &fixture, const fs::path &tmp) {
    world::CatalogRoots roots;
    roots.userObjects = fixture / "user";
    roots.userLibraries = fixture / "libraries";
    world::ScanCache cache;
    auto s = world::scanCatalog(roots, cache);
    check(s.libraries.size() == 1 && s.libraries[0].state == "ready" && s.libraries[0].objects == 2, "library record");
    auto *lamp = find(s, "user/lamp");
    check(lamp && lamp->state == "ready" && lamp->placeable && lamp->triangles == 36 && lamp->collisionTriangles == 12 &&
              lamp->thumbnail.is_string() && lamp->scale == Json::array({.5, 2}) && lamp->surfaces == Json::array({"floor"}),
          "user lamp entry");
    check(lamp->gpuBytes.is_number() && lamp->gpuBytes.get<uint64_t>() == fs::file_size(fixture / "user/lamp/object.bin"),
          "SDK GPU estimate: buffers (no images)");
    auto *broken = find(s, "user/broken");
    check(broken && broken->state == "invalid" && !broken->placeable && broken->reason.starts_with("invalid: bounds") &&
              broken->thumbnail.is_null(),
          "invalid entry");
    auto *statue = find(s, "user/statue");
    check(statue && statue->state == "ready" && !statue->placeable && statue->reason.starts_with("not movable"), "not movable");
    auto *crate = find(s, "library/com.example.kit@1.0.0/crate");
    check(crate && crate->placeable && crate->library == "com.example.kit@1.0.0", "library entry");
    auto *bench = find(s, "library/com.example.kit@1.0.0/bench");
    check(bench && bench->placeable && bench->has("seat"), "a seat places as its static body in P1");
    check(s.entries.size() == 6 && s.entries[0].ref == "library/com.example.kit@1.0.0/crate", "catalog order: libraries, then user");
    auto again = world::scanCatalog(roots, cache);
    check(find(again, "user/lamp")->digest == lamp->digest, "cached rescan");
    // A second library version: the catalog shows the highest and lists the other.
    auto libs = tmp / "libs";
    fs::create_directories(libs);
    fs::copy(fixture / "libraries/com.example.kit@1.0.0", libs / "com.example.kit@1.0.0", fs::copy_options::recursive);
    fs::copy(fixture / "libraries/com.example.kit@1.0.0", libs / "com.example.kit@1.1.0", fs::copy_options::recursive);
    {
        auto p = libs / "com.example.kit@1.1.0/library.json";
        auto j = Json::parse(std::ifstream(p));
        j["version"] = "1.1.0";
        std::ofstream(p) << j.dump();
    }
    roots.userLibraries = libs;
    s = world::scanCatalog(roots, cache);
    auto shown = std::find_if(s.entries.begin(), s.entries.end(), [](auto &e) { return e.id == "crate"; });
    check(shown != s.entries.end() && shown->ref == "library/com.example.kit@1.1.0/crate" && shown->versions == std::vector<std::string>{"1.0.0"} &&
              find(s, "library/com.example.kit@1.0.0/crate"),
          "highest version shown, older resolvable");
    // The same id@version with different content in a system root is a conflict.
    auto system = tmp / "system";
    fs::create_directories(system);
    fs::copy(fixture / "libraries/com.example.kit@1.0.0", system / "com.example.kit@1.0.0", fs::copy_options::recursive);
    std::ofstream(system / "com.example.kit@1.0.0/ATTRIBUTION.md") << "changed\n";
    roots.systemLibraries = {system};
    s = world::scanCatalog(roots, cache);
    auto conflict = std::find_if(s.libraries.begin(), s.libraries.end(), [](auto &l) { return l.version == "1.0.0"; });
    check(conflict != s.libraries.end() && conflict->state == "conflict" && !find(s, "library/com.example.kit@1.0.0/crate"),
          "library.conflict refuses the version");
}
void archives(const fs::path &fixture, const fs::path &tmp) {
    world::CatalogRoots roots;
    roots.userLibraries = tmp / "installed";
    roots.userObjects = tmp / "objects";
    auto stage = tmp / "stage";
    fs::create_directories(stage);
    fs::copy(fixture / "libraries/com.example.kit@1.0.0", stage / "com.example.kit", fs::copy_options::recursive);
    auto archive = tmp / "com.example.kit-1.0.0.tar.zst";
    check(sh("tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner --format=ustar --zstd -cf " + archive.string() + " -C " +
             stage.string() + " com.example.kit") == 0,
          "pack archive");
    auto sha = world::sha256File(archive);
    check(throws<std::invalid_argument>([&] { world::installLibrary(archive, std::string(64, '0'), roots); }), "hash checked first");
    check(throws<std::invalid_argument>([&] { world::installLibrary("relative.tar.zst", sha, roots); }), "absolute path");
    auto r = world::installLibrary(archive, sha, roots);
    check(r.id == "com.example.kit" && r.version == "1.0.0" && !r.already && fs::is_regular_file(roots.userLibraries / "com.example.kit@1.0.0/objects/crate/object.json"),
          "install");
    check(world::installLibrary(archive, sha, roots).already, "reinstall is idempotent");
    std::ofstream(stage / "com.example.kit/ATTRIBUTION.md") << "different\n";
    auto other = tmp / "other.tar.zst";
    sh("tar --format=ustar --zstd -cf " + other.string() + " -C " + stage.string() + " com.example.kit");
    check(throws<std::domain_error>([&] { world::installLibrary(other, world::sha256File(other), roots); }), "conflicting content");
    // Unsafe members: traversal, a symlink, two roots; and a library that fails validation.
    auto evil = tmp / "evil";
    fs::create_directories(evil);
    std::ofstream(evil / "make.py") << R"(import io, sys, tarfile
def pack(path, members):
    with tarfile.open(path, 'w', format=tarfile.USTAR_FORMAT) as t:
        for name, kind in members:
            i = tarfile.TarInfo(name)
            if kind == 'link': i.type = tarfile.SYMTYPE; i.linkname = '/etc/passwd'; t.addfile(i)
            elif kind == 'dir': i.type = tarfile.DIRTYPE; t.addfile(i)
            else: data = b'x'; i.size = len(data); t.addfile(i, io.BytesIO(data))
d = sys.argv[1]
pack(d + '/traversal.tar', [('com.example.kit/library.json', 'file'), ('com.example.kit/../../escape', 'file')])
pack(d + '/link.tar', [('com.example.kit/library.json', 'file'), ('com.example.kit/LICENSE', 'link')])
pack(d + '/roots.tar', [('com.example.kit/library.json', 'file'), ('other/x', 'file')])
pack(d + '/invalid.tar', [('com.example.kit/library.json', 'file')])
)";
    check(sh("python3 -I " + (evil / "make.py").string() + " " + evil.string()) == 0, "evil archives");
    for (auto name : {"traversal", "link", "roots", "invalid"}) {
        auto tar = evil / (std::string(name) + ".tar");
        auto zst = evil / (std::string(name) + ".tar.zst");
        sh("zstd -q -f " + tar.string() + " -o " + zst.string());
        check(throws<std::invalid_argument>([&] { world::installLibrary(zst, world::sha256File(zst), roots); }), name);
    }
    check(!fs::exists(tmp / "escape") && !fs::exists(roots.userLibraries.parent_path() / "escape"), "nothing escaped");
    for (auto &e : fs::directory_iterator(roots.userLibraries))
        check(!e.path().filename().string().starts_with(".install"), "staging cleaned up");
    world::removeLibrary("com.example.kit", "1.0.0", roots);
    check(!fs::exists(roots.userLibraries / "com.example.kit@1.0.0"), "remove");
    check(throws<std::invalid_argument>([&] { world::removeLibrary("com.example.kit", "1.0.0", roots); }), "remove unknown");
    roots.systemLibraries = {fixture / "libraries"};
    check(throws<std::domain_error>([&] { world::removeLibrary("com.example.kit", "1.0.0", roots); }), "system roots are read-only");
}
void overlay() {
    interaction::Tools tools;
    Json v1{{"version", 1},
            {"paths", Json::array()},
            {"locks", Json::array()},
            {"objects", Json::array({{{"worldId", "w"}, {"areaId", "a"}, {"objectId", "object:arm"}, {"position", {1, 2, 3}},
                                      {"rotation", {0, 0, 0, 1}}, {"scale", {1.5, 1.5, 1.5}}},
                                     {{"worldId", "w"}, {"areaId", "a"}, {"objectId", "node-4"}, {"position", {0, 0, 0}},
                                      {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}})}};
    tools.restore(v1);
    check(tools.migrated && tools.objects.size() == 1 && tools.objects[0]["objectId"] == "node-4", "v1 node records stay");
    auto o = tools.override("w", "arm");
    check(o.is_object() && o["pose"]["position"] == Json::array({1, 2, 3}) && o["scale"] == 1.5 && o["areaId"] == "a",
          "v1 instance record migrates to an override");
    auto saved = tools.saved();
    check(saved["version"] == 2 && saved.size() == 6, "version 2 file");
    tools.setPlacement("w", "u-123abc", {{"worldId", "w"}, {"id", "u-123abc"}, {"areaId", ""}, {"ref", "user/lamp"},
                                         {"pose", {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}}}, {"scale", 1}});
    interaction::Tools restored;
    restored.restore(tools.saved());
    check(!restored.migrated && restored.saved() == tools.saved() && restored.placement("w", "u-123abc")["ref"] == "user/lamp",
          "round trip");
    auto broken = tools.saved();
    broken["overrides"][0]["colour"] = "red";
    check(throws<std::runtime_error>([&] { restored.restore(broken); }), "closed override");
    broken = tools.saved();
    broken["placements"].push_back(broken["placements"][0]);
    check(throws<std::runtime_error>([&] { restored.restore(broken); }), "duplicate placement id");
    broken = tools.saved();
    broken["placements"][0]["pose"]["rotation"] = {0, 0, 0, 2};
    check(throws<std::runtime_error>([&] { restored.restore(broken); }), "unit quaternion");
    // User characters (IPC 0.23) and workspace screens (IPC 0.27) keep their extra fields.
    tools.setPlacement("w", "u-npc001", {{"worldId", "w"}, {"id", "u-npc001"}, {"areaId", ""}, {"ref", "user/walker"},
                                         {"pose", {{"position", {0, 0, 0}}, {"rotation", {0, 0, 0, 1}}}}, {"scale", 1},
                                         {"path", "user/path-1"}, {"phase", .25}, {"speed", 1.2}, {"reverse", true},
                                         {"accent", {.9, .5, .1}}, {"disabled", false}});
    tools.setPlacement("w", "u-scr001", {{"worldId", "w"}, {"id", "u-scr001"}, {"areaId", ""}, {"ref", "user/screen"},
                                         {"pose", {{"position", {0, 1, 0}}, {"rotation", {0, 0, 0, 1}}}}, {"scale", 1},
                                         {"workspace", 23}, {"locked", true}});
    interaction::Tools again;
    again.restore(tools.saved());
    check(again.placement("w", "u-npc001")["path"] == "user/path-1" && again.placement("w", "u-scr001")["workspace"] == 23,
          "character and screen records survive a restart");
    broken = tools.saved();
    broken["placements"][2]["workspace"] = 0;
    check(throws<std::runtime_error>([&] { again.restore(broken); }), "screen workspace 1-9999");
    broken = tools.saved();
    broken["placements"][2]["colour"] = "red";
    check(throws<std::runtime_error>([&] { again.restore(broken); }), "closed placement");
    tools.setOverride("w", "arm", nullptr);
    check(tools.override("w", "arm").is_null(), "remove override");
}
void assets(const fs::path &fixture) {
    auto manifest = Json::parse(std::ifstream(fixture / "user/lamp/object.json"));
    auto scratch = world::loadObjectAsset(fixture / "user/lamp", manifest, {});
    check(scratch->objectAssets.size() == 1 && scratch->objectAssets[0].animations.size() == 1 &&
              scratch->objectAssets[0].collision.size() == 12 && std::abs(scratch->objectAssets[0].high.y - 1.6f) < 1e-4f,
          "catalog asset decodes");
    world::World live;
    live.materials.resize(3);
    live.images.resize(2);
    unsigned index = world::mergeObjectAsset(live, *scratch);
    check(index == 0 && live.materials.size() == 3 + scratch->materials.size() && live.samplers.size() == live.images.size() &&
              live.objectAssets[0].meshes[0].material >= 3 && live.objectAssets[0].firstMaterial == 3,
          "merge re-indexes materials");
    auto broken = Json::parse(std::ifstream(fixture / "user/broken/object.json"));
    check(throws<std::runtime_error>([&] { world::loadObjectAsset(fixture / "user/broken", broken, {}); }), "decoded bounds checked");
}
void support() {
    // A 4 m floor square at y = 0 and a wall at z = -1.
    std::vector<Triangle> collision{{{-2, 0, -2}, {2, 0, 2}, {2, 0, -2}}, {{-2, 0, -2}, {-2, 0, 2}, {2, 0, 2}},
                                    {{-2, 0, -1}, {2, 0, -1}, {2, 3, -1}}, {{-2, 0, -1}, {2, 3, -1}, {-2, 3, -1}}};
    interaction::PlacementScene s;
    s.collision = collision;
    auto box = interaction::aabbBox({-.2f, 0, -.2f}, {.2f, 1, .2f});
    s.support = "floor";
    check(interaction::checkPlacement(s, box, {}).ok, "standing on the floor");
    auto floating = interaction::aabbBox({-.2f, .5f, -.2f}, {.2f, 1.5f, .2f});
    auto v = interaction::checkPlacement(s, floating, {});
    check(!v.ok && v.rule == "support", "floating floor object refused");
    s.support = "any";
    check(interaction::checkPlacement(s, floating, {}).ok, "any may float");
    s.support = "";
    check(interaction::checkPlacement(s, floating, {}).ok && !interaction::checkPlacement(s, floating, {}).supported, "advisory only");
    s.support = "wall";
    auto sign = interaction::aabbBox({-.4f, 1, -1.f}, {.4f, 1.4f, -.95f}); // back (-Z) against the wall
    check(interaction::checkPlacement(s, sign, {}).ok, "wall object on the wall");
    auto loose = interaction::aabbBox({-.4f, 1, 0}, {.4f, 1.4f, .05f});
    check(interaction::checkPlacement(s, loose, {}).rule == "support", "wall object off the wall");
    s.support = "floor,wall";
    check(interaction::checkPlacement(s, sign, {}).ok && interaction::checkPlacement(s, box, {}).ok, "either surface");
    check(interaction::brokenSoftRules(s, loose, {}).empty(), "support is not waived");
}
// Object format 0.11: workspace screens (the mount trait's mounted-workspace anchor).
void screens() {
    namespace o = world::objects;
    check(o::screenAspect(3.2, 1.8) == "16:9" && o::screenAspect(1.6 * 1.004, .9) == "16:9" &&
              o::screenAspect(1.6 * 1.006, .9).empty(),
          "16:9 within 0.5 %");
    check(o::screenAspect(2.1, .9) == "21:9" && o::screenAspect(2.56, 1.08) == "21:9" && o::screenAspect(3.44, 1.44) == "21:9" &&
              o::screenAspect(2.4, .9).empty() && o::screenAspect(4, 3).empty(),
          "21:9 from 2.33 to 2.39");
    Json anchor{{"id", "screen"}, {"role", "mounted-workspace"}, {"mountType", "hanging-wall"},
                {"pose", {{"position", {0, .96, .07}}, {"rotation", {0, 0, 0, 1}}}}, {"size", {3.2, 1.8}}, {"frameNode", "bezel"}};
    Json m{{"formatVersion", "0.11"}, {"id", "screen"}, {"version", "1.0.0"}, {"name", "Screen"}, {"category", "signage"},
           {"scene", "object.glb"}, {"license", "CC0-1.0"},
           {"attribution", Json::array({{{"path", "."}, {"author", "a"}, {"license", "CC0-1.0"}}})},
           {"bounds", {{"min", {-1.66, 0, 0}}, {"max", {1.66, 1.92, .08}}}},
           {"traits", {{"mount", {{"anchors", Json::array({anchor})}}}, {"movable", {{"surfaces", {"wall"}}, {"scale", {.5, 2}}}}}}};
    auto r = o::checkObject(m, {});
    check(r.valid, "a 16:9 wall screen");
    auto errors = [&](Json doc) { return o::checkObject(doc, {}).errors; };
    auto first = [&](Json doc, const std::string &start) {
        auto e = errors(doc);
        return !e.empty() && std::any_of(e.begin(), e.end(), [&](auto &x) { return x.starts_with(start); });
    };
    auto bad = m;
    bad["formatVersion"] = "0.10";
    check(first(bad, "traits.mount: needs object format 0.11"), "mount needs 0.11");
    bad = m;
    bad["traits"]["mount"]["anchors"][0]["size"] = {2.4, 1.8};
    check(first(bad, "traits.mount anchor screen: a workspace screen is 16:9 or 21:9, not 1.333:1"), "aspect");
    bad = m;
    bad["traits"]["mount"]["anchors"][0]["size"] = {1.2, .675};
    check(first(bad, "traits.mount anchor screen: at least 0.9 m high"), "height");
    bad = m;
    bad["traits"]["mount"]["anchors"].push_back(anchor);
    bad["traits"]["mount"]["anchors"][1]["id"] = "second";
    check(first(bad, "traits.mount: at most one mounted-workspace anchor"), "one workspace anchor");
    bad = m;
    bad["traits"]["mount"]["anchors"][0]["pose"]["position"][1] = 1.2;
    check(first(bad, "traits.mount anchor screen: lies outside the bounds grown by 0.1 m") && errors(bad).size() == 1, "inside the bounds, once");
    bad = m;
    bad["traits"]["mount"]["anchors"][0]["pose"]["rotation"] = {0, .7071068, 0, .7071068};
    check(first(bad, "traits.mount anchor screen: lies outside the bounds grown by 0.1 m"), "rotated out");
    bad = m;
    bad["traits"]["seat"] = {{"anchors", Json::array({{{"id", "a"}, {"position", {0, 0, .5}}, {"yaw", 0}, {"height", .45}}})}};
    check(first(bad, "traits: mount and seat can't be combined"), "mount and seat");
    bad = m;
    bad["traits"]["mount"]["anchors"][0]["role"] = "data-object";
    check(!o::checkObject(bad, {}).valid, "role enum");
    auto tool = m;
    tool["traits"]["mount"]["anchors"].push_back({{"id", "tool"}, {"role", "tool-mount"}, {"mountType", "hanging-wall"},
                                                  {"pose", {{"position", {1.2, .5, .07}}, {"rotation", {0, 0, 0, 1}}}}, {"size", {.4, .4}}});
    check(o::checkObject(tool, {}).valid, "tool-mount anchors keep any aspect");
    // frameNode names one scene node.
    Json gltf{{"asset", {{"version", "2.0"}}}, {"scene", 0}, {"scenes", Json::array({{{"nodes", Json::array({0u})}}})},
              {"nodes", Json::array({{{"name", "bezel"}}})}};
    o::Options options;
    options.gltf = &gltf;
    check(o::checkObject(m, options).valid, "frameNode found");
    gltf["nodes"][0]["name"] = "frame";
    auto e = o::checkObject(m, options).errors;
    check(std::any_of(e.begin(), e.end(), [](auto &x) { return x == "traits.mount anchor screen: node \"bezel\" not found in the glTF"; }),
          "frameNode missing");
}
// RFC-0014 imports (SDK `hyprune object import`), object format 0.11 sizes and import.json.
void imports(const fs::path &fixture) {
    namespace o = world::objects;
    auto root = fixture.parent_path() / "imported";
    auto barrel = world::scanObject(root / "barrel", "user/barrel", "user");
    for (auto &e : barrel.errors)
        std::fprintf(stderr, "  barrel: %s\n", e.c_str());
    check(barrel.state == "ready" && barrel.placeable && barrel.triangles == 2000 && barrel.collisionTriangles.get<uint64_t>() > 12,
          "an imported object is a ready, placeable catalog entry");
    check(barrel.provenance == Json{{"license", "CC0-1.0"}, {"author", "Hyprune contributors"}, {"source", "https://github.com/hyprune/sdk"}} &&
              barrel.imported.value("sourceName", "") == "barrel.gltf" && barrel.imported["at"].is_string(),
          "provenance and imported come from import.json");
    auto crate = world::scanObject(root / "crate", "user/crate", "user");
    check(crate.state == "ready" && crate.provenance == Json{{"license", "LicenseRef-unknown"}, {"author", "unknown"}, {"source", "unknown"}},
          "unknown provenance stays unknown, never refused");
    auto manifest = Json::parse(std::ifstream(root / "barrel/object.json"));
    auto scratch = world::loadObjectAsset(root / "barrel", manifest, {});
    auto &a = scratch->objectAssets[0];
    check(std::abs(a.low.y) < .01f && std::abs(a.high.y - 1.2f) < .01f && a.collision.size() == barrel.collisionTriangles.get<size_t>() &&
              scratch->images.size() == 1 && scratch->images[0].width == 512,
          "the imported GLB decodes: meshopt, quantised positions, KTX2 texture, proxy outside the scene");
    // Sizes over the recommendations warn in 0.11 and still refuse in older formats.
    Json m{{"formatVersion", "0.11"}, {"id", "big"}, {"version", "1.0.0"}, {"name", "Big"}, {"category", "other"},
           {"scene", "object.glb"}, {"license", "CC0-1.0"},
           {"attribution", Json::array({{{"path", "."}, {"author", "a"}, {"license", "CC0-1.0"}}})},
           {"bounds", {{"min", {-.2, 0, -.2}}, {"max", {.2, 1.6, .2}}}}, {"traits", {{"movable", Json::object()}}}};
    o::Options big;
    big.measured.gpuBytes = 200ull << 20;
    big.measured.maxTextureSize = 4096;
    auto r = o::checkObject(m, big);
    check(r.valid && r.warnings.size() == 2 && r.warnings[0].ends_with("(recommended; it still loads)"), "0.11 warns over the recommendations");
    big.measured.maxTextureSize = 16384;
    r = o::checkObject(m, big);
    check(!r.valid && r.errors[0] == "textures: 16384 px exceeds 8192 px (the hard cap)", "0.11 hard caps");
    m["formatVersion"] = "0.10";
    big.measured.maxTextureSize = 4096;
    check(!o::checkObject(m, big).valid, "0.10 keeps its budgets as errors");
    auto record = Json::parse(std::ifstream(root / "barrel/import.json"));
    check(o::checkImport(record).valid, "import.json validates");
    record["license"]["spdx"] = "Bogus licence";
    check(!o::checkImport(record).valid, "import.json SPDX");
    record = Json::parse(std::ifstream(root / "barrel/import.json"));
    record["extra"] = 1;
    check(!o::checkImport(record).valid, "import.json is closed");
}
} // namespace
// RFC-0014 world folders (world format 0.11): builder/base.json loads like world.json, the
// panorama becomes cube faces turned as documented (the image centre looks along -Z, u grows
// towards +X), project files validate, and world/<id> refs name the folder's copies.
void builder(const fs::path &fixture) {
    auto root = fixture.parent_path() / "builder";
    world::LoadOptions options;
    options.manifest = "builder/base.json";
    auto w = world::load(root, options);
    check(w->id == "local.fixture-plain" && w->manifest["formatVersion"] == "0.11" && w->objectAssets.empty(),
          "a world folder loads from builder/base.json, without its placements");
    check(w->panorama[0].width == 256 && w->environment[0].width == 128, "the panorama is resampled (256 px) and lights the world (128 px)");
    auto at = [&](int face, float x, float y) {
        int size = w->panorama[face].width, px = int((x + 1) / 2 * size), py = int((y + 1) / 2 * size);
        auto *c = &w->panorama[face].rgba[(size_t(py) * size + px) * 4];
        return std::array<int, 3>{c[0], c[1], c[2]};
    };
    // Face 5 is -Z (direction (-x, -y, -1)): its left half looks towards -X (green band), its
    // right half towards +X (blue); face 2 (+Y) is the yellow cap.
    check(at(5, .5f, 0) == std::array<int, 3>{30, 200, 40} && at(5, -.5f, 0) == std::array<int, 3>{40, 60, 230},
          "the panorama's centre looks along -Z, u grows towards +X");
    check(at(2, 0, 0) == std::array<int, 3>{255, 230, 0}, "the panorama's top row is straight up");
    auto made = world::load(root);
    check(made->id == "local.fixture-plain" && made->panorama[0].width == 256, "the folder's world.json loads too");
    auto project = interaction::readJSON(root / "builder/project.json");
    world::validateObjectDocument(project, "project", "0.1");
    auto bad = project;
    bad["placements"][0]["ref"] = "user/barrel";
    bool refused = false;
    try {
        world::validateObjectDocument(bad, "project", "0.1");
    } catch (const std::exception &) {
        refused = true;
    }
    check(refused, "project placements name only world/<id> copies");
    // projectVersion 0.2 (draft): the Open Plain starter, sky presets and the floor; 0.1 keeps its shape.
    auto v2 = project;
    v2["projectVersion"] = "0.2";
    v2["starter"] = "open-plain@0.2";
    v2["sky"]["preset"] = "sunset";
    v2["floor"] = {{"preset", "grass"}, {"image", nullptr}, {"tile", 4}, {"license", "CC0-1.0"}, {"author", "Hyprune contributors"}, {"source", "generated"}};
    world::validateObjectDocument(v2, "project", "0.1");
    auto refusedDoc = [](const Json &doc) {
        try {
            world::validateObjectDocument(doc, "project", "0.1");
        } catch (const std::exception &) {
            return true;
        }
        return false;
    };
    auto tiny = v2;
    tiny["floor"]["tile"] = 0.1;
    auto old = project;
    old["floor"] = v2["floor"];
    auto unknown = v2;
    unknown["projectVersion"] = "0.3";
    check(refusedDoc(tiny) && refusedDoc(old) && refusedDoc(unknown), "project 0.2 takes a floor and presets; 0.1 and unknown versions don't");
    auto ref = world::parseRef("world/barrel");
    check(ref && ref->source == "world" && ref->id == "barrel" && !world::parseRef("world/Barrel") && !world::parseRef("world/a/b"),
          "world/<id> refs");
    auto e = world::scanObject(root / "objects/barrel", "world/barrel", "world");
    world::decidePlaceable(e);
    check(e.state == "ready" && e.placeable && e.source == "world", "a folder's copy is a placeable world entry");
    // A panorama that isn't 2:1 is refused.
    auto temp = fs::temp_directory_path() / ("hn-builder-" + std::to_string(getpid()));
    fs::remove_all(temp);
    fs::copy(root, temp, fs::copy_options::recursive);
    fs::copy_file(root / "objects/barrel/thumbnail.png", temp / "sky/bands.png", fs::copy_options::overwrite_existing);
    refused = false;
    try {
        world::load(temp, options);
    } catch (const std::exception &x) {
        refused = std::string(x.what()).find("2:1") != std::string::npos;
    }
    fs::remove_all(temp);
    check(refused, "a panorama that isn't 2:1 is refused");
}
int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: catalog-tests <tests/fixtures/catalog>\n");
        return 2;
    }
    fs::path fixture = argv[1];
    char pattern[] = "/tmp/hn-catalog-XXXXXX";
    fs::path tmp = mkdtemp(pattern);
    manifests();
    screens();
    refs();
    catalog(fixture, tmp);
    archives(fixture, tmp);
    overlay();
    assets(fixture);
    imports(fixture);
    builder(fixture);
    support();
    fs::remove_all(tmp);
    std::cout << "PASS catalog: " << checked << " checks\n";
}
