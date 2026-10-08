#pragma once
#include "sim/Camera.hpp"
#include <array>
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
// Texture indices refer to images and samplers together; color space belongs to a slot.
struct Sampler {
    int wrapS = 10497, wrapT = 10497, minFilter = 9987, magFilter = 9729;
};
struct TextureSlot {
    int texture = -1, texCoord = 0;
    float offset[2]{}, scale[2]{1, 1}, rotation = 0;
};
struct Material {
    Vec3 color{1, 1, 1}, emissive{};
    float alpha = 1, alphaCutoff = .5f;
    int alphaMode = 0; // 0 opaque, 1 mask, 2 blend
    bool doubleSided = false;
    int baseTexture = -1, lightmap = -1; // baseTexture retained for inspection compatibility
    // base color, normal, metallic-roughness, occlusion, emissive
    std::array<TextureSlot, 5> slots;
    float metallic = 1, roughness = 1, normalScale = 1, occlusionStrength = 1;
    bool lightmapSrgb = false, unlit = false;
    float intensity = 1;
};
struct Mesh {
    std::vector<Vertex> vertices;
    unsigned material = 0;
    unsigned node = 0;
};
struct Spawn {
    std::string id;
    Vec3 position;
    float yaw = 0, pitch = 0;
};
struct CubeLevel {
    int size;
    std::array<std::vector<float>, 6> faces;
};
struct World {
    std::string id, name, defaultSpawn;
    std::vector<Spawn> spawns;
    std::vector<Mesh> meshes;
    std::vector<Triangle> collision, visualTriangles;
    sim::TriangleIndex collisionIndex, visualIndex;
    std::vector<Material> materials;
    std::vector<Image> images;
    std::vector<Sampler> samplers;
    std::vector<unsigned> collisionNodes;
    std::vector<int> nodeParents;
    std::map<std::string, unsigned> objectNodes;
    Json manifest, map = nullptr, places = Json::array();
    Json walkingNodes = Json::array(), walkingEdges = Json::array();
    Json areas = Json::array(), anchors = Json::array(), objects = Json::array(), placementZones = Json::array(),
         atmosphere;
    std::array<Image, 6> environment;
    std::vector<CubeLevel> environmentHigh, environmentLow;
    const Spawn &spawn(const std::string &id = "") const;
};
std::filesystem::path safePath(const std::filesystem::path &root, const std::string &path);
std::shared_ptr<World> load(const std::filesystem::path &package);
} // namespace hyprune::world
