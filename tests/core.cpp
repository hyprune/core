#include "surfaces/Surface.hpp"
#include "world/Schema.hpp"
#include "world/Spatial.hpp"
#include "world/World.hpp"
#include "world/Format.hpp"
#include <cgltf.h>
#include <meshoptimizer/meshoptimizer.h>
#include <cstring>
#include <fstream>
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
        {
            // Indexed GPU copy: duplicates merged, identical triangle multiset;
            // blended meshes keep their triangle order.
            std::vector<world::Vertex> unique(6);
            for (int i = 0; i < 6; ++i) {
                unique[i].position = {float(i % 3), float(i / 3 % 2), 0};
                unique[i].normal = {0, 0, 1};
            }
            unique[4] = unique[1]; // exact duplicate of vertex 1
            std::vector<uint32_t> indices{0, 1, 2, 2, 1, 3, 3, 4, 5, 0, 2, 5};
            auto triangles = [](std::span<const world::Vertex> v, std::span<const uint32_t> idx) {
                std::vector<std::array<float, 9>> out;
                for (size_t t = 0; t < idx.size(); t += 3) {
                    std::array<float, 9> tri;
                    for (int k = 0; k < 3; ++k)
                        std::copy_n(&v[idx[t + k]].position.x, 3, tri.begin() + 3 * k);
                    out.push_back(tri);
                }
                return out;
            };
            for (bool keep : {false, true}) {
                world::Mesh mesh;
                world::buildGpuMesh(mesh, unique, indices, keep);
                assert(mesh.gpuVertices.size() == 5);
                auto before = triangles(unique, indices), after = triangles(mesh.gpuVertices, mesh.indices);
                if (keep) {
                    // Same order; rotation within a triangle is not allowed either.
                    assert(before == after);
                } else {
                    std::sort(before.begin(), before.end());
                    std::sort(after.begin(), after.end());
                    // Vertex-cache order may rotate a triangle's corners; compare canonical forms.
                    auto canonical = [](std::vector<std::array<float, 9>> list) {
                        for (auto &tri : list) {
                            std::array<std::array<float, 3>, 3> c{};
                            for (int k = 0; k < 3; ++k) std::copy_n(tri.begin() + 3 * k, 3, c[k].begin());
                            auto best = c;
                            for (int r = 1; r < 3; ++r) {
                                std::rotate(c.begin(), c.begin() + 1, c.end());
                                best = std::min(best, c);
                            }
                            for (int k = 0; k < 3; ++k) std::copy_n(best[k].begin(), 3, tri.begin() + 3 * k);
                        }
                        std::sort(list.begin(), list.end());
                        return list;
                    };
                    assert(canonical(before) == canonical(after));
                }
            }
        }
        {
            // KTX2 (UASTC + zstd, 4 mips) from tests/fixtures/ktx2: RGBA fallback
            // matches the source gradient; BC7/BC1 keep the package's mip chain.
            auto path = std::filesystem::path(argv[1]).parent_path() / "ktx2/gradient8.ktx2";
            std::ifstream input(path, std::ios::binary);
            std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
            auto rgba = world::decodeKtx2(bytes.data(), bytes.size(), 0);
            assert(rgba.width == 8 && rgba.height == 8 && rgba.levels.empty() && rgba.rgba.size() == 256);
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x) {
                    auto *p = &rgba.rgba[(y * 8 + x) * 4];
                    assert(std::abs(p[0] - x * 32) <= 6 && std::abs(p[1] - y * 32) <= 6 && std::abs(p[2] - 128) <= 6);
                }
            auto bc7 = world::decodeKtx2(bytes.data(), bytes.size(), world::BC7 | world::BC1BC3);
            assert(bc7.compressedFormat == 0x8E8C && bc7.levels.size() == 4 && bc7.levels[0].size() == 4 * 16 &&
                   bc7.levels[3].size() == 16 && bc7.rgba.empty());
            auto bc1 = world::decodeKtx2(bytes.data(), bytes.size(), world::BC1BC3);
            assert(bc1.compressedFormat == 0x83F0 && bc1.levels[0].size() == 4 * 8);
            rejects([&] { world::decodeKtx2(bytes.data(), bytes.size() / 2, 0); }, "truncated KTX2 accepted");
        }
        {
            // EXT_meshopt_compression: an encoded attribute view decodes into view->data.
            std::vector<float> vertices{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
            std::vector<unsigned char> encoded(meshopt_encodeVertexBufferBound(3, 16));
            encoded.resize(meshopt_encodeVertexBuffer(encoded.data(), encoded.size(), vertices.data(), 3, 16));
            cgltf_buffer buffer{};
            buffer.data = encoded.data();
            buffer.size = encoded.size();
            cgltf_buffer_view view{};
            view.size = 48;
            view.has_meshopt_compression = true;
            view.meshopt_compression = {&buffer, 0, encoded.size(), 16, 3, cgltf_meshopt_compression_mode_attributes,
                                        cgltf_meshopt_compression_filter_none};
            cgltf_data data{};
            data.buffer_views = &view;
            data.buffer_views_count = 1;
            world::decodeMeshopt(&data);
            assert(view.data && std::memcmp(view.data, vertices.data(), 48) == 0);
            free(view.data);
            encoded[encoded.size() / 2] ^= 0xff;
            view.data = nullptr;
            view.meshopt_compression.size = 4;
            rejects([&] { world::decodeMeshopt(&data); }, "corrupt meshopt view accepted");
        }
        {
            // World-format inheritance: the same package loads under every supported
            // format (0.8's objects are covered by the world-objects test); 0.7's
            // compressed-asset extensions are rejected below 0.7.
            auto dir = std::filesystem::temp_directory_path() / ("hyprune-format-" + std::to_string(getpid()));
            std::filesystem::remove_all(dir);
            std::filesystem::copy(argv[1], dir, std::filesystem::copy_options::recursive);
            auto relabel = [&](const std::string &version) {
                auto j = world::Json::parse(std::ifstream(dir / "world.json"));
                j["formatVersion"] = version;
                std::ofstream(dir / "world.json") << j.dump();
            };
            for (auto version : world::format::supported) {
                relabel(std::string(version));
                assert(world::load(dir)->id == "org.hyprune.atrium");
            }
            // Numeric comparison; formats this core does not know never pass a gate.
            assert(world::format::atLeast("0.7", "0.6") && !world::format::atLeast("0.6", "0.7") &&
                   world::format::atLeast("0.8", "0.7") && !world::format::atLeast("0.9", "0.7"));
            relabel("0.9");
            rejects([&] { world::load(dir); }, "unknown world format accepted");
            auto scene = world::Json::parse(std::ifstream(dir / "scene.gltf"));
            scene["extensionsUsed"] = scene["extensionsRequired"] = {"KHR_mesh_quantization"};
            std::ofstream(dir / "scene.gltf") << scene.dump();
            relabel("0.7");
            world::load(dir);
            relabel("0.6");
            rejects([&] { world::load(dir); }, "0.7 glTF extension accepted in a 0.6 world");
            std::filesystem::remove_all(dir);
        }
        world::validateMapSvg("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><path d=\"M 0 0 L 10 "
                              "10\" stroke=\"#fff\"/></svg>");
        for (const auto &bad :
             {"<svg><script/></svg>", "<!DOCTYPE svg [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><svg/>",
              "<svg><image href='https://bad'/></svg>", "<svg onload='bad()'/>",
              "<svg><rect fill='url(https://bad)'/></svg>", "<?xml version='1.0'?><svg/>",
              "<svg><foreignObject/></svg>"})
            rejects([&] { world::validateMapSvg(bad); }, "unsafe SVG accepted");
        {
            // com.hyprune.blueprint 0.1: per-level SVGs in the map's frame, sent as map.levels.
            auto dir = std::filesystem::temp_directory_path() / ("hyprune-blueprint-" + std::to_string(getpid()));
            std::filesystem::create_directories(dir / "blueprint");
            const std::string level = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
                                      "<path class=\"wall\" d=\"M 0 0 L 10 0\"/><text class=\"label\" x=\"1\" y=\"2\">Hall</text></svg>";
            std::ofstream(dir / "map.svg") << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"/>";
            std::ofstream(dir / "blueprint/ground.svg") << level;
            std::ofstream(dir / "blueprint/upper.svg") << level;
            auto spatialWith = [&](const world::Json &blueprint) {
                world::World w;
                w.manifest = {{"extensions",
                               {{"com.hyprune.map", {{"version", "0.1"}, {"path", "map.svg"}, {"projection", "xz"},
                                                      {"north", {0, 0, -1}}, {"bounds", {-5, -5, 5, 5}},
                                                      {"worldToMap", {.1, 0, 0, .1, .5, .5}}}},
                                {"com.hyprune.blueprint", blueprint}}}};
                world::loadSpatial(w, dir);
                return w.map;
            };
            auto lv = [](std::string id, double lo, double hi, std::string path) {
                return world::Json{{"id", id}, {"name", id}, {"minY", lo}, {"maxY", hi}, {"path", path}};
            };
            auto map = spatialWith({{"version", "0.1"},
                                    {"levels", {lv("ground", -2, 4.5, "blueprint/ground.svg"), lv("upper", 4.5, 40, "blueprint/upper.svg")}}});
            check(map["levels"].size() == 2 && map["levels"][1]["id"] == "upper" && map["levels"][0]["svg"] == level &&
                      map["levels"][0]["sha256"].get<std::string>().size() == 64 && map.contains("svg"),
                  "blueprint levels");
            for (const auto &bad : {world::Json{{"version", "0.2"}, {"levels", {lv("a", 0, 1, "blueprint/ground.svg")}}},
                                    world::Json{{"version", "0.1"}, {"levels", world::Json::array()}},
                                    world::Json{{"version", "0.1"}, {"levels", {lv("a", 0, 5, "blueprint/ground.svg"), lv("b", 4, 9, "blueprint/upper.svg")}}},
                                    world::Json{{"version", "0.1"}, {"levels", {lv("a", 0, 1, "blueprint/ground.svg"), lv("a", 1, 2, "blueprint/upper.svg")}}},
                                    world::Json{{"version", "0.1"}, {"levels", {lv("Bad", 0, 1, "blueprint/ground.svg")}}},
                                    world::Json{{"version", "0.1"}, {"levels", {lv("a", 1, 1, "blueprint/ground.svg")}}},
                                    world::Json{{"version", "0.1"}, {"levels", {lv("a", 0, 1, "../escape.svg")}}},
                                    world::Json{{"version", "0.1"}, {"levels", {lv("a", 0, 1, "map.svg")}}, {"extra", 1}}})
                rejects([&] { spatialWith(bad); }, "invalid blueprint accepted");
            std::ofstream(dir / "blueprint/upper.svg") << "<svg xmlns=\"http://www.w3.org/2000/svg\"><path class=\"lava\" d=\"M0 0\"/></svg>";
            rejects([&] { spatialWith({{"version", "0.1"}, {"levels", {lv("u", 0, 1, "blueprint/upper.svg")}}}); },
                    "unknown blueprint class accepted");
            rejects([&] { world::validateMapSvg(level); }, "class accepted in a plain map SVG");
            world::World noMap;
            noMap.manifest = {{"extensions", {{"com.hyprune.blueprint", {{"version", "0.1"}}}}}};
            rejects([&] { world::loadSpatial(noMap, dir); }, "blueprint without a map accepted");
            std::filesystem::remove_all(dir);
        }
        world::World projected;
        projected.places =
            world::Json::array({world::Json{{"id", "front"}, {"name", "Front"}, {"position", {0, 1.7, -5}}},
                                world::Json{{"id", "behind"}, {"name", "Behind"}, {"position", {0, 1.7, 5}}}});
        sim::Camera viewer;
        viewer.position = {0, 1.7, 0};
        auto markers = world::projectMarkers(projected, viewer, 16.f / 9);
        check(markers.size() == 1 && markers[0]["id"] == "front" && std::abs(markers[0]["x"].get<float>() - .5f) < .001,
              "projection clipping/convention");
        projected.collision.push_back({{-2, 0, -2}, {2, 0, -2}, {0, 4, -2}});
        check(world::projectMarkers(projected, viewer, 16.f / 9)[0]["occluded"] == true, "marker collision occlusion");
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
        auto specGloss = gltf;
        specGloss["materials"][0].erase("pbrMetallicRoughness");
        specGloss["materials"][0]["extensions"]["KHR_materials_pbrSpecularGlossiness"] = {
            {"diffuseFactor", {.2, .4, .6, .5}}, {"diffuseTexture", {{"index", 0}}}, {"glossinessFactor", .7}};
        {
            std::ofstream f(temp / "scene.gltf");
            f << specGloss;
        }
        auto legacyMaterial = world::load(temp)->materials[0];
        check(std::abs(legacyMaterial.color.x - .2f) < 1e-6f && std::abs(legacyMaterial.alpha - .5f) < 1e-6f &&
                  legacyMaterial.slots[0].texture == 0,
              "specular-glossiness diffuse colour/texture lost");
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
