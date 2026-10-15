#pragma once
#include "world/World.hpp"
namespace hyprune::interaction {
// Complete defaults; patches are validated on a copy and applied atomically.
world::Json defaultConfig();
world::Json configPatch(const world::Json &current, const world::Json &patch);
// Player settings on the wire (IPC 0.19): enum options, numeric ranges, all keys.
world::Json playerOptions();
world::Json playerRanges();
world::Json playerSettingsKeys();
// Player settings added with IPC 0.29 (the feel settings): older peers never see them.
world::Json feelPlayerKeys();
} // namespace hyprune::interaction
