#pragma once
struct cgltf_data;
#include <span>
#include "Playable.hpp"
#include "sim/Camera.hpp"
#include "sim/Climbing.hpp"
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
    // KTX2/Basis images arrive as a transcoded block-compressed mip chain:
    // compressedFormat is the linear GL internal format (the renderer picks the
    // sRGB variant for colour slots); empty levels means rgba (mips generated).
    unsigned compressedFormat = 0;
    std::vector<std::vector<unsigned char>> levels;
};
// Block formats the GPU accepts, for transcoding KHR_texture_basisu images.
enum CompressedFormat : unsigned { BC7 = 1, BC1BC3 = 2, ASTC4x4 = 4 };
struct LoadOptions {
    unsigned compressedFormats = 0; // 0: transcode KTX2 to RGBA (no GL context, e.g. world-check)
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
    std::vector<Vertex> vertices; // flat triangle list (collision, picking, editing)
    // Optional indexed GPU copy of the same triangles; empty means draw vertices.
    std::vector<Vertex> gpuVertices;
    std::vector<uint32_t> indices;
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
    PlayableBounds playable;
    std::vector<std::string> warnings;
    std::string id, name, defaultSpawn;
    std::vector<Spawn> spawns;
    std::vector<sim::Climbable> climbables;
    struct VisibilityArea { Vec3 min, max; std::vector<int> from; };
    std::vector<VisibilityArea> visibility;
    std::vector<int> nodeVisibility;
    std::vector<Mesh> meshes;
    std::vector<Triangle> collision, visualTriangles;
    sim::TriangleIndex collisionIndex, visualIndex;
    std::vector<Material> materials;
    std::vector<Image> images;
    std::vector<Sampler> samplers;
    std::vector<unsigned> collisionNodes;
    // Node owns collision in its subtree: the only geometry a gesture or tool may move
    // (its collision moves with it). Everything else in a package is static.
    std::vector<bool> ownsCollision;
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
std::shared_ptr<World> load(const std::filesystem::path &package, LoadOptions = {});
// Marks ownsCollision from collisionNodes and nodeParents (load calls it).
void markCollisionOwners(World &);
bool movableNode(const World &, unsigned node);
// How materials sample each image: colour (sRGB base/emissive) and/or data
// (normal, metal/rough, occlusion, lightmap). Shared by budget and uploader.
struct ImageUse {
    bool color = false, data = false;
};
std::vector<ImageUse> imageUses(const World &);
// KHR_texture_basisu: KTX2 (2D LDR ETC1S/UASTC, zstd allowed) transcoded to the
// best of the given CompressedFormat bits (its mip chain), else RGBA level 0.
Image decodeKtx2(const unsigned char *data, size_t size, unsigned formats);
// EXT_meshopt_compression: decodes compressed views into view->data (freed by cgltf).
void decodeMeshopt(cgltf_data *);
// Fills mesh.gpuVertices/indices from decoded accessor vertices and indices.
void buildGpuMesh(Mesh &, std::span<const Vertex> unique, std::vector<uint32_t> indices, bool keepTriangleOrder);
// Conservative GPU bytes: vertex buffers, every image's full mip chain once
// (twice only when it is both colour and data without sRGB-decode control)
// and the given RGBA16F environment cube levels.
size_t gpuBytes(const World &, bool srgbDecode, std::span<const CubeLevel> environment);
constexpr size_t gpuBudget = 512 * 1024 * 1024;
} // namespace hyprune::world
