#pragma once
#include "sim/Camera.hpp"
#include <filesystem>
#include <json.hpp>
#include <memory>
#include <vector>
namespace hyprune::world {
using Json = nlohmann::json;
struct Vertex {
    Vec3 position, normal;
    float uv[2]{}, uv1[2]{};
};
struct Image {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
};
struct Material {
    Vec3 color{1, 1, 1}, emissive{};
    float alpha = 1;
    int baseTexture = -1, lightmap = -1;
    bool lightmapSrgb = false, unlit = false;
    float intensity = 1;
};
struct Mesh {
    std::vector<Vertex> vertices;
    unsigned material = 0;
};
struct Spawn {
    std::string id;
    Vec3 position;
    float yaw = 0, pitch = 0;
};
struct World {
    std::string id, name, defaultSpawn;
    std::vector<Spawn> spawns;
    std::vector<Mesh> meshes;
    std::vector<Triangle> collision;
    std::vector<Material> materials;
    std::vector<Image> images;
    Json manifest;
    const Spawn &spawn(const std::string &id = "") const;
};
std::filesystem::path safePath(const std::filesystem::path &root, const std::string &path);
std::shared_ptr<World> load(const std::filesystem::path &package);
} // namespace hyprune::world
