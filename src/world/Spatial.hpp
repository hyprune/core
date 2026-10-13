#pragma once
#include "World.hpp"
namespace hyprune::world {
// Package bytes are validated once in the loader worker, never in a frame.
void loadSpatial(World &, const std::filesystem::path &);
Json projectMarkers(const World &, const sim::Camera &, float aspect);
std::vector<Vec3> walkingRoute(const World &, Vec3 from, Vec3 to);
// Off the graph: the walking route to the graph place nearest `to`, then `to` itself (empty
// without a walking graph).
std::vector<Vec3> walkingRouteNear(const World &, Vec3 from, Vec3 to);
// blueprint: also accept the class attribute with the blueprint role vocabulary.
void validateMapSvg(const std::string &, bool blueprint = false);
} // namespace hyprune::world
