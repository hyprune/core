#pragma once
// Shared clip library (RFC-0010 amendment "Shared clip library"): a clip authored on one rig
// played by any character with the same skeleton. Bones match by canonical name
// (mixamorig:LeftUpLeg, LeftUpLeg and Armature_LeftUpLeg are all "leftupleg"), with a
// per-character alias map for renamed bones. Rotations transfer through both rest poses (the
// source's change from rest, in world space, applied to the target's rest), so rigs whose
// bones point differently at rest still agree; the hips translation and a locomotion clip's
// speed scale by the ratio of hip heights, so stride and seat height fit the character.
#include "Character.hpp"
#include <map>
#include <string>
namespace hyprune::world {
// Canonical bone key: the name after the last ':' (namespace prefix), lower case.
std::string canonicalBone(std::string name);
struct RetargetInfo {
    int matched = 0, unmatched = 0; // target joints with / without a source bone
    float scale = 1;                 // target hip height / source hip height
    std::vector<std::string> missing; // canonical keys of the source's animated bones the target lacks
};
// Height of the hips above the lowest leg or root joint at rest, metres (0 without hips).
float hipHeight(const Character &);
// `clip` of `source` on `target`'s skeleton. aliases: canonical key -> the target's joint name.
// Channels on target joints only; other bones keep their rest pose. Only the hips (the root's
// travel and bob) take a translation; every other bone keeps the target's bone lengths.
Character::Clip retargetClip(const Character &target, const Character &source, const Character::Clip &clip,
                             const std::map<std::string, std::string> &aliases = {}, RetargetInfo *info = nullptr);
// An in-place locomotion clip's ground speed from its feet: while a foot is planted (within 3 %
// of the hip height of its lowest point over the clip) it moves backwards at the clip's speed;
// the median backward speed of planted feet, m/s (0 without feet).
float plantedSpeed(const Character &, const Character::Clip &);
// Foot slip of an in-place locomotion clip (speed > 0): the median of |planted foot velocity +
// speed forward| / speed over the clip; -1 without feet, speed or a planted foot.
float footSlip(const Character &, const Character::Clip &);
} // namespace hyprune::world
