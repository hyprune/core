#pragma once
struct cgltf_data;
#include <span>
#include "Animation.hpp"
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
// A world 0.8 object asset: a package glTF in asset space (node transforms
// applied), drawn once per instance with the instance and animation models.
struct ObjectAsset {
    std::string id;
    bool unlit = false;                // lighting.mode "unlit": every material drawn unlit
    std::vector<Mesh> meshes;          // rendered meshes; Mesh::node is the asset's node index
    std::vector<int> nodeParents;
    std::vector<Animation> animations; // Animation::node is the asset's node index
    std::vector<int> nodeAnimation;    // asset node -> animation index (whole subtree), else -1
    std::vector<Triangle> collision;   // static proxy, asset space
    Vec3 center{};                     // swept bounds: sphere holding the asset at every pose
    float radius = 0;
    Vec3 low{}, high{};                // rest-pose bounds, asset space
    bool animated() const { return !animations.empty(); }
    int animationOf(unsigned node) const { return node < nodeAnimation.size() ? nodeAnimation[node] : -1; }
};
struct ObjectInstance {
    std::string id;
    unsigned asset = 0;
    Mat4 model = Mat4::identity(); // pose · uniform scale
    float scale = 1, speed = 1, phase = 0;
    // The authored pose (model is rebuilt from an editor overlay pose when one applies)
    // and this instance's proxy triangles in World::collision.
    Vec3 position{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    float authoredScale = 1;
    size_t collisionFirst = 0, collisionCount = 0;
    // Lighting (mode "probe"): ambient irradiance for upward/downward-facing
    // normals and the direct sun strength, from the world's baked light at load
    // (or the authored `ambient`). Linear RGB, world shader units.
    Vec3 ambientUp{.24f, .24f, .24f}, ambientDown{.24f, .24f, .24f};
    float sun = .76f, sunVisibility = 1; // strength (visibility included) and visibility 0..1
    bool authoredAmbient = false;
    Vec3 center{}; // world-space swept sphere
    float radius = 0;
    // Visibility areas owning static scene geometry that the swept sphere overlaps:
    // the object is region-visible when any of them is. Empty: frustum only.
    std::vector<int> regions;
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
    // World 0.8 placed objects (RFC-0004): assets are uploaded once, instances
    // are drawn instanced. Render-only apart from their static collision proxies.
    std::vector<ObjectAsset> objectAssets;
    std::vector<ObjectInstance> objectInstances;
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
// collisionNodes entry of world-object proxy triangles: never a scene node, so
// no gesture or editor transform can move them.
inline constexpr unsigned objectCollisionNode = 0xffffffffu;
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
