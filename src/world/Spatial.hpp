#pragma once
#include "World.hpp"
namespace hyprune::world {
// Package bytes are validated once in the loader worker, never in a frame.
void loadSpatial(World &, const std::filesystem::path &);
Json projectMarkers(const World &, const sim::Camera &, float aspect);
std::vector<Vec3> walkingRoute(const World &, Vec3 from, Vec3 to);
void validateMapSvg(const std::string &);
} // namespace hyprune::world
