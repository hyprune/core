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
