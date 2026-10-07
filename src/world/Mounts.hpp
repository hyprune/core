#pragma once
#include "World.hpp"
namespace hyprune::world {
Json defaultAtmosphere();
void validateAtmosphere(const Json &);
Spawn areaSpawn(const World &, const std::string &);
void loadMounts(World &, size_t nodeCount);
} // namespace hyprune::world
