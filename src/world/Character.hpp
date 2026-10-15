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
        bool retargeted = false;       // from a motion pack (Motion.hpp), authored on another body
        std::vector<Channel> channels;
    };
    std::vector<Primitive> primitives;
    std::vector<Material> materials;        // slots index images: base colour, normal, metallic-roughness, occlusion, emissive
    std::vector<std::string> materialNames;
    std::vector<Image> images;
    std::vector<bool> linearImages;         // per image: data (normal, metallic-roughness, occlusion), not sRGB colour
    std::vector<Joint> joints;
    std::vector<Clip> clips;
    Mat4 armature = Mat4::identity(); // transform of the skeleton root's parent nodes
    Vec3 lo{}, hi{};                  // rest-pose bounds, metres
    size_t triangles = 0;
    int skins = 0, extraWeightSets = 0, matrixJoints = 0, skeletonRoots = 0;
    float weightError = 0; // largest |sum of weights - 1|
    std::vector<std::string> meshNodes;       // names of the skinned mesh nodes (body, head, visor)
    std::vector<bool> emissive;               // per material
    const Clip *clip(std::string_view name) const;
    int joint(std::string_view name) const;
    // Humanoid roles by name, Mixamo or plain: "hips" (mixamorig:Hips, pelvis), "head", "root".
    int role(std::string_view role) const;
    // Skinning matrices (joint world x inverse bind) for a clip at time t (wrapped for loops).
    void pose(const Clip *clip, float t, bool loop, std::vector<Mat4> &out) const;
    // Crossfade: clip a at ta blended towards clip b at tb by weight wb (0..1), both looping.
    void blend(const Clip *a, float ta, const Clip *b, float tb, float wb, std::vector<Mat4> &out) const;
    // The runtime pose (RFC-0010): `from` crossfading into `to` by weight (0..1), each looping or
    // clamped, then the head (and neck) turned by lookYaw/lookPitch radians in character space
    // (+yaw to the character's left, +pitch up). The plugin and the offload worker both pose
    // characters with this, so they draw the same skeleton.
    struct PoseInput {
        const Clip *from = nullptr, *to = nullptr;
        float fromTime = 0, toTime = 0, weight = 1;
        bool fromLoop = true, toLoop = true;
        float lookYaw = 0, lookPitch = 0;
        // Seat adaptation: the hips sit this many metres lower than the clip puts them (a seat
        // below the character's own seat height); both legs bend more (two-bone IK) so each
        // ankle stays where the clip had it relative to the floor, a little forward, feet flat.
        float seatDrop = 0;
    };
    void evaluate(const PoseInput &, std::vector<Mat4> &out) const;
    // What the `sit` clip was authored for, measured in its final pose: seatHeight is the lowest
    // thigh vertex above the root (RFC-0011 `seatHeight`); sink is how far the ankles end below
    // their `idle` height (a clip that sinks into the floor; >= 0). valid: the character has
    // `sit` and thigh bones. A retargeted sit's seat is at most a fifth of the hip height below
    // the hip joints (clothing skinned to the thighs hangs lower).
    struct SeatFit {
        bool valid = false;
        float seatHeight = 0, sink = 0;
    };
    SeatFit measureSeat() const;
};
struct CharacterProblem {
    enum Level { Error, Warning, Info } level;
    std::string message;
};
// Throws on files core cannot load at all (not glTF, no skin); spec deviations are problems.
// KTX2 images transcode to the best of compressedFormats (world::CompressedFormat bits) with
// their mip chain, as world textures do; 0 (no GL context) or PNG/JPEG images give RGBA.
Character loadCharacter(const std::filesystem::path &, unsigned compressedFormats = 0, bool clipsOnly = false);
// The NPC artist spec (RFC-0010): skeleton, weights, axes and units, budget, materials,
// clips, no root motion; `reference` checks a variant shares the base skeleton.
std::vector<CharacterProblem> checkCharacter(const Character &, const Character *reference = nullptr);
} // namespace hyprune::world
