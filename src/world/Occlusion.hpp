#pragma once
// Character occlusion for render-on-demand and drawing (RFC-0010): a few rays from the eye to
// the character's bounds against the world's opaque geometry. Conservative: one clear ray
// means visible.
#include "World.hpp"
namespace hyprune::world {
// Geometry a ray cannot see through: an opaque material (alpha mode opaque, full alpha).
// Glass, masked foliage and blended surfaces never hide anything.
bool opaqueMesh(const World &, const Mesh &);
// Rebuilds occluderTriangles and occluderIndex from the opaque meshes, each placed by
// `transform` (scene nodes moved by a tool; identity when absent).
void buildOccluders(World &, const std::map<unsigned, Mat4> &transforms = {});
// True when every ray from `eye` to the character's sample points (centre, head, feet and
// the sides at mid height, of its rest bounds grown by `margin` for limbs, placed by `model`)
// hits opaque geometry well before the point. False without occluders.
bool characterOccluded(const World &, Vec3 eye, const Mat4 &model, Vec3 lo, Vec3 hi, float margin = .3f);
} // namespace hyprune::world
