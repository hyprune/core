#pragma once
// One package glTF loaded into a World's shared image, sampler and material
// arrays (internal to the world loader; used for scene.gltf and object assets).
#include "World.hpp"
#include <span>
#include <string>
namespace hyprune::world {
struct GltfScene {
    std::vector<Mesh> meshes;              // meshes of the scene graph, node transforms applied
    std::vector<Triangle> collision;       // from the collision nodes (rendered or not)
    std::vector<unsigned> collisionNodes;  // parallel to collision
    std::vector<int> parents;              // per node, -1 for roots
    std::vector<std::string> names;        // per node, "" when unnamed
    std::vector<Vec3> origins;             // per node, origin after node transforms
    std::vector<std::string> materialNames; // per glTF material, "" when unnamed
    size_t materialBase = 0;               // World::materials index of the first glTF material
};
// lightmaps: world.json lightmaps[] (the world scene only); collision:
// world.json collision[] by node index; collisionName: a node named once.
GltfScene loadGltf(World &, const std::filesystem::path &root, const std::string &path, const std::string &formatVersion,
                   LoadOptions, const Json &lightmaps, const Json &collision, const std::string &collisionName,
                   size_t &memory);
// The index of the only node with this name; throws when missing or repeated.
unsigned nodeNamed(std::span<const std::string> names, const std::string &name, const std::string &where);
} // namespace hyprune::world
