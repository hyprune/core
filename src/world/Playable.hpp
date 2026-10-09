#pragma once
#include "sim/Math.hpp"
#include <json.hpp>
#include <vector>
namespace hyprune::world {
// Unit outward normals: dot(normal, eye) <= offset. Cells form a union.
struct PlayablePlane {
    Vec3 normal;
    float offset;
};
struct PlayableBounds {
    using Cell = std::vector<PlayablePlane>;
    std::vector<Cell> cells;
    bool contains(Vec3 eye, float allowance = 0) const;
    Vec3 constrain(Vec3 previous, Vec3 desired, float allowance, float dt) const;
};
PlayableBounds loadPlayable(const nlohmann::json &manifest);
} // namespace hyprune::world
