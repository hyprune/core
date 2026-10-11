#pragma once
// Skinned glTF characters for NPCs (RFC-0010 draft): loading, the artist-spec check and
// clip sampling. The same code serves core and hyprune-npc-check, so the validator can't
// disagree with what core accepts.
#include "World.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>
namespace hyprune::world {
struct Character {
    struct Vertex {
        Vec3 position, normal;
        float uv[2]{};
        float joints[4]{}, weights[4]{};
    };
    struct Primitive {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        unsigned material = 0;
    };
    struct Joint {
        std::string name;
        int parent = -1;                       // index into joints, -1 for the skeleton root
        Vec3 translation{}, scale{1, 1, 1};    // rest pose, local
        std::array<float, 4> rotation{0, 0, 0, 1};
        Mat4 inverseBind = Mat4::identity();
    };
    struct Channel {
        int joint = -1, path = 0; // 0 translation, 1 rotation, 2 scale
        bool step = false;
        std::vector<float> times, values; // values: 3 or 4 floats per key
    };
    struct Clip {
        std::string name;
        float duration = 0, speed = 0; // speed: metres/second of an in-place locomotion clip (extras.speed)
        std::vector<Channel> channels;
    };
    std::vector<Primitive> primitives;
    std::vector<Material> materials;        // slots[0].texture indexes images (base colour)
    std::vector<std::string> materialNames;
    std::vector<Image> images;
    std::vector<Joint> joints;
    std::vector<Clip> clips;
    Mat4 armature = Mat4::identity(); // transform of the skeleton root's parent nodes
    Vec3 lo{}, hi{};                  // rest-pose bounds, metres
    size_t triangles = 0;
    int skins = 0, extraWeightSets = 0, matrixJoints = 0, skeletonRoots = 0;
    float weightError = 0; // largest |sum of weights - 1|
    const Clip *clip(std::string_view name) const;
    int joint(std::string_view name) const;
    // Skinning matrices (joint world x inverse bind) for a clip at time t (wrapped for loops).
    void pose(const Clip *clip, float t, bool loop, std::vector<Mat4> &out) const;
};
struct CharacterProblem {
    enum Level { Error, Warning, Info } level;
    std::string message;
};
// Throws on files core cannot load at all (not glTF, no skin); spec deviations are problems.
Character loadCharacter(const std::filesystem::path &);
// The NPC artist spec (RFC-0010): skeleton, weights, axes and units, budget, materials,
// clips, no root motion; `reference` checks a variant shares the base skeleton.
std::vector<CharacterProblem> checkCharacter(const Character &, const Character *reference = nullptr);
} // namespace hyprune::world
