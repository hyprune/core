#pragma once
#include "World.hpp"
namespace hyprune::world {
// Package bytes are validated once in the loader worker, never in a frame.
void loadSpatial(World &, const std::filesystem::path &);
Json projectMarkers(const World &, const sim::Camera &, float aspect);
void validateMapSvg(const std::string &);
} // namespace hyprune::world
