#pragma once
#include "world/World.hpp"
namespace hyprune::interaction {
// Complete defaults; patches are validated on a copy and applied atomically.
world::Json defaultConfig();
world::Json configPatch(const world::Json &current, const world::Json &patch);
} // namespace hyprune::interaction
