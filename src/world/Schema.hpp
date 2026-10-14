#pragma once
#include <json.hpp>
namespace hyprune::world {
// Offline interpreter for the keyword subset in the pinned world/common
// schemas. Not a general JSON Schema implementation or a license rights audit.
void validateManifest(const nlohmann::json &);
// RFC-0011 object and library manifests (`kind` "object" or "library") against the
// pinned schema of that object format ("0.9", "0.10"); "import" (import.json) and "project"
// (builder/project.json) have one schema each. Throws "<json path>: <reason>".
void validateObjectDocument(const nlohmann::json &, const std::string &kind, const std::string &version);
} // namespace hyprune::world
