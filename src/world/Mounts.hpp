#pragma once
#include "World.hpp"
namespace hyprune::world {
Json defaultAtmosphere();
void validateAtmosphere(const Json &);
void loadMounts(World &, size_t nodeCount);
} // namespace hyprune::world
