#pragma once
// RFC-0011 object and library manifest checks: the rules of the schema repository's
// validate('object' | 'library') (lib/validate.mjs), so core accepts exactly what the
// shared validator and `hyprune object check` accept. Pure: callers gather the glTF
// JSON, the directory inventory, the thumbnail header and the measured values.
#include <json.hpp>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace hyprune::world::objects {
using Json = nlohmann::json;
// RFC-0011 "Budgets and limits" (provisional), as objectLimits in the schema repository.
struct Limits {
    static constexpr size_t files = 64, directoryBytes = 64u << 20, fileBytes = 32u << 20;
    static constexpr uint64_t triangles = 50000, characterTriangles = 16000, itemTriangles = 2000;
    static constexpr int textureSize = 2048, characterTextureSize = 1024;
    static constexpr uint64_t gpuBytes = 48u << 20, itemGpuBytes = 1u << 20;
    static constexpr uint64_t collisionTriangles = 2000;
    static constexpr int thumbnailSize = 256;
    static constexpr size_t thumbnailBytes = 262144;
    static constexpr size_t libraryFiles = 4000, libraryBytes = 512u << 20;
    static constexpr double boundsTolerance = .01, characterOriginTolerance = .05, slotMargin = .5;
};
// A workspace screen anchor's aspect (object format 0.11): "16:9" within 0.5 %, "21:9" from
// 2.33 to 2.39 (21:9, 64:27 and 43:18 panels), else "" (screenAspect in the schema repository).
std::string screenAspect(double width, double height);
struct ScreenLimits {
    static constexpr double minHeight = .9, boundsMargin = .1;
};
// Object manifest formats core knows (schema directories with an object schema).
const std::vector<std::string> &formats();
bool atLeast(const std::string &version, const std::string &minimum);
struct File {
    std::string path; // directory-relative, '/' separated
    uint64_t size = 0;
};
struct Thumbnail {
    int width = 0, height = 0;
    uint64_t bytes = 0;
};
struct Measured {
    std::optional<std::array<std::array<double, 3>, 2>> bounds; // exact rest bounds {min, max}
    std::optional<int> maxTextureSize;
    std::optional<uint64_t> gpuBytes;
};
struct Options {
    const Json *gltf = nullptr;                 // the scene glTF (or a GLB's JSON chunk)
    std::optional<std::string> directory;       // the object's directory name
    const std::vector<File> *files = nullptr;   // the directory inventory
    std::optional<Thumbnail> thumbnail;
    Measured measured;
};
struct Result {
    bool valid = false;
    std::vector<std::string> errors, warnings;
    std::optional<uint64_t> triangles; // drawn triangles (with a glTF)
};
// version: the object format to check against; empty = the manifest's own when known,
// else the newest (as the JS validator defaults).
Result checkObject(const Json &manifest, const Options &, std::string version = "");
// objects: object id -> its parsed object.json (directories under objects/); files: the
// library's inventory.
Result checkLibrary(const Json &manifest, const std::map<std::string, Json> *objects, const std::vector<File> *files,
                    std::string version = "");
// SPDX license expression syntax with the vendored license and exception lists.
bool validSpdx(const std::string &expression);
// glTF triangle count of a primitive (mode 4: count/3; strips and fans: count - 2).
uint64_t primitiveTriangles(const Json &gltf, const Json &primitive);
// Drawn scene nodes (reachable from the default scene) with their world matrices.
std::map<size_t, std::array<double, 16>> sceneNodes(const Json &gltf);
} // namespace hyprune::world::objects
