#pragma once
// Motion packs (RFC-0010 amendment "Shared clip library", draft): clips on a named skeleton,
// tagged by role and style, that every character with that skeleton can play. A pack is a
// directory motion/<id>/ (in a world package, an installed library or the user folder) with
// motion.json (motionVersion 0.1) and one or more clips-only glTF rigs (a skin, no mesh).
// When a character asset loads, the packs fill the clips it lacks (embedded clips win), each
// retargeted to its skeleton (Retarget.hpp).
#include "Character.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace hyprune::world {
struct MotionPack {
    struct Clip {
        std::string name, role, style; // style "" for the role's plain clip
        bool loop = true;
        int rig = 0;                   // index into rigs
        const Character::Clip *source = nullptr;
    };
    std::string id, name, skeleton, origin; // origin: "package", "library/<id>@<version>" or "user"
    std::filesystem::path directory;
    std::vector<std::shared_ptr<Character>> rigs;
    std::vector<Clip> clips;
};
// Limits: 8 rigs and 128 clips per pack, 32 MiB per rig file, 16 packs in one scan.
inline constexpr int motionRigs = 8, motionClips = 128, motionPacksMax = 16;
inline constexpr std::array motionRoles{"idle", "walk", "run", "sit-down", "stand-up", "seated-idle", "wave", "talk", "guide", "dance"};
// Throws std::runtime_error naming the problem (motion.json or a rig).
std::shared_ptr<MotionPack> loadMotionPack(const std::filesystem::path &directory, const std::string &origin);
// Every motion/<id>/motion.json under the given roots (each a package, library or user folder
// directory), first root first; invalid packs are skipped and reported in `problems`.
std::vector<std::shared_ptr<MotionPack>> findMotionPacks(const std::vector<std::pair<std::filesystem::path, std::string>> &roots,
                                                         std::vector<std::string> *problems = nullptr);
// The clip name a character plays for a role and style: idle, walk, run, sit (sit-down),
// stand (stand-up), sit_idle (seated-idle), wave, talk, guide, dance; a style appends
// "_<style>" ("walk_confident", "dance_shimmy"). A dance always has a style.
std::string motionClipName(const std::string &role, const std::string &style);
// The skeleton id of a character: traits.character.skeleton.id when declared, else
// "mixamo-v1" when it has the humanoid Mixamo bones (hips, spine, head, both arms and legs),
// else "".
std::string characterSkeleton(const Character &, const Json &characterTrait);
struct MotionBinding {
    std::string skeleton;
    struct Bound {
        std::string name, role, style, pack;
    };
    std::vector<Bound> clips; // added from packs, in pack order
};
// Adds the packs' clips the character lacks (by motionClipName), retargeted; embedded clips
// win, an earlier pack wins over a later one. traits: the object's traits.character. A sit's
// or stand's travel stays the source's (seat anchors are placed for a world distance).
MotionBinding bindMotion(Character &, const Json &characterTrait, const std::vector<std::shared_ptr<MotionPack>> &packs);
// A character style (IPC 0.28 characterStyle): {walk?, run?, idle?}, at least one, each a slug
// of at most 32 characters.
bool validCharacterStyle(const Json &);
// The styles and dances a character can play (its own and bound clips): IPC 0.28 catalog motion.
Json characterMotion(const Character &, const Json &binding);
// The same for a character not loaded yet, from its skeleton and clip names (a clips-only load)
// and the packs it would bind: no retargeting.
Json characterMotionPreview(const Character &rig, const Json &characterTrait, const std::vector<std::shared_ptr<MotionPack>> &packs);
} // namespace hyprune::world
