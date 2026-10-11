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
// Model of an instance: translation · rotation (unit quaternion x, y, z, w) · uniform scale.
Mat4 instanceModel(Vec3 position, const std::array<float, 4> &rotation, float scale);
} // namespace hyprune::world
