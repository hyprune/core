#pragma once
#include "World.hpp"
namespace hyprune::world {
std::vector<CubeLevel> prefilterEnvironment(const World &, int resolution);
}
