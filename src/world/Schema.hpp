#pragma once
#include <json.hpp>
namespace hyprune::world {
// Offline interpreter for the keyword subset in the pinned world/common
// schemas. Not a general JSON Schema implementation or a license rights audit.
void validateManifest(const nlohmann::json &);
} // namespace hyprune::world
