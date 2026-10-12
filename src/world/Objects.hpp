#pragma once
// World 0.8 placed world objects (RFC-0004 "world 0.8 placed world objects").
#include "World.hpp"
namespace hyprune::world {
// Loads objectAssets (glTF, animations, collision proxy, lighting mode) and
// objects (instances); appends proxy collision at each instance pose. The world
// scene must be loaded; memory is the shared CPU/GPU budget counter.
void loadObjects(World &, const std::filesystem::path &root, const std::string &formatVersion, LoadOptions,
                 size_t &memory);
// Per-instance visibility region and lighting from the loaded static world:
// probe ambient (hemisphere up/down) and direct sun strength, or the authored
// ambient. Needs visibility, visualTriangles and visualIndex. `only`: one instance
// (after an editor overlay moved it).
void lightObjects(World &, std::optional<size_t> only = {});
// lightObjects in parts, so the probe can run off the compositor thread: the caches
// (per-mesh bounds, triangle sources), the visibility regions (cheap, in the tick), and
// the probe light of one instance, which only reads the world (no concurrent change to
// its visual triangles, materials or images while it runs).
struct ProbeLight {
    Vec3 ambientUp{.24f, .24f, .24f}, ambientDown{.24f, .24f, .24f};
    float sun = .76f, sunVisibility = 1;
};
void prepareLighting(World &);
void objectRegions(World &, std::optional<size_t> only = {});
// The visibility areas owning scene meshes within `radius` of `center` (what an object
// there shares); empty when none do or the mesh bounds are not built yet.
std::vector<int> regionsNear(const World &, Vec3 center, float radius);
ProbeLight probeLight(const World &, const ObjectInstance &);
void applyProbeLight(ObjectInstance &, const ProbeLight &);
// One object asset (a glTF in asset space) loaded into w's shared materials and images.
ObjectAsset buildObjectAsset(World &w, const std::filesystem::path &root, const std::string &id, const std::string &scene,
                             const std::string &formatVersion, LoadOptions, const Json &animations,
                             const std::string &proxy, bool unlit, size_t &memory);
// RFC-0011 catalog objects: an object directory (object.json) decoded into a scratch world
// holding that one asset (off the compositor thread), then merged into a live world in
// the tick: its materials, images and samplers are appended and re-indexed. Returns the
// new asset index.
std::shared_ptr<World> loadObjectAsset(const std::filesystem::path &dir, const Json &manifest, LoadOptions);
unsigned mergeObjectAsset(World &into, World &scratch);
// Model of an instance: translation · rotation (unit quaternion x, y, z, w) · uniform scale.
Mat4 instanceModel(Vec3 position, const std::array<float, 4> &rotation, float scale);
} // namespace hyprune::world
